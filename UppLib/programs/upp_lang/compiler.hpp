#pragma once

#include "../../win32/timing.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "compiler_misc.hpp"

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
struct IR_Generator;
struct Type_Signature;



struct Upp_Constant
{
    Type_Signature* type;
    int offset;
};

struct Constant_Pool
{
    Dynamic_Array<Upp_Constant> constants;
    Dynamic_Array<byte> buffer;
};
int constant_pool_add_constant(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes);

struct Extern_Sources
{
    /*
    TODO:
        - DLLs
        - LIBs
    */
    Dynamic_Array<int> headers_to_include;
    Dynamic_Array<int> source_files_to_compile;
    Dynamic_Array<int> lib_files;
    Dynamic_Array<Extern_Function_Identifier> extern_functions;
    Hashtable<Type_Signature*, int> extern_type_signatures; // Extern types to name id, e.g. HWND should not create its own structure, but use name HWND as type
};

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



#include "lexer.hpp"
#include "ast_parser.hpp"
#include "semantic_analyser.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "c_backend.hpp"
#include "c_importer.hpp"
#include "type_system.hpp"
#include "ir_code.hpp"

struct Compiler
{
    Identifier_Pool identifier_pool;
    Constant_Pool constant_pool;
    Type_System type_system;
    Extern_Sources extern_sources;

    Lexer lexer;
    AST_Parser parser;
    Semantic_Analyser analyser;
    IR_Generator ir_generator;
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
void exit_code_append_to_string(String* string, Exit_Code code);
void hardcoded_function_type_append_to_string(String* string, Hardcoded_Function_Type hardcoded);