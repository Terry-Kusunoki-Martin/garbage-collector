open Printf
open Expr
open Instruction
open ExtLib

type 'a envt = (string * 'a) list

let count = ref 0
let gen_temp base =
  count := !count + 1;
  sprintf "temp_%s_%d" base !count

type hole =
  | CHole of (cexpr -> aexpr)
  | ImmHole of (immexpr -> aexpr)

let fill_imm (h : hole) (v : immexpr) : aexpr =
  match h with
    | CHole(k) -> (k (CImmExpr(v)))
    | ImmHole(k) -> (k v)

let fill_c (h : hole) (c : cexpr) : aexpr =
  match h with
    | CHole(k) -> (k c)
    | ImmHole(k) ->
      let tmp = gen_temp "" in
      ALet(tmp, c, k (ImmId(tmp)))

let return_hole = CHole(fun ce -> ACExpr(ce))

let rec anf_list (es : expr list) (k : immexpr list -> aexpr) : aexpr =
  match es with
    | [] -> k []
    | e::rest ->
      anf e (ImmHole(fun imm ->
        anf_list rest (fun imms -> k (imm::imms))))

and anf (e : expr) (h : hole) : aexpr =
  match e with
    | ENumber(n) -> fill_imm h (ImmNumber(n)) 
    | EBool(b) -> fill_imm h (ImmBool(b)) 
    | ELambda(ids, body) ->
      fill_c h (CLambda(ids, anf body return_hole))  
    | EId(x) -> fill_imm h (ImmId(x))
    | EPrim1(op, e) ->
      anf e (ImmHole(fun imm -> (fill_c h (CPrim1(op, imm)))))
    | EPrim2(op, left, right) ->
      anf left (ImmHole(fun limm ->
        anf right (ImmHole(fun rimm ->
          (fill_c h (CPrim2(op, limm, rimm)))))))
    | EApp(f, args) ->
      anf f (ImmHole(fun fimm ->
        anf_list args (fun aimms -> fill_c h (CApp(fimm, aimms)))))
    | EPair(left, right) -> 
      anf left (ImmHole(fun limm ->
        anf right (ImmHole(fun rimm ->
          (fill_c h (CPair(limm, rimm)))))))
    | EIf(cond, thn, els) ->
      anf cond (ImmHole(fun cimm ->
        (fill_c h (CIf(cimm, (anf thn return_hole), (anf els return_hole))))))
    | ESeq([]) -> failwith "empty seq"
    | ESeq([e]) -> anf e h
    | ESeq(e::es) ->
      ASeq(anf e return_hole, anf (ESeq(es)) h)
    | ELet([], body) -> anf body h
    | ELet((name, value)::rest, body) ->
      anf value (CHole(fun ce ->
        ALet(name, ce, anf (ELet(rest, body)) h)))

let rec find ls x =
  match ls with
    | [] -> None
    | (y,v)::rest ->
      if y = x then Some(v) else find rest x

let const_true = HexConst(0xffffffff)
let const_false = HexConst(0x7fffffff)

let acompile_imm_arg (i : immexpr) _ (env : int envt) : arg =
  match i with
    | ImmNumber(n) ->
      Const((n lsl 1))
    | ImmBool(b) ->
      if b then const_true else const_false
    | ImmId(name) ->
      begin match find env name with
        | Some(stackloc) -> RegOffset(-4 * stackloc, EBP)
        | None -> failwith ("Unbound identifier in compile: " ^ name)
      end

let acompile_imm (i : immexpr) (si : int) (env : int envt) : instruction list =
  [ IMov(Reg(EAX), acompile_imm_arg i si env) ]

let throw_err code = 
  [
    IPush(Sized(DWORD_PTR, Const(code)));
    ICall(Label("error"));
  ]

let check_overflow = IJo("overflow_check")
let error_non_int = "error_non_int"
let error_non_bool = "error_non_bool"
let error_non_tuple = "error_non_tuple"
let error_non_function = "error_non_function"
let error_too_small = "error_too_small"
let error_too_large = "error_too_large"
let error_arity = "error_arity"

let check_pair =
  [
    IAnd(Reg(EAX), Const(0x00000007));
    ICmp(Reg(EAX), Const(0x00000001));
    IJne(error_non_tuple)
  ]

let check_num =
  [
    IAnd(Reg(EAX), Const(0x00000001));
    ICmp(Reg(EAX), Const(0x00000000));
    IJne(error_non_int)
  ]

let max n m = if n > m then n else m
let rec count_c_vars (ce : cexpr) : int =
  match ce with
    | CIf(_, thn, els) ->
      max (count_vars thn) (count_vars els)
    | _ -> 0

and count_vars (ae : aexpr) : int =
  match ae with
    | ALet(x, bind, body) -> 
      1 + (max (count_c_vars bind) (count_vars body))
    | ASeq(ce1, e2) -> 
      (max (count_vars ce1) (count_vars e2))
    | ACExpr(ce) -> count_c_vars ce

let rec contains x ids =
  match ids with
    | [] -> false
    | elt::xs -> (x = elt) || (contains x xs)

let add_set x ids =
  if contains x ids then ids
  else x::ids

let rec remove_dups lst =
  match lst with
    | [] -> []
    | x::xs ->
      if contains x xs then remove_dups xs else x::(remove_dups xs)

let freevars_i ids ie =
  match ie with
    | ImmId(x) -> if contains x ids then [] else [x]
    | _ -> []

let rec freevars_c ids e =
  match e with
    | CPrim1(p, ie) ->
      freevars_i ids ie
    | CPrim2(p, left, right) ->
      (freevars_i ids left) @ (freevars_i ids right)
    | CApp(f, args) ->
      (freevars_i ids f) @ (List.flatten (List.map (fun a -> freevars_i ids a) args))
    | CPair(left, right) ->
      (freevars_i ids left) @ (freevars_i ids right)
    | CLambda(args, body) ->
      freevars_e (args @ ids) body
    | CIf(c, t, e) ->
      (freevars_i ids c) @ (freevars_e ids t) @ (freevars_e ids e)
    | CImmExpr(ie) ->
      freevars_i ids ie


and freevars_e ids e =
  match e with
    | ALet(x, bind, body) ->
      (freevars_c ids bind) @ (freevars_e (x::ids) body)
    | ASeq(e1, e2) ->
      (freevars_e ids e1) @ (freevars_e ids e2)
    | ACExpr(ec) -> freevars_c ids ec

and freevars e =
  remove_dups (freevars_e [] e)

let check_nums arg1 arg2 =
  [
    IMov(Reg(EAX), arg1) 
  ] @ check_num @ [
    IMov(Reg(EAX), arg2);
  ] @ check_num

(*

Values:

  0xXXXXXXX[xxx0] - Number
  0xFFFFFFF[1111] - True
  0x7FFFFFF[1111] - False
  0xXXXXXXX[x001] - Pair

    -> [ type tag ] : [ GC word ] : [ 4-byte value ] : [ 4-byte value ]

  0xXXXXXXX[x101] - Closure

    -> [ type tag ] : [ GC word ] : [ 4-byte varcount = N ] : [ 4-byte arity ] : [ 4-byte code ptr ] : [ N*4 bytes of data ]

  0xXXXXXXX[x011] - Variable

    -> [ type tag ] : [ GC word ] : [ 4-byte value ]

  A gc-word is initially all zeroes.  During GC, the LSB is used as the mark
  bit, and the rest of the word stores a forwarding address.

  The type tag is necessary within the heap now, because we need to be able to
  walk the structure of the heap without having any references into it.

  We can find all the live data by looking at the space between ebps, skipping
  one for the return pointer.

  So GC will need the current ESP (top of stack pointer), the current EBP (to
  start walking the stack), a special word on the stack for when to _stop_
  walking with EBP (maybe a special token address from main), the start-of-heap
  pointer, and the heap size.

*)

let reserve size si =
  let ok = gen_temp "memcheck" in
  [
    IMov(Reg(EAX), LabelContents("HEAP_END"));
    ISub(Reg(EAX), Const(size));
    ICmp(Reg(EAX), Reg(ESI));
    IJge(ok);
    IMov(Reg(EAX), Reg(ESP));
    IPush(Reg(EAX)); (* stack_top in C *)
    IPush(Reg(EBP)); (* first_frame in C *)
    IPush(Const(size)); (* bytes_needed in C *)
    IPush(Reg(ESI)); (* alloc_ptr in C *)
    ICall(Label("try_gc"));
    IAdd(Reg(ESP), Const(16)); (* clean up after call *)
    (* assume gc success if returning here, so EAX holds the new ESI value *)
    IMov(Reg(ESI), Reg(EAX));
    ILabel(ok);
  ]


let rec acompile_step (s : cexpr) (si : int) (env : int envt) : instruction list =
  match s with
(*
----------------------------------------
| tag | gc word | left_val | right_val |
----------------------------------------
*)
    | CPair(left, right) ->
      let as_args = List.map (fun e -> acompile_imm_arg e si env) [left; right] in
      let movs = List.mapi (fun i a -> [
        IMov(Reg(EAX), Sized(DWORD_PTR, a));
        IMov(Sized(DWORD_PTR, RegOffset((i + 2) * 4, ESI)), Reg(EAX))]) as_args in
      let bump = IAdd(Reg(ESI), Const(16)) in
      let store_gc = IMov(RegOffset(4, ESI), Sized(DWORD_PTR, Const(0))) in
      let store_tag = IMov(RegOffset(0, ESI), Sized(DWORD_PTR, Const(1))) in
      let answer = [IMov(Reg(EAX), Reg(ESI)); IAdd(Reg(EAX), Const(1))] in
      (reserve 16 si) @ [store_gc; store_tag] @ ((List.flatten movs) @ answer @ [bump])
(*
-----------------------------------------------------------------------------
| tag | gc word | varcount | argcount | address | arg | ... | maybe_padding |
-----------------------------------------------------------------------------
*)
    | CLambda(ids, body) ->
      let frees = freevars (ACExpr(s)) in
      let bodylocs = List.mapi (fun i a -> (a, i + 1)) frees in

      let arglocs = List.mapi (fun i a -> (a, -1 * (i + 3))) ids in

      let name = gen_temp "closure" in
      (* Assume address of closure in EAX *)
      let free_copies = List.map (fun (_, l) ->
        IPush(Sized(DWORD_PTR, RegOffset(((l + 4) * 4) - 5, EAX)))
      ) bodylocs in
      let free_setup = (IMov(Reg(EAX), RegOffset(8, EBP)))::free_copies in
      let body_env = (arglocs @ bodylocs) in

      let body_exprs = [
        ILabel(name);
        IPush(Reg(EBP));
        IMov(Reg(EBP), Reg(ESP));
      ] @
      free_setup @
      (acompile_expr body ((List.length frees) + 1) body_env) @
      [
        IMov(Reg(ESP), Reg(EBP));
        IPop(Reg(EBP));
        IRet;
      ] in
      
      let as_args = List.map (fun id -> acompile_imm_arg (ImmId(id)) si env) frees in
      let free_movs = List.mapi (fun i a -> [
        IMov(Reg(EAX), Sized(DWORD_PTR, a));
        IMov(Sized(DWORD_PTR, RegOffset((i + 5) * 4, ESI)), Reg(EAX))]) as_args in
      let needed_space = (List.length frees) + 5 in
      let with_padding = needed_space + (needed_space mod 2) in
      let bump = IAdd(Reg(ESI), Sized(DWORD_PTR, Const(with_padding * 4))) in
      let answer = [IMov(Reg(EAX), Reg(ESI)); IAdd(Reg(EAX), Const(5))] in

      let store_addr = IMov(RegOffset(16, ESI), Sized(DWORD_PTR, Label(name))) in
      let store_size = IMov(RegOffset(12, ESI), Sized(DWORD_PTR, Const(List.length ids))) in
      let store_fvcount = IMov(RegOffset(8, ESI), Sized(DWORD_PTR, Const(List.length frees))) in
      let store_gc = IMov(RegOffset(4, ESI), Sized(DWORD_PTR, Const(0))) in
      let store_tag = IMov(RegOffset(0, ESI), Sized(DWORD_PTR, Const(5))) in
      let after = gen_temp "after_body" in

      (reserve (with_padding * 4) si) @ (List.flatten free_movs) @ [store_addr; store_size; store_fvcount; store_gc; store_tag] @ answer @ [bump] @ (IJmp(Label(after)))::(body_exprs @ [ILabel(after)])


    | CApp(f, iargs) ->
      let f_arg = acompile_imm_arg f si env in
      let argpushes = List.rev_map (fun a -> IPush(Sized(DWORD_PTR, acompile_imm_arg a si env))) iargs in
      let esp_dist = 4 * (List.length iargs) in
      argpushes @ [
        IMov(Reg(EAX), f_arg);
        IAnd(Reg(EAX), HexConst(0x0000007));
        ICmp(Reg(EAX), Const(5));
        IJne(error_non_function);
        IMov(Reg(EAX), f_arg);
        ICmp(Sized(DWORD_PTR, RegOffset(7, EAX)), Const(List.length iargs));
        IJne(error_arity);
        IPush(Sized(DWORD_PTR, Reg(EAX)));
        ICall(RegOffset(11, EAX));
        IAdd(Reg(ESP), Const(esp_dist + 4))
      ]
    | CPrim1(op, e) ->
      let prelude = acompile_imm e si env in
      begin match op with
        | Fst ->
          prelude @ check_pair @ prelude @ [
            IMov(Reg(EAX), RegOffset(7, EAX));
          ]
        | Snd ->
          prelude @ check_pair @ prelude @ [
            IMov(Reg(EAX), RegOffset(11, EAX));
          ]
        | Add1 ->
          prelude @ check_num @ prelude @ [
            IAdd(Reg(EAX), Const(2))
          ]
        | Sub1 ->
          prelude @ check_num @ prelude @ [
            IAdd(Reg(EAX), Const(-2))
          ]
        | IsNum ->
          prelude @ [
            IAnd(Reg(EAX), Const(0x00000001));
            IShl(Reg(EAX), Const(31));
            IXor(Reg(EAX), Const(0xFFFFFFFF));
          ]
        | IsBool ->
          let skip = gen_temp "isbool" in
          prelude @ [
            IAnd(Reg(EAX), Const(0x00000007));
            ICmp(Reg(EAX), Const(0x00000007));
            IMov(Reg(EAX), const_false);
            IJne(skip);
            IMov(Reg(EAX), const_true);
            ILabel(skip);
          ]
        | IsPair ->
          let skip = gen_temp "ispair" in
          prelude @ [
            IAnd(Reg(EAX), Const(0x00000007));
            ICmp(Reg(EAX), Const(0x00000001));
            IMov(Reg(EAX), const_false);
            IJne(skip);
            IMov(Reg(EAX), const_true);
            ILabel(skip);
          ]
        | Print ->
          prelude @ [
            IPush(Sized(DWORD_PTR, Reg(EAX)));
            ICall(Label("print"));
            IPop(Reg(EAX));
          ]
      end

    | CPrim2(op, left, right) ->
      let left_as_arg = acompile_imm_arg left si env in
      let right_as_arg = acompile_imm_arg right si env in
      let checked = check_nums left_as_arg right_as_arg in
      let do_set index =
        [
          IMov(Reg(EAX), left_as_arg);
        ] @ check_pair @
        [
          IMov(Reg(ECX), left_as_arg);
          IMov(Reg(EAX), right_as_arg);
          IMov(RegOffset(index, ECX), Reg(EAX));
        ] in
      begin match op with
        | SetFst -> do_set 7
        | SetSnd -> do_set 11
        | Plus ->
          checked @
          [
            IMov(Reg(EAX), left_as_arg);
            IAdd(Reg(EAX), right_as_arg);
            check_overflow
          ]
        | Minus ->
          checked @
          [
            IMov(Reg(EAX), left_as_arg);
            ISub(Reg(EAX), right_as_arg);
            check_overflow
          ]
        | Times ->
          checked @
          [
            IMov(Reg(EAX), left_as_arg);
            ISar(Reg(EAX), Const(1));
            IMul(Reg(EAX), right_as_arg);
            check_overflow;
          ]
        | Less ->
          checked @
          [
            IMov(Reg(EAX), left_as_arg);
            ISub(Reg(EAX), right_as_arg);
            ISub(Reg(EAX), Const(1));
            IAnd(Reg(EAX), HexConst(0x80000000));
            IOr( Reg(EAX), HexConst(0x7FFFFFFF));
          ]
        | Greater ->
          checked @
          [
            IMov(Reg(EAX), left_as_arg);
            ISub(Reg(EAX), right_as_arg);
            IAnd(Reg(EAX), HexConst(0x80000000));
            IXor(Reg(EAX), HexConst(0xFFFFFFFF));
          ]
        | Equal ->
          [
            IPush(Sized(DWORD_PTR, right_as_arg));
            IPush(Sized(DWORD_PTR, left_as_arg));
            ICall(Label("equal"));
            IAdd(Reg(ESP), Const(8));
          ]
       end
    | CImmExpr(i) -> acompile_imm i si env
    | CIf(cond, thn, els) ->
      let prelude = acompile_imm cond si env in
      let thn = acompile_expr thn si env in
      let els = acompile_expr els si env in
      let label_then = gen_temp "then" in
      let label_else = gen_temp "else" in
      let label_end = gen_temp "end" in
      prelude @ [
        ICmp(Reg(EAX), const_true);
        IJe(label_then);
        ICmp(Reg(EAX), const_false);
        IJe(label_else);
        IJmp(Label(error_non_bool));
        ILabel(label_then)
      ] @
      thn @
      [ IJmp(Label(label_end)); ILabel(label_else) ] @
      els @
      [ ILabel(label_end) ]

and acompile_expr (e : aexpr) (si : int) (env : int envt) : instruction list =
  match e with
    | ASeq(e1, e2) ->
      (acompile_expr e1 si env) @ (acompile_expr e2 si env)
    | ALet(id, e, body) ->
      let prelude = acompile_step e (si + 1) env in
      let postlude = acompile_expr body (si + 1) ((id, si)::env) in
      prelude @ [
        IPush(Reg(EAX));
      ] @ postlude @ [
        IAdd(Reg(ESP), Const(4))
      ]
    | ACExpr(s) -> acompile_step s si env

let rec find_one (l : 'a list) (elt : 'a) : bool =
  match l with
    | [] -> false
    | x::xs -> (elt = x) || (find_one xs elt)

let rec find_dup (l : 'a list) : 'a option =
  match l with
    | [] -> None
    | [x] -> None
    | x::xs ->
      if find_one xs x then Some(x) else find_dup xs

let rec well_formed_e (e : expr) (env : bool envt) =
  match e with
    | ELambda(_, _) -> []
    | ENumber(_)
    | EBool(_) -> []
    | EPair(left, right) ->
      (well_formed_e left env) @ (well_formed_e right env)
    | EId(x) ->
      begin match find env x with
        | None -> ["Unbound identifier: " ^ x]
        | Some(_) -> []
      end
    | EPrim1(op, e) ->
      well_formed_e e env
    | EPrim2(op, left, right) ->
      (well_formed_e left env) @ (well_formed_e right env)
    | EIf(cond, thn, els) ->
      (well_formed_e cond env) @
      (well_formed_e thn env) @
      (well_formed_e els env)
    | EApp(expr, args) ->
      let from_args = List.flatten (List.map (fun a -> well_formed_e a env) args) in
      (well_formed_e expr env) @ from_args
    | ESeq(es) ->
      List.flatten (List.map (fun e -> well_formed_e e env) es)
    | ELet(binds, body) ->
      let names = List.map fst binds in
      let env_from_binds = List.map (fun a -> (a, true)) names in
      let from_body = well_formed_e body (env_from_binds @ env) in
      begin match find_dup names with
        | None -> from_body
        | Some(name) -> ("Duplicate name in let: " ^ name)::from_body
      end

let compile_to_string (prog : expr) =
  match well_formed_e prog [] with
    | x::rest ->
      let errstr = (List.fold_left (fun x y -> x ^ "\n" ^ y) "" (x::rest)) in
      failwith errstr
    | [] ->
      let anfed = (anf prog return_hole) in
      count := 0;
      let compiled_main = (acompile_expr anfed 1 []) in
      let prelude = "
section .text
extern error
extern print
extern equal
extern try_gc
extern HEAP_END
extern STACK_BOTTOM
global our_code_starts_here" in
          let main_start = [
            ILabel("our_code_starts_here");
            IMov(Reg(ESI), RegOffset(4, ESP));
            IPush(Reg(EBP));
            IMov(Reg(EBP), Reg(ESP));
            IMov(LabelContents("STACK_BOTTOM"), Reg(EBP))
          ] in
          let postlude = [
            IMov(Reg(ESP), Reg(EBP));
            IPop(Reg(EBP));
            IRet;
            ILabel("overflow_check")
          ]
          @ (throw_err 3)
          @ [ILabel(error_non_int)] @ (throw_err 1)
          @ [ILabel(error_non_bool)] @ (throw_err 2)
          @ [ILabel(error_non_tuple)] @ (throw_err 4)
          @ [ILabel(error_too_small)] @ (throw_err 5)
          @ [ILabel(error_too_large)] @ (throw_err 6)
          @ [ILabel(error_arity)] @ (throw_err 7)
          @ [ILabel(error_non_function)] @ (throw_err 8) in
          let as_assembly_string = (to_asm (
            main_start @
            compiled_main @
            postlude)) in
          sprintf "%s%s\n" prelude as_assembly_string

