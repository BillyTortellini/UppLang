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
    Dynamic_Array<String*> headers_to_include;
    Dynamic_Array<String*> source_files_to_compile;
    Dynamic_Array<String*> lib_files;
    Dynamic_Array<Extern_Function_Identifier> extern_functions;
    Hashtable<Type_Signature*, String*> extern_type_signatures; // Extern types to name id, e.g. HWND should not create its own structure, but use name HWND as type
};

struct Identifier_Pool
{
    Hashtable<String, String*> identifier_lookup_table;
};
Identifier_Pool identifier_pool_create();
void identifier_pool_destroy(Identifier_Pool* pool);
String* identifier_pool_add(Identifier_Pool* pool, String identifier);
void identifier_pool_print(Identifier_Pool* pool);

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

enum class Code_Origin_Type
{
    MAIN_PROJECT,
    LOADED_FILE,
    GENERATED
};

struct Code_Origin
{
    Code_Origin_Type type;
    String* id_filename; // May be null, otherwise pointer to string in identifier_pool
    AST_Node* load_node;
};

struct Code_Source
{
    Code_Origin origin;
    String source_code;
    // Lexer result
    Dynamic_Array<Token> tokens;
    Dynamic_Array<Token> tokens_with_decoration;
    // Parser result
    AST_Node* root_node;
};

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

    Dynamic_Array<Code_Source*> code_sources;
    Code_Source* main_source;
    Timer* timer;

    bool generate_code;
};

Compiler compiler_create(Timer* timer);
void compiler_destroy(Compiler* compiler);

void compiler_compile(Compiler* compiler, String source_code, bool generate_code);
Exit_Code compiler_execute(Compiler* compiler);
void compiler_add_source_code(Compiler* compiler, String source_code, Code_Origin origin); // Takes ownership of source_code

Text_Slice token_range_to_text_slice(Token_Range range, Compiler* compiler);
void exit_code_append_to_string(String* string, Exit_Code code);
void hardcoded_function_type_append_to_string(String* string, Hardcoded_Function_Type hardcoded);
Code_Source* compiler_ast_node_to_code_source(Compiler* compiler, AST_Node* node);
void compiler_run_testcases(Timer* timer);