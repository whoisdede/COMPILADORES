#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* CodeQueue (buffer encadeado de instrucoes Jasmin, conforme relatorio) */
typedef struct CodeNode {
    char *line;
    struct CodeNode *next;
} CodeNode;

typedef struct {
    CodeNode *head;
    CodeNode *tail;
} CodeQueue;

static void cq_init(CodeQueue *q) {
    q->head = q->tail = NULL;
}

static void cq_emit(CodeQueue *q, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    CodeNode *n = (CodeNode *)malloc(sizeof(CodeNode));
    n->line = (char *)malloc(strlen(buf) + 1);
    strcpy(n->line, buf);
    n->next = NULL;

    if (!q->tail) {
        q->head = q->tail = n;
    } else {
        q->tail->next = n;
        q->tail = n;
    }
}

static void cq_write_and_free(CodeQueue *q, FILE *out) {
    CodeNode *cur = q->head;
    while (cur) {
        CodeNode *next = cur->next;
        fprintf(out, "%s\n", cur->line);
        free(cur->line);
        free(cur);
        cur = next;
    }
    q->head = q->tail = NULL;
}

typedef struct {
    SymTab *st;
    const char *class_name;
    int label_counter;
    CodeQueue out;
} GenCtx;

static int new_label(GenCtx *g) {
    return g->label_counter++;
}

static void emit(GenCtx *g, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cq_emit(&g->out, "    %s", buf);
}

static void emit_raw(GenCtx *g, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cq_emit(&g->out, "%s", buf);
}

static void emit_label(GenCtx *g, int lbl) {
    cq_emit(&g->out, "L%d:", lbl);
}

static void emit_int_const(GenCtx *g, int v) {
    if (v == -1) emit(g, "iconst_m1");
    else if (v >= 0 && v <= 5) emit(g, "iconst_%d", v);
    else if (v >= -128 && v <= 127) emit(g, "bipush %d", v);
    else if (v >= -32768 && v <= 32767) emit(g, "sipush %d", v);
    else emit(g, "ldc %d", v);
}

static void emit_double_const(GenCtx *g, double v) {
    if (v == 0.0) {
        emit(g, "dconst_0");
    } else if (v == 1.0) {
        emit(g, "dconst_1");
    } else {
        char num[64];
        snprintf(num, sizeof(num), "%.15g", v);
        /* Ensure decimal point or exponent so Jasmin parses as double, not long */
        if (!strchr(num, '.') && !strchr(num, 'e') && !strchr(num, 'E')) {
            strcat(num, ".0");
        }
        emit(g, "ldc2_w %s", num);
    }
}

static void emit_string_const(GenCtx *g, const char *s) {
    char esc[1024];
    size_t j = 0;
    esc[j++] = '"';
    for (size_t i = 0; s && s[i] && j + 4 < sizeof(esc); i++) {
        switch (s[i]) {
            case '\\': esc[j++] = '\\'; esc[j++] = '\\'; break;
            case '"':  esc[j++] = '\\'; esc[j++] = '"';  break;
            case '\n': esc[j++] = '\\'; esc[j++] = 'n';  break;
            case '\t': esc[j++] = '\\'; esc[j++] = 't';  break;
            case '\r': esc[j++] = '\\'; esc[j++] = 'r';  break;
            default:   esc[j++] = s[i]; break;
        }
    }
    esc[j++] = '"';
    esc[j] = '\0';
    emit(g, "ldc %s", esc);
}

static void emit_load_local(GenCtx *g, VarType t, int slot) {
    if (t == TYPE_DOUBLE) {
        emit(g, "dload %d", slot);
    } else if (t == TYPE_INT || t == TYPE_BOOL) {
        emit(g, "iload %d", slot);
    } else {
        emit(g, "aload %d", slot);
    }
}

static void emit_store_local(GenCtx *g, VarType t, int slot) {
    if (t == TYPE_DOUBLE) {
        emit(g, "dstore %d", slot);
    } else if (t == TYPE_INT || t == TYPE_BOOL) {
        emit(g, "istore %d", slot);
    } else {
        emit(g, "astore %d", slot);
    }
}

static void emit_load_var(GenCtx *g, int is_global, const char *name, VarType t, int slot) {
    if (is_global) {
        emit(g, "getstatic %s/%s %s", g->class_name, name, type_to_jvm_desc(t));
    } else {
        emit_load_local(g, t, slot);
    }
}

static void emit_store_var(GenCtx *g, int is_global, const char *name, VarType t, int slot) {
    if (is_global) {
        emit(g, "putstatic %s/%s %s", g->class_name, name, type_to_jvm_desc(t));
    } else {
        emit_store_local(g, t, slot);
    }
}

static void emit_box(GenCtx *g, VarType elem_t) {
    if (elem_t == TYPE_INT) {
        emit(g, "invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
    } else if (elem_t == TYPE_DOUBLE) {
        emit(g, "invokestatic java/lang/Double/valueOf(D)Ljava/lang/Double;");
    }
}

static void emit_unbox(GenCtx *g, VarType elem_t) {
    if (elem_t == TYPE_INT) {
        emit(g, "checkcast java/lang/Integer");
        emit(g, "invokevirtual java/lang/Integer/intValue()I");
    } else if (elem_t == TYPE_DOUBLE) {
        emit(g, "checkcast java/lang/Double");
        emit(g, "invokevirtual java/lang/Double/doubleValue()D");
    } else if (elem_t == TYPE_STRING) {
        emit(g, "checkcast java/lang/String");
    }
}

static void emit_pop(GenCtx *g, VarType t) {
    if (t == TYPE_VOID) return;
    if (t == TYPE_DOUBLE) emit(g, "pop2");
    else emit(g, "pop");
}

/* Forward declarations */
static void gen_expr(GenCtx *g, ASTNode *n);
static void gen_expr_as(GenCtx *g, ASTNode *n, VarType expected);
static void gen_cond_jump(GenCtx *g, ASTNode *cond, int target_label, int jump_if_true);
static void gen_stmt(GenCtx *g, ASTNode *n, FuncSym *func);
static void gen_stmt_list(GenCtx *g, ASTNode *stmts, FuncSym *func);

static void gen_expr_as(GenCtx *g, ASTNode *n, VarType expected) {
    gen_expr(g, n);
    if (expected == TYPE_DOUBLE && n->inferred_type == TYPE_INT) {
        emit(g, "i2d");
    }
}

static int is_zero_literal(const ASTNode *n) {
    if (!n) return 0;
    if (n->kind == NODE_INT && n->ival == 0) return 1;
    if (n->kind == NODE_DOUBLE && n->fval == 0.0) return 1;
    return 0;
}

static void gen_print_one(GenCtx *g, ASTNode *arg, int newline) {
    const char *mname = newline ? "println" : "print";
    VarType t = arg->inferred_type;

    emit(g, "getstatic java/lang/System/out Ljava/io/PrintStream;");
    gen_expr(g, arg);

    if (t == TYPE_INT) {
        emit(g, "invokevirtual java/io/PrintStream/%s(I)V", mname);
    } else if (t == TYPE_DOUBLE) {
        emit(g, "invokevirtual java/io/PrintStream/%s(D)V", mname);
    } else if (t == TYPE_BOOL) {
        emit(g, "invokevirtual java/io/PrintStream/%s(Z)V", mname);
    } else if (t == TYPE_STRING) {
        emit(g, "invokevirtual java/io/PrintStream/%s(Ljava/lang/String;)V", mname);
    } else if (t == TYPE_ARRAY_INT) {
        emit(g, "invokestatic java/util/Arrays/toString([I)Ljava/lang/String;");
        emit(g, "invokevirtual java/io/PrintStream/%s(Ljava/lang/String;)V", mname);
    } else if (t == TYPE_ARRAY_DOUBLE) {
        emit(g, "invokestatic java/util/Arrays/toString([D)Ljava/lang/String;");
        emit(g, "invokevirtual java/io/PrintStream/%s(Ljava/lang/String;)V", mname);
    } else if (t == TYPE_ARRAY_STRING) {
        emit(g, "invokestatic java/util/Arrays/toString([Ljava/lang/Object;)Ljava/lang/String;");
        emit(g, "invokevirtual java/io/PrintStream/%s(Ljava/lang/String;)V", mname);
    } else {
        /* ArrayList or other Object */
        emit(g, "invokevirtual java/io/PrintStream/%s(Ljava/lang/Object;)V", mname);
    }
}

static void gen_call(GenCtx *g, ASTNode *n) {
    const char *fname = n->sval;

    /* print(...) */
    if (strcmp(fname, "print") == 0) {
        if (!n->args) {
            emit(g, "getstatic java/lang/System/out Ljava/io/PrintStream;");
            emit(g, "invokevirtual java/io/PrintStream/println()V");
            return;
        }
        for (ASTNode *a = n->args; a != NULL; a = a->next) {
            int is_last = (a->next == NULL);
            gen_print_one(g, a, is_last);
            if (!is_last) {
                emit(g, "getstatic java/lang/System/out Ljava/io/PrintStream;");
                emit(g, "ldc \" \"");
                emit(g, "invokevirtual java/io/PrintStream/print(Ljava/lang/String;)V");
            }
        }
        return;
    }

    /* len(x) */
    if (strcmp(fname, "len") == 0) {
        ASTNode *a = n->args;
        gen_expr(g, a);
        if (a->inferred_type == TYPE_STRING) {
            emit(g, "invokevirtual java/lang/String/length()I");
        } else if (type_is_array(a->inferred_type)) {
            emit(g, "arraylength");
        } else {
            emit(g, "invokevirtual java/util/ArrayList/size()I");
        }
        return;
    }

    /* array(tipo, tamanho_ou_lista) */
    if (strcmp(fname, "array") == 0) {
        ASTNode *type_arg = n->args;
        ASTNode *spec_arg = type_arg->next;
        VarType elem_t = type_arg->inferred_type;

        if (spec_arg->kind == NODE_LIST_LIT) {
            int count = 0;
            for (ASTNode *e = spec_arg->args; e; e = e->next) count++;
            emit_int_const(g, count);
            if (elem_t == TYPE_INT) emit(g, "newarray int");
            else if (elem_t == TYPE_DOUBLE) emit(g, "newarray double");
            else emit(g, "anewarray java/lang/String");

            int idx = 0;
            for (ASTNode *e = spec_arg->args; e; e = e->next, idx++) {
                emit(g, "dup");
                emit_int_const(g, idx);
                gen_expr_as(g, e, elem_t);
                if (elem_t == TYPE_INT) emit(g, "iastore");
                else if (elem_t == TYPE_DOUBLE) emit(g, "dastore");
                else emit(g, "aastore");
            }
        } else {
            gen_expr(g, spec_arg);
            if (elem_t == TYPE_INT) emit(g, "newarray int");
            else if (elem_t == TYPE_DOUBLE) emit(g, "newarray double");
            else emit(g, "anewarray java/lang/String");
        }
        return;
    }

    /* int(x) */
    if (strcmp(fname, "int") == 0) {
        ASTNode *a = n->args;
        gen_expr(g, a);
        if (a->inferred_type == TYPE_DOUBLE) {
            emit(g, "d2i");
        } else if (a->inferred_type == TYPE_STRING) {
            emit(g, "invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
        }
        return;
    }

    /* float(x) */
    if (strcmp(fname, "float") == 0) {
        ASTNode *a = n->args;
        gen_expr(g, a);
        if (a->inferred_type == TYPE_INT) {
            emit(g, "i2d");
        } else if (a->inferred_type == TYPE_STRING) {
            emit(g, "invokestatic java/lang/Double/parseDouble(Ljava/lang/String;)D");
        }
        return;
    }

    /* str(x) */
    if (strcmp(fname, "str") == 0) {
        ASTNode *a = n->args;
        gen_expr(g, a);
        if (a->inferred_type == TYPE_INT) {
            emit(g, "invokestatic java/lang/String/valueOf(I)Ljava/lang/String;");
        } else if (a->inferred_type == TYPE_DOUBLE) {
            emit(g, "invokestatic java/lang/String/valueOf(D)Ljava/lang/String;");
        } else if (a->inferred_type == TYPE_BOOL) {
            emit(g, "invokestatic java/lang/String/valueOf(Z)Ljava/lang/String;");
        } else if (a->inferred_type != TYPE_STRING) {
            emit(g, "invokestatic java/lang/String/valueOf(Ljava/lang/Object;)Ljava/lang/String;");
        }
        return;
    }

    /* abs(x) */
    if (strcmp(fname, "abs") == 0) {
        ASTNode *a = n->args;
        gen_expr(g, a);
        if (a->inferred_type == TYPE_DOUBLE) {
            emit(g, "invokestatic java/lang/Math/abs(D)D");
        } else {
            emit(g, "invokestatic java/lang/Math/abs(I)I");
        }
        return;
    }

    /* User-defined function */
    FuncSym *callee = &g->st->funcs[n->func_idx];
    ASTNode *arg = n->args;
    for (int i = 0; i < callee->param_count && arg; i++, arg = arg->next) {
        gen_expr_as(g, arg, callee->param_types[i]);
    }
    char desc[256];
    symtab_func_descriptor(callee, desc, sizeof(desc));
    emit(g, "invokestatic %s/%s%s", g->class_name, callee->name, desc);
}

static void gen_method_call(GenCtx *g, ASTNode *n) {
    const char *obj = n->obj_name;
    const char *meth = n->sval;

    /* math.* */
    if (strcmp(obj, "math") == 0) {
        if (strcmp(meth, "pow") == 0) {
            gen_expr_as(g, n->args, TYPE_DOUBLE);
            gen_expr_as(g, n->args->next, TYPE_DOUBLE);
            emit(g, "invokestatic java/lang/Math/pow(DD)D");
        } else {
            gen_expr_as(g, n->args, TYPE_DOUBLE);
            const char *jm = (strcmp(meth, "fabs") == 0) ? "abs" : meth;
            emit(g, "invokestatic java/lang/Math/%s(D)D", jm);
        }
        return;
    }

    /* List methods */
    if (type_is_list(n->sym_type)) {
        VarType et = type_elem(n->sym_type);
        emit_load_var(g, n->sym_is_global, obj, n->sym_type, n->sym_slot);

        if (strcmp(meth, "append") == 0) {
            gen_expr_as(g, n->args, et);
            emit_box(g, et);
            emit(g, "invokevirtual java/util/ArrayList/add(Ljava/lang/Object;)Z");
            emit(g, "pop");
        } else if (strcmp(meth, "pop") == 0) {
            if (!n->args) {
                emit(g, "dup");
                emit(g, "invokevirtual java/util/ArrayList/size()I");
                emit(g, "iconst_1");
                emit(g, "isub");
            } else {
                gen_expr(g, n->args);
            }
            emit(g, "invokevirtual java/util/ArrayList/remove(I)Ljava/lang/Object;");
            emit_unbox(g, et);
        } else if (strcmp(meth, "clear") == 0) {
            emit(g, "invokevirtual java/util/ArrayList/clear()V");
        }
        return;
    }

    /* String methods */
    if (n->sym_type == TYPE_STRING) {
        emit_load_var(g, n->sym_is_global, obj, TYPE_STRING, n->sym_slot);
        if (strcmp(meth, "upper") == 0) {
            emit(g, "invokevirtual java/lang/String/toUpperCase()Ljava/lang/String;");
        } else if (strcmp(meth, "lower") == 0) {
            emit(g, "invokevirtual java/lang/String/toLowerCase()Ljava/lang/String;");
        }
    }
}

static const char *icmp_op(OpKind op, int invert) {
    if (!invert) {
        switch (op) {
            case OP_EQ:  return "if_icmpeq";
            case OP_NEQ: return "if_icmpne";
            case OP_LT:  return "if_icmplt";
            case OP_LE:  return "if_icmple";
            case OP_GT:  return "if_icmpgt";
            case OP_GE:  return "if_icmpge";
            default:     return "if_icmpeq";
        }
    } else {
        switch (op) {
            case OP_EQ:  return "if_icmpne";
            case OP_NEQ: return "if_icmpeq";
            case OP_LT:  return "if_icmpge";
            case OP_LE:  return "if_icmpgt";
            case OP_GT:  return "if_icmple";
            case OP_GE:  return "if_icmplt";
            default:     return "if_icmpne";
        }
    }
}

static const char *ifzero_op(OpKind op, int invert) {
    if (!invert) {
        switch (op) {
            case OP_EQ:  return "ifeq";
            case OP_NEQ: return "ifne";
            case OP_LT:  return "iflt";
            case OP_LE:  return "ifle";
            case OP_GT:  return "ifgt";
            case OP_GE:  return "ifge";
            default:     return "ifeq";
        }
    } else {
        switch (op) {
            case OP_EQ:  return "ifne";
            case OP_NEQ: return "ifeq";
            case OP_LT:  return "ifge";
            case OP_LE:  return "ifgt";
            case OP_GT:  return "ifle";
            case OP_GE:  return "iflt";
            default:     return "ifne";
        }
    }
}

static void gen_cond_jump(GenCtx *g, ASTNode *cond, int target_label, int jump_if_true) {
    if (cond->kind == NODE_UNOP && cond->op == OP_NOT) {
        gen_cond_jump(g, cond->left, target_label, !jump_if_true);
        return;
    }

    if (cond->kind == NODE_BINOP && cond->op == OP_AND) {
        if (jump_if_true) {
            int l_skip = new_label(g);
            gen_cond_jump(g, cond->left, l_skip, 0);
            gen_cond_jump(g, cond->right, target_label, 1);
            emit_label(g, l_skip);
        } else {
            gen_cond_jump(g, cond->left, target_label, 0);
            gen_cond_jump(g, cond->right, target_label, 0);
        }
        return;
    }

    if (cond->kind == NODE_BINOP && cond->op == OP_OR) {
        if (jump_if_true) {
            gen_cond_jump(g, cond->left, target_label, 1);
            gen_cond_jump(g, cond->right, target_label, 1);
        } else {
            int l_skip = new_label(g);
            gen_cond_jump(g, cond->left, l_skip, 1);
            gen_cond_jump(g, cond->right, target_label, 0);
            emit_label(g, l_skip);
        }
        return;
    }

    if (cond->kind == NODE_BINOP &&
        (cond->op == OP_EQ || cond->op == OP_NEQ ||
         cond->op == OP_LT || cond->op == OP_LE ||
         cond->op == OP_GT || cond->op == OP_GE)) {
        VarType lt = cond->left->inferred_type;
        VarType rt = cond->right->inferred_type;

        if (lt == TYPE_STRING && rt == TYPE_STRING) {
            gen_expr(g, cond->left);
            gen_expr(g, cond->right);
            emit(g, "invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z");
            int eq_jump_true = (cond->op == OP_EQ) ? jump_if_true : !jump_if_true;
            emit(g, "%s L%d", eq_jump_true ? "ifne" : "ifeq", target_label);
            return;
        }

        if (lt == TYPE_DOUBLE || rt == TYPE_DOUBLE) {
            gen_expr_as(g, cond->left, TYPE_DOUBLE);
            gen_expr_as(g, cond->right, TYPE_DOUBLE);
            emit(g, "dcmpg");
            emit(g, "%s L%d", ifzero_op(cond->op, !jump_if_true), target_label);
            return;
        }

        /* int or bool comparison */
        gen_expr(g, cond->left);
        gen_expr(g, cond->right);
        emit(g, "%s L%d", icmp_op(cond->op, !jump_if_true), target_label);
        return;
    }

    /* Generic boolean/int expression */
    gen_expr(g, cond);
    emit(g, "%s L%d", jump_if_true ? "ifne" : "ifeq", target_label);
}

static void gen_expr(GenCtx *g, ASTNode *n) {
    if (!n) return;

    switch (n->kind) {
        case NODE_INT:
            emit_int_const(g, n->ival);
            break;

        case NODE_DOUBLE:
            emit_double_const(g, n->fval);
            break;

        case NODE_BOOL:
            emit(g, n->ival ? "iconst_1" : "iconst_0");
            break;

        case NODE_STRING:
            emit_string_const(g, n->sval);
            break;

        case NODE_VAR:
            emit_load_var(g, n->sym_is_global, n->sval, n->sym_type, n->sym_slot);
            break;

        case NODE_UNOP:
            if (n->op == OP_NEG) {
                gen_expr(g, n->left);
                emit(g, (n->inferred_type == TYPE_DOUBLE) ? "dneg" : "ineg");
            } else if (n->op == OP_NOT) {
                int l_true = new_label(g);
                int l_end = new_label(g);
                gen_cond_jump(g, n, l_true, 1);
                emit(g, "iconst_0");
                emit(g, "goto L%d", l_end);
                emit_label(g, l_true);
                emit(g, "iconst_1");
                emit_label(g, l_end);
            }
            break;

        case NODE_BINOP:
            if (n->op == OP_EQ || n->op == OP_NEQ ||
                n->op == OP_LT || n->op == OP_LE ||
                n->op == OP_GT || n->op == OP_GE ||
                n->op == OP_AND || n->op == OP_OR) {
                int l_true = new_label(g);
                int l_end = new_label(g);
                gen_cond_jump(g, n, l_true, 1);
                emit(g, "iconst_0");
                emit(g, "goto L%d", l_end);
                emit_label(g, l_true);
                emit(g, "iconst_1");
                emit_label(g, l_end);
                break;
            }

            if (n->op == OP_ADD && n->inferred_type == TYPE_STRING) {
                emit(g, "new java/lang/StringBuilder");
                emit(g, "dup");
                emit(g, "invokespecial java/lang/StringBuilder/<init>()V");
                gen_expr(g, n->left);
                emit(g, "invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                gen_expr(g, n->right);
                emit(g, "invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                emit(g, "invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;");
                break;
            }

            if (n->inferred_type == TYPE_DOUBLE) {
                gen_expr_as(g, n->left, TYPE_DOUBLE);
                gen_expr_as(g, n->right, TYPE_DOUBLE);
                switch (n->op) {
                    case OP_ADD: emit(g, "dadd"); break;
                    case OP_SUB: emit(g, "dsub"); break;
                    case OP_MUL: emit(g, "dmul"); break;
                    case OP_DIV: emit(g, "ddiv"); break;
                    default: break;
                }
            } else {
                gen_expr(g, n->left);
                gen_expr(g, n->right);
                switch (n->op) {
                    case OP_ADD:      emit(g, "iadd"); break;
                    case OP_SUB:      emit(g, "isub"); break;
                    case OP_MUL:      emit(g, "imul"); break;
                    case OP_DIV:
                    case OP_FLOORDIV: emit(g, "idiv"); break;
                    case OP_MOD:      emit(g, "irem"); break;
                    default: break;
                }
            }
            break;

        case NODE_ARRAY_INIT: {
            VarType elem_t = type_elem(n->inferred_type);
            gen_expr(g, n->right); /* size */
            if (elem_t == TYPE_INT) emit(g, "newarray int");
            else if (elem_t == TYPE_DOUBLE) emit(g, "newarray double");
            else emit(g, "anewarray java/lang/String");

            if (!is_zero_literal(n->left)) {
                /* Fill array with initial value using hidden_slot1 (arr) and hidden_slot2 (i) */
                int s_arr = n->hidden_slot1;
                int s_idx = n->hidden_slot2;
                int l_loop = new_label(g);
                int l_end = new_label(g);

                emit(g, "astore %d", s_arr);
                emit(g, "iconst_0");
                emit(g, "istore %d", s_idx);
                emit_label(g, l_loop);
                emit(g, "iload %d", s_idx);
                emit(g, "aload %d", s_arr);
                emit(g, "arraylength");
                emit(g, "if_icmpge L%d", l_end);

                emit(g, "aload %d", s_arr);
                emit(g, "iload %d", s_idx);
                gen_expr_as(g, n->left, elem_t);
                if (elem_t == TYPE_INT) emit(g, "iastore");
                else if (elem_t == TYPE_DOUBLE) emit(g, "dastore");
                else emit(g, "aastore");

                emit(g, "iinc %d 1", s_idx);
                emit(g, "goto L%d", l_loop);
                emit_label(g, l_end);
                emit(g, "aload %d", s_arr);
            }
            break;
        }

        case NODE_LIST_LIT: {
            VarType elem_t = type_elem(n->inferred_type);
            emit(g, "new java/util/ArrayList");
            emit(g, "dup");
            emit(g, "invokespecial java/util/ArrayList/<init>()V");

            for (ASTNode *el = n->args; el != NULL; el = el->next) {
                emit(g, "dup");
                gen_expr_as(g, el, elem_t);
                emit_box(g, elem_t);
                emit(g, "invokevirtual java/util/ArrayList/add(Ljava/lang/Object;)Z");
                emit(g, "pop");
            }
            break;
        }

        case NODE_INDEX: {
            VarType ct = n->left->inferred_type;
            gen_expr(g, n->left);
            gen_expr(g, n->right);

            if (ct == TYPE_ARRAY_INT) {
                emit(g, "iaload");
            } else if (ct == TYPE_ARRAY_DOUBLE) {
                emit(g, "daload");
            } else if (ct == TYPE_ARRAY_STRING) {
                emit(g, "aaload");
            } else if (type_is_list(ct)) {
                emit(g, "invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;");
                emit_unbox(g, type_elem(ct));
            } else if (ct == TYPE_STRING) {
                /* s[i] -> s.substring(i, i + 1) */
                emit(g, "dup");
                emit(g, "iconst_1");
                emit(g, "iadd");
                emit(g, "invokevirtual java/lang/String/substring(II)Ljava/lang/String;");
            }
            break;
        }

        case NODE_CALL:
            gen_call(g, n);
            break;

        case NODE_METHOD_CALL:
            gen_method_call(g, n);
            break;

        default:
            break;
    }
}

static void gen_stmt(GenCtx *g, ASTNode *n, FuncSym *func) {
    if (!n) return;

    switch (n->kind) {
        case NODE_FUNC_DEF:
        case NODE_IMPORT:
        case NODE_GLOBAL:
        case NODE_PASS:
            break;

        case NODE_ASSIGN:
            gen_expr_as(g, n->left, n->sym_type);
            emit_store_var(g, n->sym_is_global, n->sval, n->sym_type, n->sym_slot);
            break;

        case NODE_INDEX_ASSIGN: {
            VarType ct = n->sym_type;
            VarType et = type_elem(ct);
            emit_load_var(g, n->sym_is_global, n->sval, ct, n->sym_slot);
            gen_expr(g, n->left);
            gen_expr_as(g, n->right, et);
            if (ct == TYPE_ARRAY_INT) {
                emit(g, "iastore");
            } else if (ct == TYPE_ARRAY_DOUBLE) {
                emit(g, "dastore");
            } else if (ct == TYPE_ARRAY_STRING) {
                emit(g, "aastore");
            } else if (type_is_list(ct)) {
                emit_box(g, et);
                emit(g, "invokevirtual java/util/ArrayList/set(ILjava/lang/Object;)Ljava/lang/Object;");
                emit(g, "pop");
            }
            break;
        }

        case NODE_IF: {
            if (!n->else_branch) {
                int l_end = new_label(g);
                gen_cond_jump(g, n->cond, l_end, 0);
                gen_stmt_list(g, n->body, func);
                emit_label(g, l_end);
            } else {
                int l_else = new_label(g);
                int l_end = new_label(g);
                gen_cond_jump(g, n->cond, l_else, 0);
                gen_stmt_list(g, n->body, func);
                emit(g, "goto L%d", l_end);
                emit_label(g, l_else);
                if (n->else_branch->kind == NODE_IF) {
                    gen_stmt(g, n->else_branch, func);
                } else {
                    gen_stmt_list(g, n->else_branch, func);
                }
                emit_label(g, l_end);
            }
            break;
        }

        case NODE_WHILE: {
            int l_cond = new_label(g);
            int l_end = new_label(g);
            emit_label(g, l_cond);
            gen_cond_jump(g, n->cond, l_end, 0);
            gen_stmt_list(g, n->body, func);
            emit(g, "goto L%d", l_cond);
            emit_label(g, l_end);
            break;
        }

        case NODE_FOR: {
            ASTNode *iter = n->left;
            if (iter->kind == NODE_CALL && strcmp(iter->sval, "range") == 0) {
                ASTNode *a1 = iter->args;
                ASTNode *a2 = a1 ? a1->next : NULL;
                ASTNode *a3 = a2 ? a2->next : NULL;
                int s_stop = n->hidden_slot1;
                int s_step = n->hidden_slot2;

                if (!a2) {
                    /* range(stop) -> 0 .. stop, step 1 */
                    emit_int_const(g, 0);
                    emit_store_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                    gen_expr(g, a1);
                    emit(g, "istore %d", s_stop);
                    emit_int_const(g, 1);
                    emit(g, "istore %d", s_step);
                } else if (!a3) {
                    /* range(start, stop) -> start .. stop, step 1 */
                    gen_expr(g, a1);
                    emit_store_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                    gen_expr(g, a2);
                    emit(g, "istore %d", s_stop);
                    emit_int_const(g, 1);
                    emit(g, "istore %d", s_step);
                } else {
                    /* range(start, stop, step) */
                    gen_expr(g, a1);
                    emit_store_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                    gen_expr(g, a2);
                    emit(g, "istore %d", s_stop);
                    gen_expr(g, a3);
                    emit(g, "istore %d", s_step);
                }

                int l_cond = new_label(g);
                int l_neg = new_label(g);
                int l_body = new_label(g);
                int l_end = new_label(g);

                emit_label(g, l_cond);
                emit(g, "iload %d", s_step);
                emit(g, "iflt L%d", l_neg);
                /* Positive step: continue while var < stop */
                emit_load_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                emit(g, "iload %d", s_stop);
                emit(g, "if_icmpge L%d", l_end);
                emit(g, "goto L%d", l_body);
                /* Negative step: continue while var > stop */
                emit_label(g, l_neg);
                emit_load_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                emit(g, "iload %d", s_stop);
                emit(g, "if_icmple L%d", l_end);

                emit_label(g, l_body);
                gen_stmt_list(g, n->body, func);

                /* var = var + step */
                emit_load_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                emit(g, "iload %d", s_step);
                emit(g, "iadd");
                emit_store_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                emit(g, "goto L%d", l_cond);
                emit_label(g, l_end);
            } else if (iter->inferred_type == TYPE_INT) {
                /* Compatibility: `for i in n:` iterates 0 .. n-1 */
                int s_stop = n->hidden_slot1;
                int l_cond = new_label(g);
                int l_end = new_label(g);

                gen_expr(g, iter);
                emit(g, "istore %d", s_stop);
                emit_int_const(g, 0);
                emit_store_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);

                emit_label(g, l_cond);
                emit_load_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                emit(g, "iload %d", s_stop);
                emit(g, "if_icmpge L%d", l_end);

                gen_stmt_list(g, n->body, func);

                emit_load_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                emit(g, "iconst_1");
                emit(g, "iadd");
                emit_store_var(g, n->sym_is_global, n->sval, TYPE_INT, n->sym_slot);
                emit(g, "goto L%d", l_cond);
                emit_label(g, l_end);
            } else {
                /* Iterating over array or ArrayList */
                VarType ct = iter->inferred_type;
                VarType et = type_elem(ct);
                int s_ref = n->hidden_slot1;
                int s_idx = n->hidden_slot2;
                int s_len = n->hidden_slot3;
                int l_cond = new_label(g);
                int l_end = new_label(g);

                gen_expr(g, iter);
                emit(g, "dup");
                emit(g, "astore %d", s_ref);
                if (type_is_array(ct)) {
                    emit(g, "arraylength");
                } else {
                    emit(g, "invokevirtual java/util/ArrayList/size()I");
                }
                emit(g, "istore %d", s_len);
                emit(g, "iconst_0");
                emit(g, "istore %d", s_idx);

                emit_label(g, l_cond);
                emit(g, "iload %d", s_idx);
                emit(g, "iload %d", s_len);
                emit(g, "if_icmpge L%d", l_end);

                emit(g, "aload %d", s_ref);
                emit(g, "iload %d", s_idx);
                if (ct == TYPE_ARRAY_INT) {
                    emit(g, "iaload");
                } else if (ct == TYPE_ARRAY_DOUBLE) {
                    emit(g, "daload");
                } else if (ct == TYPE_ARRAY_STRING) {
                    emit(g, "aaload");
                } else {
                    emit(g, "invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;");
                    emit_unbox(g, et);
                }
                if (n->sym_type == TYPE_DOUBLE && et == TYPE_INT) {
                    emit(g, "i2d");
                }
                emit_store_var(g, n->sym_is_global, n->sval, n->sym_type, n->sym_slot);

                gen_stmt_list(g, n->body, func);

                emit(g, "iinc %d 1", s_idx);
                emit(g, "goto L%d", l_cond);
                emit_label(g, l_end);
            }
            break;
        }

        case NODE_RETURN: {
            if (!n->left || !func || func->return_type == TYPE_VOID) {
                emit(g, "return");
            } else {
                gen_expr_as(g, n->left, func->return_type);
                if (func->return_type == TYPE_DOUBLE) {
                    emit(g, "dreturn");
                } else if (func->return_type == TYPE_INT || func->return_type == TYPE_BOOL) {
                    emit(g, "ireturn");
                } else {
                    emit(g, "areturn");
                }
            }
            break;
        }

        case NODE_EXPR_STMT:
            gen_expr(g, n->left);
            emit_pop(g, n->left->inferred_type);
            break;

        default:
            break;
    }
}

static void gen_stmt_list(GenCtx *g, ASTNode *stmts, FuncSym *func) {
    for (ASTNode *cur = stmts; cur != NULL; cur = cur->next) {
        gen_stmt(g, cur, func);
    }
}

int codegen_generate(ASTNode *program, SymTab *st, const char *class_name, FILE *out) {
    if (!program || !st || !out) return 1;

    GenCtx g;
    memset(&g, 0, sizeof(GenCtx));
    g.st = st;
    g.class_name = class_name ? class_name : "Programa";
    cq_init(&g.out);

    /* Class header */
    emit_raw(&g, "; Codigo Jasmin gerado automaticamente pelo compilador Python->JVM (Parte 3)");
    emit_raw(&g, ".class public %s", g.class_name);
    emit_raw(&g, ".super java/lang/Object");
    emit_raw(&g, "");

    /* Global fields */
    if (st->global_count > 0) {
        emit_raw(&g, "; Variaveis globais");
        for (int i = 0; i < st->global_count; i++) {
            emit_raw(&g, ".field public static %s %s",
                     st->globals[i].name, type_to_jvm_desc(st->globals[i].type));
        }
        emit_raw(&g, "");
    }

    /* Default constructor <init>()V */
    emit_raw(&g, ".method public <init>()V");
    emit(&g, ".limit stack 1");
    emit(&g, ".limit locals 1");
    emit(&g, "aload_0");
    emit(&g, "invokespecial java/lang/Object/<init>()V");
    emit(&g, "return");
    emit_raw(&g, ".end method");
    emit_raw(&g, "");

    /* User-defined functions (def ...) */
    for (int i = 0; i < st->func_count; i++) {
        FuncSym *f = &st->funcs[i];
        char desc[256];
        symtab_func_descriptor(f, desc, sizeof(desc));

        int locals_limit = (f->next_slot > 0) ? f->next_slot : 1;
        emit_raw(&g, ".method public static %s%s", f->name, desc);
        emit(&g, ".limit stack 64");
        emit(&g, ".limit locals %d", locals_limit);

        gen_stmt_list(&g, f->ast_node->body, f);

        /* Safety fallback return to satisfy JVM bytecode verifier */
        if (f->return_type == TYPE_VOID || f->return_type == TYPE_UNKNOWN) {
            emit(&g, "return");
        } else if (f->return_type == TYPE_DOUBLE) {
            emit(&g, "dconst_0");
            emit(&g, "dreturn");
        } else if (f->return_type == TYPE_INT || f->return_type == TYPE_BOOL) {
            emit(&g, "iconst_0");
            emit(&g, "ireturn");
        } else {
            emit(&g, "aconst_null");
            emit(&g, "areturn");
        }

        emit_raw(&g, ".end method");
        emit_raw(&g, "");
    }

    /* Main method containing all top-level statements */
    int main_locals = (st->main_next_slot > 1) ? st->main_next_slot : 1;
    emit_raw(&g, ".method public static main([Ljava/lang/String;)V");
    emit(&g, ".limit stack 64");
    emit(&g, ".limit locals %d", main_locals);

    gen_stmt_list(&g, program->body, NULL);

    emit(&g, "return");
    emit_raw(&g, ".end method");

    cq_write_and_free(&g.out, out);
    return 0;
}
