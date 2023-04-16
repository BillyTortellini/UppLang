#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp" // Upp_Constant

// struct Function_Progress;
// struct Polymorphic_Base;
struct ModTree_Global;
struct Type_Signature;
struct Workload_Definition;
struct Workload_Function_Parameter;
struct Workload_Using_Resolve;
struct Module_Progress;

struct Symbol;
struct Symbol_Table;
struct Symbol_Data;

struct Function_Progress;
struct Polymorphic_Base;

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

    ALIAS_OR_IMPORTED_SYMBOL, // Alias created by using, e.g. using Algorithms~sort
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
        Type_Signature* variable_type;
        Module_Progress* module_progress;
        Function_Progress* function;
        Workload_Definition* definition_workload;
        Workload_Using_Resolve* alias_workload;
        Polymorphic_Base* polymorphic_function;
        Hardcoded_Type hardcoded;
        Type_Signature* type;
        ModTree_Global* global;
        struct {
            bool is_polymorphic;
            int ast_index;  // Index in ast_signature_node
            int type_index; // Index in function_signature
            Workload_Function_Parameter* workload;
        } parameter;
        Upp_Constant constant;
    } options;

    String* id;
    Symbol_Table* origin_table;
    AST::Node* definition_node; // Note: This is a base because it could be either AST::Definition, AST::Parameter or AST::Using
    bool internal;  // Internal symbols are only valid if referenced in the same internal scope (Variables, parameters). Required for anonymous structs or functions.
    Dynamic_Array<AST::Symbol_Lookup*> references;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    bool internal; // Internal symbol tables can access internal variables of the parent symbol table (Currently only Code-Blocks). See Symbol
    Hashtable<String*, Symbol*> symbols;
};

struct Symbol_Error
{
    Symbol* existing_symbol;
    AST::Node* error_node;
};

Symbol* symbol_table_define_symbol(Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, bool is_internal);
Symbol_Table* symbol_table_create(Symbol_Table* parent, bool is_internal);
void symbol_table_destroy(Symbol_Table* symbol_table);
void symbol_destroy(Symbol* symbol);
void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);
Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool search_parents, bool interals_ok, AST::Symbol_Lookup* reference);

