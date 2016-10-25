%{
open Expr

%}

%token <int> NUM
%token <string> ID
%token SETFST SETSND BEGIN END LBRACK RBRACK DEF FST SND ADD1 SUB1 LPAREN RPAREN LET IN EQUAL COMMA PLUS MINUS TIMES IF COLON ELSECOLON TRUE FALSE ISBOOL ISPAIR ISNUM LAMBDA EQEQ LESS GREATER PRINT SEMI EOF

%left PLUS MINUS TIMES GREATER LESS EQEQ



%type <Expr.expr> program

%start program

%%

const :
  | NUM { ENumber($1) }
  | TRUE { EBool(true) }
  | FALSE { EBool(false) }

prim1 :
  | ADD1 { Add1 }
  | SUB1 { Sub1 }
  | PRINT { Print }
  | ISBOOL { IsBool }
  | ISNUM { IsNum }
  | ISPAIR { IsPair }
  | FST { Fst }
  | SND { Snd }

prim2 :
  | SETFST { SetFst }
  | SETSND { SetSnd }

binds :
  | ID EQUAL expr { [($1, $3)] }
  | ID EQUAL expr COMMA binds { ($1, $3)::$5 }

ids :
  | ID { [$1] }
  | ID COMMA ids { $1::$3 }

exprs :
  | expr { [$1] }
  | expr COMMA exprs { $1::$3 }

simple_expr :
  | prim1 LPAREN expr RPAREN { EPrim1($1, $3) }
  | prim2 LPAREN expr COMMA expr RPAREN { EPrim2($1, $3, $5) }
  | LPAREN expr COMMA expr RPAREN { EPair($2, $4) }
  | simple_expr LPAREN exprs RPAREN { EApp($1, $3) }
  | simple_expr LPAREN RPAREN { EApp($1, []) }
  | LPAREN LAMBDA ids COLON expr RPAREN { ELambda($3, $5) }
  | LPAREN LAMBDA COLON expr RPAREN { ELambda([], $4) }
  | LPAREN expr RPAREN { $2 }
  | BEGIN block_exprs END { ESeq($2) }
  | const { $1 }
  | ID { EId($1) }

binop_expr :
  | binop_expr PLUS simple_expr { EPrim2(Plus, $1, $3) }
  | binop_expr MINUS simple_expr { EPrim2(Minus, $1, $3) }
  | binop_expr TIMES simple_expr { EPrim2(Times, $1, $3) }
  | binop_expr EQEQ simple_expr { EPrim2(Equal, $1, $3) }
  | binop_expr LESS simple_expr { EPrim2(Less, $1, $3) }
  | binop_expr GREATER simple_expr { EPrim2(Greater, $1, $3) }
  | simple_expr { $1 }

expr :
  | LET binds IN expr { ELet($2, $4) }
  | IF expr COLON expr ELSECOLON expr { EIf($2, $4, $6) }
  | binop_expr { $1 }

block_exprs :
  | expr SEMI { [$1] }
  | expr SEMI block_exprs { $1::$3 }

program : expr EOF { $1 }

%%

