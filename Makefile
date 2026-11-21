CC      = gcc
CFLAGS  = -Wall -Wextra -Isrc -Ibuild
FLEX    = flex
BISON   = bison
JASMIN  = jasmin
JAVA    = java

BUILD_DIR = build
TARGET    = pycomp
CLASS     = Programa
FILE     ?= tests/test1_aritmetica.py

SRCS = src/ast.c src/symtab.c src/semantic.c src/codegen.c src/main.c
GEN_PARSER_C = $(BUILD_DIR)/parser.tab.c
GEN_PARSER_H = $(BUILD_DIR)/parser.tab.h
GEN_LEXER_C  = $(BUILD_DIR)/lex.yy.c

.PHONY: all compiler run test clean

all: compiler

compiler: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(GEN_PARSER_C) $(GEN_PARSER_H): src/parser.y | $(BUILD_DIR)
	$(BISON) -Wcounterexamples -d -o $(GEN_PARSER_C) src/parser.y

$(GEN_LEXER_C): src/lexer.l $(GEN_PARSER_H) | $(BUILD_DIR)
	$(FLEX) -o $(GEN_LEXER_C) src/lexer.l

$(TARGET): $(GEN_PARSER_C) $(GEN_LEXER_C) $(SRCS)
	$(CC) $(CFLAGS) -Wno-unused-function $(GEN_PARSER_C) $(GEN_LEXER_C) $(SRCS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) $(FILE) -o $(BUILD_DIR)/output.j -c $(CLASS) --symtab
	$(JASMIN) -d $(BUILD_DIR) $(BUILD_DIR)/output.j
	@echo "=== SAIDA DA JVM ($(CLASS)) ==="
	$(JAVA) -cp $(BUILD_DIR) $(CLASS)

test: $(TARGET)
	@bash tests/run_tests.sh

clean:
	rm -rf $(BUILD_DIR) $(TARGET) output.j *.class
