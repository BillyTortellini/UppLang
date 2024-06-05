#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp" // Upp_Constant
#include "constant_pool.hpp"
#include "ast.hpp"

// struct Function_Progress;
struct ModTree_Global;
struct ModTree_Function;
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
struct Polymorphic_Function_Base;
struct Function_Parameter;

namespace AST
{
    struct Symbol_Lookup;
    struct Node;
}


// OPERATOR CONTEXT
enum class Cast_Mode
{
    AUTO = 1,
    IMPLICIT,
    EXPLICIT,
    NONE
};

struct Datatype_Pair
{
    Datatype* from;
    Datatype* to;
};

bool datatype_pair_equals(Datatype_Pair* a, Datatype_Pair* b);
u64 datatype_pair_hash(Datatype_Pair* pair);

struct Custom_Cast
{
    ModTree_Function* function;
    Cast_Mode cast_mode;
};

struct Workload_Operator_Context_Change;
struct Operator_Context
{
    Workload_Operator_Context_Change* workload; // May be null (In case of root operator context)
    Cast_Mode cast_mode_settings[AST::CONTEXT_SETTING_CAST_MODE_COUNT];
    bool boolean_settings[AST::CONTEXT_SETTING_BOOLEAN_COUNT];
    Hashtable<Datatype_Pair, Custom_Cast> custom_casts;
};



// SYMBOLS

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
    POLYMORPHIC_VALUE, // Either comptime parameter, implicit parameter, or struct parameter
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
        Polymorphic_Function_Base* polymorphic_function;
        Workload_Definition* definition_workload;
        Workload_Import_Resolve* alias_workload;
        Hardcoded_Type hardcoded;
        Datatype* type;
        ModTree_Global* global;
        struct {
            Function_Progress* function;
            int index_in_polymorphic_signature;
            int index_in_non_polymorphic_signature;
        } parameter;
        struct {
            int defined_in_parameter_index;
            int access_index;
        } polymorphic_value;
        Upp_Constant constant;
    } options;

    String* id;
    Symbol_Table* origin_table;
    AST::Node* definition_node; // Note: This is a base because it could be either AST::Definition, AST::Parameter, AST::Import or AST::Expression::Polymorphic_Symbol
    Symbol_Access_Level access_level;
    Dynamic_Array<AST::Symbol_Lookup*> references;
};



// SYMBOL TABLE
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
    Operator_Context* operator_context;
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

