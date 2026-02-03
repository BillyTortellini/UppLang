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
struct ModTree_Global;
struct ModTree_Function;
struct Datatype;
struct Workload_Definition;
struct Workload_Import_Resolve;
struct Workload_Structure_Polymorphic;

struct Symbol;
struct Symbol_Table;
struct Symbol_Data;
struct Datatype_Pattern_Variable;
struct Workload_Base;
struct Upp_Module;

struct Function_Progress;
struct Poly_Function;
struct Function_Parameter;
struct Poly_Header;
struct Pattern_Variable;
struct Custom_Operator_Table;

namespace AST
{
    struct Symbol_Lookup;
    struct Node;
    struct Import;
}


// CUSTOM_OPERATORS
/*
    Notes on Custom_Operator_Key:
    Operator_Context stores multiple ways to reach a single Custom-Operator
    The key-types are always stored as the base_type (E.g. no pointer/optional_pointer/subtype).
    After querying the analyse has to make sure that the type-mods are correct
*/
struct Custom_Operator_Key
{
    Custom_Operator_Type type;
    union
    {
        struct {
            Datatype* from_type; // Stored as base-type
            Datatype* to_type;   // Stored as base-type
        } custom_cast;
        AST::Binop binop;
        AST::Unop unop;
    } options;
};

struct Custom_Operator
{
    Custom_Operator_Type type;
    union
    {
        struct
        {
            Function_Progress* function;
            bool call_by_reference;
            bool return_by_reference;
            bool auto_cast;
        } custom_cast;
        struct
        {
            Datatype* left_type;
            Datatype* right_type;
            Function_Progress* function;
            bool switch_left_and_right;
            bool take_pointer_left;
            bool take_pointer_right;
        } binop;
        struct
        {
            Datatype* datatype;
            Function_Progress* function;
            bool take_pointer;
        } unop;
        struct
        {
            Datatype* container_type;
            Datatype* index_type;
            Function_Progress* function;
            bool take_pointer_for_container;
            bool take_pointer_for_index;
        } array_access;
        struct
        {
            Datatype* iterable_type;
            Datatype* iterator_type;
            Function_Progress* create;
            Function_Progress* has_next;
            Function_Progress* next;
            Function_Progress* get_value;
            bool take_pointer_for_iterable;
            bool take_pointer_for_iterator;
        } iterator;
    } options;
};

struct Custom_Operator_Install
{
    Custom_Operator* custom_operator;
    AST::Custom_Operator_Node* node;
};

struct Workload_Custom_Operator;
struct Custom_Operator_Table
{
    Workload_Custom_Operator* workloads[(int)Custom_Operator_Type::MAX_ENUM_VALUE];
    bool contains_operator[(int)Custom_Operator_Type::MAX_ENUM_VALUE];
    // Note: The DynArrays are allocated in analysis-data arena
    Hashtable<Custom_Operator_Key, DynArray<Custom_Operator_Install>> installed_operators;
};

struct Reachable_Operator_Table
{
	Custom_Operator_Table* operator_table;
	int depth;
};

u64  hash_custom_operator(Custom_Operator* op);
bool equals_custom_operator(Custom_Operator* a, Custom_Operator* b);



// SYMBOLS

// Note: Both symbols and symbol-table includes have access levels
enum class Symbol_Type
{
    DEFINITION_UNFINISHED,   // A Definition that isn't ready yet (global variable or comptime value)
    VARIABLE_UNDEFINED,      // A variable/parameter/global that hasn't been defined yet
    ALIAS_UNFINISHED,         // An import that isn't finished yet

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
        ModTree_Function* function;
        Poly_Function poly_function;
        Workload_Definition* definition_workload;
        Symbol* alias_for;
        int unfinished_alias_index;
        Hardcoded_Type hardcoded;
        Datatype* datatype;
        ModTree_Global* global;
        Upp_Module* upp_module;
        struct {
            Function_Progress* function;
            int index_in_polymorphic_signature;
            int index_in_non_polymorphic_signature;
        } parameter;
        Pattern_Variable* pattern_variable;
        Upp_Constant constant;
    } options;

    String* id;
    Symbol_Table* origin_table;
    Symbol_Access_Level access_level;
    Dynamic_Array<AST::Symbol_Lookup*> references;

    AST::Node* definition_node; // Note: This is a base because it could be either AST::Definition, AST::Parameter, AST::Import or AST::Expression::Polymorphic_Symbol
    Compilation_Unit* definition_unit; // May be null
    Text_Index definition_text_index;
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
    Symbol_Table* parent_table;
    Symbol_Access_Level parent_access_level;
    Dynamic_Array<Symbol_Table_Import> imports;
    Hashtable<String*, Dynamic_Array<Symbol*>> symbols;
    Custom_Operator_Table* custom_operator_table;
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
    Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, Symbol_Access_Level access_level, Compilation_Data* compilation_data
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

