#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp"
#include "type_system.hpp"

struct ModTree_Function;
struct Polymorphic_Function;
struct ModTree_Global;
struct Type_Signature;
struct Dependency_Analyser;

struct Compiler;
struct Symbol;
struct Symbol_Table;
struct Symbol_Data;
struct Analysis_Item;
struct Code_Source;
struct Analysis_Pass;

namespace AST
{
    struct Node;
    struct Expression;
    struct Statement;
    struct Code_Block;
    struct Symbol_Read;
    struct Definition;
    struct Module;
}



// Symbol Table
enum class Symbol_Type
{
    UNRESOLVED,              // An Analysis Item/Analysis-Progress exists for this Symbol, but it isn't usable yet (Function without header analysis, struct not started...)
    VARIABLE_UNDEFINED,      // A variable/parameter/global that hasn't been defined yet

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
        Symbol_Table* module_table;
        ModTree_Function* function;
        Polymorphic_Function* polymorphic_function;
        Hardcoded_Type hardcoded;
        Type_Signature* type;
        ModTree_Global* global;
        struct {
            bool is_polymorphic;
            int index; // If not polymorphic, index in function signature, otherwise index in polymorphic evaluation order 
            ModTree_Function* function; 
        } parameter;
        Upp_Constant constant;
        Symbol* alias;
        struct {
            bool is_parameter;
            int parameter_index;
        } variable_undefined;
    } options;

    String* id;
    Symbol_Table* origin_table;
    AST::Node* definition_node; // Note: This is a base because it could be either AST::Definition or AST::Parameter
    bool internal;  // Internal symbols are only valid if referenced in the same internal scope (Variables, parameters). Required for anonymous structs or functions.
    Dynamic_Array<AST::Symbol_Read*> references;
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
void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);
Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool search_parents, bool interals_ok, AST::Symbol_Read* reference);



// Analysis Items
enum class Analysis_Item_Type
{
    DEFINITION,
    STRUCTURE,
    FUNCTION,
    FUNCTION_BODY,
    BAKE,
    IMPORT
};

struct Analysis_Item
{
    Analysis_Item_Type type;
    Dynamic_Array<AST::Symbol_Read*> symbol_reads;
    Symbol* symbol; // Optional

    Dynamic_Array<Analysis_Pass*> passes;
    AST::Node* node;
    int ast_node_count;

    union {
        Analysis_Item* function_body_item;
    } options;
};

struct Dependency_Analyser
{
    Code_Source* code_source;

    // Output
    Symbol_Table* root_symbol_table;
    Dynamic_Array<Symbol_Error> errors;
    Hashtable<AST::Node*, Analysis_Item*> mapping_ast_to_items;

    // Used during analysis
    Compiler* compiler;
    Symbol_Table* symbol_table;
    Analysis_Item* analysis_item;
    Dependency_Type dependency_type;

    // Allocations, TODO: Use actual allocators
    Dynamic_Array<Symbol_Table*> allocated_symbol_tables;
};

Dependency_Analyser* dependency_analyser_initialize();
void dependency_analyser_destroy();
void dependency_analyser_reset(Compiler* compiler);
void dependency_analyser_analyse(Code_Source* code_source);

void analysis_item_destroy(Analysis_Item* item);
void dependency_analyser_log_error(Symbol* existing_symbol, AST::Node* error_node);
void dependency_analyser_append_to_string(String* string);
