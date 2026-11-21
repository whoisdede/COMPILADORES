#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    SymTab *st;
    int errors;
    int changed;
    int report_errors;
    int in_progress[MAX_FUNCS];
} SemCtx;

static void sem_error(SemCtx *ctx, int line, int col, const char *fmt, ...) {
    if (!ctx->report_errors) return;
    ctx->errors++;
    fprintf(stderr, "[ERRO SEMANTICO] Linha %d, Coluna %d: ", line, col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static VarType unify_numeric(VarType a, VarType b) {
    if (a == TYPE_DOUBLE || b == TYPE_DOUBLE) return TYPE_DOUBLE;
    if (a == TYPE_INT && b == TYPE_INT) return TYPE_INT;
    return TYPE_UNKNOWN;
}

/* Can a value of type `src` be assigned/passed to target of type `dst`? */
static int type_compatible(VarType dst, VarType src) {
    if (dst == TYPE_UNKNOWN || src == TYPE_UNKNOWN) return 1;
    if (dst == src) return 1;
    if (dst == TYPE_DOUBLE && src == TYPE_INT) return 1;
    if (type_is_list(dst) && src == TYPE_LIST_EMPTY) return 1;
    if (dst == TYPE_LIST_EMPTY && type_is_list(src)) return 1;
    return 0;
}

static int count_nodes(const ASTNode *list) {
    int c = 0;
    while (list) { c++; list = list->next; }
    return c;
}

static VarType parse_type_name(const char *name) {
    if (!name) return TYPE_UNKNOWN;
    if (strcmp(name, "int") == 0) return TYPE_INT;
    if (strcmp(name, "float") == 0 || strcmp(name, "double") == 0) return TYPE_DOUBLE;
    if (strcmp(name, "str") == 0 || strcmp(name, "string") == 0) return TYPE_STRING;
    return TYPE_UNKNOWN;
}

/* Forward declarations */
static void analyze_stmt_list(SemCtx *ctx, ASTNode *stmts, FuncSym *func);
static VarType analyze_expr(SemCtx *ctx, ASTNode *n, FuncSym *func);
static void analyze_func_body(SemCtx *ctx, FuncSym *f);

static VarSym *resolve_var_for_read(SemCtx *ctx, FuncSym *func, const char *name, int *is_global) {
    if (func && !symtab_is_global_decl(func, name)) {
        VarSym *loc = symtab_find_local(ctx->st, func, name);
        if (loc) {
            *is_global = 0;
            return loc;
        }
    }
    VarSym *glob = symtab_find_global(ctx->st, name);
    if (glob) {
        *is_global = 1;
        return glob;
    }
    *is_global = 0;
    return NULL;
}

static VarSym *resolve_var_for_write(SemCtx *ctx, FuncSym *func, const char *name, VarType rhs_type, int line, int *is_global) {
    if (!func || symtab_is_global_decl(func, name)) {
        *is_global = 1;
        VarSym *g = symtab_find_global(ctx->st, name);
        if (!g) {
            g = symtab_add_global(ctx->st, name, rhs_type, line);
            if (rhs_type != TYPE_UNKNOWN) ctx->changed = 1;
        } else {
            if (g->type == TYPE_UNKNOWN && rhs_type != TYPE_UNKNOWN) {
                g->type = rhs_type;
                ctx->changed = 1;
            } else if (g->type == TYPE_INT && rhs_type == TYPE_DOUBLE) {
                g->type = TYPE_DOUBLE;
                ctx->changed = 1;
            } else if (g->type == TYPE_LIST_EMPTY && type_is_list(rhs_type) && rhs_type != TYPE_LIST_EMPTY) {
                g->type = rhs_type;
                ctx->changed = 1;
            }
        }
        return g;
    } else {
        *is_global = 0;
        VarSym *l = symtab_find_local(ctx->st, func, name);
        if (!l) {
            l = symtab_add_local(ctx->st, func, name, rhs_type, 0, line);
            if (rhs_type != TYPE_UNKNOWN) ctx->changed = 1;
        } else {
            if (l->type == TYPE_UNKNOWN && rhs_type != TYPE_UNKNOWN) {
                l->type = rhs_type;
                ctx->changed = 1;
            } else if (l->type == TYPE_INT && rhs_type == TYPE_DOUBLE) {
                l->type = TYPE_DOUBLE;
                ctx->changed = 1;
            } else if (l->type == TYPE_LIST_EMPTY && type_is_list(rhs_type) && rhs_type != TYPE_LIST_EMPTY) {
                l->type = rhs_type;
                ctx->changed = 1;
            }
        }
        return l;
    }
}

static int is_const_zero_node(const ASTNode *n) {
    if (!n) return 0;
    if (n->kind == NODE_INT && n->ival == 0) return 1;
    if (n->kind == NODE_DOUBLE && n->fval == 0.0) return 1;
    return 0;
}

static VarType analyze_call(SemCtx *ctx, ASTNode *n, FuncSym *func) {
    const char *fname = n->sval;
    int argc = count_nodes(n->args);

    /* Built-in: print(...) */
    if (strcmp(fname, "print") == 0) {
        ASTNode *arg = n->args;
        while (arg) {
            VarType at = analyze_expr(ctx, arg, func);
            if (at == TYPE_VOID) {
                sem_error(ctx, arg->line, arg->col, "Nao e possivel imprimir expressao de tipo void");
            }
            arg = arg->next;
        }
        n->inferred_type = TYPE_VOID;
        return TYPE_VOID;
    }

    /* Built-in: len(x) */
    if (strcmp(fname, "len") == 0) {
        if (argc != 1) {
            sem_error(ctx, n->line, n->col, "len() espera exatamente 1 argumento, recebeu %d", argc);
            n->inferred_type = TYPE_INT;
            return TYPE_INT;
        }
        VarType at = analyze_expr(ctx, n->args, func);
        if (at != TYPE_UNKNOWN && at != TYPE_STRING && !type_is_array(at) && !type_is_list(at)) {
            sem_error(ctx, n->args->line, n->args->col,
                      "len() espera string, array ou lista, recebeu '%s'", type_to_str(at));
        }
        n->inferred_type = TYPE_INT;
        return TYPE_INT;
    }

    /* Built-in: array(tipo, tamanho_ou_lista) */
    if (strcmp(fname, "array") == 0) {
        if (argc != 2) {
            sem_error(ctx, n->line, n->col, "array() espera 2 argumentos: array(tipo, tamanho_ou_lista)");
            n->inferred_type = TYPE_ARRAY_INT;
            return TYPE_ARRAY_INT;
        }
        ASTNode *type_arg = n->args;
        ASTNode *spec_arg = type_arg->next;
        VarType elem_t = TYPE_UNKNOWN;
        if (type_arg->kind == NODE_VAR) {
            elem_t = parse_type_name(type_arg->sval);
        }
        if (elem_t == TYPE_UNKNOWN) {
            sem_error(ctx, type_arg->line, type_arg->col,
                      "Tipo invalido em array(): use int, float ou str");
            elem_t = TYPE_INT;
        }
        type_arg->inferred_type = elem_t;

        if (spec_arg->kind == NODE_LIST_LIT) {
            ASTNode *el = spec_arg->args;
            while (el) {
                VarType et = analyze_expr(ctx, el, func);
                if (!type_compatible(elem_t, et)) {
                    sem_error(ctx, el->line, el->col,
                              "Elemento de tipo '%s' incompativel com array de '%s'",
                              type_to_str(et), type_to_str(elem_t));
                }
                el = el->next;
            }
            spec_arg->inferred_type = type_make_list(elem_t);
        } else {
            VarType sz_t = analyze_expr(ctx, spec_arg, func);
            if (sz_t != TYPE_UNKNOWN && sz_t != TYPE_INT) {
                sem_error(ctx, spec_arg->line, spec_arg->col,
                          "Tamanho do array deve ser int, recebeu '%s'", type_to_str(sz_t));
            }
        }
        n->inferred_type = type_make_array(elem_t);
        return n->inferred_type;
    }

    /* Built-in: int(x) */
    if (strcmp(fname, "int") == 0) {
        if (argc != 1) {
            sem_error(ctx, n->line, n->col, "int() espera 1 argumento, recebeu %d", argc);
        } else {
            VarType at = analyze_expr(ctx, n->args, func);
            if (at != TYPE_UNKNOWN && !type_is_numeric(at) && at != TYPE_BOOL && at != TYPE_STRING) {
                sem_error(ctx, n->args->line, n->args->col,
                          "Nao e possivel converter '%s' para int", type_to_str(at));
            }
        }
        n->inferred_type = TYPE_INT;
        return TYPE_INT;
    }

    /* Built-in: float(x) */
    if (strcmp(fname, "float") == 0) {
        if (argc != 1) {
            sem_error(ctx, n->line, n->col, "float() espera 1 argumento, recebeu %d", argc);
        } else {
            VarType at = analyze_expr(ctx, n->args, func);
            if (at != TYPE_UNKNOWN && !type_is_numeric(at) && at != TYPE_STRING) {
                sem_error(ctx, n->args->line, n->args->col,
                          "Nao e possivel converter '%s' para double", type_to_str(at));
            }
        }
        n->inferred_type = TYPE_DOUBLE;
        return TYPE_DOUBLE;
    }

    /* Built-in: str(x) */
    if (strcmp(fname, "str") == 0) {
        if (argc != 1) {
            sem_error(ctx, n->line, n->col, "str() espera 1 argumento, recebeu %d", argc);
        } else {
            VarType at = analyze_expr(ctx, n->args, func);
            if (at == TYPE_VOID) {
                sem_error(ctx, n->args->line, n->args->col, "Nao e possivel converter void para string");
            }
        }
        n->inferred_type = TYPE_STRING;
        return TYPE_STRING;
    }

    /* Built-in: abs(x) */
    if (strcmp(fname, "abs") == 0) {
        if (argc != 1) {
            sem_error(ctx, n->line, n->col, "abs() espera 1 argumento, recebeu %d", argc);
            n->inferred_type = TYPE_INT;
            return TYPE_INT;
        }
        VarType at = analyze_expr(ctx, n->args, func);
        if (at != TYPE_UNKNOWN && !type_is_numeric(at)) {
            sem_error(ctx, n->args->line, n->args->col,
                      "abs() espera valor numerico, recebeu '%s'", type_to_str(at));
            n->inferred_type = TYPE_INT;
            return TYPE_INT;
        }
        n->inferred_type = (at == TYPE_DOUBLE) ? TYPE_DOUBLE : TYPE_INT;
        return n->inferred_type;
    }

    /* Built-in: range(...) outside for */
    if (strcmp(fname, "range") == 0) {
        sem_error(ctx, n->line, n->col, "range() deve ser utilizado dentro de um laco 'for'");
        n->inferred_type = TYPE_UNKNOWN;
        return TYPE_UNKNOWN;
    }

    /* User-defined function */
    int fidx = symtab_find_func_idx(ctx->st, fname);
    if (fidx < 0) {
        sem_error(ctx, n->line, n->col, "Funcao '%s' nao declarada", fname);
        ASTNode *arg = n->args;
        while (arg) { analyze_expr(ctx, arg, func); arg = arg->next; }
        n->inferred_type = TYPE_UNKNOWN;
        return TYPE_UNKNOWN;
    }

    FuncSym *callee = &ctx->st->funcs[fidx];
    n->func_idx = fidx;
    callee->called = 1;

    if (argc != callee->param_count) {
        sem_error(ctx, n->line, n->col,
                  "Funcao '%s' espera %d argumento(s), mas recebeu %d",
                  fname, callee->param_count, argc);
    }

    ASTNode *arg = n->args;
    for (int i = 0; arg != NULL; i++, arg = arg->next) {
        VarType at = analyze_expr(ctx, arg, func);
        if (i < callee->param_count) {
            if (!ctx->report_errors) {
                /* Type inference / unification for parameter i */
                if (callee->param_types[i] == TYPE_UNKNOWN && at != TYPE_UNKNOWN) {
                    callee->param_types[i] = at;
                    ctx->changed = 1;
                } else if (callee->param_types[i] == TYPE_INT && at == TYPE_DOUBLE) {
                    callee->param_types[i] = TYPE_DOUBLE;
                    ctx->changed = 1;
                } else if (callee->param_types[i] == TYPE_LIST_EMPTY && type_is_list(at) && at != TYPE_LIST_EMPTY) {
                    callee->param_types[i] = at;
                    ctx->changed = 1;
                }
            } else {
                VarType pt = callee->param_types[i];
                if (!type_compatible(pt, at)) {
                    sem_error(ctx, arg->line, arg->col,
                              "Argumento %d de '%s': esperado '%s', recebeu '%s'",
                              i + 1, fname, type_to_str(pt), type_to_str(at));
                }
            }
        }
    }

    if (!ctx->report_errors && !ctx->in_progress[fidx]) {
        analyze_func_body(ctx, callee);
    }

    VarType rt = callee->return_type;
    if (ctx->report_errors && rt == TYPE_UNKNOWN) rt = TYPE_VOID;
    n->inferred_type = rt;
    return rt;
}

static VarType analyze_method_call(SemCtx *ctx, ASTNode *n, FuncSym *func) {
    const char *obj = n->obj_name;
    const char *meth = n->sval;
    int argc = count_nodes(n->args);

    /* Module call: math.<func>(...) */
    if (strcmp(obj, "math") == 0) {
        if (!symtab_has_import(ctx->st, "math")) {
            sem_error(ctx, n->line, n->col, "Modulo 'math' nao importado (adicione 'import math')");
        }
        int expected_args = 1;
        if (strcmp(meth, "pow") == 0) {
            expected_args = 2;
        } else if (strcmp(meth, "sqrt") == 0 || strcmp(meth, "sin") == 0 ||
                   strcmp(meth, "cos") == 0 || strcmp(meth, "fabs") == 0 ||
                   strcmp(meth, "abs") == 0 || strcmp(meth, "floor") == 0 ||
                   strcmp(meth, "ceil") == 0) {
            expected_args = 1;
        } else {
            sem_error(ctx, n->line, n->col, "Funcao 'math.%s' nao suportada", meth);
            n->inferred_type = TYPE_DOUBLE;
            return TYPE_DOUBLE;
        }
        if (argc != expected_args) {
            sem_error(ctx, n->line, n->col, "math.%s() espera %d argumento(s), recebeu %d",
                      meth, expected_args, argc);
        }
        ASTNode *a = n->args;
        while (a) {
            VarType at = analyze_expr(ctx, a, func);
            if (at != TYPE_UNKNOWN && !type_is_numeric(at)) {
                sem_error(ctx, a->line, a->col,
                          "math.%s() espera argumento numerico, recebeu '%s'", meth, type_to_str(at));
            }
            a = a->next;
        }
        n->inferred_type = TYPE_DOUBLE;
        return TYPE_DOUBLE;
    }

    /* Object method call on variable */
    int is_glob = 0;
    VarSym *v = resolve_var_for_read(ctx, func, obj, &is_glob);
    if (!v) {
        sem_error(ctx, n->line, n->col, "Variavel ou modulo '%s' nao declarado", obj);
        ASTNode *a = n->args;
        while (a) { analyze_expr(ctx, a, func); a = a->next; }
        n->inferred_type = TYPE_UNKNOWN;
        return TYPE_UNKNOWN;
    }

    n->sym_is_global = is_glob;
    n->sym_slot = v->slot;
    n->sym_type = v->type;

    if (type_is_list(v->type)) {
        if (strcmp(meth, "append") == 0) {
            if (argc != 1) {
                sem_error(ctx, n->line, n->col, "append() espera 1 argumento, recebeu %d", argc);
                n->inferred_type = TYPE_VOID;
                return TYPE_VOID;
            }
            VarType at = analyze_expr(ctx, n->args, func);
            if (v->type == TYPE_LIST_EMPTY && at != TYPE_UNKNOWN) {
                VarType upgraded = type_make_list(at);
                if (upgraded != TYPE_UNKNOWN) {
                    v->type = upgraded;
                    n->sym_type = upgraded;
                    ctx->changed = 1;
                } else {
                    sem_error(ctx, n->args->line, n->args->col,
                              "Tipo '%s' nao suportado como elemento de lista", type_to_str(at));
                }
            } else if (v->type != TYPE_LIST_EMPTY && at != TYPE_UNKNOWN) {
                VarType et = type_elem(v->type);
                if (et == TYPE_INT && at == TYPE_DOUBLE && !ctx->report_errors) {
                    v->type = TYPE_LIST_DOUBLE;
                    n->sym_type = TYPE_LIST_DOUBLE;
                    ctx->changed = 1;
                } else if (!type_compatible(et, at)) {
                    sem_error(ctx, n->args->line, n->args->col,
                              "append(): elemento de tipo '%s' incompativel com '%s'",
                              type_to_str(at), type_to_str(v->type));
                }
            }
            n->inferred_type = TYPE_VOID;
            return TYPE_VOID;
        }
        if (strcmp(meth, "pop") == 0) {
            if (argc > 1) {
                sem_error(ctx, n->line, n->col, "pop() aceita 0 ou 1 argumento, recebeu %d", argc);
            } else if (argc == 1) {
                VarType it = analyze_expr(ctx, n->args, func);
                if (it != TYPE_UNKNOWN && it != TYPE_INT) {
                    sem_error(ctx, n->args->line, n->args->col,
                              "Indice de pop() deve ser int, recebeu '%s'", type_to_str(it));
                }
            }
            VarType et = type_elem(v->type);
            if (et == TYPE_UNKNOWN && ctx->report_errors) et = TYPE_INT;
            n->inferred_type = et;
            return et;
        }
        if (strcmp(meth, "clear") == 0) {
            if (argc != 0) {
                sem_error(ctx, n->line, n->col, "clear() nao aceita argumentos");
            }
            n->inferred_type = TYPE_VOID;
            return TYPE_VOID;
        }
        sem_error(ctx, n->line, n->col, "Metodo '%s' nao suportado em listas", meth);
        n->inferred_type = TYPE_UNKNOWN;
        return TYPE_UNKNOWN;
    }

    if (v->type == TYPE_STRING) {
        if (strcmp(meth, "upper") == 0 || strcmp(meth, "lower") == 0) {
            if (argc != 0) {
                sem_error(ctx, n->line, n->col, "%s() nao aceita argumentos", meth);
            }
            n->inferred_type = TYPE_STRING;
            return TYPE_STRING;
        }
    }

    sem_error(ctx, n->line, n->col, "Metodo '%s' nao suportado para tipo '%s'", meth, type_to_str(v->type));
    n->inferred_type = TYPE_UNKNOWN;
    return TYPE_UNKNOWN;
}

static VarType analyze_expr(SemCtx *ctx, ASTNode *n, FuncSym *func) {
    if (!n) return TYPE_VOID;

    switch (n->kind) {
        case NODE_INT:
            n->inferred_type = TYPE_INT;
            return TYPE_INT;
        case NODE_DOUBLE:
            n->inferred_type = TYPE_DOUBLE;
            return TYPE_DOUBLE;
        case NODE_BOOL:
            n->inferred_type = TYPE_BOOL;
            return TYPE_BOOL;
        case NODE_STRING:
            n->inferred_type = TYPE_STRING;
            return TYPE_STRING;

        case NODE_VAR: {
            int is_glob = 0;
            VarSym *v = resolve_var_for_read(ctx, func, n->sval, &is_glob);
            if (!v) {
                sem_error(ctx, n->line, n->col, "Variavel '%s' utilizada antes de ser declarada", n->sval);
                n->inferred_type = TYPE_UNKNOWN;
                return TYPE_UNKNOWN;
            }
            n->sym_is_global = is_glob;
            n->sym_slot = v->slot;
            n->sym_type = v->type;
            n->inferred_type = v->type;
            return v->type;
        }

        case NODE_UNOP: {
            VarType t = analyze_expr(ctx, n->left, func);
            if (n->op == OP_NEG) {
                if (t != TYPE_UNKNOWN && !type_is_numeric(t)) {
                    sem_error(ctx, n->line, n->col,
                              "Operador unario '-' requer tipo numerico, recebeu '%s'", type_to_str(t));
                    n->inferred_type = TYPE_INT;
                    return TYPE_INT;
                }
                n->inferred_type = t;
                return t;
            }
            if (n->op == OP_NOT) {
                if (t != TYPE_UNKNOWN && t != TYPE_BOOL && t != TYPE_INT) {
                    sem_error(ctx, n->line, n->col,
                              "Operador 'not' requer booleano ou inteiro, recebeu '%s'", type_to_str(t));
                }
                n->inferred_type = TYPE_BOOL;
                return TYPE_BOOL;
            }
            return TYPE_UNKNOWN;
        }

        case NODE_BINOP: {
            /* Special Python idiom: [val] * n -> native JVM array */
            if (n->op == OP_MUL && n->left && n->left->kind == NODE_LIST_LIT &&
                n->left->args && n->left->args->next == NULL) {
                ASTNode *list_wrapper = n->left;
                ASTNode *init_val = list_wrapper->args;
                list_wrapper->args = NULL;
                ast_free(list_wrapper);
                n->kind = NODE_ARRAY_INIT;
                n->left = init_val;
                ctx->changed = 1;
                return analyze_expr(ctx, n, func);
            }

            VarType lt = analyze_expr(ctx, n->left, func);
            VarType rt = analyze_expr(ctx, n->right, func);

            switch (n->op) {
                case OP_ADD:
                    if (lt == TYPE_STRING && rt == TYPE_STRING) {
                        n->inferred_type = TYPE_STRING;
                        return TYPE_STRING;
                    }
                    if (type_is_numeric(lt) && type_is_numeric(rt)) {
                        n->inferred_type = unify_numeric(lt, rt);
                        return n->inferred_type;
                    }
                    if (lt != TYPE_UNKNOWN && rt != TYPE_UNKNOWN) {
                        sem_error(ctx, n->line, n->col,
                                  "Operacao '+' incompativel entre '%s' e '%s'",
                                  type_to_str(lt), type_to_str(rt));
                    }
                    n->inferred_type = TYPE_UNKNOWN;
                    return TYPE_UNKNOWN;

                case OP_SUB:
                case OP_MUL:
                case OP_DIV:
                    if (type_is_numeric(lt) && type_is_numeric(rt)) {
                        n->inferred_type = unify_numeric(lt, rt);
                        return n->inferred_type;
                    }
                    if (lt != TYPE_UNKNOWN && rt != TYPE_UNKNOWN) {
                        sem_error(ctx, n->line, n->col,
                                  "Operacao '%s' requer operandos numericos, recebeu '%s' e '%s'",
                                  op_to_str(n->op), type_to_str(lt), type_to_str(rt));
                    }
                    n->inferred_type = TYPE_UNKNOWN;
                    return TYPE_UNKNOWN;

                case OP_FLOORDIV:
                case OP_MOD:
                    if (lt == TYPE_INT && rt == TYPE_INT) {
                        n->inferred_type = TYPE_INT;
                        return TYPE_INT;
                    }
                    if (lt != TYPE_UNKNOWN && rt != TYPE_UNKNOWN) {
                        sem_error(ctx, n->line, n->col,
                                  "Operacao '%s' requer operandos inteiros, recebeu '%s' e '%s'",
                                  op_to_str(n->op), type_to_str(lt), type_to_str(rt));
                    }
                    n->inferred_type = TYPE_INT;
                    return TYPE_INT;

                case OP_EQ:
                case OP_NEQ:
                    if (lt != TYPE_UNKNOWN && rt != TYPE_UNKNOWN) {
                        int ok = (type_is_numeric(lt) && type_is_numeric(rt)) ||
                                 (lt == TYPE_STRING && rt == TYPE_STRING) ||
                                 (lt == TYPE_BOOL && rt == TYPE_BOOL);
                        if (!ok) {
                            sem_error(ctx, n->line, n->col,
                                      "Comparacao '%s' incompativel entre '%s' e '%s'",
                                      op_to_str(n->op), type_to_str(lt), type_to_str(rt));
                        }
                    }
                    n->inferred_type = TYPE_BOOL;
                    return TYPE_BOOL;

                case OP_LT:
                case OP_LE:
                case OP_GT:
                case OP_GE:
                    if (lt != TYPE_UNKNOWN && rt != TYPE_UNKNOWN) {
                        if (!(type_is_numeric(lt) && type_is_numeric(rt))) {
                            sem_error(ctx, n->line, n->col,
                                      "Comparacao '%s' requer operandos numericos, recebeu '%s' e '%s'",
                                      op_to_str(n->op), type_to_str(lt), type_to_str(rt));
                        }
                    }
                    n->inferred_type = TYPE_BOOL;
                    return TYPE_BOOL;

                case OP_AND:
                case OP_OR:
                    if (lt != TYPE_UNKNOWN && lt != TYPE_BOOL && lt != TYPE_INT) {
                        sem_error(ctx, n->left->line, n->left->col,
                                  "Operador '%s' requer condicao booleana, recebeu '%s'",
                                  op_to_str(n->op), type_to_str(lt));
                    }
                    if (rt != TYPE_UNKNOWN && rt != TYPE_BOOL && rt != TYPE_INT) {
                        sem_error(ctx, n->right->line, n->right->col,
                                  "Operador '%s' requer condicao booleana, recebeu '%s'",
                                  op_to_str(n->op), type_to_str(rt));
                    }
                    n->inferred_type = TYPE_BOOL;
                    return TYPE_BOOL;

                default:
                    return TYPE_UNKNOWN;
            }
        }

        case NODE_ARRAY_INIT: {
            VarType val_t = analyze_expr(ctx, n->left, func);
            VarType sz_t = analyze_expr(ctx, n->right, func);
            if (sz_t != TYPE_UNKNOWN && sz_t != TYPE_INT) {
                sem_error(ctx, n->right->line, n->right->col,
                          "Tamanho de array deve ser int, recebeu '%s'", type_to_str(sz_t));
            }
            VarType arr_t = type_make_array(val_t);
            if (val_t != TYPE_UNKNOWN && arr_t == TYPE_UNKNOWN) {
                sem_error(ctx, n->left->line, n->left->col,
                          "Tipo '%s' nao suportado em array", type_to_str(val_t));
                arr_t = TYPE_ARRAY_INT;
            }
            n->inferred_type = arr_t;
            if (ctx->report_errors && !is_const_zero_node(n->left)) {
                /* Need hidden local slots for fill loop: array ref, loop index */
                n->hidden_slot1 = symtab_alloc_hidden_slot(ctx->st, func, arr_t);
                n->hidden_slot2 = symtab_alloc_hidden_slot(ctx->st, func, TYPE_INT);
            }
            return arr_t;
        }

        case NODE_LIST_LIT: {
            if (!n->args) {
                n->inferred_type = TYPE_LIST_EMPTY;
                return TYPE_LIST_EMPTY;
            }
            VarType elem_t = TYPE_UNKNOWN;
            ASTNode *el = n->args;
            while (el) {
                VarType et = analyze_expr(ctx, el, func);
                if (elem_t == TYPE_UNKNOWN) {
                    elem_t = et;
                } else if (elem_t == TYPE_INT && et == TYPE_DOUBLE) {
                    elem_t = TYPE_DOUBLE;
                } else if (elem_t == TYPE_DOUBLE && et == TYPE_INT) {
                    /* ok, promote element to double */
                } else if (et != TYPE_UNKNOWN && et != elem_t) {
                    sem_error(ctx, el->line, el->col,
                              "Lista homogenea nao pode misturar '%s' e '%s'",
                              type_to_str(elem_t), type_to_str(et));
                }
                el = el->next;
            }
            VarType lt = type_make_list(elem_t);
            if (elem_t != TYPE_UNKNOWN && lt == TYPE_UNKNOWN) {
                sem_error(ctx, n->line, n->col,
                          "Tipo '%s' nao suportado em literal de lista", type_to_str(elem_t));
                lt = TYPE_LIST_INT;
            }
            n->inferred_type = (lt == TYPE_UNKNOWN) ? TYPE_LIST_EMPTY : lt;
            return n->inferred_type;
        }

        case NODE_INDEX: {
            VarType ct = analyze_expr(ctx, n->left, func);
            VarType it = analyze_expr(ctx, n->right, func);
            if (it != TYPE_UNKNOWN && it != TYPE_INT) {
                sem_error(ctx, n->right->line, n->right->col,
                          "Indice de acesso deve ser int, recebeu '%s'", type_to_str(it));
            }
            if (ct == TYPE_STRING) {
                n->inferred_type = TYPE_STRING;
                return TYPE_STRING;
            }
            if (type_is_array(ct) || type_is_list(ct)) {
                VarType et = type_elem(ct);
                if (et == TYPE_UNKNOWN && ctx->report_errors) {
                    sem_error(ctx, n->line, n->col, "Nao e possivel indexar lista vazia sem tipo definido");
                    et = TYPE_INT;
                }
                n->inferred_type = et;
                return et;
            }
            if (ct != TYPE_UNKNOWN) {
                sem_error(ctx, n->line, n->col,
                          "Tipo '%s' nao suporta indexacao []", type_to_str(ct));
            }
            n->inferred_type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
        }

        case NODE_CALL:
            return analyze_call(ctx, n, func);

        case NODE_METHOD_CALL:
            return analyze_method_call(ctx, n, func);

        default:
            return TYPE_UNKNOWN;
    }
}

static void analyze_stmt(SemCtx *ctx, ASTNode *n, FuncSym *func) {
    if (!n) return;

    switch (n->kind) {
        case NODE_FUNC_DEF:
            /* Analyzed via inference and final func loop */
            break;

        case NODE_IMPORT:
        case NODE_GLOBAL:
        case NODE_PASS:
            break;

        case NODE_ASSIGN: {
            VarType rt = analyze_expr(ctx, n->left, func);
            if (rt == TYPE_VOID) {
                sem_error(ctx, n->line, n->col,
                          "Nao e possivel atribuir retorno void a variavel '%s'", n->sval);
                rt = TYPE_INT;
            }
            int is_glob = 0;
            VarSym *v = resolve_var_for_write(ctx, func, n->sval, rt, n->line, &is_glob);
            if (ctx->report_errors && v && !type_compatible(v->type, rt)) {
                sem_error(ctx, n->line, n->col,
                          "Atribuicao incompativel para '%s': variavel e '%s', expressao e '%s'",
                          n->sval, type_to_str(v->type), type_to_str(rt));
            }
            if (v) {
                n->sym_is_global = is_glob;
                n->sym_slot = v->slot;
                n->sym_type = v->type;
                n->inferred_type = v->type;
            }
            break;
        }

        case NODE_INDEX_ASSIGN: {
            int is_glob = 0;
            VarSym *v = resolve_var_for_read(ctx, func, n->sval, &is_glob);
            VarType it = analyze_expr(ctx, n->left, func);
            VarType vt = analyze_expr(ctx, n->right, func);

            if (!v) {
                sem_error(ctx, n->line, n->col, "Array/lista '%s' nao declarado", n->sval);
                break;
            }
            if (it != TYPE_UNKNOWN && it != TYPE_INT) {
                sem_error(ctx, n->left->line, n->left->col,
                          "Indice de atribuicao deve ser int, recebeu '%s'", type_to_str(it));
            }
            if (v->type == TYPE_LIST_EMPTY && vt != TYPE_UNKNOWN) {
                VarType upgraded = type_make_list(vt);
                if (upgraded != TYPE_UNKNOWN) {
                    v->type = upgraded;
                    ctx->changed = 1;
                }
            }
            if (v->type != TYPE_UNKNOWN && !type_is_array(v->type) && !type_is_list(v->type)) {
                sem_error(ctx, n->line, n->col,
                          "Variavel '%s' de tipo '%s' nao suporta atribuicao indexada",
                          n->sval, type_to_str(v->type));
            } else if (v->type != TYPE_LIST_EMPTY && v->type != TYPE_UNKNOWN) {
                VarType et = type_elem(v->type);
                if (!type_compatible(et, vt)) {
                    sem_error(ctx, n->right->line, n->right->col,
                              "Valor de tipo '%s' incompativel com elemento '%s' de '%s'",
                              type_to_str(vt), type_to_str(et), n->sval);
                }
            }
            n->sym_is_global = is_glob;
            n->sym_slot = v->slot;
            n->sym_type = v->type;
            break;
        }

        case NODE_IF: {
            VarType ct = analyze_expr(ctx, n->cond, func);
            if (ct != TYPE_UNKNOWN && ct != TYPE_BOOL && ct != TYPE_INT) {
                sem_error(ctx, n->cond->line, n->cond->col,
                          "Condicao do 'if' deve ser booleana ou inteira, recebeu '%s'",
                          type_to_str(ct));
            }
            analyze_stmt_list(ctx, n->body, func);
            if (n->else_branch) {
                if (n->else_branch->kind == NODE_IF) {
                    analyze_stmt(ctx, n->else_branch, func);
                } else {
                    analyze_stmt_list(ctx, n->else_branch, func);
                }
            }
            break;
        }

        case NODE_WHILE: {
            VarType ct = analyze_expr(ctx, n->cond, func);
            if (ct != TYPE_UNKNOWN && ct != TYPE_BOOL && ct != TYPE_INT) {
                sem_error(ctx, n->cond->line, n->cond->col,
                          "Condicao do 'while' deve ser booleana ou inteira, recebeu '%s'",
                          type_to_str(ct));
            }
            analyze_stmt_list(ctx, n->body, func);
            break;
        }

        case NODE_FOR: {
            ASTNode *iter = n->left;
            VarType loop_var_type = TYPE_INT;

            if (iter->kind == NODE_CALL && strcmp(iter->sval, "range") == 0) {
                int argc = count_nodes(iter->args);
                if (argc < 1 || argc > 3) {
                    sem_error(ctx, iter->line, iter->col,
                              "range() espera de 1 a 3 argumentos, recebeu %d", argc);
                }
                ASTNode *a = iter->args;
                while (a) {
                    VarType at = analyze_expr(ctx, a, func);
                    if (at != TYPE_UNKNOWN && at != TYPE_INT) {
                        sem_error(ctx, a->line, a->col,
                                  "Argumento de range() deve ser int, recebeu '%s'", type_to_str(at));
                    }
                    a = a->next;
                }
                iter->inferred_type = TYPE_INT;
                loop_var_type = TYPE_INT;

                if (ctx->report_errors) {
                    /* hidden_slot1 = stop, hidden_slot2 = step */
                    n->hidden_slot1 = symtab_alloc_hidden_slot(ctx->st, func, TYPE_INT);
                    n->hidden_slot2 = symtab_alloc_hidden_slot(ctx->st, func, TYPE_INT);
                }
            } else {
                VarType it = analyze_expr(ctx, iter, func);
                if (it == TYPE_INT) {
                    /* Compatibility with Etapa 2: `for i in n:` iterates 0..n-1 */
                    loop_var_type = TYPE_INT;
                    if (ctx->report_errors) {
                        n->hidden_slot1 = symtab_alloc_hidden_slot(ctx->st, func, TYPE_INT);
                    }
                } else if (type_is_array(it) || type_is_list(it)) {
                    loop_var_type = type_elem(it);
                    if (loop_var_type == TYPE_UNKNOWN) loop_var_type = TYPE_INT;
                    if (ctx->report_errors) {
                        /* hidden_slot1 = container ref, hidden_slot2 = index, hidden_slot3 = length */
                        n->hidden_slot1 = symtab_alloc_hidden_slot(ctx->st, func, it);
                        n->hidden_slot2 = symtab_alloc_hidden_slot(ctx->st, func, TYPE_INT);
                        n->hidden_slot3 = symtab_alloc_hidden_slot(ctx->st, func, TYPE_INT);
                    }
                } else if (it != TYPE_UNKNOWN) {
                    sem_error(ctx, iter->line, iter->col,
                              "Tipo '%s' nao e iteravel em laco 'for'", type_to_str(it));
                }
            }

            int is_glob = 0;
            VarSym *lv = resolve_var_for_write(ctx, func, n->sval, loop_var_type, n->line, &is_glob);
            if (lv) {
                n->sym_is_global = is_glob;
                n->sym_slot = lv->slot;
                n->sym_type = lv->type;
            }
            analyze_stmt_list(ctx, n->body, func);
            break;
        }

        case NODE_RETURN: {
            if (!func) {
                sem_error(ctx, n->line, n->col, "'return' fora de funcao");
                break;
            }
            if (!n->left) {
                func->has_return_void = 1;
                if (!ctx->report_errors) {
                    if (func->return_type == TYPE_UNKNOWN) {
                        func->return_type = TYPE_VOID;
                        ctx->changed = 1;
                    }
                } else if (func->return_type != TYPE_VOID && func->return_type != TYPE_UNKNOWN) {
                    sem_error(ctx, n->line, n->col,
                              "Funcao '%s' retorna '%s', mas encontrou 'return' vazio",
                              func->name, type_to_str(func->return_type));
                }
                n->inferred_type = TYPE_VOID;
            } else {
                func->has_return_expr = 1;
                VarType rt = analyze_expr(ctx, n->left, func);
                if (!ctx->report_errors) {
                    if (func->return_type == TYPE_UNKNOWN && rt != TYPE_UNKNOWN) {
                        func->return_type = rt;
                        ctx->changed = 1;
                    } else if (func->return_type == TYPE_INT && rt == TYPE_DOUBLE) {
                        func->return_type = TYPE_DOUBLE;
                        ctx->changed = 1;
                    } else if (func->return_type == TYPE_VOID && rt != TYPE_UNKNOWN) {
                        func->return_type = rt;
                        ctx->changed = 1;
                    }
                } else {
                    if (!type_compatible(func->return_type, rt)) {
                        sem_error(ctx, n->line, n->col,
                                  "Tipo de retorno '%s' incompativel com retorno '%s' de '%s'",
                                  type_to_str(rt), type_to_str(func->return_type), func->name);
                    }
                }
                n->inferred_type = func->return_type;
            }
            break;
        }

        case NODE_EXPR_STMT:
            analyze_expr(ctx, n->left, func);
            break;

        default:
            break;
    }
}

static void analyze_stmt_list(SemCtx *ctx, ASTNode *stmts, FuncSym *func) {
    for (ASTNode *cur = stmts; cur != NULL; cur = cur->next) {
        analyze_stmt(ctx, cur, func);
    }
}

static void sync_func_params_as_locals(SemCtx *ctx, FuncSym *f) {
    for (int i = 0; i < f->param_count; i++) {
        VarSym *p = symtab_find_local(ctx->st, f, f->param_names[i]);
        if (!p) {
            symtab_add_local(ctx->st, f, f->param_names[i], f->param_types[i], 1, f->line);
        } else {
            if (p->type != f->param_types[i] && f->param_types[i] != TYPE_UNKNOWN) {
                p->type = f->param_types[i];
                ctx->changed = 1;
            } else if (f->param_types[i] == TYPE_UNKNOWN && p->type != TYPE_UNKNOWN) {
                f->param_types[i] = p->type;
                ctx->changed = 1;
            }
        }
    }
}

static void analyze_func_body(SemCtx *ctx, FuncSym *f) {
    int fidx = symtab_find_func_idx(ctx->st, f->name);
    if (fidx < 0 || ctx->in_progress[fidx]) return;

    ctx->in_progress[fidx] = 1;
    sync_func_params_as_locals(ctx, f);
    analyze_stmt_list(ctx, f->ast_node->body, f);
    f->analyzed = 1;
    ctx->in_progress[fidx] = 0;
}

static void collect_global_decls_in_block(FuncSym *f, ASTNode *stmts) {
    for (ASTNode *cur = stmts; cur != NULL; cur = cur->next) {
        if (cur->kind == NODE_GLOBAL) {
            symtab_add_global_decl(f, cur->sval);
        } else if (cur->kind == NODE_IF) {
            collect_global_decls_in_block(f, cur->body);
            if (cur->else_branch) {
                if (cur->else_branch->kind == NODE_IF) {
                    /* single elif chain node */
                    ASTNode tmp_next = *cur->else_branch;
                    (void)tmp_next;
                    collect_global_decls_in_block(f, cur->else_branch);
                } else {
                    collect_global_decls_in_block(f, cur->else_branch);
                }
            }
        } else if (cur->kind == NODE_WHILE || cur->kind == NODE_FOR) {
            collect_global_decls_in_block(f, cur->body);
        }
    }
}

static void assign_exact_jvm_slots(SymTab *st) {
    /* Finalize defaults for any remaining unresolved types */
    for (int i = 0; i < st->global_count; i++) {
        if (st->globals[i].type == TYPE_LIST_EMPTY) {
            st->globals[i].type = TYPE_LIST_INT;
        } else if (st->globals[i].type == TYPE_UNKNOWN) {
            st->globals[i].type = TYPE_INT;
        }
    }

    st->main_next_slot = 1; /* slot 0 = String[] args */
    for (int i = 0; i < st->main_local_count; i++) {
        if (st->main_locals[i].type == TYPE_LIST_EMPTY) st->main_locals[i].type = TYPE_LIST_INT;
        if (st->main_locals[i].type == TYPE_UNKNOWN) st->main_locals[i].type = TYPE_INT;
        st->main_locals[i].slot = st->main_next_slot;
        st->main_next_slot += type_jvm_slots(st->main_locals[i].type);
    }

    for (int fidx = 0; fidx < st->func_count; fidx++) {
        FuncSym *f = &st->funcs[fidx];
        if (f->return_type == TYPE_UNKNOWN) f->return_type = TYPE_VOID;
        if (f->return_type == TYPE_LIST_EMPTY) f->return_type = TYPE_LIST_INT;

        for (int p = 0; p < f->param_count; p++) {
            if (f->param_types[p] == TYPE_UNKNOWN) f->param_types[p] = TYPE_INT;
            if (f->param_types[p] == TYPE_LIST_EMPTY) f->param_types[p] = TYPE_LIST_INT;
        }

        /* Recompute exact JVM slots: params first in declaration order, then locals */
        int slot = 0;
        for (int p = 0; p < f->param_count; p++) {
            VarSym *pv = symtab_find_local(st, f, f->param_names[p]);
            if (pv) {
                pv->type = f->param_types[p];
                pv->slot = slot;
                slot += type_jvm_slots(pv->type);
            }
        }
        for (int l = 0; l < f->local_count; l++) {
            if (f->locals[l].is_param) continue;
            if (f->locals[l].type == TYPE_LIST_EMPTY) f->locals[l].type = TYPE_LIST_INT;
            if (f->locals[l].type == TYPE_UNKNOWN) f->locals[l].type = TYPE_INT;
            f->locals[l].slot = slot;
            slot += type_jvm_slots(f->locals[l].type);
        }
        f->next_slot = slot;
    }
}

int semantic_analyze(ASTNode *program, SymTab *st) {
    if (!program || program->kind != NODE_PROGRAM) return 0;

    SemCtx ctx;
    memset(&ctx, 0, sizeof(SemCtx));
    ctx.st = st;

    /* Pass 1: Register all functions, imports, and global declarations */
    ctx.report_errors = 1;
    for (ASTNode *cur = program->body; cur != NULL; cur = cur->next) {
        if (cur->kind == NODE_FUNC_DEF) {
            if (symtab_find_func(st, cur->sval)) {
                sem_error(&ctx, cur->line, cur->col, "Funcao '%s' redeclarada", cur->sval);
                continue;
            }
            /* Check duplicate parameter names */
            for (ASTNode *p1 = cur->params; p1; p1 = p1->next) {
                for (ASTNode *p2 = p1->next; p2; p2 = p2->next) {
                    if (strcmp(p1->sval, p2->sval) == 0) {
                        sem_error(&ctx, p2->line, p2->col,
                                  "Parametro '%s' duplicado na funcao '%s'", p2->sval, cur->sval);
                    }
                }
            }
            int idx = symtab_add_func(st, cur->sval, cur, cur->line);
            cur->func_idx = idx;
            collect_global_decls_in_block(&st->funcs[idx], cur->body);
        } else if (cur->kind == NODE_IMPORT) {
            if (strcmp(cur->sval, "math") != 0 && strcmp(cur->sval, "array") != 0) {
                sem_error(&ctx, cur->line, cur->col,
                          "Modulo '%s' nao suportado (modulos disponiveis: math, array)", cur->sval);
            } else {
                symtab_add_import(st, cur->sval);
            }
        }
    }

    /* Pass 2: Fixed-point type inference (silent) */
    ctx.report_errors = 0;
    for (int iter = 0; iter < 10; iter++) {
        ctx.changed = 0;
        analyze_stmt_list(&ctx, program->body, NULL);
        for (int i = 0; i < st->func_count; i++) {
            if (st->funcs[i].called || iter > 0) {
                analyze_func_body(&ctx, &st->funcs[i]);
            }
        }
        if (!ctx.changed && iter >= 1) break;
    }

    /* Ensure any uncalled functions have default int params and are analyzed */
    for (int i = 0; i < st->func_count; i++) {
        FuncSym *f = &st->funcs[i];
        for (int p = 0; p < f->param_count; p++) {
            if (f->param_types[p] == TYPE_UNKNOWN) f->param_types[p] = TYPE_INT;
        }
        analyze_func_body(&ctx, f);
    }

    /* Pass 3: Assign exact JVM local variable slots */
    assign_exact_jvm_slots(st);

    /* Pass 4: Final semantic validation + AST resolution + hidden slots */
    ctx.report_errors = 1;
    analyze_stmt_list(&ctx, program->body, NULL);
    for (int i = 0; i < st->func_count; i++) {
        analyze_func_body(&ctx, &st->funcs[i]);
    }

    return ctx.errors;
}
