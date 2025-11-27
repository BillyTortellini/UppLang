#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/allocators.hpp"
#include "compiler_misc.hpp" // Upp_Constant
#include "constant_pool.hpp"
#include "ast.hpp"
#include "type_system.hpp"

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

namespace AST
{
    struct Symbol_Lookup;
    struct Node;
    struct Import;
}


// OPERATOR CONTEXT

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
    Context_Change_Type type;
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
            Datatype* datatype; // Iterable type
        } iterator;
        // Cast_Option cast_option;
    } options;
};

union Custom_Operator
{
    struct {
        bool switch_left_and_right;
        // Type_Mods left_mods;
        // Type_Mods right_mods;
        ModTree_Function* function;
    } binop;
    struct {
        ModTree_Function* function;
        // Type_Mods mods;
    } unop;
    struct {
        bool is_polymorphic;
        // Type_Mods mods;
        union {
            ModTree_Function* function;
            Poly_Function poly_function;
        } options;
    } array_access;
    struct {
        // Cast_Mode cast_mode;
        // Type_Mods mods;
        bool is_polymorphic;
        union {
            ModTree_Function* function;
            Poly_Function poly_function;
        } options;
    } custom_cast;
    struct {
        // Type_Mods iterable_mods;
        bool is_polymorphic; 
        union {
            struct {
                ModTree_Function* create;
                ModTree_Function* has_next;
                ModTree_Function* next;
                ModTree_Function* get_value;
            } normal;
            struct {
                Poly_Function fn_create;
                Poly_Function has_next;
                Poly_Function next;
                Poly_Function get_value;
            } polymorphic;
        } options;
    } iterator;
    // Cast_Mode cast_mode;
};

struct Workload_Operator_Context_Change;
struct Operator_Context
{
    Dynamic_Array<Operator_Context*> context_imports; // Where context 0 is always the parent import
    Workload_Operator_Context_Change* workloads[(int)Context_Change_Type::MAX_ENUM_VALUE];
    Hashtable<Custom_Operator_Key, Custom_Operator> custom_operators;
};



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
enum class Include_Type
{
    DOT_CALL_INCLUDE = 1,
    DOT_CALL_INCLUDE_TRANSITIVE = 2,
    TRANSITIVE = 3,
    NORMAL = 4,
    PARENT = 5, // Parent is also transitive
};

struct Included_Table
{
    Include_Type include_type;
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

Symbol* symbol_table_define_symbol(
    Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, Symbol_Access_Level access_level);
void symbol_table_add_include_table(
    Symbol_Table* symbol_table, Symbol_Table* included_table, Include_Type include_type, Symbol_Access_Level access_level,
    AST::Node* error_report_node, Node_Section error_report_section
);
DynArray<Symbol*> symbol_table_query_id(
    Symbol_Table* symbol_table, String* id, Lookup_Type lookup_type, Symbol_Access_Level access_level, Arena* arena);
DynArray<Symbol*> symbol_table_query_all_symbols(
    Symbol_Table* symbol_table, Lookup_Type lookup_type, Symbol_Access_Level access_level, Arena* arena);
void symbol_table_query_resolve_aliases(DynArray<Symbol*>& symbols);

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);
void symbol_type_append_to_string(Symbol_Type type, String* string);

