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
    // Value Accesses
    GLOBAL_DATA,
    CONSTANT,
    PARAMETER,
    REGISTER,
    NOTHING, // Placeholder for function that return nothing

    // Operation Accesses
    MEMBER_ACCESS,
    ARRAY_ELEMENT_ACCESS, // Either on a slice or on an array
    POINTER_DEREFERENCE, // &ip
    ADDRESS_OF_VALUE, // &ip
};

struct IR_Data_Access
{
    IR_Data_Access_Type type;
    Datatype* datatype;
    union
    {
        int global_index;
        int constant_index;
        struct {
            IR_Function* function; // For parameters
            int index;
        } parameter;
        struct {
            IR_Code_Block* definition_block; // For variables
            int index;
        } register_access;
        struct {
            IR_Data_Access* struct_access;
            Struct_Member member;
        } member_access;
        struct {
            IR_Data_Access* array_access;
            IR_Data_Access* index_access;
        } array_access;
        IR_Data_Access* pointer_value; // For pointer dereferences
        IR_Data_Access* address_of_value;
    } option;
};

struct IR_Instruction_Move
{
    IR_Data_Access* destination;
    IR_Data_Access* source;
};

struct IR_Instruction_If
{
    IR_Data_Access* condition;
    IR_Code_Block* true_branch;
    IR_Code_Block* false_branch;
};

struct IR_Instruction_While
{
    IR_Code_Block* condition_code;
    IR_Data_Access* condition_access;
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
        ModTree_Function* function;
        IR_Data_Access* pointer_access;
        Hardcoded_Type hardcoded;
    } options;
    Dynamic_Array<IR_Data_Access*> arguments;
    IR_Data_Access* destination;
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
        IR_Data_Access* return_value;
    } options;
};

enum class IR_Binop
{
    ADDITION,
    SUBTRACTION,
    DIVISION,
    MULTIPLICATION,
    MODULO,

    AND,
    OR,
    BITWISE_AND,
    BITWISE_OR,
    BITWISE_XOR,
    BITWISE_SHIFT_LEFT,
    BITWISE_SHIFT_RIGHT,

    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_OR_EQUAL,
    GREATER,
    GREATER_OR_EQUAL,
};


struct IR_Instruction_Binary_OP
{
    IR_Binop type;
    IR_Data_Access* destination;
    IR_Data_Access* operand_left;
    IR_Data_Access* operand_right;
};

enum class IR_Unop
{
    NOT,
    BITWISE_NOT,
    NEGATE,
};

struct IR_Instruction_Unary_OP
{
    IR_Unop type;
    IR_Data_Access* destination;
    IR_Data_Access* source;
};

enum class IR_Cast_Type
{
    INTEGERS,
    FLOATS,
    FLOAT_TO_INT,
    INT_TO_FLOAT,
    POINTERS,
    POINTER_TO_ADDRESS,
    ADDRESS_TO_POINTER,
    ENUM_TO_INT,
    INT_TO_ENUM,
};

struct IR_Instruction_Cast
{
    IR_Cast_Type type;
    IR_Data_Access* destination;
    IR_Data_Access* source;
};

struct IR_Instruction_Function_Address
{
    IR_Data_Access* destination;
    int function_slot_index;
};

struct IR_Instruction;

struct IR_Register
{
    Datatype* type;
    bool has_definition_instruction;
    Optional<String*> name; // If it's a variable
};

struct IR_Code_Block
{
    IR_Function* function;
    IR_Code_Block* parent_block; // May be null if function
    int parent_instruction_index;
    Dynamic_Array<IR_Register> registers;
    Dynamic_Array<IR_Instruction> instructions;
};

struct IR_Switch_Case
{
    int value;
    IR_Code_Block* block;
};

struct IR_Instruction_Switch
{
    IR_Data_Access* condition_access;
    Dynamic_Array<IR_Switch_Case> cases;
    IR_Code_Block* default_block;
};

struct IR_Instruction_Variable_Definition
{
    Symbol* symbol;
    IR_Data_Access* variable_access;
    Optional<IR_Data_Access*> initial_value;
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
    FUNCTION_ADDRESS,
    UNARY_OP,
    BINARY_OP,

    // Required for const variable initialization in C-Code
    // Note: not all registers have a variable definition instruction, as temporary register don't have Statements for these...
    VARIABLE_DEFINITION,
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
        IR_Instruction_Function_Address function_address;
        IR_Instruction_Unary_OP unary_op;
        IR_Instruction_Binary_OP binary_op;
        IR_Instruction_Variable_Definition variable_definition;
        IR_Code_Block* block;
        int label_index;
    } options;

    // Mapping from AST to ir-code
    AST::Statement* associated_statement;
    AST::Expression* associated_expr;
    Analysis_Pass* associated_pass;
};

struct IR_Program;
struct IR_Function
{
    IR_Program* program;
    Datatype_Function* function_type;
    IR_Code_Block* code;
    int function_slot_index; // Note: not + 1 
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
            IR_Data_Access* index_access;
            IR_Data_Access* iterable_access;
            IR_Data_Access* loop_variable_access;

            bool is_custom_iterator;
            // Only valid for custom iterators
            IR_Data_Access* iterator_access;
            ModTree_Function* next_function;
            int iterator_deref_value;
        } foreach_loop;
    } options;
};

struct Defer_Item
{
    bool is_block;
    union {
        AST::Code_Block* block;
        struct {
            IR_Data_Access* left_access;
            IR_Data_Access* restore_value;
        } defer_restore;
    } options;
};

struct IR_Instruction_Reference
{
    IR_Code_Block* block;
    int index;
};

struct IR_Generator
{
    IR_Program* program;
    ModTree_Program* modtree;
    IR_Function* default_allocate_function;
    IR_Function* default_free_function;
    IR_Function* default_reallocate_function;

    // Stuff needed for compilation
    Dynamic_Array<IR_Data_Access*> data_accesses;
    IR_Data_Access nothing_access;

    Hashtable<AST::Definition_Symbol*, IR_Data_Access*> variable_mapping;
    Hashtable<AST::Code_Block*, Loop_Increment> loop_increment_instructions; // For for loops

    Dynamic_Array<int> queued_function_slot_indices;

    Dynamic_Array<Defer_Item> defer_stack;
    Dynamic_Array<Unresolved_Goto> fill_out_continues;
    Dynamic_Array<Unresolved_Goto> fill_out_breaks;
    Dynamic_Array<IR_Instruction_Reference> label_positions;

    Hashtable<AST::Code_Block*, int> labels_break;
    Hashtable<AST::Code_Block*, int> labels_continue;
    Hashtable<AST::Code_Block*, int> block_defer_depths;

    Analysis_Pass* current_pass;
    AST::Expression* current_expr;
    AST::Statement* current_statement;
    IR_Code_Block* current_block;
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

void ir_program_append_to_string(IR_Program* program, String* string, bool print_generated_functions, Compiler_Analysis_Data* analysis_data);
void ir_instruction_append_to_string(IR_Instruction* instruction, String* string, int indentation, IR_Code_Block* code_block, Compiler_Analysis_Data* analysis_data);
IR_Binop ast_binop_to_ir_binop(AST::Binop binop);



