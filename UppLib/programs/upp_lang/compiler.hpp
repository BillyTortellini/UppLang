#pragma once

#include "../../win32/timing.hpp"

struct Lexer;
struct Compiler;
struct AST_Parser;
struct Type_System;
struct Semantic_Analyser;
struct Intermediate_Generator;
struct Bytecode_Generator;
struct Bytecode_Interpreter;
struct C_Generator;
struct C_Compiler;

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
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "c_backend.hpp"

struct Compiler
{
    Lexer lexer;
    AST_Parser parser;
    Type_System type_system;
    Semantic_Analyser analyser;
    Bytecode_Generator bytecode_generator;
    Bytecode_Interpreter bytecode_interpreter;
    C_Generator c_generator;
    C_Compiler c_compiler;
    Timer* timer;
};

Compiler compiler_create(Timer* timer);
void compiler_destroy(Compiler* compiler);
void compiler_compile(Compiler* compiler, String* source_code, bool generate_code);
void compiler_execute(Compiler* compiler);
Text_Slice token_range_to_text_slice(Token_Range range, Compiler* compiler);