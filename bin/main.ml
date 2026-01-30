open Core
open Prime_factors

let algorithms =
  Command.Arg_type.of_alist_exn
    [ "auto", `Auto; "trial", `Trial; "rho", `Rho ]
;;

let factorize_command =
  Command.basic
    ~summary:"Factorize an integer into prime factors"
    (let%map_open.Command number = anon ("NUMBER" %: string)
     and algorithm =
       flag
         "-algorithm"
         (optional_with_default `Auto algorithms)
         ~aliases:[ "-a" ]
         ~doc:"ALGO factorization algorithm: auto (default), trial, rho"
     and verify_flag =
       flag "-verify" no_arg ~aliases:[ "-v" ] ~doc:" verify the factorization"
     in
     fun () ->
       let n =
         try Z.of_string number with
         | _ ->
           eprintf "Error: '%s' is not a valid integer\n" number;
           exit 1
       in
       match Z.compare n (Z.of_int 2) < 0 with
       | true ->
         eprintf "Error: number must be >= 2\n";
         exit 1
       | false ->
         let result =
           match algorithm with
           | `Auto -> factorize n
           | `Trial -> trial_division n
           | `Rho -> pollard_rho n
         in
         (match result with
          | Error (`Invalid_input msg) ->
            eprintf "Error: %s\n" msg;
            exit 1
          | Ok factors ->
            printf "%s = %s\n" (Z.to_string n) (to_string factors);
            (match verify_flag with
             | true ->
               let valid = verify n factors in
               printf
                 "Verification: %s\n"
                 (match valid with
                  | true -> "PASS"
                  | false -> "FAIL")
             | false -> ())))
;;

let primality_command =
  Command.basic
    ~summary:"Test if a number is probably prime (Miller-Rabin)"
    (let%map_open.Command number = anon ("NUMBER" %: string)
     and rounds =
       flag
         "-rounds"
         (optional_with_default 20 int)
         ~aliases:[ "-r" ]
         ~doc:"N number of Miller-Rabin rounds (default: 20)"
     in
     fun () ->
       let n =
         try Z.of_string number with
         | _ ->
           eprintf "Error: '%s' is not a valid integer\n" number;
           exit 1
       in
       let result = is_probably_prime ~rounds n in
       printf
         "%s is %s\n"
         (Z.to_string n)
         (match result with
          | true -> "probably prime"
          | false -> "composite"))
;;

let () =
  Command.group
    ~summary:"Prime factorization toolkit"
    [ "factor", factorize_command; "is-prime", primality_command ]
  |> Command_unix.run
;;
