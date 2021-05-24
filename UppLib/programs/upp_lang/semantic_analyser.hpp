#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

struct Compiler;
struct Lexer;
struct Compiler_Error;

enum class Primitive_Type
{
    BOOLEAN, // I have boolean, which does the same as unsigned_int_8, but for semantic analysis this is important
    SIGNED_INT_8,
    SIGNED_INT_16,
    SIGNED_INT_32,
    SIGNED_INT_64,
    UNSIGNED_INT_8, // byte is an alias for this
    UNSIGNED_INT_16,
    UNSIGNED_INT_32,
    UNSIGNED_INT_64,
    FLOAT_32,
    FLOAT_64,
};
String primitive_type_to_string(Primitive_Type type);
bool primitive_type_is_float(Primitive_Type type);
bool primitive_type_is_signed(Primitive_Type type);
bool primitive_type_is_integer(Primitive_Type type);

enum class Signature_Type
{
    VOID_TYPE,
    PRIMITIVE,
    POINTER,
    FUNCTION,
    STRUCT,
    ARRAY_SIZED, // Array with known size, like [5]int
    ARRAY_UNSIZED, // With unknown size, int[]
    ERROR_TYPE,
    // Future: Union, Tagged Union ...
};

struct Type_Signature;
struct Struct_Member
{
    Type_Signature* type;
    int offset;
    int name_handle;
};

struct Type_Signature
{
    Signature_Type type;
    int size_in_bytes;
    int alignment_in_bytes;
    // Primitve type
    Primitive_Type primitive_type;
    // Function
    DynamicArray<Type_Signature*> parameter_types;
    Type_Signature* return_type;
    // Array or Pointer
    Type_Signature* child_type;
    // Array
    int array_element_count;
    // Struct
    int struct_name_handle;
    DynamicArray<Struct_Member> member_types;
};
void type_signature_append_to_string(String* string, Type_Signature* signature);

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
    Type_Signature* void_ptr_type;
};

Type_System type_system_create();
void type_system_destroy(Type_System* system);
void type_system_reset_all(Type_System* system);
Type_Signature* type_system_make_pointer(Type_System* system, Type_Signature* child_type);
Type_Signature* type_system_make_array_unsized(Type_System* system, Type_Signature* element_type);
Type_Signature* type_system_make_array_sized(Type_System* system, Type_Signature* element_type, int array_element_count);
Type_Signature* type_system_make_function(Type_System* system, DynamicArray<Type_Signature*> parameter_types, Type_Signature* return_type);
void type_system_print(Type_System* system);





namespace Symbol_Type
{
    enum ENUM
    {
        VARIABLE,
        FUNCTION,
        TYPE, // Used to map identifiers to types (E.g. "float" to type f32, struct identifier to struct type)
    };
};

struct Symbol
{
    int name_handle;
    Symbol_Type::ENUM symbol_type; // Required since functions, variables and Types could have the same type? TODO: Check this
    Type_Signature* type;
    int token_index_definition;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    DynamicArray<Symbol> symbols;
};

Symbol_Table symbol_table_create(Symbol_Table* parent);
void symbol_table_destroy(Symbol_Table* table);
Symbol* symbol_table_find_symbol(Symbol_Table* table, int name_handle);
Symbol* symbol_table_find_symbol_by_string(Symbol_Table* table, String* string, Lexer* lexer);
Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name_handle, Symbol_Type::ENUM symbol_type);
void symbol_table_append_to_string(String* string, Symbol_Table* table, Lexer* lexer, bool print_root);





struct Semantic_Node_Information
{
    // Available on all node-types
    int symbol_table_index;
    // Available on expressions
    Type_Signature* expression_result_type;
    // Available on structs
    Type_Signature* struct_signature;
    // Available on member access expression
    bool member_access_is_address_of;
    bool member_access_is_constant_size; // If this is true, then the size is stored in member_access_offset
    bool member_access_needs_pointer_dereference;
    int member_access_offset;
    // Available on functions
    Type_Signature* function_signature;
    bool needs_empty_return_at_end;
    // Available on delete
    bool delete_is_array_delete;
    // Available on expressions before binary operation, function arguments and assignment statements
    bool needs_casting_to_cast_type;
    Type_Signature* cast_result_type;
};

struct Struct_Fill_Out
{
    Type_Signature* signature;
    int struct_node_index;
    bool marked;
    bool generated;
    int name_id;
};

enum class Hardcoded_Function_Type
{
    PRINT_I32,
    PRINT_F32,
    PRINT_BOOL,
    PRINT_LINE,
    READ_I32,
    READ_F32,
    READ_BOOL,
    RANDOM_I32,
    MALLOC_SIZE_I32,
    FREE_POINTER,

    HARDCODED_FUNCTION_COUNT, // Should always be last element
};

struct Hardcoded_Function
{
    Hardcoded_Function_Type type;
    int name_handle;
    Type_Signature* function_type;
};

struct Semantic_Analyser
{
    DynamicArray<Symbol_Table*> symbol_tables;
    DynamicArray<Semantic_Node_Information> semantic_information;
    DynamicArray<Compiler_Error> errors;
    Array<Hardcoded_Function> hardcoded_functions;

    // Temporary stuff needed for analysis
    Compiler* compiler;
    DynamicArray<Struct_Fill_Out> struct_fill_outs;
    Type_Signature* function_return_type;
    int loop_depth;

    int size_token_index;
    int data_token_index;
    int main_token_index;
};

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_analyse(Semantic_Analyser* analyser, Compiler* compiler);
