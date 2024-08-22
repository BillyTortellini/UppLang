#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "semantic_analyser.hpp"

struct Compiler;
struct IR_Function;
struct IR_Data_Access;
struct IR_Code_Block;
struct IR_Program;
struct Datatype;
struct Analysis_Pass;

enum class IR_Data_Access_Type
{
    GLOBAL_DATA,
    PARAMETER,
    REGISTER,
    CONSTANT,
    NOTHING, // Placeholder for function that return nothing
};

struct IR_Data_Access
{
    IR_Data_Access_Type type;
    bool is_memory_access; // If true, access the memory through the pointer
    union
    {
        IR_Function* function; // For parameters
        IR_Code_Block* definition_block; // For variables
    } option;
    int index;
};
Datatype* ir_data_access_get_type(IR_Data_Access* access);

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
        struct {
            Datatype_Function* signature;
            Hardcoded_Type type;
        } hardcoded;
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

struct IR_Instruction_Return
{
    IR_Instruction_Return_Type type;
    union
    {
        Exit_Code exit_code;
        IR_Data_Access return_value;
    } options;
};

struct IR_Instruction_Binary_OP
{
    AST::Binop type;
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

enum class IR_Cast_Type
{
    INTEGERS,
    FLOATS,
    FLOAT_TO_INT,
    INT_TO_FLOAT,
    POINTERS,
    POINTER_TO_U64,
    U64_TO_POINTER,
    ENUM_TO_INT,
    INT_TO_ENUM,
};

struct IR_Instruction_Cast
{
    IR_Cast_Type type;
    IR_Data_Access destination;
    IR_Data_Access source;
};

enum class IR_Instruction_Address_Of_Type
{
    DATA,
    FUNCTION,
    EXTERN_FUNCTION,
    STRUCT_MEMBER,
    ARRAY_ELEMENT // Source can be both array or slice, and the result is always a pointer
};

struct IR_Instruction_Address_Of
{
    IR_Instruction_Address_Of_Type type;
    IR_Data_Access destination;
    IR_Data_Access source;
    union {
        IR_Function* function;
        Extern_Function_Identifier extern_function;
        Struct_Member member;
        IR_Data_Access index_access;
    } options;
};

struct IR_Instruction;
struct IR_Code_Block
{
    IR_Function* function;
    Dynamic_Array<Datatype*> registers;
    Dynamic_Array<IR_Instruction> instructions;
};

struct IR_Switch_Case
{
    int value;
    IR_Code_Block* block;
};

struct IR_Instruction_Switch
{
    IR_Data_Access condition_access;
    Dynamic_Array<IR_Switch_Case> cases;
    IR_Code_Block* default_block;
};

enum class IR_Instruction_Type
{
    FUNCTION_CALL,
    IF,
    WHILE,
    SWITCH,
    BLOCK,

    LABEL,
    GOTO,
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
        IR_Instruction_Switch switch_instr;
        IR_Instruction_Move move;
        IR_Instruction_Cast cast;
        IR_Instruction_Address_Of address_of;
        IR_Instruction_Unary_OP unary_op;
        IR_Instruction_Binary_OP binary_op;
        IR_Code_Block* block;
        int label_index;
    } options;
};

struct IR_Program;
struct IR_Function
{
    IR_Program* program;
    Datatype_Function* function_type;
    IR_Code_Block* code;
    ModTree_Function* origin;
};

struct IR_Program
{
    Dynamic_Array<IR_Function*> functions;
    IR_Function* entry_function;
};

struct Type_System;
struct ModTree_Program;
struct ModTree_Function;
struct ModTree_Global;
struct Compiler;

struct Unresolved_Goto
{
    IR_Code_Block* block;
    int instruction_index;
    AST::Code_Block* break_block;
};

struct Function_Stub
{
    ModTree_Function* mod_func;
    IR_Function* ir_func;
};

enum class Loop_Type
{
    FOR_LOOP,
    FOREACH_LOOP
};

struct Loop_Increment
{
    Loop_Type type;
    struct
    {
        AST::Statement* increment_statement; // Valid for normal for loops
        struct {
            IR_Data_Access index_access;
            IR_Data_Access iterable_access;
            IR_Data_Access loop_variable_access;

            bool is_custom_iterator;
            // Only valid for custom iterators
            IR_Data_Access iterator_access;
            IR_Function* next_function;
            int iterator_deref_value;
        } foreach_loop;
    } options;
};

struct IR_Generator
{
    IR_Program* program;
    ModTree_Program* modtree;

    // Stuff needed for compilation
    Hashtable<AST::Definition_Symbol*, IR_Data_Access> variable_mapping; 
    Hashtable<ModTree_Function*, IR_Function*> function_mapping;
    Hashtable<AST::Code_Block*, Loop_Increment> loop_increment_instructions; // For for loops

    Dynamic_Array<Function_Stub> queue_functions;

    Dynamic_Array<AST::Code_Block*> defer_stack;
    Dynamic_Array<Unresolved_Goto> fill_out_continues;
    Dynamic_Array<Unresolved_Goto> fill_out_breaks;

    Hashtable<AST::Code_Block*, int> labels_break;
    Hashtable<AST::Code_Block*, int> labels_continue;
    Hashtable<AST::Code_Block*, int> block_defer_depths;

    int next_label_index;
    Analysis_Pass* current_pass;
};

extern IR_Generator ir_generator;

IR_Generator* ir_generator_initialize();
void ir_generator_destroy();
void ir_generator_reset();

void ir_generator_queue_function(ModTree_Function* function);
void ir_generator_generate_queued_items(bool gen_bytecode);
void ir_generator_finish(bool gen_bytecode);

IR_Program* ir_program_create(Type_System* type_system);
void ir_program_destroy(IR_Program* program);

void ir_program_append_to_string(IR_Program* program, String* string);
Datatype* ir_data_access_get_type(IR_Data_Access* access);



