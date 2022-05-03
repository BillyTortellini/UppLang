#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp"
#include "type_system.hpp"

struct ModTree_Variable;
struct ModTree_Function;
struct ModTree_Hardcoded_Function;
struct ModTree_Extern_Function;
struct Modtree_Polymorphic_Function;
struct Type_Signature;
struct Dependency_Analyser;

struct Compiler;
struct Symbol;
struct Symbol_Table;
struct Symbol_Data;
struct Analysis_Item;
struct Symbol_Dependency;

namespace AST
{
    struct Base;
    struct Expression;
    struct Statement;
    struct Code_Block;
    struct Symbol_Read;
    struct Definition;
}

namespace Dependency_Analysis
{
    struct Symbol_Dependency;
}

// Symbol Table
enum class Symbol_Type
{
    UNRESOLVED,
    VARIABLE_UNDEFINED,
    POLYMORPHIC_PARAMETER,

    HARDCODED_FUNCTION,
    EXTERN_FUNCTION,
    FUNCTION,
    TYPE,
    CONSTANT_VALUE,
    VARIABLE,
    MODULE,
    SYMBOL_ALIAS,
    ERROR_SYMBOL,
};

struct Symbol
{
    Symbol_Type type;
    union
    {
        ModTree_Variable* variable;
        Symbol_Table* module_table;
        ModTree_Function* function;
        ModTree_Hardcoded_Function* hardcoded_function;
        ModTree_Extern_Function* extern_function;
        Type_Signature* type;
        Upp_Constant constant;
        Symbol* alias;
        struct {
            bool is_parameter;
            int parameter_index;
        } variable_undefined;
        struct {
            int parameter_index;
            ModTree_Function* function;
        } polymorphic;
    } options;

    String* id;
    Symbol_Table* origin_table;
    AST::Base* definition_node;
    Analysis_Item* origin_item;
    Dynamic_Array<AST::Symbol_Read*> references;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    Hashtable<String*, Symbol*> symbols;
};

struct Symbol_Error
{
    Symbol* existing_symbol;
    AST::Base* error_node;
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

    Symbol* hardcoded_print_bool;
    Symbol* hardcoded_print_i32;
    Symbol* hardcoded_print_f32;
    Symbol* hardcoded_print_string;
    Symbol* hardcoded_print_line;
    Symbol* hardcoded_read_i32;
    Symbol* hardcoded_read_f32;
    Symbol* hardcoded_read_bool;
    Symbol* hardcoded_random_i32;

    Symbol* function_assert;
    Symbol* global_type_informations;

    Symbol* error_symbol;
};

Symbol_Table* symbol_table_create(Symbol_Table* parent, AST::Base* definition_node);
void symbol_table_destroy(Symbol_Table* symbol_table);
void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root);
void symbol_append_to_string(Symbol* symbol, String* string);
Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool only_current_scope, Symbol_Dependency* reference);



enum class Dependency_Type
{
    NORMAL,
    MEMBER_IN_MEMORY,
    MEMBER_REFERENCE,
};

struct Symbol_Dependency
{
    Dependency_Type type;
    AST::Symbol_Read* read;
    Symbol_Table* symbol_table;
    Analysis_Item* item;
};

struct Item_Dependency
{
    Analysis_Item* dependent;
    Analysis_Item* depends_on;
    Dependency_Type type;
};

enum class Analysis_Item_Type
{
    DEFINITION,
    STRUCTURE,
    FUNCTION,
    FUNCTION_BODY,
    BAKE,
    ROOT, // At unexpected global scope
};

struct Analysis_Item
{
    Analysis_Item_Type type;
    Dynamic_Array<Symbol_Dependency> symbol_dependencies;
    AST::Base* node;
    Symbol* symbol; // Optional
    union {
        Analysis_Item* function_body_item;
    } options;
};

struct Dependency_Analyser
{
    // Output
    Dynamic_Array<Analysis_Item*> analysis_items;
    Dynamic_Array<Item_Dependency> item_dependencies;
    Symbol_Table* root_symbol_table; // Not the same as the module table of the ast, since we could load multiple files
    Predefined_Symbols predefined_symbols;
    Dynamic_Array<Symbol_Error> errors;
    Hashtable<AST::Base*, Analysis_Item*> mapping_ast_to_items;

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
void dependency_analyser_analyse(AST::Module* root_module);

void dependency_analyser_log_error(Symbol* existing_symbol, AST::Base* error_node);
void dependency_analyser_append_to_string(String* string);
