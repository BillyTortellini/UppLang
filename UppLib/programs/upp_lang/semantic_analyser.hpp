#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

#include "lexer.hpp"
#include "ast_parser.hpp"

enum class Primitive_Type
{
    BOOLEAN, // I have boolean, which does the same as unsigned_int_8, but for semantic analysis this is important
    SIGNED_INT_8,
    SIGNED_INT_16,
    SIGNED_INT_32,
    SIGNED_INT_64,
    UNSIGNED_INT_8, // Same as byte
    UNSIGNED_INT_16,
    UNSIGNED_INT_32,
    UNSIGNED_INT_64,
    FLOAT_32,
    FLOAT_64,
};
String primitive_type_to_string(Primitive_Type type);

enum class Signature_Type
{
    VOID_TYPE,
    PRIMITIVE,
    POINTER,
    FUNCTION,
    ARRAY_SIZED, // Array with known size, like [5]int
    ARRAY_UNSIZED, // With unknown size, int[]
    ERROR_TYPE,
    // Future: Struct, Union, Tagged Union ...
};

struct Type_Signature
{
    Signature_Type type;
    int size_in_bytes;
    int alignment_in_bytes;
    // Primitve type
    Primitive_Type primitive_type;
    // Array or Pointer Stuff
    Type_Signature* child_type;
    // Function Stuff
    DynamicArray<Type_Signature*> parameter_types;
    Type_Signature* return_type;
    // Array Stuff
    int array_element_count;
};
void type_signature_append_to_string(String* string, Type_Signature* signature);

// This will later also contain which types can be implicitly cast, maybe i need to rethink some stuff for structs
struct Type_System
{
    DynamicArray<Type_Signature*> types;

    Type_Signature* error_type;
    Type_Signature* bool_type;
    Type_Signature* i8_type;
    Type_Signature* i16_type;
    Type_Signature* i32_type;
    Type_Signature* i64_type;
    Type_Signature* u8_type;
    Type_Signature* u16_type;
    Type_Signature* u32_type;
    Type_Signature* u64_type;
    Type_Signature* f32_type;
    Type_Signature* f64_type;
    Type_Signature* void_type;
};

Type_System type_system_create();
void type_system_destroy(Type_System* system);
void type_system_reset_all(Type_System* system);
Type_Signature* type_system_make_pointer(Type_System* system, Type_Signature* child_type);
Type_Signature* type_system_make_array_unsized(Type_System* system, Type_Signature* element_type);
Type_Signature* type_system_make_array_sized(Type_System* system, Type_Signature* element_type, int array_element_count);
void type_system_print(Type_System* system);



enum class Hardcoded_Function_Type
{
    PRINT_I32, // print(15) "15"
    PRINT_F32, // print(23.32) 
    PRINT_BOOL, // print(true)
    PRINT_LINE, // print_line()
    READ_I32,
    READ_F32,
    READ_BOOL,
    RANDOM_I32,

    HARDCODED_FUNCTION_COUNT, // Should always be last element
};

struct Hardcoded_Function
{
    Hardcoded_Function_Type type;
    int name_handle;
    Type_Signature* function_type;
};



namespace Symbol_Type
{
    enum ENUM
    {
        VARIABLE,
        FUNCTION,
        TYPE, // This is already used to map u8, int, f64 are mapped to types
    };
};

struct Symbol
{
    int name_handle;
    Symbol_Type::ENUM symbol_type; // Required since functions, variables and Types could have the same type? TODO: Check this
    Type_Signature* type;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    DynamicArray<Symbol> symbols;
};

struct Semantic_Node_Information
{
    int symbol_table_index; // Which symbol table is active in this node
    Type_Signature* expression_result_type;
    Type_Signature* function_signature;
};

struct Semantic_Analyser
{
    Type_System type_system;
    DynamicArray<Symbol_Table*> symbol_tables;
    DynamicArray<Semantic_Node_Information> semantic_information;
    DynamicArray<Compiler_Error> errors;
    Array<Hardcoded_Function> hardcoded_functions;

    // Usefull stuff for now
    int size_token_index;
    int data_token_index;
    int main_token_index;

    // Temporary stuff needed for analysis
    AST_Parser* parser;
    Type_Signature* function_return_type;
    int loop_depth;
};

enum class Statement_Analysis_Result
{
    NO_RETURN,
    RETURN,
    CONTINUE,
    BREAK
};

struct Expression_Analysis_Result
{
    Type_Signature* type;
    bool has_memory_address;
};

Symbol_Table symbol_table_create(Symbol_Table* parent);
void symbol_table_destroy(Symbol_Table* table);
Symbol* symbol_table_find_symbol(Symbol_Table* table, int name_handle, bool* in_current_scope);
Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name_handle, Symbol_Type::ENUM symbol_type);

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_analyse(Semantic_Analyser* analyser, AST_Parser* parser);
