#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp" // Upp_Constant
#include "constant_pool.hpp"

// struct Function_Progress;
struct ModTree_Global;
struct Datatype;
struct Workload_Definition;
struct Workload_Import_Resolve;
struct Workload_Structure_Polymorphic;
struct Module_Progress;

struct Symbol;
struct Symbol_Table;
struct Symbol_Data;
struct Datatype_Template_Parameter;

struct Function_Progress;
struct Function_Parameter;

namespace AST
{
    struct Symbol_Lookup;
    struct Node;
}



// Symbol Table

// Note: Both symbols and symbol-table includes have access levels
enum class Symbol_Access_Level
{
    GLOBAL = 0,      // Can be accessed everywhere (comptime definitions, functions, structs)
    POLYMORPHIC = 1, // Access level for polymorphic parameters (anonymous structs/lambdas/bake)
    INTERNAL = 2     // Access level for variables/parameters of functions, which only have meaningful values during execution
};

enum class Symbol_Type
{
    DEFINITION_UNFINISHED,   // A Definition that isn't ready yet (global variable or comptime value)
    VARIABLE_UNDEFINED,      // A variable/parameter/global that hasn't been defined yet

    HARDCODED_FUNCTION,
    FUNCTION,
    POLYMORPHIC_FUNCTION,

    VARIABLE,
    GLOBAL,
    PARAMETER,

    TYPE,
    TEMPLATE_PARAMETER, // Type template in polymorphic function, e.g. "$T"
    STRUCT_PARAMETER,
    COMPTIME_VALUE,
    ALIAS_OR_IMPORTED_SYMBOL, // Alias created by import, e.g. import Algorithms~bubble_sort as sort
    MODULE,
    ERROR_SYMBOL, 
};

struct Symbol
{
    Symbol_Type type;
    union
    {
        Datatype* variable_type;
        Module_Progress* module_progress;
        Function_Progress* function;
        Function_Progress* polymorphic_function;
        Workload_Definition* definition_workload;
        Workload_Import_Resolve* alias_workload;
        Hardcoded_Type hardcoded;
        Datatype* type;
        ModTree_Global* global;
        Function_Parameter* parameter;
        Upp_Constant constant;
        Datatype_Template_Parameter* template_parameter;
        struct {
            Workload_Structure_Polymorphic* workload;
            int parameter_index;
        } struct_parameter;
    } options;

    String* id;
    Symbol_Table* origin_table;
    AST::Node* definition_node; // Note: This is a base because it could be either AST::Definition, AST::Parameter, AST::Import or AST::Expression::Polymorphic_Symbol
    Symbol_Access_Level access_level;
    Dynamic_Array<AST::Symbol_Lookup*> references;
};

struct Included_Table
{
    bool transitive;
    Symbol_Access_Level access_level;
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
Symbol_Table* symbol_table_create_with_parent(Symbol_Table* parent_table, Symbol_Access_Level access_level);
void symbol_table_destroy(Symbol_Table* symbol_table);
void symbol_destroy(Symbol* symbol);

Symbol* symbol_table_define_symbol(Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, Symbol_Access_Level access_level);
void symbol_table_add_include_table(Symbol_Table* symbol_table, Symbol_Table* included_table, bool transitive, Symbol_Access_Level access_level, AST::Node* include_node);
// Note: when id == 0, all symbols that are possible will be added
void symbol_table_query_id(Symbol_Table* table, String* id, bool search_includes, Symbol_Access_Level access_level, Dynamic_Array<Symbol*>* results);

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);

