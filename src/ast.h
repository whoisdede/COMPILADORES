#ifndef AST_H
#define AST_H

#include <stdio.h>

typedef enum {
    TYPE_UNKNOWN = 0,
    TYPE_VOID,
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_ARRAY_INT,
    TYPE_ARRAY_DOUBLE,
    TYPE_ARRAY_STRING,
    TYPE_LIST_INT,
    TYPE_LIST_DOUBLE,
    TYPE_LIST_STRING,
    TYPE_LIST_EMPTY
} VarType;

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_FLOORDIV,
    OP_MOD,
    OP_NEG,
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_AND,
    OP_OR,
    OP_NOT
} OpKind;

typedef enum {
    NODE_PROGRAM,
    NODE_ASSIGN,
    NODE_INDEX_ASSIGN,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_FUNC_DEF,
    NODE_PARAM,
    NODE_RETURN,
    NODE_IMPORT,
    NODE_GLOBAL,
    NODE_PASS,
    NODE_EXPR_STMT,

    NODE_INT,
    NODE_DOUBLE,
    NODE_BOOL,
    NODE_STRING,
    NODE_VAR,
    NODE_BINOP,
    NODE_UNOP,
    NODE_CALL,
    NODE_METHOD_CALL,
    NODE_LIST_LIT,
    NODE_ARRAY_INIT,
    NODE_INDEX
} ASTNodeKind;

typedef struct ASTNode {
    ASTNodeKind kind;
    int line;
    int col;

    /* Literals / names */
    int ival;
    double fval;
    char *sval;         /* string literal, variable name, func name, module name */
    char *obj_name;     /* receiver/module name in NODE_METHOD_CALL */
    OpKind op;

    /* Children */
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *cond;
    struct ASTNode *body;
    struct ASTNode *else_branch;
    struct ASTNode *params;
    struct ASTNode *args;

    /* Sibling pointer for statement/arg/param lists */
    struct ASTNode *next;

    /* Semantic resolution metadata */
    VarType inferred_type;
    VarType sym_type;
    int sym_is_global;
    int sym_slot;
    int func_idx;

    /* Hidden local variable slots (used by for loops, array init, etc.) */
    int hidden_slot1;
    int hidden_slot2;
    int hidden_slot3;
    int hidden_slot4;
} ASTNode;

/* Type helpers */
const char *type_to_str(VarType t);
const char *type_to_jvm_desc(VarType t);
int type_jvm_slots(VarType t);
int type_is_numeric(VarType t);
int type_is_array(VarType t);
int type_is_list(VarType t);
VarType type_elem(VarType container);
VarType type_make_array(VarType elem);
VarType type_make_list(VarType elem);
const char *op_to_str(OpKind op);

/* Constructors */
ASTNode *ast_new(ASTNodeKind kind, int line, int col);
ASTNode *ast_program(ASTNode *stmts);
ASTNode *ast_assign(const char *name, ASTNode *expr, int line, int col);
ASTNode *ast_index_assign(const char *name, ASTNode *index, ASTNode *val, int line, int col);
ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body, int line, int col);
ASTNode *ast_while(ASTNode *cond, ASTNode *body, int line, int col);
ASTNode *ast_for(const char *var_name, ASTNode *iterable, ASTNode *body, int line, int col);
ASTNode *ast_func_def(const char *name, ASTNode *params, ASTNode *body, int line, int col);
ASTNode *ast_param(const char *name, int line, int col);
ASTNode *ast_return(ASTNode *expr, int line, int col);
ASTNode *ast_import(const char *mod_name, int line, int col);
ASTNode *ast_global(const char *var_name, int line, int col);
ASTNode *ast_pass(int line, int col);
ASTNode *ast_expr_stmt(ASTNode *expr, int line, int col);

ASTNode *ast_int(int val, int line, int col);
ASTNode *ast_double(double val, int line, int col);
ASTNode *ast_bool(int val, int line, int col);
ASTNode *ast_string(const char *val, int line, int col);
ASTNode *ast_var(const char *name, int line, int col);
ASTNode *ast_binop(OpKind op, ASTNode *left, ASTNode *right, int line, int col);
ASTNode *ast_unop(OpKind op, ASTNode *operand, int line, int col);
ASTNode *ast_call(const char *name, ASTNode *args, int line, int col);
ASTNode *ast_method_call(const char *obj_name, const char *method_name, ASTNode *args, int line, int col);
ASTNode *ast_list_lit(ASTNode *elems, int line, int col);
ASTNode *ast_index(ASTNode *target, ASTNode *index, int line, int col);

/* List utility */
ASTNode *ast_append(ASTNode *list, ASTNode *item);

/* Debug / cleanup */
void ast_print(const ASTNode *node, int indent);
void ast_free(ASTNode *node);

#endif
