#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"
#include "symtab.h"

/*
 * Generates Jasmin JVM assembly code into `out` for class `class_name`.
 * Returns 0 on success, non-zero on error.
 */
int codegen_generate(ASTNode *program, SymTab *st, const char *class_name, FILE *out);

#endif
