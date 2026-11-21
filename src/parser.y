%code requires {
#include "ast.h"
}

%locations

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

extern int yylex(void);
extern char *yytext;

ASTNode *ast_root = NULL;
int erros_sintaticos = 0;

void yyerror(const char *s);
%}

%union {
    int ival;
    double fval;
    char *sval;
    ASTNode *node;
}

%token <ival> T_INT
%token <fval> T_REAL
%token <sval> T_STRING T_ID
%token T_TRUE T_FALSE

%token T_DEF T_RETURN T_IF T_ELIF T_ELSE T_WHILE T_FOR T_IN
%token T_IMPORT T_GLOBAL T_PASS
%token T_AND T_OR T_NOT

%token T_COLON T_LPAREN T_RPAREN T_LBRACKET T_RBRACKET T_COMMA T_DOT
%token T_ASSIGN T_NEWLINE
%token T_INDENT T_DEDENT

%token T_PLUS T_MINUS T_MULTIPLY T_DIVIDE T_FLOORDIV T_MOD
%token T_EQ T_NEQ T_LT T_LE T_GT T_GE

%type <node> programa lista_comandos item_comando comando simple_stmt compound_stmt
%type <node> atribuicao return_stmt import_stmt global_stmt
%type <node> if_stmt elif_chain while_stmt for_stmt funcao bloco
%type <node> parametros_opt parametros
%type <node> condicao and_expr not_expr rel_expr expressao termo fator primary
%type <node> arg_list_opt arg_list

%start programa

%%

programa:
      /* vazio */ {
          ast_root = ast_program(NULL);
          $$ = ast_root;
      }
    | lista_comandos {
          ast_root = ast_program($1);
          $$ = ast_root;
      }
    ;

lista_comandos:
      item_comando {
          $$ = $1;
      }
    | lista_comandos item_comando {
          $$ = ast_append($1, $2);
      }
    ;

item_comando:
      T_NEWLINE {
          $$ = NULL;
      }
    | comando {
          $$ = $1;
      }
    ;

comando:
      simple_stmt T_NEWLINE {
          $$ = $1;
      }
    | compound_stmt {
          $$ = $1;
      }
    ;

simple_stmt:
      atribuicao {
          $$ = $1;
      }
    | return_stmt {
          $$ = $1;
      }
    | import_stmt {
          $$ = $1;
      }
    | global_stmt {
          $$ = $1;
      }
    | T_PASS {
          $$ = ast_pass(@1.first_line, @1.first_column);
      }
    | condicao {
          $$ = ast_expr_stmt($1, @1.first_line, @1.first_column);
      }
    ;

compound_stmt:
      if_stmt { $$ = $1; }
    | while_stmt { $$ = $1; }
    | for_stmt { $$ = $1; }
    | funcao { $$ = $1; }
    ;

atribuicao:
      T_ID T_ASSIGN condicao {
          $$ = ast_assign($1, $3, @1.first_line, @1.first_column);
          free($1);
      }
    | T_ID T_LBRACKET expressao T_RBRACKET T_ASSIGN condicao {
          $$ = ast_index_assign($1, $3, $6, @1.first_line, @1.first_column);
          free($1);
      }
    ;

import_stmt:
      T_IMPORT T_ID {
          $$ = ast_import($2, @1.first_line, @1.first_column);
          free($2);
      }
    ;

global_stmt:
      T_GLOBAL T_ID {
          $$ = ast_global($2, @1.first_line, @1.first_column);
          free($2);
      }
    ;

return_stmt:
      T_RETURN {
          $$ = ast_return(NULL, @1.first_line, @1.first_column);
      }
    | T_RETURN condicao {
          $$ = ast_return($2, @1.first_line, @1.first_column);
      }
    ;

if_stmt:
      T_IF condicao T_COLON bloco elif_chain {
          $$ = ast_if($2, $4, $5, @1.first_line, @1.first_column);
      }
    ;

elif_chain:
      /* vazio */ {
          $$ = NULL;
      }
    | T_ELSE T_COLON bloco {
          $$ = $3;
      }
    | T_ELIF condicao T_COLON bloco elif_chain {
          $$ = ast_if($2, $4, $5, @1.first_line, @1.first_column);
      }
    ;

while_stmt:
      T_WHILE condicao T_COLON bloco {
          $$ = ast_while($2, $4, @1.first_line, @1.first_column);
      }
    ;

for_stmt:
      T_FOR T_ID T_IN expressao T_COLON bloco {
          $$ = ast_for($2, $4, $6, @1.first_line, @1.first_column);
          free($2);
      }
    ;

funcao:
      T_DEF T_ID T_LPAREN parametros_opt T_RPAREN T_COLON bloco {
          $$ = ast_func_def($2, $4, $7, @1.first_line, @1.first_column);
          free($2);
      }
    ;

parametros_opt:
      /* vazio */ {
          $$ = NULL;
      }
    | parametros {
          $$ = $1;
      }
    ;

parametros:
      T_ID {
          $$ = ast_param($1, @1.first_line, @1.first_column);
          free($1);
      }
    | parametros T_COMMA T_ID {
          $$ = ast_append($1, ast_param($3, @3.first_line, @3.first_column));
          free($3);
      }
    ;

bloco:
      T_NEWLINE T_INDENT lista_comandos T_DEDENT {
          $$ = $3;
      }
    ;

condicao:
      and_expr {
          $$ = $1;
      }
    | condicao T_OR and_expr {
          $$ = ast_binop(OP_OR, $1, $3, @2.first_line, @2.first_column);
      }
    ;

and_expr:
      not_expr {
          $$ = $1;
      }
    | and_expr T_AND not_expr {
          $$ = ast_binop(OP_AND, $1, $3, @2.first_line, @2.first_column);
      }
    ;

not_expr:
      rel_expr {
          $$ = $1;
      }
    | T_NOT not_expr {
          $$ = ast_unop(OP_NOT, $2, @1.first_line, @1.first_column);
      }
    ;

rel_expr:
      expressao {
          $$ = $1;
      }
    | expressao T_EQ expressao {
          $$ = ast_binop(OP_EQ, $1, $3, @2.first_line, @2.first_column);
      }
    | expressao T_NEQ expressao {
          $$ = ast_binop(OP_NEQ, $1, $3, @2.first_line, @2.first_column);
      }
    | expressao T_LT expressao {
          $$ = ast_binop(OP_LT, $1, $3, @2.first_line, @2.first_column);
      }
    | expressao T_LE expressao {
          $$ = ast_binop(OP_LE, $1, $3, @2.first_line, @2.first_column);
      }
    | expressao T_GT expressao {
          $$ = ast_binop(OP_GT, $1, $3, @2.first_line, @2.first_column);
      }
    | expressao T_GE expressao {
          $$ = ast_binop(OP_GE, $1, $3, @2.first_line, @2.first_column);
      }
    ;

expressao:
      termo {
          $$ = $1;
      }
    | expressao T_PLUS termo {
          $$ = ast_binop(OP_ADD, $1, $3, @2.first_line, @2.first_column);
      }
    | expressao T_MINUS termo {
          $$ = ast_binop(OP_SUB, $1, $3, @2.first_line, @2.first_column);
      }
    ;

termo:
      fator {
          $$ = $1;
      }
    | termo T_MULTIPLY fator {
          $$ = ast_binop(OP_MUL, $1, $3, @2.first_line, @2.first_column);
      }
    | termo T_DIVIDE fator {
          $$ = ast_binop(OP_DIV, $1, $3, @2.first_line, @2.first_column);
      }
    | termo T_FLOORDIV fator {
          $$ = ast_binop(OP_FLOORDIV, $1, $3, @2.first_line, @2.first_column);
      }
    | termo T_MOD fator {
          $$ = ast_binop(OP_MOD, $1, $3, @2.first_line, @2.first_column);
      }
    ;

fator:
      T_MINUS fator {
          $$ = ast_unop(OP_NEG, $2, @1.first_line, @1.first_column);
      }
    | primary {
          $$ = $1;
      }
    ;

primary:
      T_INT {
          $$ = ast_int($1, @1.first_line, @1.first_column);
      }
    | T_REAL {
          $$ = ast_double($1, @1.first_line, @1.first_column);
      }
    | T_TRUE {
          $$ = ast_bool(1, @1.first_line, @1.first_column);
      }
    | T_FALSE {
          $$ = ast_bool(0, @1.first_line, @1.first_column);
      }
    | T_STRING {
          $$ = ast_string($1, @1.first_line, @1.first_column);
          free($1);
      }
    | T_ID {
          $$ = ast_var($1, @1.first_line, @1.first_column);
          free($1);
      }
    | T_ID T_LPAREN arg_list_opt T_RPAREN {
          $$ = ast_call($1, $3, @1.first_line, @1.first_column);
          free($1);
      }
    | T_ID T_DOT T_ID T_LPAREN arg_list_opt T_RPAREN {
          $$ = ast_method_call($1, $3, $5, @1.first_line, @1.first_column);
          free($1);
          free($3);
      }
    | T_ID T_LBRACKET expressao T_RBRACKET {
          ASTNode *var = ast_var($1, @1.first_line, @1.first_column);
          $$ = ast_index(var, $3, @1.first_line, @1.first_column);
          free($1);
      }
    | T_LBRACKET arg_list_opt T_RBRACKET {
          $$ = ast_list_lit($2, @1.first_line, @1.first_column);
      }
    | T_LPAREN condicao T_RPAREN {
          $$ = $2;
      }
    ;

arg_list_opt:
      /* vazio */ {
          $$ = NULL;
      }
    | arg_list {
          $$ = $1;
      }
    ;

arg_list:
      condicao {
          $$ = $1;
      }
    | arg_list T_COMMA condicao {
          $$ = ast_append($1, $3);
      }
    ;

%%

void yyerror(const char *s) {
    erros_sintaticos++;
    fprintf(stderr, "[ERRO SINTATICO] Linha %d, Coluna %d: %s proximo ao token '%s'\n",
            yylloc.first_line, yylloc.first_column, s, yytext ? yytext : "");
}
