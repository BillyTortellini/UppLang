#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

#include "type_system.hpp"
#include "compiler_misc.hpp"

/*
    As a learning experience, I am going to generate ir-code from the built modtree now

    1. Build modtree while analysing
    2. Remove IR Code generation (Maybe move to other file for the time being?)
    3. Generate IR Code from Modtree
    4. Continue working on compile-time code execution/templates
    5. Macros

    What can differentiate ModTree from IR_Code?
        Module Hierarchy:          Keep    vs. Flatten
        Resolve Identifiers to:    Symbols vs. Structures (Functions, Data_Accesses...)
        Variable Definitions:      Keep Position vs. in Block vs. In Function
        Expression Results:        As Tree       vs. Flatten (In Registers)
        Data Access:               Through expressions vs. Data_Access structure

    Problem:
        Symbol tables are registered in templated context, since code_block does not know if templated

        2 Symbol tables for template instancing is probably bad, because for structures 
        the type-names of the members must always refer to the same types, even when instancing,
        so I probably have to undo that at some point

        Function call does not take expressions but rather identifiers, so we cannot call members that are funciton pointers

    ModTree Problems:
        Expressions cannot resolve sized_array to array cast to just statements
        Function pointers are kinda shitty

    TODO: Things that expect functions to be availabe need to check if signature != 0
*/

struct Type_Signature;
struct Symbol;
struct Compiler;
struct Symbol_Table;
struct ModTree_Block;
struct ModTree_Module;
struct ModTree_Function;
struct ModTree_Variable;

/*
    MODTREE
*/
enum class ModTree_Binary_Operation_Type
{
    ADDITION,
    SUBTRACTION,
    DIVISION,
    MULTIPLICATION,
    MODULO,

    AND,
    OR,

    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_OR_EQUAL,
    GREATER,
    GREATER_OR_EQUAL,
};

enum class ModTree_Unary_Operation_Type
{
    NEGATE,
    LOGICAL_NOT,
    ADDRESS_OF,
    DEREFERENCE
};

enum class ModTree_Cast_Type
{
    INTEGERS, // Implicit to bigger
    FLOATS, // Implicit to bigger
    FLOAT_TO_INT,
    INT_TO_FLOAT, // Implicit
    POINTERS, // Implicit from/to void*
    POINTER_TO_U64,
    U64_TO_POINTER,
    ARRAY_SIZED_TO_UNSIZED // Implicit
};

enum class ModTree_Expression_Type
{
    BINARY_OPERATION,
    UNARY_OPERATION,
    LITERAL_READ,
    FUNCTION_CALL,
    VARIABLE_READ,
    FUNCTION_POINTER_READ,
    ARRAY_ACCESS,
    MEMBER_ACCESS,
    NEW_ALLOCATION,
    CAST,
};

struct ModTree_Expression
{
    ModTree_Expression_Type expression_type;
    Type_Signature* result_type;
    bool has_memory_address;
    void* value;
    union 
    {
        struct 
        {
            ModTree_Binary_Operation_Type operation_type;
            ModTree_Expression* left_operand;
            ModTree_Expression* right_operand;
        } binary_operation;
        struct 
        {
            ModTree_Unary_Operation_Type operation_type;
            ModTree_Expression* operand;
        } unary_operation;
        struct {
            int constant_index;
        } literal_read;
        struct {
            bool is_pointer_call;
            ModTree_Variable* pointer_variable;
            ModTree_Function* function;
            Dynamic_Array<ModTree_Expression*> arguments;
        } function_call;
        ModTree_Variable* variable_read;
        ModTree_Function* function_pointer_read;
        struct {
            ModTree_Expression* array_expression;
            ModTree_Expression* index_expression;
        } array_access;
        struct {
            ModTree_Expression* structure_expression;
            Struct_Member member;
        } member_access;
        struct {
            ModTree_Expression* cast_argument;
            ModTree_Cast_Type type;
        } cast;
        struct {
            int allocation_size;
            Optional<ModTree_Expression*> element_count; // If true this is an array allocation
        } new_allocation;
    } options;
};

enum class ModTree_Statement_Type
{
    BLOCK,
    IF, 
    WHILE, 

    BREAK,
    CONTINUE,
    RETURN,
    EXIT,

    EXPRESSION,
    ASSIGNMENT,
    DELETION,
};

struct ModTree_Statement
{
    ModTree_Statement_Type type;
    union
    {
        ModTree_Block* block;
        struct {
            ModTree_Expression* condition;
            ModTree_Block* if_block;
            ModTree_Block* else_block;
        } if_statement;
        struct {
            ModTree_Expression* condition;
            ModTree_Block* while_block;
        } while_statement;
        struct {
            ModTree_Expression* destination;
            ModTree_Expression* source;
        } assignment;
        struct {
            ModTree_Expression* expression;
            bool is_array;
        } deletion;
        ModTree_Expression* expression;
        ModTree_Variable* variable_definition;
        Optional<ModTree_Expression*> return_value;
        Exit_Code exit_code;
    } options;
};

struct ModTree_Block
{
    Dynamic_Array<ModTree_Statement*> statements;
    Dynamic_Array<ModTree_Variable*> variables;
    Symbol_Table* symbol_table;
    ModTree_Function* function;
};

enum class ModTree_Variable_Origin_Type
{
    GLOBAL,
    LOCAL,
    PARAMETER
};

struct ModTree_Variable_Origin
{
    ModTree_Variable_Origin_Type type;
    union
    {
        ModTree_Module* definition_module;
        ModTree_Block* local_block;
        struct {
            ModTree_Function* function;
            int index;
        } parameter;
    } options;
};

struct ModTree_Variable
{
    Type_Signature* data_type;
    ModTree_Variable_Origin origin;
    Symbol* symbol; // May be null
};

enum class ModTree_Function_Type
{
    FUNCTION,
    HARDCODED_FUNCTION,
    EXTERN_FUNCTION,
};

struct ModTree_Function
{
    ModTree_Function_Type function_type;
    Type_Signature* signature;
    union
    {
        struct {
            ModTree_Block* body;
            Dynamic_Array<ModTree_Variable*> parameters;
        } function;
        Hardcoded_Function_Type hardcoded_type;
        Extern_Function_Identifier extern_function;
    } options;
    ModTree_Module* parent_module;
    Symbol* symbol; // May be null
};

struct ModTree_Module
{
    Symbol_Table* symbol_table;
    Dynamic_Array<ModTree_Function*> functions;
    Dynamic_Array<ModTree_Variable*> globals;
    Dynamic_Array<ModTree_Module*> modules;
    Symbol* symbol; // May be null (Root module)
    ModTree_Module* parent_module; // May be null
};

struct ModTree_Program
{
    ModTree_Module* root_module;
    ModTree_Function* entry_function;
};





/*
    SYMBOL TABLE
*/
enum class Symbol_Type
{
    FUNCTION,
    TYPE, // Structs or others (Not implemented yet, would be Enums, Tagged Enums, Unions...)
    VARIABLE, // Either local, global or parameter
    MODULE,
};

struct Symbol_Table;
union Symbol_Options
{
    ModTree_Variable* variable;
    ModTree_Module* module;
    ModTree_Function* function;
    Type_Signature* type; // Structs, future: Enums, Unions
};

struct Symbol;
struct Symbol_Table;
struct Symbol_Template_Instance
{
    Dynamic_Array<Type_Signature*> arguments;
    Symbol_Table* template_symbol_table;
    Symbol* instance_symbol;
};

struct Symbol_Template_Data
{
    bool is_templated;
    Dynamic_Array<int> parameter_names;
    Dynamic_Array<Symbol_Template_Instance*> instances;
};

struct Symbol
{
    Symbol_Type type;
    Symbol_Options options;

    // Infos
    int name_handle;
    Symbol_Table* symbol_table; // Is 0 if the symbol is a template instance
    int definition_node_index;

    // Template Data
    Symbol_Template_Data template_data;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    Hashtable<int, Symbol*> symbols;
};

struct Semantic_Analyser;
Symbol_Table* symbol_table_create(Semantic_Analyser* analyser, Symbol_Table* parent, int node_index, bool inside_defer);
void symbol_table_destroy(Symbol_Table* symbol_table);

Symbol* symbol_table_find_symbol(Symbol_Table* table, int name_handle, bool only_current_scope);
void symbol_table_append_to_string(String* string, Symbol_Table* table, Semantic_Analyser* analyser, bool print_root);
struct Identifier_Pool;
Symbol* symbol_table_find_symbol_by_string(Symbol_Table* table, String* string, Identifier_Pool* pool);





/*
    WORKLOADS
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
    ModTree_Block* block;
    int current_child_index;
    // Defer infos
    bool inside_defer;
    int local_block_defer_depth;
    int surrounding_loop_defer_depth;
    Dynamic_Array<int> active_defer_statements;
    // Context infos
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
};

struct Analysis_Workload_Function_Header
{
    ModTree_Function* function;
    int next_parameter_index;
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
        ModTree_Module* definition_module;
    } options;
};

enum class Workload_Dependency_Type
{
    IDENTIFER_NOT_FOUND,
    TYPE_SIZE_UNKNOWN, // Either of Sized_Array or Struct
    CODE_BLOCK_NOT_FINISHED,
    FUNCTION_HEADER_NOT_ANALYSED, // ModTree_Function* signature is 0
};

struct Workload_Dependency_Identifier_Not_Found
{
    Symbol_Table* symbol_table;
    bool current_scope_only;
};

struct Workload_Dependency
{
    Workload_Dependency_Type type;
    int node_index;
    union {
        Type_Signature* type_signature;
        ModTree_Block* code_block;
        Workload_Dependency_Identifier_Not_Found identifier_not_found;
        ModTree_Function* function_header_not_analysed;
    } options;
};

struct Waiting_Workload
{
    Analysis_Workload workload;
    Workload_Dependency dependency;
};






/*
ERRORS
*/

enum class Expected_Type_Classes
{
    PRIMITIVE,
    INTEGERS,
    FLOATS,
    POINTERS,
    ARRAYS,
    FUNCTION_POINTER,
    SPECIFIC_TYPE,
};

enum class Semantic_Error_Type
{
    TEMPLATE_ARGUMENTS_INVALID_COUNT,
    TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE,
    TEMPLATE_ARGUMENTS_REQUIRED,

    EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL, // Error_node = is identifier_node
    EXTERN_HEADER_PARSING_FAILED, // Error_node = EXTERN_HEADER_IMPORT

    INVALID_TYPE_VOID_USAGE,
    INVALID_TYPE_FUNCTION_CALL_EXPECTED_FUNCTION_POINTER, // Expression
    INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER,
    INVALID_TYPE_ARGUMENT_TYPE_MISMATCH,
    INVALID_TYPE_ARRAY_ACCESS, // x: int; x[5];
    INVALID_TYPE_ARRAY_ACCESS_INDEX, // x: int; x[5];
    INVALID_TYPE_ARRAY_ALLOCATION_SIZE, // new [false]int;
    INVALID_TYPE_ARRAY_SIZE, // x: [bool]int;
    INVALID_TYPE_ON_MEMBER_ACCESS,
    INVALID_TYPE_IF_CONDITION,
    INVALID_TYPE_WHILE_CONDITION,
    INVALID_TYPE_UNARY_OPERATOR,
    INVALID_TYPE_BINARY_OPERATOR,
    INVALID_TYPE_ASSIGNMENT,
    INVALID_TYPE_RETURN,
    INVALID_TYPE_DELETE,

    SYMBOL_EXPECTED_FUNCTION_OR_VARIABLE_ON_FUNCTION_CALL,
    SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER,
    SYMBOL_EXPECTED_VARIABLE_OR_FUNCTION_ON_VARIABLE_READ,

    SYMBOL_TABLE_UNRESOLVED_SYMBOL,
    SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED,
    SYMBOL_TABLE_MODULE_ALREADY_DEFINED,

    FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH,

    EXPRESSION_INVALID_CAST,
    EXPRESSION_MEMBER_NOT_FOUND,
    EXPRESSION_ADDRESS_OF_REQUIRES_MEMORY_ADDRESS,
    EXPRESSION_BINARY_OP_TYPES_MUST_MATCH,
    EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL,

    OTHERS_STRUCT_MUST_CONTAIN_MEMBER,
    OTHERS_STRUCT_MEMBER_ALREADY_DEFINED,
    OTHERS_WHILE_ONLY_RUNS_ONCE,
    OTHERS_WHILE_ALWAYS_RETURNS,
    OTHERS_WHILE_NEVER_STOPS,
    OTHERS_STATEMENT_UNREACHABLE,
    OTHERS_DEFER_NO_RETURNS_ALLOWED,
    OTHERS_BREAK_NOT_INSIDE_LOOP,
    OTHERS_CONTINUE_NOT_INSIDE_LOOP,
    OTHERS_MISSING_RETURN_STATEMENT,
    OTHERS_UNFINISHED_WORKLOAD_FUNCTION_HEADER,
    OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK,
    OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE,
    OTHERS_MAIN_CANNOT_BE_TEMPLATED,
    OTHERS_MAIN_NOT_DEFINED,
    OTHERS_MAIN_UNEXPECTED_SIGNATURE,
    OTHERS_NO_CALLING_TO_MAIN,
    OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS,
    OTHERS_RETURN_EXPECTED_NO_VALUE,
    OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION,

    MISSING_FEATURE_TEMPLATED_GLOBALS,
    MISSING_FEATURE_NON_INTEGER_ARRAY_SIZE_EVALUATION,
    MISSING_FEATURE_NESTED_TEMPLATED_MODULES,
    MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES,
    MISSING_FEATURE_EXTERN_GLOBAL_IMPORT,
    MISSING_FEATURE_NESTED_DEFERS
};

struct Semantic_Error
{
    Semantic_Error_Type type;
    int error_node_index;

    // Information
    struct {
        int expected;
        int given;
    } invalid_argument_count;

    int name_id;
    Symbol* symbol;
    Type_Signature* given_type;
    Type_Signature* expected_type;
    Type_Signature* function_type;
    Type_Signature* binary_op_left_type;
    Type_Signature* binary_op_right_type;
};

struct Token_Range;
void semantic_error_append_to_string(Semantic_Analyser* analyser, Semantic_Error e, String* string);
void semantic_error_get_error_location(Semantic_Analyser* analyser, Semantic_Error error, Dynamic_Array<Token_Range>* locations);





/*
    ANALYSER
*/
enum class Statement_Analysis_Result
{
    NO_RETURN,
    RETURN,
    CONTINUE,
    BREAK
};

enum class Analysis_Result_Type
{
    SUCCESS,
    ERROR_OCCURED,
    DEPENDENCY
};

struct Identifier_Analysis_Result
{
    Analysis_Result_Type type;
    union
    {
        Symbol* symbol;
        Workload_Dependency dependency;
    } options;
};



struct Compiler;
struct Semantic_Analyser
{
    // Result
    Dynamic_Array<Semantic_Error> errors;

    Dynamic_Array<Symbol_Table*> symbol_tables;
    Hashtable<int, Symbol_Table*> ast_to_symbol_table;

    // ModTree stuff
    ModTree_Program* program;
    ModTree_Function* global_init_function;
    ModTree_Function* free_function;
    ModTree_Function* malloc_function;

    // Temporary stuff needed for analysis
    Compiler* compiler;

    //IR_Function* global_init_function;
    Hashtable<ModTree_Block*, Statement_Analysis_Result> finished_code_blocks;
    Dynamic_Array<Analysis_Workload> active_workloads;
    Dynamic_Array<Waiting_Workload> waiting_workload;
    Dynamic_Array<void*> known_expression_values;

    int token_index_size;
    int token_index_data;
    int token_index_main;
};

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_analyse(Semantic_Analyser* analyser, Compiler* compiler);

struct AST_Parser;
void symbol_append_to_string(Symbol* symbol, String* string, Semantic_Analyser* analyser);
void hardcoded_function_type_append_to_string(String* string, Hardcoded_Function_Type hardcoded);
void exit_code_append_to_string(String* string, Exit_Code code);
