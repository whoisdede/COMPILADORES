#include "symtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *c = (char *)malloc(n + 1);
    if (c) memcpy(c, s, n + 1);
    return c;
}

void symtab_init(SymTab *st) {
    memset(st, 0, sizeof(SymTab));
    st->main_next_slot = 1; /* slot 0 reserved for String[] args in main */
}

void symtab_free(SymTab *st) {
    for (int i = 0; i < st->global_count; i++) {
        free(st->globals[i].name);
    }
    for (int i = 0; i < st->main_local_count; i++) {
        free(st->main_locals[i].name);
    }
    for (int i = 0; i < st->func_count; i++) {
        FuncSym *f = &st->funcs[i];
        free(f->name);
        for (int p = 0; p < f->param_count; p++) {
            free(f->param_names[p]);
        }
        for (int l = 0; l < f->local_count; l++) {
            free(f->locals[l].name);
        }
        for (int g = 0; g < f->global_decl_count; g++) {
            free(f->global_decls[g]);
        }
    }
    for (int i = 0; i < st->import_count; i++) {
        free(st->imports[i]);
    }
    memset(st, 0, sizeof(SymTab));
}

VarSym *symtab_find_global(SymTab *st, const char *name) {
    for (int i = 0; i < st->global_count; i++) {
        if (strcmp(st->globals[i].name, name) == 0) {
            return &st->globals[i];
        }
    }
    return NULL;
}

VarSym *symtab_add_global(SymTab *st, const char *name, VarType type, int line) {
    VarSym *existing = symtab_find_global(st, name);
    if (existing) {
        if (existing->type == TYPE_UNKNOWN || existing->type == TYPE_LIST_EMPTY) {
            existing->type = type;
        }
        return existing;
    }
    if (st->global_count >= MAX_GLOBALS) {
        fprintf(stderr, "Erro fatal: limite de variaveis globais excedido\n");
        exit(1);
    }
    VarSym *v = &st->globals[st->global_count++];
    v->name = dup_str(name);
    v->type = type;
    v->slot = -1;
    v->is_param = 0;
    v->line = line;
    return v;
}

VarSym *symtab_find_local(SymTab *st, FuncSym *func, const char *name) {
    if (func) {
        for (int i = 0; i < func->local_count; i++) {
            if (strcmp(func->locals[i].name, name) == 0) {
                return &func->locals[i];
            }
        }
    } else {
        for (int i = 0; i < st->main_local_count; i++) {
            if (strcmp(st->main_locals[i].name, name) == 0) {
                return &st->main_locals[i];
            }
        }
    }
    return NULL;
}

VarSym *symtab_add_local(SymTab *st, FuncSym *func, const char *name, VarType type, int is_param, int line) {
    VarSym *existing = symtab_find_local(st, func, name);
    if (existing) {
        if (existing->type == TYPE_UNKNOWN || existing->type == TYPE_LIST_EMPTY) {
            existing->type = type;
        }
        return existing;
    }
    int slots = type_jvm_slots(type);
    if (slots <= 0) slots = 1;

    if (func) {
        if (func->local_count >= MAX_LOCALS) {
            fprintf(stderr, "Erro fatal: limite de variaveis locais excedido em '%s'\n", func->name);
            exit(1);
        }
        VarSym *v = &func->locals[func->local_count++];
        v->name = dup_str(name);
        v->type = type;
        v->slot = func->next_slot;
        v->is_param = is_param;
        v->line = line;
        func->next_slot += slots;
        return v;
    } else {
        if (st->main_local_count >= MAX_LOCALS) {
            fprintf(stderr, "Erro fatal: limite de variaveis locais excedido em main\n");
            exit(1);
        }
        VarSym *v = &st->main_locals[st->main_local_count++];
        v->name = dup_str(name);
        v->type = type;
        v->slot = st->main_next_slot;
        v->is_param = is_param;
        v->line = line;
        st->main_next_slot += slots;
        return v;
    }
}

int symtab_alloc_hidden_slot(SymTab *st, FuncSym *func, VarType type) {
    int slots = type_jvm_slots(type);
    if (slots <= 0) slots = 1;
    if (func) {
        int s = func->next_slot;
        func->next_slot += slots;
        return s;
    } else {
        int s = st->main_next_slot;
        st->main_next_slot += slots;
        return s;
    }
}

void symtab_add_global_decl(FuncSym *func, const char *name) {
    if (!func) return;
    if (symtab_is_global_decl(func, name)) return;
    if (func->global_decl_count < MAX_GLOBAL_DECLS) {
        func->global_decls[func->global_decl_count++] = dup_str(name);
    }
}

int symtab_is_global_decl(const FuncSym *func, const char *name) {
    if (!func) return 0;
    for (int i = 0; i < func->global_decl_count; i++) {
        if (strcmp(func->global_decls[i], name) == 0) return 1;
    }
    return 0;
}

int symtab_find_func_idx(SymTab *st, const char *name) {
    for (int i = 0; i < st->func_count; i++) {
        if (strcmp(st->funcs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

FuncSym *symtab_find_func(SymTab *st, const char *name) {
    int idx = symtab_find_func_idx(st, name);
    return (idx >= 0) ? &st->funcs[idx] : NULL;
}

int symtab_add_func(SymTab *st, const char *name, ASTNode *ast_node, int line) {
    if (st->func_count >= MAX_FUNCS) {
        fprintf(stderr, "Erro fatal: limite de funcoes excedido\n");
        exit(1);
    }
    int idx = st->func_count++;
    FuncSym *f = &st->funcs[idx];
    memset(f, 0, sizeof(FuncSym));
    f->name = dup_str(name);
    f->return_type = TYPE_UNKNOWN;
    f->ast_node = ast_node;
    f->line = line;

    ASTNode *p = ast_node ? ast_node->params : NULL;
    while (p && f->param_count < MAX_PARAMS) {
        f->param_names[f->param_count] = dup_str(p->sval);
        f->param_types[f->param_count] = TYPE_UNKNOWN;
        f->param_count++;
        p = p->next;
    }
    return idx;
}

void symtab_func_descriptor(const FuncSym *f, char *buf, size_t buf_size) {
    size_t pos = 0;
    pos += (size_t)snprintf(buf + pos, buf_size - pos, "(");
    for (int i = 0; i < f->param_count; i++) {
        VarType pt = f->param_types[i];
        if (pt == TYPE_UNKNOWN) pt = TYPE_INT;
        pos += (size_t)snprintf(buf + pos, buf_size - pos, "%s", type_to_jvm_desc(pt));
    }
    VarType rt = (f->return_type == TYPE_UNKNOWN) ? TYPE_VOID : f->return_type;
    snprintf(buf + pos, buf_size - pos, ")%s", type_to_jvm_desc(rt));
}

int symtab_has_import(const SymTab *st, const char *mod_name) {
    for (int i = 0; i < st->import_count; i++) {
        if (strcmp(st->imports[i], mod_name) == 0) return 1;
    }
    return 0;
}

void symtab_add_import(SymTab *st, const char *mod_name) {
    if (symtab_has_import(st, mod_name)) return;
    if (st->import_count < MAX_IMPORTS) {
        st->imports[st->import_count++] = dup_str(mod_name);
    }
}

void symtab_print(const SymTab *st) {
    printf("\n==================== TABELA DE SIMBOLOS ====================\n");

    printf("\n[1] VARIAVEIS GLOBAIS (.field public static)\n");
    printf("%-18s | %-15s | %-24s | %s\n", "IDENTIFICADOR", "TIPO", "DESCRITOR JVM", "LINHA");
    if (st->global_count == 0) {
        printf("(nenhuma variavel global)\n");
    } else {
        for (int i = 0; i < st->global_count; i++) {
            printf("%-18s | %-15s | %-24s | %d\n",
                   st->globals[i].name,
                   type_to_str(st->globals[i].type),
                   type_to_jvm_desc(st->globals[i].type),
                   st->globals[i].line);
        }
    }

    printf("\n[2] FUNCOES / PROCEDIMENTOS (.method public static)\n");
    printf("%-18s | %-15s | %-28s | %-8s | %s\n", "NOME", "RETORNO", "DESCRITOR JVM", "LOCALS", "LINHA");
    if (st->func_count == 0) {
        printf("(nenhuma funcao declarada)\n");
    } else {
        for (int i = 0; i < st->func_count; i++) {
            char desc[256];
            symtab_func_descriptor(&st->funcs[i], desc, sizeof(desc));
            VarType rt = (st->funcs[i].return_type == TYPE_UNKNOWN) ? TYPE_VOID : st->funcs[i].return_type;
            printf("%-18s | %-15s | %-28s | %-8d | %d\n",
                   st->funcs[i].name,
                   type_to_str(rt),
                   desc,
                   st->funcs[i].next_slot,
                   st->funcs[i].line);
        }
    }

    printf("\n[3] VARIAVEIS LOCAIS POR FUNCAO (Slots do Frame JVM)\n");
    int any_local = 0;
    for (int i = 0; i < st->func_count; i++) {
        const FuncSym *f = &st->funcs[i];
        if (f->local_count == 0) continue;
        any_local = 1;
        printf("  Escopo funcao:%s\n", f->name);
        printf("    %-16s | %-15s | %-6s | %s\n", "IDENTIFICADOR", "TIPO", "SLOT", "PAPEL");
        for (int l = 0; l < f->local_count; l++) {
            printf("    %-16s | %-15s | %-6d | %s\n",
                   f->locals[l].name,
                   type_to_str(f->locals[l].type),
                   f->locals[l].slot,
                   f->locals[l].is_param ? "parametro" : "local");
        }
    }
    if (!any_local) {
        printf("(nenhuma variavel local em funcoes)\n");
    }

    if (st->import_count > 0) {
        printf("\n[4] MODULOS IMPORTADOS\n");
        for (int i = 0; i < st->import_count; i++) {
            printf("  - %s\n", st->imports[i]);
        }
    }
    printf("============================================================\n");
}
