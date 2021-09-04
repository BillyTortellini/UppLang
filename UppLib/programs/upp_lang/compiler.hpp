#pragma once

#include "../../win32/timing.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"

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

struct Identifier_Pool
{
    Dynamic_Array<String> identifiers;
    Hashtable<String, int> identifier_index_lookup_table;
};
Identifier_Pool identifier_pool_create();
void identifier_pool_destroy(Identifier_Pool* pool);
int identifier_pool_add_or_find_identifier_by_string(Identifier_Pool* lexer, String identifier);
String identifier_pool_index_to_string(Identifier_Pool* Lexer, int index);
void identifier_pool_print(Identifier_Pool* pool);

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
#include "c_importer.hpp"

struct Compiler
{
    Identifier_Pool* identifier_pool;
    Lexer lexer;
    AST_Parser parser;
    Type_System type_system;
    Semantic_Analyser analyser;
    Bytecode_Generator bytecode_generator;
    Bytecode_Interpreter bytecode_interpreter;
    C_Generator c_generator;
    C_Compiler c_compiler;
    C_Importer c_importer;
    Timer* timer;
};

Compiler compiler_create(Timer* timer);
void compiler_destroy(Compiler* compiler);
void compiler_compile(Compiler* compiler, String* source_code, bool generate_code);
void compiler_execute(Compiler* compiler);
Text_Slice token_range_to_text_slice(Token_Range range, Compiler* compiler);