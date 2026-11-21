#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "symtab.h"
#include "semantic.h"
#include "codegen.h"

extern int yyparse(void);
extern FILE *yyin;
extern ASTNode *ast_root;
extern int erros_lexicos;
extern int erros_sintaticos;

extern void exibir_tabela_tokens(void);
extern void limpar_tabela_tokens(void);
extern void lexer_reset(void);

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Uso: %s <arquivo.py> [opcoes]\n"
            "Opcoes:\n"
            "  -o <arquivo.j>    Arquivo de saida Jasmin (padrao: output.j)\n"
            "  -c <Classe>       Nome da classe JVM gerada (padrao: Programa)\n"
            "  --tokens          Exibe a tabela de tokens reconhecidos\n"
            "  --ast             Exibe a arvore sintatica abstrata (AST)\n"
            "  --symtab          Exibe a tabela de simbolos\n",
            prog);
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = "output.j";
    const char *class_name = "Programa";
    int show_tokens = 0;
    int show_ast = 0;
    int show_symtab = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            class_name = argv[++i];
        } else if (strcmp(argv[i], "--tokens") == 0) {
            show_tokens = 1;
        } else if (strcmp(argv[i], "--ast") == 0) {
            show_ast = 1;
        } else if (strcmp(argv[i], "--symtab") == 0) {
            show_symtab = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Opcao desconhecida: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            input_path = argv[i];
        }
    }

    if (!input_path) {
        print_usage(argv[0]);
        return 1;
    }

    yyin = fopen(input_path, "r");
    if (!yyin) {
        fprintf(stderr, "Erro ao abrir arquivo de entrada '%s'\n", input_path);
        return 1;
    }

    lexer_reset();
    erros_sintaticos = 0;
    ast_root = NULL;

    int parse_status = yyparse();
    fclose(yyin);

    if (show_tokens) {
        exibir_tabela_tokens();
    }

    if (erros_lexicos > 0 || erros_sintaticos > 0 || parse_status != 0) {
        fprintf(stderr, "Compilacao abortada: %d erro(s) lexico(s), %d erro(s) sintatico(s).\n",
                erros_lexicos, erros_sintaticos);
        ast_free(ast_root);
        limpar_tabela_tokens();
        return 1;
    }

    SymTab st;
    symtab_init(&st);
    int sem_errors = semantic_analyze(ast_root, &st);

    if (show_ast) {
        printf("\n==================== AST ====================\n");
        ast_print(ast_root, 0);
        printf("=============================================\n");
    }

    if (show_symtab) {
        symtab_print(&st);
    }

    if (sem_errors > 0) {
        fprintf(stderr, "Compilacao abortada: %d erro(s) semantico(s).\n", sem_errors);
        symtab_free(&st);
        ast_free(ast_root);
        limpar_tabela_tokens();
        return 1;
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Erro ao criar arquivo de saida '%s'\n", output_path);
        symtab_free(&st);
        ast_free(ast_root);
        limpar_tabela_tokens();
        return 1;
    }

    int gen_status = codegen_generate(ast_root, &st, class_name, out);
    fclose(out);

    if (gen_status == 0) {
        printf("Compilacao concluida com sucesso! Codigo Jasmin gerado em '%s' (classe %s).\n",
               output_path, class_name);
    }

    symtab_free(&st);
    ast_free(ast_root);
    limpar_tabela_tokens();
    return gen_status;
}
