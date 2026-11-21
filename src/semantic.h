#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"
#include "symtab.h"

/*
 * Performs full semantic analysis, scope resolution, and type inference.
 * Returns the number of semantic errors encountered (0 = success).
 */
int semantic_analyze(ASTNode *program, SymTab *st);

#endif
