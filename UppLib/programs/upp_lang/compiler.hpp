#pragma once

struct Lexer;
struct Compiler;
struct AST_Parser;
struct Type_System;
struct Semantic_Analyser;
struct Intermediate_Generator;
struct Bytecode_Generator;
struct Bytecode_Interpreter;
struct C_Generator;

struct Token_Range
{
    int start_index;
    int end_index;
};
Token_Range token_range_make(int start_index, int end_index);

struct Compiler_Error
{
    const char* message;
    Token_Range range;
};

struct Compiler;

#include "lexer.hpp"
#include "ast_parser.hpp"
#include "semantic_analyser.hpp"
#include "intermediate_code.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "c_backend.hpp"

struct Compiler
{
    Lexer lexer;
    AST_Parser parser;
    Type_System type_system;
    Semantic_Analyser analyser;
    Intermediate_Generator intermediate_generator;
    Bytecode_Generator bytecode_generator;
    Bytecode_Interpreter bytecode_interpreter;
    C_Generator c_generator;
};

Compiler compiler_create();
void compiler_destroy(Compiler* compiler);
void compiler_compile(Compiler* compiler, String* source_code, bool generate_code);
void compiler_execute(Compiler* compiler);