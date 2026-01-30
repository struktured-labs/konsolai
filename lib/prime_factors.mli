(** Prime factorization algorithms using arbitrary-precision integers. *)

module Factor : sig
  type t =
    { prime : Z.t
    ; power : int
    }

  val create : prime:Z.t -> power:int -> t
  val to_string : t -> string
  val value : t -> Z.t
  val compare : t -> t -> int
end

(** {2 Primality Testing} *)

(** Miller-Rabin probabilistic primality test. Default [rounds] is 20,
    giving a false positive probability of at most 4^{-20}. *)
val is_probably_prime : ?rounds:int -> Z.t -> bool

val is_probably_prime_exn : ?rounds:int -> Z.t -> bool

(** {2 Factorization Algorithms} *)

(** Trial division. Efficient for numbers with small prime factors. *)
val trial_division : Z.t -> (Factor.t list, [> `Invalid_input of string ]) result

val trial_division_exn : Z.t -> Factor.t list

(** Pollard's rho algorithm. Better for numbers with larger prime factors. *)
val pollard_rho : Z.t -> (Factor.t list, [> `Invalid_input of string ]) result

val pollard_rho_exn : Z.t -> Factor.t list

(** Combined factorization: trial division for small factors, Pollard's rho
    for the remainder. This is the recommended entry point. *)
val factorize : Z.t -> (Factor.t list, [> `Invalid_input of string ]) result

val factorize_exn : Z.t -> Factor.t list

(** {2 Utilities} *)

(** Pretty-print a factorization as ["p1^e1 * p2^e2 * ..."]. *)
val to_string : Factor.t list -> string

(** Verify a factorization by multiplying the factors back together. *)
val verify : Z.t -> Factor.t list -> bool
