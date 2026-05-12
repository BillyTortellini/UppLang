#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/allocators.hpp"
#include "compiler_misc.hpp" // Upp_Constant
#include "constant_pool.hpp"
#include "ast.hpp"
#include "type_system.hpp"

struct Compilation_Data;
struct Semantic_Context;
struct Upp_Global;
struct Upp_Function;
struct Datatype;
struct Workload_Global;
struct Workload_Import_Resolve;
struct Workload_Structure_Header;
struct Workload_Custom_Operators;
struct Custom_Operator;

struct Symbol;
struct Symbol_Table;
struct Symbol_Data;
struct Datatype_Pattern_Variable;
struct Workload_Base;
struct Upp_Module;

struct Poly_Function;
struct Function_Parameter;
struct Poly_Header;
struct Pattern_Variable;

namespace AST
{
    struct Definition_Custom_Operator;
    struct Node;
    struct Symbol_Node;
}


// CUSTOM_OPERATORS

enum class Custom_Operator_Query_Node_Type
{
    OPERATOR_TYPE,
    NORMAL_DATATYPE,
    POLYMORPHIC_DATATYPE_TYPE,
    POLYMORPHIC_STRUCT_BASE,
    PATTERN_VARIABLE,
    CUSTOM_OPERATOR
};

struct Custom_Operator_Query_Node
{
    Custom_Operator_Query_Node_Type type;
    int parent_index; // -1 if root
    union
    {
        Custom_Operator_Type op_type;
        Datatype* datatype;
        Datatype_Type poly_datatype_type;
        Poly_Header* poly_struct_base;
        int custom_operator_child_index;
    } options;
};

struct Custom_Operator_Query_Node_Value
{
    int index; // Either node-index for further querying, or custom-operator index 
    u32 has_child_query_node_type_mask;
};

// In instances the types are pointers if it's by ref
struct Custom_Operator_Instance_Key
{
    Custom_Operator_Type type;
    Datatype* datatypes[2];
    Upp_Function* functions[2]; // Custom_Iterator has create and next function, all other's have 1 function
};

struct Custom_Operator_Instance_Value
{
    Custom_Operator* custom_op;
    Upp_Function* instance_functions[2];
};

struct Custom_Operator_Query
{
	Custom_Operator_Type operator_type;
    bool argument_is_temporary[2];
    Datatype* argument_datatypes[2];
};

enum class Custom_Operator_Query_Result_Type
{
    SUCCESS,
    FOUND_BUT_FUNCTION_INVALID, // If function does not match the types given in the operator node
    VALUE_MUST_NOT_BE_TEMPORARY,
    NOT_FOUND,
};

struct Custom_Operator_Query_Result
{
    Custom_Operator_Query_Result_Type type;
    Custom_Operator_Instance_Value value;
};

// Note: If the function is nullptr, then the function analysis contained errors
// Note: Commutative binops get inserted twice
struct Custom_Operator_Parameter
{
    Datatype* datatype; // if nullptr, then parameter is not used
    int parameter_index; // -1 if return type
    bool by_reference;
    bool is_return_type;
};

struct Custom_Operator
{
    Custom_Operator_Type type;
    AST::Definition_Custom_Operator* node;
    Custom_Operator_Parameter parameters[2];
    Upp_Function* functions[2];
    bool result_by_reference;
};

Custom_Operator_Query custom_operator_query_make(
    Custom_Operator_Type op_type, Datatype* datatype_0, bool arg_0_is_temporary, Datatype* datatype_1 = nullptr, bool arg_1_is_temporary = false
);

u64 hash_custom_operator_instance_key(Custom_Operator_Instance_Key* op);
bool equals_custom_operator_instance_key(Custom_Operator_Instance_Key* op_a, Custom_Operator_Instance_Key* op_b);
u64 hash_custom_operator_query_node(Custom_Operator_Query_Node* node);
bool equals_custom_operator_query_node(Custom_Operator_Query_Node* node_a, Custom_Operator_Query_Node* node_b);


// SYMBOLS

// Note: Both symbols and symbol-table includes have access levels
enum class Symbol_Type
{
    WAITING_FOR_WORKLOAD, // A Definition that isn't ready yet (global, const, extern-import or enum)
    VARIABLE_UNDEFINED,   // A variable that is defined in this scope but has not been reached yet
    ALIAS_UNFINISHED,     // An import that isn't finished yet

    HARDCODED_FUNCTION,
    FUNCTION,
    POLYMORPHIC_FUNCTION,

    VARIABLE,
    GLOBAL,
    PARAMETER,

    DATATYPE,
    PATTERN_VARIABLE, // Either comptime parameter or pattern value
    COMPTIME_VALUE,
    ALIAS, // Alias created by import, e.g. import Algorithms~bubble_sort as sort
    MODULE,
    ERROR_SYMBOL, 
};

struct Symbol
{
    Symbol_Type type;
    union
    {
        Datatype* variable_type;
        Upp_Function* function;
        Poly_Function poly_function;
        Workload_Base* waiting_for_workload;
        Symbol* alias_for;
        int unfinished_alias_index;
        Hardcoded_Type hardcoded;
        Datatype* datatype;
        Upp_Global* global;
        Upp_Module* upp_module;
        struct {
            Upp_Function* function;
            int index;
        } parameter;
        Datatype_Pattern_Variable* pattern_variable_type;
        Upp_Constant constant;
    } options;

    String* id;
    Symbol_Table* origin_table;
    Symbol_Access_Level access_level;
    Dynamic_Array<AST::Symbol_Node*> references;

    AST::Symbol_Node* definition_node;
};



// SYMBOL TABLE
struct Symbol_Table_Import
{
    Symbol_Table* table;
    Import_Type type;
    Symbol_Access_Level access_level;
    bool is_transitive;
};

struct Symbol_Table
{
    Hashtable<String*, Dynamic_Array<Symbol*>> symbols;

    // Connections to other symbol tables
    Symbol_Table* parent_table;
    Symbol_Access_Level parent_access_level;
    Dynamic_Array<Symbol_Table_Import> imports;

    // Custom operators
    Workload_Custom_Operators* custom_operators_workload;
    DynTable<Custom_Operator_Query_Node, Custom_Operator_Query_Node_Value> custom_operator_query_table;
    DynArray<Custom_Operator> custom_operators;
    int next_query_node_index;
};

struct Reachable_Table
{
	Symbol_Table* table;
	Symbol_Access_Level access_level;
	int depth; // How many includes were traversed to find this query-table
	bool search_imports;
	bool search_parents;
};

struct Symbol_Query_Info
{
    Symbol_Access_Level access_level;
    Import_Type import_search_type;
    bool search_parents;
};

struct Symbol_Error
{
    Symbol* existing_symbol;
    AST::Node* error_node;
};

Symbol_Table* symbol_table_create(Compilation_Data* compilation_data);
Symbol_Table* symbol_table_create_with_parent(Symbol_Table* parent_table, Symbol_Access_Level parent_access_level, Compilation_Data* compilation_data);
void symbol_table_destroy(Symbol_Table* symbol_table);
void symbol_destroy(Symbol* symbol);

Symbol* symbol_table_define_symbol(
    Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Symbol_Node* definition_node, Symbol_Access_Level access_level,
    Compilation_Data* compilation_data
);
void symbol_table_add_import(
    Symbol_Table* symbol_table, Symbol_Table* imported_table, Import_Type import_type, 
    bool is_transitive, Symbol_Access_Level access_level, Semantic_Context* semantic_context,
    AST::Node* error_report_node, Node_Section error_report_section
);

Symbol_Query_Info symbol_query_info_make(
    Symbol_Access_Level access_level, Import_Type import_search_type, bool search_parents
);
DynArray<Reachable_Table> symbol_table_query_all_reachable_tables(Symbol_Table* symbol_table, Symbol_Query_Info query_info, Arena* arena);
DynArray<Symbol*> symbol_table_query_id(Symbol_Table* symbol_table, String* id, Symbol_Query_Info query_info, Arena* arena);
DynArray<Symbol*> symbol_table_query_all_symbols(Symbol_Table* symbol_table, Symbol_Query_Info query_info, Arena* arena);
void symbol_table_query_resolve_aliases(DynArray<Symbol*>& symbols);

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);
void symbol_type_append_to_string(Symbol_Type type, String* string);

