%option noyywrap

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "parser.tab.h"

int linha_atual = 1;
int coluna_atual = 1;
int erros_lexicos = 0;

#define MAX_INDENT_LEVELS 128
static int indent_stack[MAX_INDENT_LEVELS] = {0};
static int indent_top = 0;
static int pending_dedents = 0;
static int bracket_depth = 0;
static int eof_newline_sent = 0;
static int inicio_scanner = 1;

/* Tabela de Tokens */
typedef struct {
    char *lexema;
    char *tipo;
    int linha;
    int coluna;
} TokenInfo;

static TokenInfo *tabela_tokens = NULL;
static int total_tokens = 0;
static int capacidade_tokens = 0;

static char *copiar_string(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *c = (char *)malloc(len + 1);
    if (c) memcpy(c, s, len + 1);
    return c;
}

static char *decodificar_string(const char *raw) {
    size_t len = strlen(raw);
    if (len < 2) return copiar_string("");
    char *out = (char *)malloc(len + 1);
    size_t j = 0;
    for (size_t i = 1; i + 1 < len; i++) {
        if (raw[i] == '\\' && i + 2 < len) {
            i++;
            switch (raw[i]) {
                case 'n':  out[j++] = '\n'; break;
                case 't':  out[j++] = '\t'; break;
                case 'r':  out[j++] = '\r'; break;
                case '\\': out[j++] = '\\'; break;
                case '"':  out[j++] = '"';  break;
                case '\'': out[j++] = '\''; break;
                default:   out[j++] = raw[i]; break;
            }
        } else {
            out[j++] = raw[i];
        }
    }
    out[j] = '\0';
    return out;
}

void registrar_token(const char *lexema, const char *tipo, int linha, int coluna) {
    if (total_tokens >= capacidade_tokens) {
        capacidade_tokens = (capacidade_tokens == 0) ? 128 : capacidade_tokens * 2;
        tabela_tokens = (TokenInfo *)realloc(tabela_tokens, (size_t)capacidade_tokens * sizeof(TokenInfo));
    }
    tabela_tokens[total_tokens].lexema = copiar_string(lexema);
    tabela_tokens[total_tokens].tipo = copiar_string(tipo);
    tabela_tokens[total_tokens].linha = linha;
    tabela_tokens[total_tokens].coluna = coluna;
    total_tokens++;
}

void exibir_tabela_tokens(void) {
    printf("\n==================== TABELA DE TOKENS ====================\n");
    printf("%-6s | %-6s | %-25s | %s\n", "LINHA", "COLUNA", "TIPO DO TOKEN", "LEXEMA");
    for (int i = 0; i < total_tokens; i++) {
        printf("%-6d | %-6d | %-25s | %s\n",
               tabela_tokens[i].linha,
               tabela_tokens[i].coluna,
               tabela_tokens[i].tipo,
               tabela_tokens[i].lexema);
    }
    printf("Total de tokens reconhecidos: %d\n", total_tokens);
    printf("==========================================================\n");
}

void limpar_tabela_tokens(void) {
    for (int i = 0; i < total_tokens; i++) {
        free(tabela_tokens[i].lexema);
        free(tabela_tokens[i].tipo);
    }
    free(tabela_tokens);
    tabela_tokens = NULL;
    total_tokens = 0;
    capacidade_tokens = 0;
}

void lexer_reset(void) {
    linha_atual = 1;
    coluna_atual = 1;
    erros_lexicos = 0;
    indent_top = 0;
    indent_stack[0] = 0;
    pending_dedents = 0;
    bracket_depth = 0;
    eof_newline_sent = 0;
    inicio_scanner = 1;
    limpar_tabela_tokens();
}

#define YY_USER_ACTION \
    yylloc.first_line = linha_atual; \
    yylloc.first_column = coluna_atual; \
    yylloc.last_line = linha_atual; \
    yylloc.last_column = coluna_atual + yyleng - 1; \
    coluna_atual += yyleng;

int flex_yylex(void);
#define YY_DECL int flex_yylex(void)

%}

%x LINE_START

DIGITO [0-9]
ID [a-zA-Z_][a-zA-Z0-9_]*
STRING_D \"([^\"\\\n]|\\.)*\"
STRING_S \'([^\'\\\n]|\\.)*\'

%%

    if (inicio_scanner) {
        inicio_scanner = 0;
        BEGIN(LINE_START);
    }

<LINE_START>{
    [ \t]*\r?\n {
        linha_atual++;
        coluna_atual = 1;
    }

    [ \t]*#[^\n]*\r?\n {
        linha_atual++;
        coluna_atual = 1;
    }

    [ \t]*#[^\n]* {
        /* comentario na ultima linha sem quebra de linha */
    }

    [ \t]+/[^ \t\r\n#] {
        int indent_cols = 0;
        for (int i = 0; i < yyleng; i++) {
            if (yytext[i] == '\t') indent_cols += 4;
            else if (yytext[i] == ' ') indent_cols += 1;
        }
        BEGIN(INITIAL);

        int current_top = indent_stack[indent_top];
        if (indent_cols > current_top) {
            if (indent_top + 1 < MAX_INDENT_LEVELS) {
                indent_top++;
                indent_stack[indent_top] = indent_cols;
                yylloc.first_line = yylloc.last_line = linha_atual;
                yylloc.first_column = yylloc.last_column = 1;
                registrar_token("<INIDEBL>", "INIDEBL", linha_atual, 1);
                return T_INDENT;
            }
        } else if (indent_cols < current_top) {
            int dedents = 0;
            while (indent_top > 0 && indent_stack[indent_top] > indent_cols) {
                indent_top--;
                dedents++;
            }
            if (indent_stack[indent_top] != indent_cols) {
                fprintf(stderr, "[ERRO LEXICO] Linha %d, Coluna 1: Indentacao inconsistente (%d espacos)\n",
                        linha_atual, indent_cols);
                erros_lexicos++;
            }
            if (dedents > 0) {
                pending_dedents = dedents - 1;
                yylloc.first_line = yylloc.last_line = linha_atual;
                yylloc.first_column = yylloc.last_column = 1;
                registrar_token("<FIMDEBL>", "FIMDEBL", linha_atual, 1);
                return T_DEDENT;
            }
        }
    }

    [ \t]+ {
        /* espacos soltos antes de EOF */
    }

    [^ \t\r\n#] {
        coluna_atual -= yyleng;
        yyless(0);
        BEGIN(INITIAL);

        if (indent_stack[indent_top] > 0) {
            int dedents = 0;
            while (indent_top > 0 && indent_stack[indent_top] > 0) {
                indent_top--;
                dedents++;
            }
            if (dedents > 0) {
                pending_dedents = dedents - 1;
                yylloc.first_line = yylloc.last_line = linha_atual;
                yylloc.first_column = yylloc.last_column = 1;
                registrar_token("<FIMDEBL>", "FIMDEBL", linha_atual, 1);
                return T_DEDENT;
            }
        }
    }

    <<EOF>> {
        if (indent_top > 0) {
            indent_top--;
            yylloc.first_line = yylloc.last_line = linha_atual;
            yylloc.first_column = yylloc.last_column = 1;
            registrar_token("<FIMDEBL>", "FIMDEBL", linha_atual, 1);
            return T_DEDENT;
        }
        yyterminate();
    }
}

<INITIAL>{
    \r?\n {
        int col = yylloc.first_column;
        int line = linha_atual;
        linha_atual++;
        coluna_atual = 1;
        if (bracket_depth == 0) {
            BEGIN(LINE_START);
            registrar_token("\\n", "NEWLINE", line, col);
            return T_NEWLINE;
        }
    }

    [ \t\r]+ ;

    #[^\n]* ;

    "return" { registrar_token(yytext, "RETURN", yylloc.first_line, yylloc.first_column); return T_RETURN; }
    "if"     { registrar_token(yytext, "IF",     yylloc.first_line, yylloc.first_column); return T_IF; }
    "elif"   { registrar_token(yytext, "ELIF",   yylloc.first_line, yylloc.first_column); return T_ELIF; }
    "else"   { registrar_token(yytext, "ELSE",   yylloc.first_line, yylloc.first_column); return T_ELSE; }
    "while"  { registrar_token(yytext, "WHILE",  yylloc.first_line, yylloc.first_column); return T_WHILE; }
    "for"    { registrar_token(yytext, "FOR",    yylloc.first_line, yylloc.first_column); return T_FOR; }
    "in"     { registrar_token(yytext, "IN",     yylloc.first_line, yylloc.first_column); return T_IN; }
    "def"    { registrar_token(yytext, "DEF",    yylloc.first_line, yylloc.first_column); return T_DEF; }
    "import" { registrar_token(yytext, "IMPORT", yylloc.first_line, yylloc.first_column); return T_IMPORT; }
    "global" { registrar_token(yytext, "GLOBAL", yylloc.first_line, yylloc.first_column); return T_GLOBAL; }
    "pass"   { registrar_token(yytext, "PASS",   yylloc.first_line, yylloc.first_column); return T_PASS; }
    "and"    { registrar_token(yytext, "AND",    yylloc.first_line, yylloc.first_column); return T_AND; }
    "or"     { registrar_token(yytext, "OR",     yylloc.first_line, yylloc.first_column); return T_OR; }
    "not"    { registrar_token(yytext, "NOT",    yylloc.first_line, yylloc.first_column); return T_NOT; }
    "True"   { registrar_token(yytext, "TRUE",   yylloc.first_line, yylloc.first_column); return T_TRUE; }
    "False"  { registrar_token(yytext, "FALSE",  yylloc.first_line, yylloc.first_column); return T_FALSE; }

    ":"  { registrar_token(yytext, "DOIS_PONTOS",      yylloc.first_line, yylloc.first_column); return T_COLON; }
    "("  { bracket_depth++; registrar_token(yytext, "ABRE_PARENTESES",  yylloc.first_line, yylloc.first_column); return T_LPAREN; }
    ")"  { if (bracket_depth > 0) bracket_depth--; registrar_token(yytext, "FECHA_PARENTESES", yylloc.first_line, yylloc.first_column); return T_RPAREN; }
    "["  { bracket_depth++; registrar_token(yytext, "ABRE_COLCHETES",   yylloc.first_line, yylloc.first_column); return T_LBRACKET; }
    "]"  { if (bracket_depth > 0) bracket_depth--; registrar_token(yytext, "FECHA_COLCHETES",  yylloc.first_line, yylloc.first_column); return T_RBRACKET; }
    ","  { registrar_token(yytext, "VIRGULA",          yylloc.first_line, yylloc.first_column); return T_COMMA; }
    "."  { registrar_token(yytext, "PONTO",            yylloc.first_line, yylloc.first_column); return T_DOT; }

    "==" { registrar_token(yytext, "IGUALDADE",        yylloc.first_line, yylloc.first_column); return T_EQ; }
    "!=" { registrar_token(yytext, "DIFERENTE",        yylloc.first_line, yylloc.first_column); return T_NEQ; }
    ">=" { registrar_token(yytext, "MAIOR_IGUAL",      yylloc.first_line, yylloc.first_column); return T_GE; }
    "<=" { registrar_token(yytext, "MENOR_IGUAL",      yylloc.first_line, yylloc.first_column); return T_LE; }
    ">"  { registrar_token(yytext, "MAIOR_QUE",        yylloc.first_line, yylloc.first_column); return T_GT; }
    "<"  { registrar_token(yytext, "MENOR_QUE",        yylloc.first_line, yylloc.first_column); return T_LT; }
    "="  { registrar_token(yytext, "ATRIBUICAO",       yylloc.first_line, yylloc.first_column); return T_ASSIGN; }

    "//" { registrar_token(yytext, "DIVISAO_INTEIRA",  yylloc.first_line, yylloc.first_column); return T_FLOORDIV; }
    "+"  { registrar_token(yytext, "MAIS",             yylloc.first_line, yylloc.first_column); return T_PLUS; }
    "-"  { registrar_token(yytext, "MENOS",            yylloc.first_line, yylloc.first_column); return T_MINUS; }
    "*"  { registrar_token(yytext, "MULTIPLICACAO",    yylloc.first_line, yylloc.first_column); return T_MULTIPLY; }
    "/"  { registrar_token(yytext, "DIVISAO",          yylloc.first_line, yylloc.first_column); return T_DIVIDE; }
    "%"  { registrar_token(yytext, "MODULO",           yylloc.first_line, yylloc.first_column); return T_MOD; }

    {DIGITO}+"."{DIGITO}+ {
        yylval.fval = strtod(yytext, NULL);
        registrar_token(yytext, "VALOR_REAL", yylloc.first_line, yylloc.first_column);
        return T_REAL;
    }

    {DIGITO}+{ID} {
        fprintf(stderr, "[ERRO LEXICO] Linha %d, Coluna %d: Identificador invalido '%s'\n",
                yylloc.first_line, yylloc.first_column, yytext);
        registrar_token(yytext, "ERRO_ID_INVALIDO", yylloc.first_line, yylloc.first_column);
        erros_lexicos++;
    }

    {DIGITO}+ {
        yylval.ival = atoi(yytext);
        registrar_token(yytext, "VALOR_INTEIRO", yylloc.first_line, yylloc.first_column);
        return T_INT;
    }

    {STRING_D}|{STRING_S} {
        yylval.sval = decodificar_string(yytext);
        registrar_token(yytext, "STRING", yylloc.first_line, yylloc.first_column);
        return T_STRING;
    }

    {ID} {
        yylval.sval = copiar_string(yytext);
        registrar_token(yytext, "IDENTIFICADOR", yylloc.first_line, yylloc.first_column);
        return T_ID;
    }

    <<EOF>> {
        if (!eof_newline_sent) {
            eof_newline_sent = 1;
            BEGIN(LINE_START);
            yylloc.first_line = yylloc.last_line = linha_atual;
            yylloc.first_column = yylloc.last_column = coluna_atual;
            registrar_token("\\n", "NEWLINE", linha_atual, coluna_atual);
            return T_NEWLINE;
        }
        BEGIN(LINE_START);
    }

    . {
        fprintf(stderr, "[ERRO LEXICO] Linha %d, Coluna %d: Caractere nao reconhecido '%s'\n",
                yylloc.first_line, yylloc.first_column, yytext);
        registrar_token(yytext, "ERRO_CARACTERE_INVALIDO", yylloc.first_line, yylloc.first_column);
        erros_lexicos++;
    }
}

%%

int yylex(void) {
    if (pending_dedents > 0) {
        pending_dedents--;
        yylloc.first_line = yylloc.last_line = linha_atual;
        yylloc.first_column = yylloc.last_column = 1;
        registrar_token("<FIMDEBL>", "FIMDEBL", linha_atual, 1);
        return T_DEDENT;
    }
    return flex_yylex();
}
