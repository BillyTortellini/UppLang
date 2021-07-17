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
    // Future: Enum and Unions
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
    Dynamic_Array<Type_Signature*> parameter_types;
    Type_Signature* return_type;
    // Array or Pointer
    Type_Signature* child_type;
    // Array
    int array_element_count;
    // Struct
    int struct_name_handle;
    Dynamic_Array<Struct_Member> member_types;
};
void type_signature_append_to_string(String* string, Type_Signature* signature);
void type_signature_print_value(Type_Signature* type, byte* value_ptr);

/*
    Basic Data types Documentation:
        Sized Array:
            Is a block of memory of the given size, meaning [2]int are 2 ints
        Unsized Array:
            Is a pointer to a given datatype + i32 size, size 16 Byte (At some point we want u64 size), alignment 8 Byte
        String:
            Is almost the same as a dynamic array, currently NULL-Terminated:
             * character_buffer: Unsized Array with the actual buffer size
             * size: Character count
             Size: 20 byte (When u64 size it will be 24), alignment: 8 Byte
*/
struct Type_System
{
    Lexer* lexer;
    Dynamic_Array<Type_Signature*> types;

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
    Type_Signature* string_type;
};

Type_System type_system_create(Lexer* lexer);
void type_system_destroy(Type_System* system);
void type_system_reset_all(Type_System* system, Lexer* lexer);
Type_Signature* type_system_make_pointer(Type_System* system, Type_Signature* child_type);
Type_Signature* type_system_make_array_unsized(Type_System* system, Type_Signature* element_type);
Type_Signature* type_system_make_array_sized(Type_System* system, Type_Signature* element_type, int array_element_count);
Type_Signature* type_system_make_function(Type_System* system, Dynamic_Array<Type_Signature*> parameter_types, Type_Signature* return_type);
void type_system_print(Type_System* system);



// Data Access
enum class IR_Data_Access_Type
{
    GLOBAL_DATA,
    PARAMETER,
    REGISTER,
    CONSTANT,
};

struct IR_Code_Block;
struct IR_Program;
struct IR_Function;
struct IR_Data_Access
{
    IR_Data_Access_Type type;
    bool is_memory_access; // If true, access the memory through the pointer
    union
    {
        IR_Function* function; // For parameters
        IR_Code_Block* definition_block; // For variables
        IR_Program* program; // For globals/constants
    } option;
    int index;
};
Type_Signature* ir_data_access_get_type(IR_Data_Access* access);



struct IR_Function;
struct IR_Data_Access;
struct IR_Hardcoded_Function;
enum class Symbol_Type
{
    MODULE,
    FUNCTION,
    HARDCODED_FUNCTION,
    TYPE, // Structs or others (Future)
    VARIABLE,
};

struct Symbol_Table;
struct Symbol
{
    Symbol_Type symbol_type;
    int name_handle;
    int definition_node_index;
    // Symbol Data
    union
    {
        IR_Function* function; // Functions
        IR_Data_Access variable_access; // Variables/Parameters
        Type_Signature* data_type; // Structs
        IR_Hardcoded_Function* hardcoded_function; // Hardcoded function
        Symbol_Table* module_table; // Modules
    } options;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    Dynamic_Array<Symbol> symbols;
    int ast_node_index;
};

Symbol* symbol_table_find_symbol(Symbol_Table* table, int name_handle);
Symbol* symbol_table_find_symbol_by_string(Symbol_Table* table, String* string, Lexer* lexer);
Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name_handle, Symbol_Type symbol_type);
void symbol_table_append_to_string(String* string, Symbol_Table* table, Lexer* lexer, bool print_root);


// Instructions
struct IR_Instruction_Move
{
    IR_Data_Access destination;
    IR_Data_Access source;
};

struct IR_Instruction_If
{
    IR_Data_Access condition;
    IR_Code_Block* true_branch;
    IR_Code_Block* false_branch;
};

struct IR_Instruction_While
{
    IR_Code_Block* condition_code;
    IR_Data_Access condition_access;
    IR_Code_Block* code;
};

enum class IR_Instruction_Call_Type
{
    FUNCTION_CALL,
    FUNCTION_POINTER_CALL,
    HARDCODED_FUNCTION_CALL,
};

struct IR_Function;
struct IR_Instruction_Call
{
    IR_Instruction_Call_Type call_type;
    union
    {
        IR_Function* function;
        IR_Data_Access pointer_access;
        IR_Hardcoded_Function* hardcoded;
    } options;
    Dynamic_Array<IR_Data_Access> arguments;
    IR_Data_Access destination;
};

enum class IR_Instruction_Return_Type
{
    EXIT,
    RETURN_EMPTY,
    RETURN_DATA,
};

enum class Exit_Code
{
    SUCCESS,
    OUT_OF_BOUNDS, 
    STACK_OVERFLOW,
    RETURN_VALUE_OVERFLOW,
};
void exit_code_append_to_string(String* string, Exit_Code code);

struct IR_Instruction_Return
{
    IR_Instruction_Return_Type type;
    union
    {
        Exit_Code exit_code;
        IR_Data_Access return_value;
    } options;
};

enum class IR_Instruction_Binary_OP_Type
{
    // Arithmetic
    ADDITION,
    SUBTRACTION,
    MULTIPLICATION,
    DIVISION,
    MODULO, // Only works for integer types

    // Comparison
    EQUAL,
    NOT_EQUAL,
    GREATER_THAN,
    GREATER_EQUAL,
    LESS_THAN,
    LESS_EQUAL,

    // Boolean
    AND,
    OR
};

struct IR_Instruction_Binary_OP
{
    IR_Instruction_Binary_OP_Type type;
    IR_Data_Access destination;
    IR_Data_Access operand_left;
    IR_Data_Access operand_right;
};

enum class IR_Instruction_Unary_OP_Type
{
    NOT,
    NEGATE,
};

struct IR_Instruction_Unary_OP
{
    IR_Instruction_Unary_OP_Type type;
    IR_Data_Access destination;
    IR_Data_Access source;
};

enum class IR_Instruction_Cast_Type
{
    PRIMITIVE_TYPES,
    POINTERS,
    POINTER_TO_U64,
    U64_TO_POINTER,
    ARRAY_SIZED_TO_UNSIZED
};

struct IR_Instruction_Cast
{
    IR_Instruction_Cast_Type type;
    IR_Data_Access destination;
    IR_Data_Access source;
};

enum class IR_Instruction_Address_Of_Type
{
    DATA,
    FUNCTION,
    STRUCT_MEMBER, 
    ARRAY_ELEMENT
};

struct IR_Instruction_Address_Of
{
    IR_Instruction_Address_Of_Type type;
    IR_Data_Access destination;
    IR_Data_Access source;
    union {
        IR_Function* function;
        Struct_Member member;
        IR_Data_Access index_access;
    } options;
};

struct IR_Instruction;
struct IR_Code_Block
{
    IR_Function* function;
    Dynamic_Array<Type_Signature*> registers;
    Dynamic_Array<IR_Instruction> instructions;
};

enum class IR_Instruction_Type
{
    FUNCTION_CALL,
    IF,
    WHILE,
    BLOCK,

    BREAK,
    CONTINUE,
    RETURN,

    MOVE,
    CAST,
    ADDRESS_OF,
    UNARY_OP,
    BINARY_OP,
};

struct IR_Instruction
{
    IR_Instruction_Type type;
    union
    {
        IR_Instruction_Call call;
        IR_Instruction_If if_instr;
        IR_Instruction_While while_instr;
        IR_Instruction_Return return_instr;
        IR_Instruction_Move move;
        IR_Instruction_Cast cast;
        IR_Instruction_Address_Of address_of;
        IR_Instruction_Unary_OP unary_op;
        IR_Instruction_Binary_OP binary_op;
        IR_Code_Block* block;
    } options;
};

struct IR_Program;
struct IR_Function
{
    IR_Program* program;
    Type_Signature* function_type;
    IR_Code_Block* code;
};

struct IR_Constant
{
    Type_Signature* type;
    int offset;
};

struct IR_Constant_Pool
{
    Dynamic_Array<IR_Constant> constants;
    Dynamic_Array<byte> constant_memory;
};

enum class IR_Hardcoded_Function_Type
{
    PRINT_I32,
    PRINT_F32,
    PRINT_BOOL,
    PRINT_LINE,
    PRINT_STRING,
    READ_I32,
    READ_F32,
    READ_BOOL,
    RANDOM_I32,
    MALLOC_SIZE_I32,
    FREE_POINTER,

    HARDCODED_FUNCTION_COUNT, // Should always be last element
};
void ir_hardcoded_function_type_append_to_string(String* string, IR_Hardcoded_Function_Type hardcoded);

struct IR_Hardcoded_Function
{
    IR_Hardcoded_Function_Type type;
    Type_Signature* signature;
};

struct IR_Program
{
    Dynamic_Array<IR_Function*> functions;
    Dynamic_Array<IR_Hardcoded_Function*> hardcoded_functions;
    Dynamic_Array<Type_Signature*> globals; // Global initialization needs to be done in the main function
    IR_Constant_Pool constant_pool;
    IR_Function* entry_function;
};
struct Semantic_Analyser;
void ir_program_append_to_string(IR_Program* program, String* string, Semantic_Analyser* analyser);

/*
    Semantic Analyser
*/

struct AST_Top_Level_Node_Location
{
    Symbol_Table* table;
    int node_index;
};

struct Semantic_Analyser
{
    IR_Program* program;
    Symbol_Table* root_table;
    Dynamic_Array<Symbol_Table*> symbol_tables;
    Hashtable<int, Symbol_Table*> ast_to_symbol_table;
    Dynamic_Array<Compiler_Error> errors;

    // Temporary stuff needed for analysis
    Compiler* compiler;
    Dynamic_Array<AST_Top_Level_Node_Location> location_functions;
    Dynamic_Array<AST_Top_Level_Node_Location> location_structs;
    Dynamic_Array<AST_Top_Level_Node_Location> location_globals;
    //Type_Signature* function_return_type;
    int loop_depth;

    int token_index_size;
    int token_index_data;
    int token_index_main;

    //Dynamic_Array<Struct_Fill_Out> struct_fill_outs;
    //Dynamic_Array<Semantic_Node_Information> semantic_information;
};

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_analyse(Semantic_Analyser* analyser, Compiler* compiler);
