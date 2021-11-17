#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../datastructures/list.hpp"

#include "type_system.hpp"
#include "compiler_misc.hpp"

/*
    What can differentiate ModTree from IR_Code?
        Module Hierarchy:          Keep    vs. Flatten
        Resolve Identifiers to:    Symbols vs. Structures (Functions, Data_Accesses...)
        Variable Definitions:      Keep Position vs. in Block vs. In Function
        Expression Results:        As Tree       vs. Flatten (In Registers)
        Data Access:               Through expressions vs. Data_Access structure (Resolves expression evaluation order)

    Problem:
        Symbol tables are registered in templated context, since code_block does not know if templated

        Function call does not take expressions but rather identifiers, so we cannot call members that are function pointers

    What type does the tag have?
    Member access on enum type results in the enum tag
    x = Address.IPV4; // This should instanciate a value of type Address that has the tag set to ipv4
    x.tag == Address.IPV4; ???
*/

struct Type_Signature;
struct Symbol;
struct Compiler;
struct Symbol_Table;
struct ModTree_Block;
struct ModTree_Module;
struct ModTree_Function;
struct ModTree_Variable;
struct ModTree_Expression;
struct AST_Node;

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
    DEREFERENCE,
    TEMPORARY_TO_STACK, // Puts temporary values onto the stack, e.g. 5 is temporary
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
    ENUM_TO_INT,
    INT_TO_ENUM,
    ARRAY_SIZED_TO_UNSIZED, // Implicit
    TO_ANY,
    FROM_ANY,
};

struct Constant_Value
{
    Type_Signature* signature;
    void* data;
};

struct Member_Initializer
{
    Struct_Member member;
    ModTree_Expression* init_expr;
    AST_Node* init_node;
};

enum class ModTree_Expression_Type
{
    BINARY_OPERATION,
    UNARY_OPERATION,
    CONSTANT_READ,
    FUNCTION_CALL,
    VARIABLE_READ,
    FUNCTION_POINTER_READ,
    ARRAY_ACCESS,
    MEMBER_ACCESS,
    NEW_ALLOCATION,
    ARRAY_INITIALIZER,
    STRUCT_INITIALIZER,
    CAST,
};

struct ModTree_Expression
{
    ModTree_Expression_Type expression_type;
    Type_Signature* result_type;
    Constant_Value constant_value;
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
            ModTree_Expression* pointer_expression;
            ModTree_Function* function;
            Dynamic_Array<ModTree_Expression*> arguments;
        } function_call;
        ModTree_Variable* variable_read;
        ModTree_Function* function_pointer_read;
        Dynamic_Array<ModTree_Expression*> array_initializer;
        Dynamic_Array<Member_Initializer> struct_initializer;
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
    SWITCH,

    BREAK,
    CONTINUE,
    RETURN,
    EXIT,

    EXPRESSION,
    ASSIGNMENT,
    DELETION,
};

struct ModTree_Switch_Case
{
    int value;
    ModTree_Expression* expression;
    ModTree_Block* body;
};

struct ModTree_Statement
{
    ModTree_Statement_Type type;
    union
    {
        ModTree_Block* block;
        ModTree_Block* break_to_block;
        ModTree_Block* continue_to_block;
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
            ModTree_Expression* condition;
            Dynamic_Array<ModTree_Switch_Case> cases;
            ModTree_Block* default_block;
        } switch_statement;
        struct {
            ModTree_Expression* destination;
            ModTree_Expression* source;
        } assignment;
        struct {
            ModTree_Expression* expression;
            bool is_array;
        } deletion;
        ModTree_Expression* expression;
        Optional<ModTree_Expression*> return_value;
        Exit_Code exit_code;
    } options;
};

enum class Block_Type
{
    FUNCTION_BODY,
    ANONYMOUS_BLOCK,
    DEFER_BLOCK,
    IF_TRUE_BLOCK,
    IF_ELSE_BLOCK,
    WHILE_BODY,
    SWITCH_CASE,
    SWITCH_DEFAULT_CASE,
};

struct ModTree_Block;
union Block_Origin
{
    ModTree_Function* function;
    ModTree_Block* parent;
};

struct ModTree_Block
{
    Dynamic_Array<ModTree_Statement*> statements;
    Dynamic_Array<ModTree_Variable*> variables;
    Symbol_Table* symbol_table;
    Block_Type type;
    Block_Origin origin;
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
        ModTree_Module* parent_module;
        ModTree_Block* local_block;
        struct {
            ModTree_Function* function;
            int index;
        } parameter;
    } options;
};

struct ModTree_Variable_Value
{
    Constant_Value constant_value;
    ModTree_Block* last_write_block; // If null, variable was not initialized yet
    bool address_was_taken; // If the variable address was taken, we must not write make value valid again
};

struct ModTree_Variable
{
    Type_Signature* data_type;
    ModTree_Variable_Origin origin;
    Symbol* symbol; // May be null
    ModTree_Variable_Value value;
};

enum class ModTree_Function_Type
{
    FUNCTION,
    HARDCODED_FUNCTION,
    EXTERN_FUNCTION,
};

struct ModTree_Intern_Function
{
    ModTree_Block* body;
    Dynamic_Array<ModTree_Variable*> parameters;
    // Dependencies
    Dynamic_Array<ModTree_Function*> dependency_functions; // Pointer reads and function calls
    Dynamic_Array<ModTree_Variable*> dependency_globals;
    //Dynamic_Array<Type_Signature*> dependency_signatures;
};

struct ModTree_Function
{
    ModTree_Function_Type function_type;
    Type_Signature* signature;
    union
    {
        ModTree_Intern_Function function;
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
    Dynamic_Array<String*> parameter_names;
    Dynamic_Array<Symbol_Template_Instance*> instances;
};

enum class Usage_Type
{
    FUNCTION_CALL,
    FUNCTION_POINTER_READ,
    VARIABLE_READ,
    VARIABLE_WRITE,
    MEMBER_ACCESS,

    VARIABLE_TYPE,
    CAST_SOURCE_TYPE,
    CAST_DESTINATION_TYPE,
    FUNCTION_RETURN_TYPE,
    ALLOCATION_TYPE,
    EXTERN_FUNCTION_TYPE,
    MEMBER_TYPE,
    ARRAY_INITIALIZER_TYPE,
    STRUCT_INITIALIZER_TYPE,

    TEMPLATE_ARGUMENT,
    BAKE_TYPE,

    IGNORE_REFERENCE
};

struct Symbol_Reference
{
    Usage_Type type;
    AST_Node* reference_node;
};

struct Symbol
{
    Symbol_Type type;
    Symbol_Options options;

    // Infos
    String* id;
    Symbol_Table* symbol_table; // Is 0 if the symbol is a template instance
    AST_Node* definition_node;
    Dynamic_Array<Symbol_Reference> references;

    // Template Data
    Symbol_Template_Data template_data;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    ModTree_Module* module; // May be null
    Hashtable<String*, Symbol*> symbols;
};

struct Semantic_Analyser;
struct AST_Node;
Symbol_Table* symbol_table_create(Semantic_Analyser* analyser, Symbol_Table* parent, AST_Node* node);
void symbol_table_destroy(Symbol_Table* symbol_table);

void symbol_table_append_to_string(String* string, Symbol_Table* table, Semantic_Analyser* analyser, bool print_root);
Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool only_current_scope, Symbol_Reference reference, Semantic_Analyser* analyser);



/*
    WORKLOADS
*/
struct Analysis_Workload_Enum 
{
    Symbol_Table* symbol_table;
    Type_Signature* enum_type;
    int next_integer_value;
    AST_Node* current_node;
};

struct Analysis_Workload_Extern_Function {
    ModTree_Module* parent_module;
    AST_Node* node;
};

struct Analysis_Workload_Extern_Header {
    ModTree_Module* parent_module;
    AST_Node* node;
};

struct Analysis_Workload_Module_Analysis {
    AST_Node* root_node;
};

struct Analysis_Workload_Array_Size {
    Type_Signature* array_signature;
};

struct Analysis_Workload_Global
{
    ModTree_Module* parent_module;
    AST_Node* node;
};

enum class Block_Control_Flow
{
    NO_RETURN,
    RETURN,
    CONTINUE,
    BREAK
};

struct Block_Analysis
{
    ModTree_Block* block;
    AST_Node* block_node;
    AST_Node* current_statement_node;
    Block_Control_Flow block_flow;

    ModTree_Statement* last_analysed_statement;
    AST_Node* last_analysed_node;

    int defer_count_block_start;
};

struct Analysis_Workload_Code
{
    ModTree_Function* function;
    List<Block_Analysis> block_queue;
    Block_Analysis* active_block;
    Dynamic_Array<AST_Node*> defer_nodes;
    Hashtable<AST_Node*, ModTree_Statement*> active_switches;
};

struct Analysis_Workload_Struct_Body
{
    Type_Signature* struct_signature;
    Symbol_Table* symbol_table;
    AST_Node* current_member_node;
};

struct Analysis_Workload_Function_Header
{
    ModTree_Function* function;
    Symbol_Table* symbol_table;

    AST_Node* function_node;
    AST_Node* next_parameter_node;
};

enum class Analysis_Workload_Type
{
    FUNCTION_HEADER,
    CODE,
    STRUCT_BODY,
    ENUM_BODY,
    GLOBAL,
    ARRAY_SIZE,

    MODULE_ANALYSIS,
    EXTERN_FUNCTION_DECLARATION,
    EXTERN_HEADER_IMPORT
};

struct Analysis_Workload
{
    Analysis_Workload_Type type;
    union
    {
        Analysis_Workload_Struct_Body struct_body;
        Analysis_Workload_Code code_block;
        Analysis_Workload_Function_Header function_header;
        Analysis_Workload_Global global;
        Analysis_Workload_Array_Size array_size;
        Analysis_Workload_Module_Analysis module_analysis;
        Analysis_Workload_Extern_Header extern_header;
        Analysis_Workload_Extern_Function extern_function;
        Analysis_Workload_Enum enum_body;
    } options;
};

enum class Workload_Dependency_Type
{
    IDENTIFER_NOT_FOUND,
    TYPE_SIZE_UNKNOWN, // Either of Sized_Array, Enum or Struct
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
    AST_Node* node;
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
enum class Expression_Context_Type
{
    ARITHMETIC_OPERAND, // + - * / %, unary -, requires primitive type either int or float
    UNKNOWN,
    FUNCTION_CALL, // Requires some type of function
    TYPE_KNOWN,
    TYPE_EXPECTED,
    ARRAY,
    MEMBER_ACCESS, // Only valid on enums, structs, slices and arrays
    SWITCH_CONDITION, // Enums
};

struct Expression_Context
{
    Expression_Context_Type type;
    Type_Signature* signature;
};

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

enum class Expression_Result_Any_Type
{
    EXPRESSION,
    TYPE,
    FUNCTION,
    ERROR_OCCURED,
    DEPENDENCY
};

enum class Semantic_Error_Type
{
    TEMPLATE_ARGUMENTS_INVALID_COUNT,
    TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE,
    TEMPLATE_ARGUMENTS_REQUIRED,

    EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL, // Error_node = is identifier_node
    EXTERN_HEADER_PARSING_FAILED, // Error_node = EXTERN_HEADER_IMPORT

    EXPECTED_TYPE,
    EXPECTED_VALUE,
    EXPECTED_CALLABLE,

    INVALID_TYPE_VOID_USAGE,
    INVALID_TYPE_FUNCTION_CALL, // Expression
    INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER,
    INVALID_TYPE_ARGUMENT,
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
    INVALID_TYPE_BAKE_MUST_BE_PRIMITIVE,
    INVALID_TYPE_ENUM_VALUE,

    ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN,
    ENUM_VALUE_MUST_BE_UNIQUE,
    ENUM_MEMBER_NAME_MUST_BE_UNIQUE,
    ENUM_DOES_NOT_CONTAIN_THIS_MEMBER,

    SWITCH_REQUIRES_ENUM,
    SWITCH_CASES_MUST_BE_COMPTIME_KNOWN,
    SWITCH_MUST_HANDLE_ALL_CASES,
    SWITCH_MUST_NOT_BE_EMPTY,
    SWITCH_ONLY_ONE_DEFAULT_ALLOWED,
    SWITCH_CASE_TYPE_INVALID,
    SWITCH_CASE_MUST_BE_UNIQUE,

    SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH,
    SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER,
    SYMBOL_MODULE_INVALID,

    SYMBOL_TABLE_UNRESOLVED_SYMBOL,
    SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED,
    SYMBOL_TABLE_MODULE_ALREADY_DEFINED,

    FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH,
    AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED,
    AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT,
    AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED,

    EXPRESSION_INVALID_CAST,
    EXPRESSION_MEMBER_NOT_FOUND,
    EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT,
    EXPRESSION_BINARY_OP_TYPES_MUST_MATCH,
    EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL,

    BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS,
    BAKE_FUNCTION_DID_NOT_SUCCEED,

    EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE,
    TYPE_NOT_KNOWN_AT_COMPILE_TIME,
    EXPRESSION_IS_NOT_A_TYPE,

    BREAK_NOT_INSIDE_LOOP_OR_SWITCH,
    BREAK_LABLE_NOT_FOUND,
    CONTINUE_NOT_INSIDE_LOOP,
    CONTINUE_LABEL_NOT_FOUND,
    CONTINUE_REQUIRES_LOOP_BLOCK,
    LABEL_ALREADY_IN_USE,

    ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN,
    ARRAY_SIZE_MUST_BE_GREATER_ZERO,

    ARRAY_INITIALIZER_REQUIRES_TYPE_SYMBOL,
    ARRAY_INITIALIZER_INVALID_TYPE,
    ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE,

    STRUCT_INITIALIZER_REQUIRES_TYPE_SYMBOL,
    STRUCT_INITIALIZER_MEMBERS_MISSING,
    STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE,
    STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT,
    STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST,
    STRUCT_INITIALIZER_INVALID_MEMBER_TYPE,
    STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER,
    AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE,

    OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM,
    OTHERS_MEMBER_ACCESS_INVALID_ON_FUNCTION,
    OTHERS_STRUCT_MUST_CONTAIN_MEMBER,
    OTHERS_STRUCT_MEMBER_ALREADY_DEFINED,
    OTHERS_WHILE_ONLY_RUNS_ONCE,
    OTHERS_WHILE_ALWAYS_RETURNS,
    OTHERS_WHILE_NEVER_STOPS,
    OTHERS_STATEMENT_UNREACHABLE,
    OTHERS_DEFER_NO_RETURNS_ALLOWED,
    OTHERS_MISSING_RETURN_STATEMENT,
    OTHERS_UNFINISHED_WORKLOAD_FUNCTION_HEADER,
    OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK,
    OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE,
    OTHERS_MAIN_CANNOT_BE_TEMPLATED,
    OTHERS_CANNOT_TAKE_ADDRESS_OF_MAIN,
    OTHERS_MAIN_NOT_DEFINED,
    OTHERS_MAIN_UNEXPECTED_SIGNATURE,
    OTHERS_NO_CALLING_TO_MAIN,
    OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS,
    OTHERS_RETURN_EXPECTED_NO_VALUE,
    OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION,
    OTHERS_COULD_NOT_LOAD_FILE,

    MISSING_FEATURE_TEMPLATED_GLOBALS,
    MISSING_FEATURE_NESTED_TEMPLATED_MODULES,
    MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES,
    MISSING_FEATURE_EXTERN_GLOBAL_IMPORT,
    MISSING_FEATURE,
    MISSING_FEATURE_NESTED_DEFERS
};

struct Semantic_Error
{
    Semantic_Error_Type type;
    AST_Node* error_node;

    // Information
    struct {
        int expected;
        int given;
    } invalid_argument_count;

    String* id;
    Symbol* symbol;
    ModTree_Function* function;
    Exit_Code exit_code;
    Type_Signature* given_type;
    Type_Signature* expected_type;
    Type_Signature* function_type;
    Type_Signature* binary_op_left_type;
    Type_Signature* binary_op_right_type;
    Expression_Result_Any_Type expression_type;
};

struct Token_Range;
void semantic_error_append_to_string(Semantic_Analyser* analyser, Semantic_Error e, String* string);
void semantic_error_get_error_location(Semantic_Analyser* analyser, Semantic_Error error, Dynamic_Array<Token_Range>* locations);





/*
    ANALYSER
*/
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

struct Partial_Compile_Result
{
    Analysis_Result_Type type;
    Workload_Dependency dependency;
};

struct Bytecode_Execute_Result
{
    Analysis_Result_Type type;
    union
    {
        Constant_Value value;
        Workload_Dependency dependency;
    } options;
};

struct Expression_Location
{
    AST_Node* node;
    ModTree_Block* block;
};

union Cached_Expression
{
    ModTree_Function* lambda;
    ModTree_Function* bake_function;
    Type_Signature* type;
};

struct Compiler;
struct Semantic_Analyser
{
    // Result
    Dynamic_Array<Semantic_Error> errors;

    Dynamic_Array<Symbol_Table*> symbol_tables;
    Hashtable<AST_Node*, Symbol_Table*> ast_to_symbol_table;

    // ModTree stuff
    ModTree_Program* program;
    ModTree_Function* global_init_function;
    ModTree_Function* free_function;
    ModTree_Function* malloc_function;
    ModTree_Function* assert_function;
    ModTree_Variable* global_type_informations;

    // Temporary stuff needed for analysis
    Compiler* compiler;
    Symbol_Table* base_table;
    Hashset<String*> loaded_filenames;
    Stack_Allocator allocator_values;

    Analysis_Workload* current_workload;
    Hashtable<ModTree_Block*, Block_Control_Flow> finished_code_blocks;
    Hashtable<Expression_Location, Cached_Expression> cached_expressions;
    Dynamic_Array<Analysis_Workload> active_workloads;
    Dynamic_Array<Waiting_Workload> waiting_workload;

    Hashset<ModTree_Function*> visited_functions;

    String* id_size;
    String* id_data;
    String* id_main;
    String* id_tag;
    String* id_type_of;
    String* id_type_info;
};

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_execute_workloads(Semantic_Analyser* analyser);
void semantic_analyser_reset(Semantic_Analyser* analyser, Compiler* compiler);

struct AST_Parser;
void symbol_append_to_string(Symbol* symbol, String* string);
void hardcoded_function_type_append_to_string(String* string, Hardcoded_Function_Type hardcoded);
void exit_code_append_to_string(String* string, Exit_Code code);
