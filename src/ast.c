#include "ast.h"
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *copy = (char *)malloc(n + 1);
    if (copy) memcpy(copy, s, n + 1);
    return copy;
}

const char *type_to_str(VarType t) {
    switch (t) {
        case TYPE_UNKNOWN:      return "unknown";
        case TYPE_VOID:         return "void";
        case TYPE_INT:          return "int";
        case TYPE_DOUBLE:       return "double";
        case TYPE_BOOL:         return "bool";
        case TYPE_STRING:       return "string";
        case TYPE_ARRAY_INT:    return "int[]";
        case TYPE_ARRAY_DOUBLE: return "double[]";
        case TYPE_ARRAY_STRING: return "string[]";
        case TYPE_LIST_INT:     return "list<int>";
        case TYPE_LIST_DOUBLE:  return "list<double>";
        case TYPE_LIST_STRING:  return "list<string>";
        case TYPE_LIST_EMPTY:   return "list<?>";
    }
    return "unknown";
}

const char *type_to_jvm_desc(VarType t) {
    switch (t) {
        case TYPE_VOID:         return "V";
        case TYPE_INT:          return "I";
        case TYPE_DOUBLE:       return "D";
        case TYPE_BOOL:         return "Z";
        case TYPE_STRING:       return "Ljava/lang/String;";
        case TYPE_ARRAY_INT:    return "[I";
        case TYPE_ARRAY_DOUBLE: return "[D";
        case TYPE_ARRAY_STRING: return "[Ljava/lang/String;";
        case TYPE_LIST_INT:
        case TYPE_LIST_DOUBLE:
        case TYPE_LIST_STRING:
        case TYPE_LIST_EMPTY:   return "Ljava/util/ArrayList;";
        default:                return "I";
    }
}

int type_jvm_slots(VarType t) {
    if (t == TYPE_DOUBLE) return 2;
    if (t == TYPE_VOID) return 0;
    return 1;
}

int type_is_numeric(VarType t) {
    return t == TYPE_INT || t == TYPE_DOUBLE;
}

int type_is_array(VarType t) {
    return t == TYPE_ARRAY_INT || t == TYPE_ARRAY_DOUBLE || t == TYPE_ARRAY_STRING;
}

int type_is_list(VarType t) {
    return t == TYPE_LIST_INT || t == TYPE_LIST_DOUBLE || t == TYPE_LIST_STRING || t == TYPE_LIST_EMPTY;
}

VarType type_elem(VarType container) {
    switch (container) {
        case TYPE_ARRAY_INT:    case TYPE_LIST_INT:    return TYPE_INT;
        case TYPE_ARRAY_DOUBLE: case TYPE_LIST_DOUBLE: return TYPE_DOUBLE;
        case TYPE_ARRAY_STRING: case TYPE_LIST_STRING: return TYPE_STRING;
        default: return TYPE_UNKNOWN;
    }
}

VarType type_make_array(VarType elem) {
    switch (elem) {
        case TYPE_INT:    return TYPE_ARRAY_INT;
        case TYPE_DOUBLE: return TYPE_ARRAY_DOUBLE;
        case TYPE_STRING: return TYPE_ARRAY_STRING;
        default:          return TYPE_UNKNOWN;
    }
}

VarType type_make_list(VarType elem) {
    switch (elem) {
        case TYPE_INT:    return TYPE_LIST_INT;
        case TYPE_DOUBLE: return TYPE_LIST_DOUBLE;
        case TYPE_STRING: return TYPE_LIST_STRING;
        default:          return TYPE_UNKNOWN;
    }
}

const char *op_to_str(OpKind op) {
    switch (op) {
        case OP_ADD:      return "+";
        case OP_SUB:      return "-";
        case OP_MUL:      return "*";
        case OP_DIV:      return "/";
        case OP_FLOORDIV: return "//";
        case OP_MOD:      return "%";
        case OP_NEG:      return "-";
        case OP_EQ:       return "==";
        case OP_NEQ:      return "!=";
        case OP_LT:       return "<";
        case OP_LE:       return "<=";
        case OP_GT:       return ">";
        case OP_GE:       return ">=";
        case OP_AND:      return "and";
        case OP_OR:       return "or";
        case OP_NOT:      return "not";
    }
    return "?";
}

ASTNode *ast_new(ASTNodeKind kind, int line, int col) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!n) {
        fprintf(stderr, "Erro fatal: sem memoria para no AST\n");
        exit(1);
    }
    n->kind = kind;
    n->line = line;
    n->col = col;
    n->sym_slot = -1;
    n->func_idx = -1;
    n->hidden_slot1 = -1;
    n->hidden_slot2 = -1;
    n->hidden_slot3 = -1;
    n->hidden_slot4 = -1;
    return n;
}

ASTNode *ast_program(ASTNode *stmts) {
    ASTNode *n = ast_new(NODE_PROGRAM, 1, 1);
    n->body = stmts;
    return n;
}

ASTNode *ast_assign(const char *name, ASTNode *expr, int line, int col) {
    ASTNode *n = ast_new(NODE_ASSIGN, line, col);
    n->sval = dup_str(name);
    n->left = expr;
    return n;
}

ASTNode *ast_index_assign(const char *name, ASTNode *index, ASTNode *val, int line, int col) {
    ASTNode *n = ast_new(NODE_INDEX_ASSIGN, line, col);
    n->sval = dup_str(name);
    n->left = index;
    n->right = val;
    return n;
}

ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body, int line, int col) {
    ASTNode *n = ast_new(NODE_IF, line, col);
    n->cond = cond;
    n->body = then_body;
    n->else_branch = else_body;
    return n;
}

ASTNode *ast_while(ASTNode *cond, ASTNode *body, int line, int col) {
    ASTNode *n = ast_new(NODE_WHILE, line, col);
    n->cond = cond;
    n->body = body;
    return n;
}

ASTNode *ast_for(const char *var_name, ASTNode *iterable, ASTNode *body, int line, int col) {
    ASTNode *n = ast_new(NODE_FOR, line, col);
    n->sval = dup_str(var_name);
    n->left = iterable;
    n->body = body;
    return n;
}

ASTNode *ast_func_def(const char *name, ASTNode *params, ASTNode *body, int line, int col) {
    ASTNode *n = ast_new(NODE_FUNC_DEF, line, col);
    n->sval = dup_str(name);
    n->params = params;
    n->body = body;
    return n;
}

ASTNode *ast_param(const char *name, int line, int col) {
    ASTNode *n = ast_new(NODE_PARAM, line, col);
    n->sval = dup_str(name);
    return n;
}

ASTNode *ast_return(ASTNode *expr, int line, int col) {
    ASTNode *n = ast_new(NODE_RETURN, line, col);
    n->left = expr;
    return n;
}

ASTNode *ast_import(const char *mod_name, int line, int col) {
    ASTNode *n = ast_new(NODE_IMPORT, line, col);
    n->sval = dup_str(mod_name);
    return n;
}

ASTNode *ast_global(const char *var_name, int line, int col) {
    ASTNode *n = ast_new(NODE_GLOBAL, line, col);
    n->sval = dup_str(var_name);
    return n;
}

ASTNode *ast_pass(int line, int col) {
    return ast_new(NODE_PASS, line, col);
}

ASTNode *ast_expr_stmt(ASTNode *expr, int line, int col) {
    ASTNode *n = ast_new(NODE_EXPR_STMT, line, col);
    n->left = expr;
    return n;
}

ASTNode *ast_int(int val, int line, int col) {
    ASTNode *n = ast_new(NODE_INT, line, col);
    n->ival = val;
    n->inferred_type = TYPE_INT;
    return n;
}

ASTNode *ast_double(double val, int line, int col) {
    ASTNode *n = ast_new(NODE_DOUBLE, line, col);
    n->fval = val;
    n->inferred_type = TYPE_DOUBLE;
    return n;
}

ASTNode *ast_bool(int val, int line, int col) {
    ASTNode *n = ast_new(NODE_BOOL, line, col);
    n->ival = val ? 1 : 0;
    n->inferred_type = TYPE_BOOL;
    return n;
}

ASTNode *ast_string(const char *val, int line, int col) {
    ASTNode *n = ast_new(NODE_STRING, line, col);
    n->sval = dup_str(val);
    n->inferred_type = TYPE_STRING;
    return n;
}

ASTNode *ast_var(const char *name, int line, int col) {
    ASTNode *n = ast_new(NODE_VAR, line, col);
    n->sval = dup_str(name);
    return n;
}

ASTNode *ast_binop(OpKind op, ASTNode *left, ASTNode *right, int line, int col) {
    ASTNode *n = ast_new(NODE_BINOP, line, col);
    n->op = op;
    n->left = left;
    n->right = right;
    return n;
}

ASTNode *ast_unop(OpKind op, ASTNode *operand, int line, int col) {
    ASTNode *n = ast_new(NODE_UNOP, line, col);
    n->op = op;
    n->left = operand;
    return n;
}

ASTNode *ast_call(const char *name, ASTNode *args, int line, int col) {
    ASTNode *n = ast_new(NODE_CALL, line, col);
    n->sval = dup_str(name);
    n->args = args;
    return n;
}

ASTNode *ast_method_call(const char *obj_name, const char *method_name, ASTNode *args, int line, int col) {
    ASTNode *n = ast_new(NODE_METHOD_CALL, line, col);
    n->obj_name = dup_str(obj_name);
    n->sval = dup_str(method_name);
    n->args = args;
    return n;
}

ASTNode *ast_list_lit(ASTNode *elems, int line, int col) {
    ASTNode *n = ast_new(NODE_LIST_LIT, line, col);
    n->args = elems;
    return n;
}

ASTNode *ast_index(ASTNode *target, ASTNode *index, int line, int col) {
    ASTNode *n = ast_new(NODE_INDEX, line, col);
    n->left = target;
    n->right = index;
    return n;
}

ASTNode *ast_append(ASTNode *list, ASTNode *item) {
    if (!item) return list;
    if (!list) return item;
    ASTNode *cur = list;
    while (cur->next) cur = cur->next;
    cur->next = item;
    return list;
}

static void print_indent(int n) {
    for (int i = 0; i < n; i++) printf("  ");
}

void ast_print(const ASTNode *node, int indent) {
    while (node) {
        print_indent(indent);
        switch (node->kind) {
            case NODE_PROGRAM:
                printf("PROGRAM\n");
                ast_print(node->body, indent + 1);
                break;
            case NODE_ASSIGN:
                printf("ASSIGN %s : %s (L%d)\n", node->sval, type_to_str(node->sym_type), node->line);
                ast_print(node->left, indent + 1);
                break;
            case NODE_INDEX_ASSIGN:
                printf("INDEX_ASSIGN %s[...] (L%d)\n", node->sval, node->line);
                print_indent(indent + 1); printf("INDEX:\n");
                ast_print(node->left, indent + 2);
                print_indent(indent + 1); printf("VALUE:\n");
                ast_print(node->right, indent + 2);
                break;
            case NODE_IF:
                printf("IF (L%d)\n", node->line);
                print_indent(indent + 1); printf("COND:\n");
                ast_print(node->cond, indent + 2);
                print_indent(indent + 1); printf("THEN:\n");
                ast_print(node->body, indent + 2);
                if (node->else_branch) {
                    print_indent(indent + 1); printf("ELSE:\n");
                    ast_print(node->else_branch, indent + 2);
                }
                break;
            case NODE_WHILE:
                printf("WHILE (L%d)\n", node->line);
                print_indent(indent + 1); printf("COND:\n");
                ast_print(node->cond, indent + 2);
                print_indent(indent + 1); printf("BODY:\n");
                ast_print(node->body, indent + 2);
                break;
            case NODE_FOR:
                printf("FOR %s (L%d)\n", node->sval, node->line);
                print_indent(indent + 1); printf("IN:\n");
                ast_print(node->left, indent + 2);
                print_indent(indent + 1); printf("BODY:\n");
                ast_print(node->body, indent + 2);
                break;
            case NODE_FUNC_DEF:
                printf("DEF %s (L%d)\n", node->sval, node->line);
                if (node->params) {
                    print_indent(indent + 1); printf("PARAMS:\n");
                    ast_print(node->params, indent + 2);
                }
                print_indent(indent + 1); printf("BODY:\n");
                ast_print(node->body, indent + 2);
                break;
            case NODE_PARAM:
                printf("PARAM %s\n", node->sval);
                break;
            case NODE_RETURN:
                printf("RETURN (L%d)\n", node->line);
                if (node->left) ast_print(node->left, indent + 1);
                break;
            case NODE_IMPORT:
                printf("IMPORT %s (L%d)\n", node->sval, node->line);
                break;
            case NODE_GLOBAL:
                printf("GLOBAL %s (L%d)\n", node->sval, node->line);
                break;
            case NODE_PASS:
                printf("PASS (L%d)\n", node->line);
                break;
            case NODE_EXPR_STMT:
                printf("EXPR_STMT (L%d)\n", node->line);
                ast_print(node->left, indent + 1);
                break;
            case NODE_INT:
                printf("INT %d\n", node->ival);
                break;
            case NODE_DOUBLE:
                printf("DOUBLE %g\n", node->fval);
                break;
            case NODE_BOOL:
                printf("BOOL %s\n", node->ival ? "True" : "False");
                break;
            case NODE_STRING:
                printf("STRING \"%s\"\n", node->sval ? node->sval : "");
                break;
            case NODE_VAR:
                printf("VAR %s : %s\n", node->sval, type_to_str(node->inferred_type));
                break;
            case NODE_BINOP:
                printf("BINOP '%s' : %s\n", op_to_str(node->op), type_to_str(node->inferred_type));
                ast_print(node->left, indent + 1);
                ast_print(node->right, indent + 1);
                break;
            case NODE_UNOP:
                printf("UNOP '%s' : %s\n", op_to_str(node->op), type_to_str(node->inferred_type));
                ast_print(node->left, indent + 1);
                break;
            case NODE_CALL:
                printf("CALL %s(...) : %s\n", node->sval, type_to_str(node->inferred_type));
                ast_print(node->args, indent + 1);
                break;
            case NODE_METHOD_CALL:
                printf("METHOD %s.%s(...) : %s\n", node->obj_name, node->sval, type_to_str(node->inferred_type));
                ast_print(node->args, indent + 1);
                break;
            case NODE_LIST_LIT:
                printf("LIST_LIT : %s\n", type_to_str(node->inferred_type));
                ast_print(node->args, indent + 1);
                break;
            case NODE_ARRAY_INIT:
                printf("ARRAY_INIT : %s\n", type_to_str(node->inferred_type));
                ast_print(node->left, indent + 1);
                ast_print(node->right, indent + 1);
                break;
            case NODE_INDEX:
                printf("INDEX : %s\n", type_to_str(node->inferred_type));
                ast_print(node->left, indent + 1);
                ast_print(node->right, indent + 1);
                break;
        }
        node = node->next;
    }
}

void ast_free(ASTNode *node) {
    while (node) {
        ASTNode *next = node->next;
        free(node->sval);
        free(node->obj_name);
        ast_free(node->left);
        ast_free(node->right);
        ast_free(node->cond);
        ast_free(node->body);
        ast_free(node->else_branch);
        ast_free(node->params);
        ast_free(node->args);
        free(node);
        node = next;
    }
}
