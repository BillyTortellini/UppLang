#include "symbol_table.hpp"

#include "compiler_misc.hpp"
#include "compiler.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "ast.hpp"

// PROTOTYPES

// SYMBOL TABLE FUNCTIONS
Symbol_Table* symbol_table_create(Symbol_Table* parent, bool is_internal)
{
    auto analyser = compiler.semantic_analyser;
    Symbol_Table* result = new Symbol_Table;
    dynamic_array_push_back(&analyser->allocated_symbol_tables, result);
    result->parent = parent;
    result->symbols = hashtable_create_pointer_empty<String*, Symbol*>(1);
    result->internal = is_internal;
    return result;
}

void symbol_destroy(Symbol* symbol) {
    dynamic_array_destroy(&symbol->references);
}

void symbol_table_destroy(Symbol_Table* symbol_table)
{
    auto it = hashtable_iterator_create(&symbol_table->symbols);
    while (hashtable_iterator_has_next(&it)) {
        Symbol* symbol = *it.value;
        symbol_destroy(symbol);
        delete symbol;
        hashtable_iterator_next(&it);
    }
    hashtable_destroy(&symbol_table->symbols);
    delete symbol_table;
}

Symbol* symbol_table_define_symbol(Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, bool is_internal)
{
    assert(id != 0, "HEY");

    // Check if already defined in same scope, if so, use temporary id
    Symbol* found_symbol = symbol_table_find_symbol(symbol_table, id, false, true, 0);
    if (found_symbol != 0) {
        semantic_analyser_log_error(
            Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED, definition_node == 0 ? AST::upcast(compiler.main_source->source_parse->root) : definition_node
        );
        String temp_identifer = string_create_empty(128);
        SCOPE_EXIT(string_destroy(&temp_identifer));
        static int counter = 0; // Note: This is pretty bad, since we will flood the identifer pool this way
        string_append_formated(&temp_identifer, "__temporary_%d", counter);
        counter += 1;
        id = identifier_pool_add(&compiler.identifier_pool, temp_identifer);
    }

    // Create new symbol
    Symbol* new_sym = new Symbol;
    new_sym->definition_node = definition_node;
    new_sym->id = id;
    new_sym->type = type;
    new_sym->origin_table = symbol_table;
    new_sym->internal = is_internal;
    new_sym->references = dynamic_array_create_empty<AST::Symbol_Read*>(1);

    hashtable_insert_element(&symbol_table->symbols, id, new_sym);
    return new_sym;
}

Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool search_parents, bool internals_ok, AST::Symbol_Read* reference)
{
    // Sanity check
    if (reference != 0 && reference->symbol != 0) {
        panic("Symbol already found, I dont know if this path has a use case");
        return reference->symbol;
    }

    // Search for symbol
    Symbol* symbol = nullptr;
    bool add_reference = true;
    {
        Symbol** found = hashtable_find_element(&table->symbols, id);
        if (found != 0) {
            symbol = *found;
            if (symbol->internal && !internals_ok) { // Keep searching if internal was found
                symbol = 0; 
            }
        }

        if (symbol == 0 && search_parents && table->parent != 0) {
            symbol = symbol_table_find_symbol(table->parent, id, true, internals_ok && table->internal, reference);
            add_reference = false; // Don't add reference, since this is was already done in recursive call
        }
    }

    // Add reference to reference list
    if (reference != 0 && symbol != 0 && add_reference) {
        dynamic_array_push_back(&(symbol->references), reference);
    }
    return symbol;
}

void symbol_append_to_string(Symbol* symbol, String* string)
{
    string_append_formated(string, "%s ", symbol->id->characters);
    switch (symbol->type)
    {
    case Symbol_Type::VARIABLE_UNDEFINED:
        string_append_formated(string, "Variable Undefined");
        break;
    case Symbol_Type::PARAMETER:
        string_append_formated(string, "Parameter");
        break;
    case Symbol_Type::POLYMORPHIC_FUNCTION:
        string_append_formated(string, "Polymorphic Function");
        break;
    case Symbol_Type::DEFINITION_UNFINISHED:
        string_append_formated(string, "Definition Unfinished");
        break;
    case Symbol_Type::VARIABLE:
        string_append_formated(string, "Variable");
        break;
    case Symbol_Type::GLOBAL:
        string_append_formated(string, "Global");
        break;
    case Symbol_Type::TYPE:
        string_append_formated(string, "Type");
        break;
    case Symbol_Type::ERROR_SYMBOL:
        string_append_formated(string, "Error");
        break;
    case Symbol_Type::COMPTIME_VALUE:
        string_append_formated(string, "Constant %d", symbol->options.constant.constant_index);
        break;
    case Symbol_Type::HARDCODED_FUNCTION:
        string_append_formated(string, "Hardcoded Function");
        break;
    case Symbol_Type::FUNCTION:
        string_append_formated(string, "Function");
        break;
    case Symbol_Type::MODULE:
        string_append_formated(string, "Module");
        break;
    default: panic("What");
    }
}

void symbol_table_append_to_string_with_parent_info(String* string, Symbol_Table* table, bool is_parent, bool print_root)
{
    if (!print_root && table->parent == 0) return;
    if (!is_parent) {
        string_append_formated(string, "Symbols: \n");
    }
    auto iter = hashtable_iterator_create(&table->symbols);
    while (hashtable_iterator_has_next(&iter))
    {
        Symbol* s = *iter.value;
        if (is_parent) {
            string_append_formated(string, "\t");
        }
        symbol_append_to_string(s, string);
        string_append_formated(string, "\n");
        hashtable_iterator_next(&iter);
    }
    if (table->parent != 0) {
        symbol_table_append_to_string_with_parent_info(string, table->parent, true, print_root);
    }
}

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root) {
    symbol_table_append_to_string_with_parent_info(string, table, false, print_root);
}
