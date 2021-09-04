#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

/*
    Defers need to be executed after:
        * Statement block exits
        * Continue, Return or Breaks

    Defer Rules:
        No Returns, Continues or Breaks inside a defer
        No nested defers

    Reset loop depth in defer
*/


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
    TEMPLATE_TYPE,
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
    // Primitive type
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
    // Template
    int template_name;
};
void type_signature_append_to_string(String* string, Type_Signature* signature);
void type_signature_append_value_to_string(Type_Signature* type, byte* value_ptr, String* string);

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
    FUNCTION,
    HARDCODED_FUNCTION,
    TYPE, // Structs or others (Not implemented yet, would be Enums, Tagged Enums, Unions...)
    VARIABLE,
    EXTERN_FUNCTION
};

struct Extern_Function_Identifier
{
    Type_Signature* function_signature;
    int name_id;
};

union Symbol_Options
{
    IR_Function* function; // Functions
    IR_Data_Access variable_access; // Variables/Parameters
    Type_Signature* data_type; // Structs
    IR_Hardcoded_Function* hardcoded_function; // Hardcoded function
    Extern_Function_Identifier extern_function;
};

struct Symbol_Template_Instance
{
    Dynamic_Array<Type_Signature*> template_arguments;
    Symbol_Options options;
    bool instanciated;
};

struct Symbol
{
    Symbol_Type symbol_type;
    int name_handle;
    int definition_node_index;
    bool is_templated;
    Symbol_Options options;
    // Template Data
    Dynamic_Array<int> template_parameter_names;
    Dynamic_Array<Symbol_Template_Instance> template_instances;
};

struct Symbol_Table;
struct Symbol_Table_Module
{
    bool is_templated;
    Dynamic_Array<int> template_parameter_names;
    Symbol_Table* module_table;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    Hashtable<int, Symbol> symbols;
    Hashtable<int, Symbol_Table_Module> modules;
};

struct Semantic_Analyser;
Symbol_Table* symbol_table_create(Semantic_Analyser* analyser, Symbol_Table* parent, int node_index, bool inside_defer);
void symbol_table_destroy(Symbol_Table* symbol_table);

Symbol* symbol_table_find_symbol(Symbol_Table* table, int name_handle, bool only_current_scope);
void symbol_table_append_to_string(String* string, Symbol_Table* table, Semantic_Analyser* analyser, bool print_root);
struct Identifier_Pool;
Symbol* symbol_table_find_symbol_by_string(Symbol_Table* table, String* string, Identifier_Pool* pool);



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
    EXTERN_FUNCTION_CALL,
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
        Extern_Function_Identifier extern_function;
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

enum class IR_Exit_Code
{
    SUCCESS,
    OUT_OF_BOUNDS,
    STACK_OVERFLOW,
    RETURN_VALUE_OVERFLOW,
};
void ir_exit_code_append_to_string(String* string, IR_Exit_Code code);

struct IR_Instruction_Return
{
    IR_Instruction_Return_Type type;
    union
    {
        IR_Exit_Code exit_code;
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

struct Extern_Program_Sources
{
    /*
    TODO:
        - DLLs
        - LIBs
    */
    Dynamic_Array<int> headers_to_include;
    Dynamic_Array<int> source_files_to_compile;
    Dynamic_Array<Extern_Function_Identifier> extern_functions;
    Hashset<Type_Signature*> extern_type_signatures;
};

struct IR_Program
{
    Dynamic_Array<IR_Function*> functions;
    Dynamic_Array<IR_Hardcoded_Function*> hardcoded_functions;
    Dynamic_Array<Type_Signature*> globals; // Global initialization needs to be done in the main function
    IR_Constant_Pool constant_pool;
    IR_Function* entry_function;
    Extern_Program_Sources extern_program_sources;
};
struct Semantic_Analyser;
void ir_program_append_to_string(IR_Program* program, String* string, Semantic_Analyser* analyser);



/*
    Semantic Analyser
*/

enum class Analysis_Workload_Type
{
    FUNCTION_HEADER,
    CODE_BLOCK,
    STRUCT_BODY,
    SIZED_ARRAY_SIZE,
    GLOBAL,
    EXTERN_FUNCTION_DECLARATION,
    EXTERN_HEADER_IMPORT
};

struct Analysis_Workload_Code_Block
{
    IR_Code_Block* code_block;
    int current_child_index;
    bool inside_defer;
    int local_block_defer_depth;
    int surrounding_loop_defer_depth;
    Dynamic_Array<int> active_defer_statements;
    bool requires_return;
    bool inside_loop;
    bool check_last_instruction_result;
};

struct Analysis_Workload_Struct_Body
{
    Type_Signature* struct_signature;
    int current_child_index;
    int offset;
    int alignment;

    // For templating stuff
    Symbol_Table* type_lookup_table;
    bool is_template_instance;
    int symbol_name_id;
    int symbol_instance_index;
};

struct Analysis_Workload_Function_Header
{
    Symbol_Table* type_lookup_table;
    bool is_template_instance;
    int symbol_name_id;
    int symbol_instance_index;
    bool is_template_analysis;
    Dynamic_Array<int> template_parameter_names;
};

struct Analysis_Workload
{
    Analysis_Workload_Type type;
    Symbol_Table* symbol_table;
    int node_index;
    union
    {
        Analysis_Workload_Struct_Body struct_body;
        Analysis_Workload_Code_Block code_block;
        Type_Signature* sized_array_type;
        Analysis_Workload_Function_Header function_header;
    } options;
};

enum class Workload_Dependency_Type
{
    IDENTIFER_NOT_FOUND,
    TYPE_SIZE_UNKNOWN, // Either of Sized_Array or Struct
    CODE_BLOCK_NOT_FINISHED,
    TEMPLATE_INSTANCE_NOT_FINISHED,
};

struct Workload_Dependency_Template_Instance_Not_Finished
{
    Symbol_Table* symbol_table;
    int symbol_name_id;
    int instance_index;
};

struct Workload_Dependency_Identifier_Not_Found
{
    Symbol_Table* symbol_table;
    bool current_scope_only;
    Dynamic_Array<Type_Signature*> template_parameter_names;
};

struct Workload_Dependency
{
    Workload_Dependency_Type type;
    int node_index;
    union {
        Type_Signature* type_signature;
        IR_Code_Block* code_block;
        Workload_Dependency_Identifier_Not_Found identifier_not_found;
        Workload_Dependency_Template_Instance_Not_Finished template_not_finished;
    } options;
};

struct Waiting_Workload
{
    Analysis_Workload workload;
    Workload_Dependency dependency;
};

struct AST_Top_Level_Node_Location
{
    Symbol_Table* table;
    int node_index;
};

enum class Statement_Analysis_Result
{
    NO_RETURN,
    RETURN,
    CONTINUE,
    BREAK
};

struct Semantic_Analyser
{
    // Result
    IR_Program* program;
    Dynamic_Array<Compiler_Error> errors;

    Symbol_Table* root_table;
    Dynamic_Array<Symbol_Table*> symbol_tables;
    Hashtable<int, Symbol_Table*> ast_to_symbol_table;

    // Temporary stuff needed for analysis
    Compiler* compiler;
    IR_Function* global_init_function;
    Hashtable<IR_Code_Block*, Statement_Analysis_Result> finished_code_blocks;
    Dynamic_Array<Analysis_Workload> active_workloads;
    Dynamic_Array<Waiting_Workload> waiting_workload;

    int token_index_size;
    int token_index_data;
    int token_index_main;
};

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_analyse(Semantic_Analyser* analyser, Compiler* compiler);
