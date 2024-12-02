#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp" // Upp_Constant
#include "constant_pool.hpp"
#include "ast.hpp"
#include "type_system.hpp"

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
struct Datatype_Template;

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
    NONE = 1,
    EXPLICIT, // cast{u64} i
    INFERRED, // cast i
    POINTER_EXPLICIT, // cast_pointer{*int} ip
    POINTER_INFERRED, // cast_pointer ip
    IMPLICIT, // x: u32 = i
    
    MAX_ENUM_VALUE
};

/*
    Notes on Custom_Operator_Key:
    The key-types are always stored as the base_type (E.g. no pointer/constant types).
    After querying the analyse has to make sure that the pointer/constant levels are valid
*/

// Note: Left and right types are always stored as the base types of pointers + pointer level.
//       Polymorphic custom operators make use of the struct-base as the datatype and null for some Datatype* values.
//       For binop commutativity the custom operator is inserted twice, so only a single lookup will find one of the versions
struct Custom_Operator_Key
{
    AST::Context_Change_Type type;
    union
    {
        struct {
            AST::Binop binop;
            Datatype* left_type;
            Datatype* right_type;
        } binop;
        struct {
            AST::Unop unop;
            Datatype* type;
        } unop;
        struct {
            Datatype* array_type;
        } array_access;
        struct {
            Datatype* from_type;
            Datatype* to_type; // May be null for polymorphic casts
        } custom_cast;
        struct {
            Datatype* datatype;
            String* id;
        } dot_call;
        struct {
            Datatype* datatype; // Iterable type
        } iterator;
        Cast_Option cast_option;
    } options;
};

union Custom_Operator
{
    struct {
        bool switch_left_and_right;
        Type_Mods left_mods;
        Type_Mods right_mods;
        ModTree_Function* function;
    } binop;
    struct {
        ModTree_Function* function;
        Type_Mods mods;
    } unop;
    struct {
        bool is_polymorphic;
        Type_Mods mods;
        union {
            ModTree_Function* function;
            Polymorphic_Function_Base* poly_base;
        } options;
    } array_access;
    struct {
        Cast_Mode cast_mode;
        Type_Mods mods;
        bool is_polymorphic;
        union {
            ModTree_Function* function;
            Polymorphic_Function_Base* poly_base;
        } options;
    } custom_cast;
    struct {
        bool dot_call_as_member_access;
        Type_Mods mods;
        bool is_polymorphic;
        union {
            ModTree_Function* function;
            Polymorphic_Function_Base* poly_base;
        } options;
    } dot_call;
    struct {
        Type_Mods iterable_mods;
        bool is_polymorphic; 
        union {
            struct {
                ModTree_Function* create;
                ModTree_Function* has_next;
                ModTree_Function* next;
                ModTree_Function* get_value;
            } normal;
            struct {
                Polymorphic_Function_Base* fn_create;
                Polymorphic_Function_Base* has_next;
                Polymorphic_Function_Base* next;
                Polymorphic_Function_Base* get_value;
            } polymorphic;
        } options;
    } iterator;
    Cast_Mode cast_mode;
};

struct Workload_Operator_Context_Change;
struct Operator_Context
{
    Dynamic_Array<Operator_Context*> context_imports; // Where context 0 is always the parent import
    Workload_Operator_Context_Change* workloads[(int)AST::Context_Change_Type::MAX_ENUM_VALUE];
    Hashtable<Custom_Operator_Key, Custom_Operator> custom_operators;
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
    POLYMORPHIC_VALUE, // Either comptime parameter or inferred parameter
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
        ModTree_Function* function;
        Polymorphic_Function_Base* polymorphic_function;
        Workload_Definition* definition_workload;
        Workload_Import_Resolve* alias_workload;
        Hardcoded_Type hardcoded;
        Datatype* type;
        ModTree_Global* global;
        struct {
            Module_Progress* progress; // Some modules (e.g. compiler created) don't have a progress
            Symbol_Table* symbol_table;
        } module;
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
    Symbol_Access_Level access_level;
    Dynamic_Array<AST::Symbol_Lookup*> references;

    AST::Node* definition_node; // Note: This is a base because it could be either AST::Definition, AST::Parameter, AST::Import or AST::Expression::Polymorphic_Symbol
    Compilation_Unit* definition_unit; // May be null
    Text_Index definition_text_index;
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
void symbol_table_add_include_table(Symbol_Table* symbol_table, Symbol_Table* included_table, bool transitive, Symbol_Access_Level access_level, AST::Node* error_report_node);
// Note: when id == 0, all symbols that are possible will be added
void symbol_table_query_id(
    Symbol_Table* table, String* id, bool search_includes, Symbol_Access_Level access_level, Dynamic_Array<Symbol*>* results, Hashset<Symbol_Table*>* already_visited);

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);
void symbol_type_append_to_string(Symbol_Type type, String* string);

