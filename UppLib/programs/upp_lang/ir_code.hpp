#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "semantic_analyser.hpp"

/*
    Some Reasons why differentiating IR_Code/ModTree could be ok:
        - Expressions cannot resolve sized_array to array cast to just statements
        - Expression evaluation order not given by modtree
        - Differentiate Statement/Expression VS. Instructions only
*/

struct Compiler;
struct IR_Function;
struct IR_Data_Access;
struct IR_Code_Block;
struct IR_Program;
struct Type_Signature;
struct Constant_Pool;

enum class IR_Data_Access_Type
{
    GLOBAL_DATA,
    PARAMETER,
    REGISTER,
    CONSTANT,
};

struct IR_Data_Access
{
    IR_Data_Access_Type type;
    bool is_memory_access; // If true, access the memory through the pointer
    union
    {
        IR_Function* function; // For parameters
        IR_Code_Block* definition_block; // For variables
        IR_Program* program; // For globals
        Constant_Pool* constant_pool; // For constants
    } option;
    int index;
};
Type_Signature* ir_data_access_get_type(IR_Data_Access* access);

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
            Type_Signature* signature;
            Hardcoded_Function_Type type;
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
    ModTree_Binary_Operation_Type type;
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

struct IR_Instruction_Cast
{
    ModTree_Cast_Type type;
    IR_Data_Access destination;
    IR_Data_Access source;
};

enum class IR_Instruction_Address_Of_Type
{
    DATA,
    FUNCTION,
    EXTERN_FUNCTION,
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
        Extern_Function_Identifier extern_function;
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
    Type_Signature* function_type;
    IR_Code_Block* code;
};

struct IR_Program
{
    Dynamic_Array<IR_Function*> functions;
    Dynamic_Array<Type_Signature*> globals;
    IR_Function* entry_function;
};

struct Type_System;
struct ModTree_Program;
struct ModTree_Variable;
struct ModTree_Function;
struct Compiler;
struct Type_Signature;

struct Unresolved_Goto
{
    IR_Code_Block* block;
    int instruction_index;
    ModTree_Block* break_block;
};

struct IR_Generator
{
    Compiler* compiler;
    IR_Program* program;
    Type_System* type_system;
    ModTree_Program* modtree;

    // Stuff needed for compilation
    Hashtable<ModTree_Variable*, IR_Data_Access> variable_mapping;
    Hashtable<ModTree_Function*, IR_Function*> function_mapping;

    Dynamic_Array<ModTree_Function*> function_stubs;
    Dynamic_Array<ModTree_Variable*> queue_globals;
    Dynamic_Array<ModTree_Function*> queue_functions;

    Dynamic_Array<Unresolved_Goto> fill_out_continues;
    Dynamic_Array<Unresolved_Goto> fill_out_breaks;

    Hashtable<ModTree_Block*, int> labels_break;
    Hashtable<ModTree_Block*, int> labels_continue;

    int next_label_index;
};

IR_Generator ir_generator_create();
void ir_generator_destroy(IR_Generator* generator);
void ir_generator_reset(IR_Generator* generator, Compiler* compiler);
void ir_generator_generate_queued_items(IR_Generator* generator);
void ir_generator_queue_function(IR_Generator* generator, ModTree_Function* function);
void ir_generator_queue_global(IR_Generator* generator, ModTree_Variable* variable);
void ir_generator_generate_queue_and_generate_all(IR_Generator* generator);

IR_Program* ir_program_create(Type_System* type_system);
void ir_program_destroy(IR_Program* program);

void ir_program_append_to_string(IR_Program* program, String* string);
Type_Signature* ir_data_access_get_type(IR_Data_Access* access);



