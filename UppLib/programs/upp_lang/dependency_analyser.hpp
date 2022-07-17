#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp"
#include "type_system.hpp"

struct ModTree_Function;
struct ModTree_Global;
struct Type_Signature;
struct Dependency_Analyser;

struct Compiler;
struct Symbol;
struct Symbol_Table;
struct Symbol_Data;
struct Analysis_Item;
struct Symbol_Dependency;
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
    UNRESOLVED,            // An Analysis Item exists for this Symbol, but it hasn't advance yet (Function without header analysis, struct not started...)
    VARIABLE_UNDEFINED,    // A variable/parameter/global that hasn't been defined yet
    SYMBOL_ALIAS,          // Comptime definition pointing at another symbol

    HARDCODED_FUNCTION,
    EXTERN_FUNCTION,
    FUNCTION,
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
        Hardcoded_Type hardcoded;
        Type_Signature* type;
        ModTree_Global* global;
        struct {
            Type_Signature* parameter_type;
            int parameter_index;
        };
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
    Analysis_Item* origin_item;
    Dynamic_Array<Symbol_Dependency*> references;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    Hashtable<String*, Symbol*> symbols;
};

struct Symbol_Error
{
    Symbol* existing_symbol;
    AST::Node* error_node;
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

    Symbol* hardcoded_type_info;
    Symbol* hardcoded_type_of;
    Symbol* hardcoded_assert;
    Symbol* hardcoded_print_bool;
    Symbol* hardcoded_print_i32;
    Symbol* hardcoded_print_f32;
    Symbol* hardcoded_print_string;
    Symbol* hardcoded_print_line;
    Symbol* hardcoded_read_i32;
    Symbol* hardcoded_read_f32;
    Symbol* hardcoded_read_bool;
    Symbol* hardcoded_random_i32;

    Symbol* error_symbol;
};

Symbol_Table* symbol_table_create(Symbol_Table* parent, AST::Node* definition_node);
void symbol_table_destroy(Symbol_Table* symbol_table);
void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);
Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool only_current_scope, Symbol_Dependency* dependency, Analysis_Item* searching_from);



// Analysis Items
struct Symbol_Dependency
{
    Dependency_Type type;
    AST::Symbol_Read* read;
    Symbol* resolved_symbol;
    Symbol_Table* symbol_table;
    Analysis_Item* item;
};

enum class Analysis_Item_Type
{
    DEFINITION,
    STRUCTURE,
    FUNCTION,
    FUNCTION_BODY,
    BAKE,
    ROOT, // At unexpected global scope
    IMPORT
};

struct Analysis_Item
{
    Analysis_Item_Type type;
    Dynamic_Array<Symbol_Dependency> symbol_dependencies;
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
    Predefined_Symbols predefined_symbols;
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
