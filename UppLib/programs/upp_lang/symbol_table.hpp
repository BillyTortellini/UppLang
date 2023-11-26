#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp" // Upp_Constant

// struct Function_Progress;
struct ModTree_Global;
struct Type_Base;
struct Workload_Definition;
struct Workload_Function_Parameter;
struct Workload_Import_Resolve;
struct Module_Progress;

struct Symbol;
struct Symbol_Table;
struct Symbol_Data;

struct Function_Progress;

namespace AST
{
    struct Symbol_Lookup;
    struct Node;
}



// Symbol Table
enum class Symbol_Type
{
    DEFINITION_UNFINISHED,   // A Definition that isn't ready yet (global variable or comptime value)
    VARIABLE_UNDEFINED,      // A variable/parameter/global that hasn't been defined yet

    ALIAS_OR_IMPORTED_SYMBOL, // Alias created by import, e.g. import Algorithms~bubble_sort as sort
    HARDCODED_FUNCTION,
    FUNCTION,
    POLYMORPHIC_FUNCTION,
    TYPE,
    COMPTIME_VALUE,
    VARIABLE,
    GLOBAL,
    PARAMETER,
    MODULE,
    ERROR_SYMBOL, 
};

struct Symbol
{
    Symbol_Type type;
    union
    {
        Type_Base* variable_type;
        Module_Progress* module_progress;
        Function_Progress* function;
        Function_Progress* polymorphic_function;
        Workload_Definition* definition_workload;
        Workload_Import_Resolve* alias_workload;
        Hardcoded_Type hardcoded;
        Type_Base* type;
        ModTree_Global* global;
        Workload_Function_Parameter* parameter_workload;
        Upp_Constant constant;
    } options;

    String* id;
    Symbol_Table* origin_table;
    AST::Node* definition_node; // Note: This is a base because it could be either AST::Definition, AST::Parameter or AST::Import
    bool internal;  // Internal symbols are only valid if referenced in the same internal scope (Variables, parameters). Required for anonymous structs or functions.
    Dynamic_Array<AST::Symbol_Lookup*> references;
};

struct Included_Table
{
    bool transitive;
    bool is_internal; // If it's internal we can access internal symbols of the parent table
    Symbol_Table* table;
};

struct Symbol_Table
{
    Dynamic_Array<Included_Table> included_tables;
    Hashtable<String*, Dynamic_Array<Symbol*>> symbols;
};

struct Symbol_Error
{
    Symbol* existing_symbol;
    AST::Node* error_node;
};

Symbol_Table* symbol_table_create();
Symbol_Table* symbol_table_create_with_parent(Symbol_Table* parent_table, bool internal);
void symbol_table_destroy(Symbol_Table* symbol_table);
void symbol_destroy(Symbol* symbol);

Symbol* symbol_table_define_symbol(Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, bool is_internal);
void symbol_table_add_include_table(Symbol_Table* symbol_table, Symbol_Table* included_table, bool transitive, bool internal, AST::Node* include_node);
// Note: when id == 0, all symbols that are possible will be added
void symbol_table_query_id(Symbol_Table* table, String* id, bool search_includes, bool internals_ok, Dynamic_Array<Symbol*>* results);

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);

