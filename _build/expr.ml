type prim1 =
  | Add1
  | Sub1
  | Print
  | IsNum
  | IsBool
  | IsPair
  | Fst
  | Snd

type prim2 =
  | Plus
  | Minus
  | Times
  | Less
  | Greater
  | Equal
  | SetFst
  | SetSnd

type expr =
  | ELet of (string * expr) list * expr
  | ESeq of expr list
  | EPrim1 of prim1 * expr
  | EPrim2 of prim2 * expr * expr
  | EApp of expr * expr list
  | EPair of expr * expr
  | ELambda of string list * expr
  | EIf of expr * expr * expr
  | ENumber of int
  | EBool of bool
  | EId of string

type immexpr =
  | ImmNumber of int
  | ImmBool of bool
  | ImmId of string

and cexpr =
  | CPrim1 of prim1 * immexpr
  | CPrim2 of prim2 * immexpr * immexpr
  | CApp of immexpr * immexpr list
  | CPair of immexpr * immexpr
  | CLambda of string list * aexpr
  | CIf of immexpr * aexpr * aexpr
  | CImmExpr of immexpr

and aexpr =
  | ALet of string * cexpr * aexpr
  | ASeq of aexpr * aexpr
  | ACExpr of cexpr

