open Core

(** {1 Prime Factorization Library}

    Algorithms for integer factorization using arbitrary-precision integers
    via Zarith. *)

module Factor = struct
  type t =
    { prime : Z.t
    ; power : int
    }

  let create ~prime ~power = { prime; power }

  let to_string { prime; power } =
    match power = 1 with
    | true -> Z.to_string prime
    | false -> sprintf "%s^%d" (Z.to_string prime) power
  ;;

  let value { prime; power } = Z.pow prime power

  let compare a b =
    match Z.compare a.prime b.prime with
    | 0 -> Int.compare a.power b.power
    | c -> c
  ;;
end

(** {2 Utility} *)

let two = Z.of_int 2
let three = Z.of_int 3

(** Generate a random Z.t in [lo, hi] inclusive. *)
let random_z_range ~lo ~hi =
  let range = Z.sub hi lo in
  let bits = Z.numbits range in
  let byte_count = (bits + 7) / 8 in
  let rec pick () =
    let buf = Bytes.create byte_count in
    for i = 0 to byte_count - 1 do
      Bytes.set buf i (Char.of_int_exn (Random.int 256))
    done;
    let candidate = Z.(abs (of_bits (Bytes.to_string buf)) mod succ range) in
    match Z.compare candidate range <= 0 with
    | true -> Z.add candidate lo
    | false -> pick ()
  in
  pick ()
;;

(** {2 Primality Testing} *)

(** Miller-Rabin primality test.

    Returns [true] if [n] is probably prime. The probability of a false
    positive is at most 4^{-rounds}. Default is 20 rounds. *)
let is_probably_prime ?(rounds = 20) n =
  (* Handle small cases *)
  match Z.compare n two with
  | c when c < 0 -> false
  | 0 -> true
  | _ ->
    (match Z.equal (Z.rem n two) Z.zero with
     | true -> false
     | false ->
       (* Write n-1 = 2^r * d where d is odd *)
       let n_minus_1 = Z.pred n in
       let r = Z.trailing_zeros n_minus_1 in
       let d = Z.shift_right n_minus_1 r in
       let check_witness a =
         let x = ref (Z.powm a d n) in
         match Z.equal !x Z.one || Z.equal !x n_minus_1 with
         | true -> true (* probably prime *)
         | false ->
           let found_minus_1 = ref false in
           let i = ref 1 in
           while !i < r && not !found_minus_1 do
             x := Z.powm !x two n;
             (match Z.equal !x n_minus_1 with
              | true -> found_minus_1 := true
              | false -> ());
             incr i
           done;
           !found_minus_1
       in
       let all_passed = ref true in
       let round = ref 0 in
       while !round < rounds && !all_passed do
         let a =
           match Z.compare n (Z.of_int 4) <= 0 with
           | true -> two
           | false -> random_z_range ~lo:two ~hi:(Z.sub n two)
         in
         (match check_witness a with
          | true -> ()
          | false -> all_passed := false);
         incr round
       done;
       !all_passed)
;;

let is_probably_prime_exn ?(rounds = 20) n =
  match Z.compare n two < 0 with
  | true ->
    raise_s [%message "is_probably_prime: n must be >= 2" ~n:(Z.to_string n : string)]
  | false -> is_probably_prime ~rounds n
;;

(** {2 Trial Division} *)

(** Factor out all occurrences of [divisor] from [n].
    Returns [(remaining, power)]. *)
let extract_factor n divisor =
  let rec go n power =
    match Z.equal (Z.rem n divisor) Z.zero with
    | true -> go (Z.div n divisor) (power + 1)
    | false -> n, power
  in
  go n 0
;;

(** Factorize [n] using trial division up to sqrt(n).
    Efficient for numbers with small prime factors. *)
let trial_division n =
  match Z.compare n two < 0 with
  | true -> Error (`Invalid_input "n must be >= 2")
  | false ->
    let factors = ref [] in
    let n = ref n in
    (* Factor out 2 *)
    let remaining, power = extract_factor !n two in
    n := remaining;
    (match power > 0 with
     | true -> factors := Factor.create ~prime:two ~power :: !factors
     | false -> ());
    (* Factor out odd numbers from 3 upward *)
    let d = ref three in
    while Z.compare (Z.mul !d !d) !n <= 0 do
      let remaining, power = extract_factor !n !d in
      n := remaining;
      (match power > 0 with
       | true -> factors := Factor.create ~prime:!d ~power :: !factors
       | false -> ());
      d := Z.add !d two
    done;
    (* If anything remains, it's a prime factor *)
    (match Z.compare !n Z.one > 0 with
     | true -> factors := Factor.create ~prime:!n ~power:1 :: !factors
     | false -> ());
    Ok (List.rev !factors)
;;

let trial_division_exn n =
  match trial_division n with
  | Ok factors -> factors
  | Error (`Invalid_input msg) -> raise_s [%message "trial_division" msg]
;;

(** {2 Pollard's Rho Algorithm} *)

(** Find a single non-trivial factor of [n] using Pollard's rho with
    Brent's improvement. Returns [None] if [n] is prime. *)
let pollard_rho_find_factor n =
  match Z.equal (Z.rem n two) Z.zero with
  | true -> Some two
  | false ->
    let found = ref None in
    let attempts = ref 0 in
    while Option.is_none !found && !attempts < 50 do
      let c = random_z_range ~lo:Z.one ~hi:(Z.sub n two) in
      let f x = Z.(rem (add (mul x x) c) n) in
      let x = ref (random_z_range ~lo:two ~hi:(Z.sub n two)) in
      let y = ref !x in
      let d = ref Z.one in
      while Z.equal !d Z.one do
        x := f !x;
        y := f (f !y);
        d := Z.gcd (Z.abs (Z.sub !x !y)) n
      done;
      (match Z.equal !d n with
       | true -> () (* retry with different c *)
       | false -> found := Some !d);
      incr attempts
    done;
    !found
;;

(** Fully factorize [n] using Pollard's rho, recursing on found factors. *)
let pollard_rho n =
  match Z.compare n two < 0 with
  | true -> Error (`Invalid_input "n must be >= 2")
  | false ->
    let factor_counts = Hashtbl.create (module String) in
    let add_prime p =
      let key = Z.to_string p in
      Hashtbl.update factor_counts key ~f:(function
        | None -> p, 1
        | Some (p, count) -> p, count + 1)
    in
    let rec go n =
      match Z.compare n two < 0 with
      | true -> ()
      | false ->
        (match is_probably_prime n with
         | true -> add_prime n
         | false ->
           (* Extract small factors with trial division up to 1000 *)
           let n = ref n in
           let d = ref two in
           while Z.compare !d (Z.of_int 1000) <= 0 && Z.compare !n Z.one > 0 do
             while Z.equal (Z.rem !n !d) Z.zero do
               add_prime !d;
               n := Z.div !n !d
             done;
             d := Z.add !d Z.one
           done;
           (match Z.compare !n Z.one > 0 && not (is_probably_prime !n) with
            | true ->
              (match pollard_rho_find_factor !n with
               | Some factor ->
                 go factor;
                 go (Z.div !n factor)
               | None ->
                 (* Fallback: treat as prime (shouldn't happen often) *)
                 add_prime !n)
            | false ->
              (match Z.compare !n Z.one > 0 with
               | true -> add_prime !n
               | false -> ())))
    in
    go n;
    let factors =
      Hashtbl.data factor_counts
      |> List.map ~f:(fun (prime, power) -> Factor.create ~prime ~power)
      |> List.sort ~compare:Factor.compare
    in
    Ok factors
;;

let pollard_rho_exn n =
  match pollard_rho n with
  | Ok factors -> factors
  | Error (`Invalid_input msg) -> raise_s [%message "pollard_rho" msg]
;;

(** {2 Combined Factorization} *)

(** Factorize [n] using a combination of trial division (for small factors)
    and Pollard's rho (for larger factors). This is the recommended
    entry point. *)
let factorize n =
  match Z.compare n two < 0 with
  | true -> Error (`Invalid_input "n must be >= 2")
  | false ->
    let factor_counts = Hashtbl.create (module String) in
    let add_prime p count =
      let key = Z.to_string p in
      Hashtbl.update factor_counts key ~f:(function
        | None -> p, count
        | Some (p, existing) -> p, existing + count)
    in
    (* Phase 1: trial division for small primes up to 10000 *)
    let n = ref n in
    let remaining, power = extract_factor !n two in
    n := remaining;
    (match power > 0 with
     | true -> add_prime two power
     | false -> ());
    let d = ref three in
    while Z.compare !d (Z.of_int 10_000) <= 0 && Z.compare !n Z.one > 0 do
      let remaining, power = extract_factor !n !d in
      n := remaining;
      (match power > 0 with
       | true -> add_prime !d power
       | false -> ());
      d := Z.add !d two
    done;
    (* Phase 2: Pollard's rho for remaining large factors *)
    let rec rho_split m =
      match Z.compare m two < 0 with
      | true -> ()
      | false ->
        (match is_probably_prime m with
         | true -> add_prime m 1
         | false ->
           (match pollard_rho_find_factor m with
            | Some factor ->
              rho_split factor;
              rho_split (Z.div m factor)
            | None ->
              (* Fallback *)
              add_prime m 1))
    in
    (match Z.compare !n Z.one > 0 with
     | true -> rho_split !n
     | false -> ());
    let factors =
      Hashtbl.data factor_counts
      |> List.map ~f:(fun (prime, power) -> Factor.create ~prime ~power)
      |> List.sort ~compare:Factor.compare
    in
    Ok factors
;;

let factorize_exn n =
  match factorize n with
  | Ok factors -> factors
  | Error (`Invalid_input msg) -> raise_s [%message "factorize" msg]
;;

(** Pretty-print a factorization. *)
let to_string factors =
  List.map factors ~f:Factor.to_string |> String.concat ~sep:" * "
;;

(** Verify a factorization by multiplying factors back together. *)
let verify n factors =
  let product =
    List.fold factors ~init:Z.one ~f:(fun acc f -> Z.mul acc (Factor.value f))
  in
  Z.equal n product
;;
