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
    Primitive_Type primitive_type;
    int size_in_bytes;
    int alignment_in_bytes;
    // Array or Pointer Child
    int child_type_index;
    // Function Stuff
    DynamicArray<int> parameter_type_indices;
    int return_type_index;
    // Array Data
    int array_size;
};

// This will later also contain which types can be implicitly cast, maybe i need to rethink some stuff for structs
struct Type_System
{
    DynamicArray<Type_Signature> types;
};

Type_Signature type_signature_make_error();
Type_Signature type_signature_make_pointer(int type_index_pointed_to);
Type_Signature type_signature_make_primitive(Primitive_Type type);
Type_Signature type_signature_make_array_unsized(Type_System* system, int array_element_index);
Type_Signature type_signature_make_array_sized(Type_System* system, int array_element_index, int array_size);

Type_System type_system_create();
void type_system_destroy(Type_System* system);
void type_system_reset_all(Type_System* system);
int type_system_find_or_create_type(Type_System* system, Type_Signature s);
Type_Signature* type_system_get_type(Type_System* system, int index);
Type_Signature* type_system_get_child_type(Type_System* system, int index);
void type_index_append_to_string(String* string, Type_System* system, int index);
void type_system_print(Type_System* system);



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
    int name;
    Symbol_Type::ENUM symbol_type; // Required since functions, variables and Types could have the same type? TODO: Check this
    int type_index;
    int function_node_index; // For Functions
};

struct Symbol_Table
{
    Symbol_Table* parent;
    DynamicArray<Symbol> symbols;
};

struct Semantic_Node_Information
{
    int symbol_table_index; // Which symbol table is active in this node
    int expression_result_type_index;
    int function_signature_index;
};

struct Semantic_Analyser
{
    Type_System type_system;
    DynamicArray<Symbol_Table*> symbol_tables;
    DynamicArray<Semantic_Node_Information> semantic_information;
    DynamicArray<Compiler_Error> errors;

    // Temporary stuff needed for analysis
    AST_Parser* parser;
    int function_return_type_index;
    int loop_depth;

    int size_token_index;
    int data_token_index;
    int main_token_index;
    int error_type_index;
    int bool_type_index;
    int i32_type_index;
    int f32_type_index;
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
    int type_index;
    bool has_memory_address;
};

Symbol_Table symbol_table_create(Symbol_Table* parent);
void symbol_table_destroy(Symbol_Table* table);
Symbol* symbol_table_find_symbol(Symbol_Table* table, int name, bool* in_current_scope);
Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name, Symbol_Type::ENUM symbol_type);

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_analyse(Semantic_Analyser* analyser, AST_Parser* parser);
