#ifndef SYMTAB_H
#define SYMTAB_H

#include "ast.h"

#define MAX_PARAMS 32
#define MAX_LOCALS 256
#define MAX_GLOBALS 256
#define MAX_FUNCS 128
#define MAX_IMPORTS 32
#define MAX_GLOBAL_DECLS 64

typedef struct {
    char *name;
    VarType type;
    int slot;          /* JVM local variable index (for locals) or -1 (for globals) */
    int is_param;
    int line;
} VarSym;

typedef struct {
    char *name;
    int param_count;
    char *param_names[MAX_PARAMS];
    VarType param_types[MAX_PARAMS];
    VarType return_type;

    VarSym locals[MAX_LOCALS];
    int local_count;
    int next_slot;     /* Total JVM local slots needed for this function */

    char *global_decls[MAX_GLOBAL_DECLS];
    int global_decl_count;

    ASTNode *ast_node;
    int line;
    int called;
    int analyzed;
    int has_return_expr;
    int has_return_void;
} FuncSym;

typedef struct {
    VarSym globals[MAX_GLOBALS];
    int global_count;

    /* Main method locals (e.g., hidden loop counters and temp vars in top-level) */
    VarSym main_locals[MAX_LOCALS];
    int main_local_count;
    int main_next_slot; /* Starts at 1 because slot 0 is String[] args */

    FuncSym funcs[MAX_FUNCS];
    int func_count;

    char *imports[MAX_IMPORTS];
    int import_count;
} SymTab;

void symtab_init(SymTab *st);
void symtab_free(SymTab *st);

/* Globals */
VarSym *symtab_find_global(SymTab *st, const char *name);
VarSym *symtab_add_global(SymTab *st, const char *name, VarType type, int line);

/* Locals (inside a FuncSym or main if func == NULL) */
VarSym *symtab_find_local(SymTab *st, FuncSym *func, const char *name);
VarSym *symtab_add_local(SymTab *st, FuncSym *func, const char *name, VarType type, int is_param, int line);
int symtab_alloc_hidden_slot(SymTab *st, FuncSym *func, VarType type);

/* Global declarations inside functions (`global x`) */
void symtab_add_global_decl(FuncSym *func, const char *name);
int symtab_is_global_decl(const FuncSym *func, const char *name);

/* Functions */
int symtab_find_func_idx(SymTab *st, const char *name);
FuncSym *symtab_find_func(SymTab *st, const char *name);
int symtab_add_func(SymTab *st, const char *name, ASTNode *ast_node, int line);
void symtab_func_descriptor(const FuncSym *f, char *buf, size_t buf_size);

/* Imports */
int symtab_has_import(const SymTab *st, const char *mod_name);
void symtab_add_import(SymTab *st, const char *mod_name);

/* Display */
void symtab_print(const SymTab *st);

#endif
