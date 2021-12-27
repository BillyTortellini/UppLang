#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp"
#include "type_system.hpp"
#include "lexer.hpp"

struct ModTree_Variable;
struct ModTree_Function;
struct ModTree_Hardcoded_Function;
struct ModTree_Extern_Function;
struct Modtree_Polymorphic_Function;
struct Type_Signature;
struct RC_Analyser;
struct AST_Node;
struct RC_Block;
struct Compiler;
struct Symbol;
struct Symbol_Table;
struct Symbol_Data;
struct Semantic_Analyser;
struct RC_Analysis_Item;
struct Analysis_Workload;
struct RC_Expression;
struct RC_Statement;
struct RC_Symbol_Read;


/*
RC CODE
*/
enum class RC_Binary_Operation_Type
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
    POINTER_EQUAL,
    POINTER_NOT_EQUAL,
};

enum class RC_Unary_Operation_Type
{
    NEGATE,
    LOGICAL_NOT,
};

struct RC_Enum_Member
{
    String* id;
    Optional<RC_Expression*> value_expression;
    AST_Node* node;
};

struct RC_Parameter
{
    String* param_id;
    RC_Expression* type_expression;
    AST_Node* param_node;
};

struct RC_Member_Initializer
{
    Optional<String*> member_id;
    RC_Expression* init_expression;
};

enum class RC_Expression_Type
{
    MODULE,
    ANALYSIS_ITEM,
    BAKE,
    SYMBOL_READ,
    ENUM,
    ARRAY_TYPE,
    SLICE_TYPE,
    FUNCTION_SIGNATURE,
    FUNCTION_CALL,
    BINARY_OPERATION,
    UNARY_OPERATION,
    LITERAL_READ,
    NEW_EXPR,
    ARRAY_ACCESS,
    ARRAY_INITIALIZER,
    STRUCT_INITIALIZER,
    AUTO_ENUM,
    MEMBER_ACCESS,
    CAST,
    CAST_RAW,
    TYPE_INFO,
    TYPE_OF,
    DEREFERENCE,
    POINTER,
};

enum class RC_Cast_Type
{
    PTR_TO_RAW,
    RAW_TO_PTR,
    TYPE_TO_TYPE,
    AUTO_CAST
};

struct RC_Expression
{
    RC_Expression_Type type;
    union
    {
        RC_Analysis_Item* analysis_item;
        Symbol_Table* module_table;
        struct {
            RC_Expression* type_expression;
            RC_Block* body;
        } bake;
        struct {
            Dynamic_Array<RC_Enum_Member> members;
        } enumeration;
        struct {
            Dynamic_Array<RC_Parameter> parameters;
            Optional<RC_Expression*> return_type_expression;
        } function_signature;
        struct {
            RC_Expression* call_expr;
            Dynamic_Array<RC_Expression*> arguments;
        } function_call;
        struct {
            RC_Expression* size_expression;
            RC_Expression* element_type_expression;
        } array_type;
        struct {
            RC_Expression* element_type_expression;
        } slice_type;
        RC_Symbol_Read* symbol_read;
        struct {
            RC_Binary_Operation_Type op_type;
            RC_Expression* left_operand;
            RC_Expression* right_operand;
        } binary_operation;
        struct {
            RC_Unary_Operation_Type op_type;
            RC_Expression* operand;
        } unary_expression;
        Token literal_read;
        struct {
            RC_Expression* array_expression;
            RC_Expression* index_expression;
        } array_access;
        struct {
            RC_Expression* type_expression;
            Optional<RC_Expression*> count_expression; // If its a new array
        } new_expression;
        String* auto_enum_member_id;
        struct {
            RC_Expression* expression;
            String* member_name;
        } member_access;
        struct {
            RC_Cast_Type type;
            RC_Expression* operand;
            RC_Expression* type_expression;
        } cast;
        RC_Expression* type_info_expression;
        RC_Expression* type_of_expression;
        RC_Expression* dereference_expression;
        RC_Expression* pointer_expression;
        struct {
            Optional<RC_Expression*> type_expression;
            Dynamic_Array<RC_Expression*> element_initializers;
        } array_initializer;
        struct {
            Optional<RC_Expression*> type_expression;
            Dynamic_Array<RC_Member_Initializer> member_initializers;
        } struct_initializer;
    } options;
};

struct RC_Switch_Case
{
    Optional<RC_Expression*> expression; // If false, this is the default case
    RC_Block* body;
};

enum class RC_Statement_Type
{
    VARIABLE_DEFINITION,
    STATEMENT_BLOCK,
    ASSIGNMENT_STATEMENT,
    DEFER,
    IF_STATEMENT,
    WHILE_STATEMENT,
    SWITCH_STATEMENT,
    BREAK_STATEMENT,
    CONTINUE_STATEMENT,
    RETURN_STATEMENT,
    EXPRESSION_STATEMENT,
    DELETE_STATEMENT,
};

struct RC_Statement
{
    RC_Statement_Type type;
    union
    {
        struct {
            Symbol* symbol;
            Optional<RC_Expression*> type_expression;
            Optional<RC_Expression*> value_expression;
        } variable_definition;
        RC_Block* statement_block;
        struct {
            RC_Expression* left_expression;
            RC_Expression* right_expression;
        } assignment;
        RC_Block* defer_block;
        struct {
            RC_Expression* condition;
            RC_Block* true_block;
            Optional<RC_Block*> false_block;
        } if_statement;
        struct {
            RC_Expression* condition;
            RC_Block* body;
        } while_statement;
        struct {
            RC_Expression* condition;
            Dynamic_Array<RC_Switch_Case> cases;
        } switch_statement;
        String* break_id;
        String* continue_id;
        Optional<RC_Expression*> return_statement;
        RC_Expression* expression_statement;
        RC_Expression* delete_expression;
    } options;
};

enum class RC_Block_Type
{
    FUNCTION_BODY,
    BAKE_BLOCK,
    DEFER_BLOCK,
    IF_TRUE_BLOCK,
    IF_ELSE_BLOCK,
    WHILE_BODY,
    SWITCH_CASE,
    SWITCH_DEFAULT,
    ANONYMOUS_BLOCK_CASE,
};

struct RC_Block
{
    RC_Block_Type type;
    String* block_id;
    Symbol_Table* symbol_table;
    Dynamic_Array<RC_Statement*> statements;
};



/*
    Symbol Tables
*/
enum class Symbol_Type
{
    UNRESOLVED,
    VARIABLE_UNDEFINED,

    HARDCODED_FUNCTION,
    EXTERN_FUNCTION,
    FUNCTION,
    TYPE,
    CONSTANT_VALUE,
    VARIABLE,
    MODULE,
    SYMBOL_ALIAS,
    ERROR_SYMBOL,
};

struct Symbol
{
    Symbol_Type type;
    union
    {
        ModTree_Variable* variable;
        Symbol_Table* module_table;
        ModTree_Function* function;
        ModTree_Hardcoded_Function* hardcoded_function;
        ModTree_Extern_Function* extern_function;
        Type_Signature* type;
        Upp_Constant constant;
        Symbol* alias;
    } options;

    String* id;
    Symbol_Table* origin_table;
    AST_Node* definition_node;
    RC_Analysis_Item* origin_item;
    Dynamic_Array<RC_Symbol_Read*> references;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    Hashtable<String*, Symbol*> symbols;
};

Symbol_Table* symbol_table_create(RC_Analyser* analyser, Symbol_Table* parent, AST_Node* definition_node);
void symbol_table_destroy(Symbol_Table* symbol_table);
void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);
Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool only_current_scope, RC_Symbol_Read* reference);

struct Symbol_Error
{
    Symbol* existing_symbol;
    AST_Node* error_node;
};

struct Predefined_Symbols
{
    Symbol* type_bool;
    Symbol* type_int;
    Symbol* type_float;
    Symbol* type_u8;
    Symbol* type_u16;
    Symbol* type_u32;
    Symbol* type_u64;
    Symbol* type_i8;
    Symbol* type_i16;
    Symbol* type_i32;
    Symbol* type_i64;
    Symbol* type_f32;
    Symbol* type_f64;
    Symbol* type_byte;
    Symbol* type_void;
    Symbol* type_string;
    Symbol* type_type;
    Symbol* type_type_information;
    Symbol* type_any;
    Symbol* type_empty;

    Symbol* hardcoded_print_bool;
    Symbol* hardcoded_print_i32;
    Symbol* hardcoded_print_f32;
    Symbol* hardcoded_print_string;
    Symbol* hardcoded_print_line;
    Symbol* hardcoded_read_i32;
    Symbol* hardcoded_read_f32;
    Symbol* hardcoded_read_bool;
    Symbol* hardcoded_random_i32;

    Symbol* function_assert;
    Symbol* global_type_informations;

    Symbol* error_symbol;
};




/*
ANALYSER
*/

enum class RC_Dependency_Type
{
    NORMAL,
    BAKE,
    MEMBER_IN_MEMORY,
    MEMBER_REFERENCE,
};

struct RC_Symbol_Read
{
    RC_Dependency_Type type;
    AST_Node* identifier_node;
    Symbol_Table* symbol_table;
    Symbol* symbol;
    RC_Analysis_Item* item;
};

struct RC_Item_Dependency
{
    RC_Analysis_Item* item;
    RC_Dependency_Type type;
};

struct RC_Struct_Member
{
    String* id;
    RC_Expression* type_expression;
};

enum class RC_Analysis_Item_Type
{
    ROOT,
    DEFINITION,
    STRUCTURE,
    FUNCTION,
    FUNCTION_BODY,
};

struct RC_Analysis_Item
{
    RC_Analysis_Item_Type type;
    Dynamic_Array<RC_Item_Dependency> item_dependencies;
    Dynamic_Array<RC_Symbol_Read*> symbol_dependencies;
    union 
    {
        struct 
        {
            Symbol* symbol;
            bool is_comptime_definition; 
            Optional<RC_Expression*> value_expression;
            Optional<RC_Expression*> type_expression;
        } definition;
        struct {
            Symbol* symbol; // Could be null
            Structure_Type structure_type;
            Dynamic_Array<RC_Struct_Member> members;
        } structure;
        struct {
            Symbol* symbol; // Could be null
            Dynamic_Array<Symbol*> parameter_symbols;
            RC_Expression* signature_expression;
            RC_Analysis_Item* body_item; // Needs to be separate from function, since body has other dependencies
        } function;
        RC_Block* function_body;
    } options;
};

struct RC_Analyser
{
    // Output
    RC_Analysis_Item* root_item;
    Dynamic_Array<Symbol_Error> errors;
    Symbol_Table* root_symbol_table;
    Predefined_Symbols predefined_symbols;

    Hashtable<AST_Node*, Symbol_Table*> mapping_ast_to_symbol_table;
    Hashtable<RC_Expression*, AST_Node*> mapping_expressions_to_ast;
    Hashtable<RC_Statement*, AST_Node*> mapping_statements_to_ast;

    // Used during analysis
    Compiler* compiler;
    Symbol_Table* symbol_table;
    RC_Analysis_Item* analysis_item;
    RC_Dependency_Type dependency_type;
    bool inside_bake;

    // Allocations, TODO: Use actual allocators
    Dynamic_Array<RC_Expression*> allocated_expressions;
    Dynamic_Array<RC_Block*> allocated_blocks;
    Dynamic_Array<Symbol_Table*> allocated_symbol_tables;
    Dynamic_Array<RC_Statement*> allocated_statements;
};

RC_Analyser rc_analyser_create();
void rc_analyser_destroy(RC_Analyser* analyser);
void rc_analyser_reset(RC_Analyser* analyser, Compiler* compiler);
void rc_analyser_analyse(RC_Analyser* analyser, AST_Node* root_node);
void rc_analyser_log_error(RC_Analyser* analyser, Symbol* existing_symbol, AST_Node* error_node);
void rc_analysis_item_append_to_string(RC_Analysis_Item* item, String* string, int indentation);
