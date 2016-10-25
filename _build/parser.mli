type token =
  | NUM of (int)
  | ID of (string)
  | SETFST
  | SETSND
  | BEGIN
  | END
  | LBRACK
  | RBRACK
  | DEF
  | FST
  | SND
  | ADD1
  | SUB1
  | LPAREN
  | RPAREN
  | LET
  | IN
  | EQUAL
  | COMMA
  | PLUS
  | MINUS
  | TIMES
  | IF
  | COLON
  | ELSECOLON
  | TRUE
  | FALSE
  | ISBOOL
  | ISPAIR
  | ISNUM
  | LAMBDA
  | EQEQ
  | LESS
  | GREATER
  | PRINT
  | SEMI
  | EOF

val program :
  (Lexing.lexbuf  -> token) -> Lexing.lexbuf -> Expr.expr
