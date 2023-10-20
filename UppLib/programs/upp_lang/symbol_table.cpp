#include "symbol_table.hpp"

#include "compiler_misc.hpp"
#include "compiler.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "ast.hpp"

// PROTOTYPES

// SYMBOL TABLE FUNCTIONS
Symbol_Table* symbol_table_create()
{
    auto analyser = compiler.semantic_analyser;
    Symbol_Table* result = new Symbol_Table;
    dynamic_array_push_back(&analyser->allocated_symbol_tables, result);
    result->included_tables = dynamic_array_create_empty<Included_Table>(1);
    result->symbols = hashtable_create_pointer_empty<String*, Dynamic_Array<Symbol*>>(1);
    return result;
}

Symbol_Table* symbol_table_create_with_parent(Symbol_Table* parent_table, bool internal)
{
    Symbol_Table* result = symbol_table_create();
    symbol_table_add_include_table(result, parent_table, true, internal, 0);
    return result;
}

void symbol_table_destroy(Symbol_Table* symbol_table)
{
    for (auto iter = hashtable_iterator_create(&symbol_table->symbols); hashtable_iterator_has_next(&iter); hashtable_iterator_next(&iter)) {
        // Note: Symbols are allocated elsewhere...
        dynamic_array_destroy(iter.value);
    }
    hashtable_destroy(&symbol_table->symbols);
    delete symbol_table;
}

void symbol_table_add_include_table(Symbol_Table* symbol_table, Symbol_Table* included_table, bool transitive, bool internal, AST::Node* include_node)
{
    // Check for errors
    if (symbol_table == included_table) {
        semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, include_node);
        semantic_analyser_add_error_info(error_information_make_text("Trying to include symbol table to itself!"));
        return;
    }
    for (int i = 0; i < symbol_table->included_tables.size; i++) {
        auto include = symbol_table->included_tables[i];
        if (include.table == included_table) {
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, include_node);
            semantic_analyser_add_error_info(error_information_make_text("Table is already included!"));
            return;
        }
    }

    // Add include
    Included_Table included;
    included.is_internal = internal;
    included.transitive = transitive;
    included.table = included_table;
    dynamic_array_push_back(&symbol_table->included_tables, included);
}

void symbol_destroy(Symbol* symbol) {
    dynamic_array_destroy(&symbol->references);
    delete symbol;
}

Symbol* symbol_table_define_symbol(Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, bool is_internal)
{
    assert(id != 0, "HEY");

    Dynamic_Array<Symbol*>* symbols = hashtable_find_element(&symbol_table->symbols, id);
    if (symbols == 0) {
        Dynamic_Array<Symbol*> new_symbols = dynamic_array_create_empty<Symbol*>(1);
        hashtable_insert_element(&symbol_table->symbols, id, new_symbols);
        symbols = hashtable_find_element(&symbol_table->symbols, id);
        assert(symbols != 0, "Just inserted!");
    }

    // Create new symbol
    Symbol* new_sym = new Symbol;
    dynamic_array_push_back(&compiler.semantic_analyser->allocated_symbols, new_sym);
    new_sym->definition_node = definition_node;
    new_sym->id = id;
    new_sym->type = type;
    new_sym->origin_table = symbol_table;
    new_sym->internal = is_internal;
    new_sym->references = dynamic_array_create_empty<AST::Symbol_Lookup*>(1);
    dynamic_array_push_back(symbols, new_sym);
    return new_sym;
}


void symbol_table_query_id_recursive(
    Symbol_Table* table, String* id, bool search_includes, bool internals_ok, Dynamic_Array<Symbol*>* results
)
{
    // Check if already visited
    auto visited = compiler.semantic_analyser->symbol_lookup_visited;
    if (hashset_contains(&visited, table)) {
        return;
    }
    hashset_insert_element(&visited, table);

    // Check if all symbols should be added (If id parameter == 0)
    bool stop_further_lookup = false;
    if (id != 0) {
        // Try to find symbol
        Dynamic_Array<Symbol*>* symbols = hashtable_find_element(&table->symbols, id);
        if (symbols != 0) {
            for (int i = 0; i < symbols->size; i++) {
                Symbol* symbol = (*symbols)[i];
                if (!(symbol->internal && !internals_ok)) {
                    dynamic_array_push_back(results, symbol);
                    if (symbol->internal) {
                        stop_further_lookup = true; // Once a internal symbol is found (variable/parameter), stop the search
                    }
                }
            }
        }
    }
    else {
        // Otherwise add all symbols in this table
        for (auto iter = hashtable_iterator_create(&table->symbols); hashtable_iterator_has_next(&iter); hashtable_iterator_next(&iter)) {
            Dynamic_Array<Symbol*>* symbols = iter.value;
            for (int i = 0; i < symbols->size; i++) {
                dynamic_array_push_back(results, (*symbols)[i]);
            }
        }
    }

    if (stop_further_lookup) {
        return;
    }

    // With includes, even if we have found a symbol, we keep searching
    for (int i = 0; i < table->included_tables.size && search_includes; i++) {
        auto included = table->included_tables[i];
        symbol_table_query_id_recursive(
            included.table,
            id,
            included.transitive,
            internals_ok && included.is_internal,
            results
        );
    }
}

void symbol_table_query_id(Symbol_Table* table, String* id, bool search_includes, bool internals_ok, Dynamic_Array<Symbol*>* results) {
    hashset_reset(&compiler.semantic_analyser->symbol_lookup_visited);
    return symbol_table_query_id_recursive(table, id, search_includes, internals_ok, results);
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
    case Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL:
        string_append_formated(string, "Alias or imported symbol");
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
    // if (!print_root && table->parent == 0) return;
    if (!is_parent) {
        string_append_formated(string, "Symbols: \n");
    }
    for (auto iter = hashtable_iterator_create(&table->symbols); hashtable_iterator_has_next(&iter); hashtable_iterator_next(&iter))
    {
        Dynamic_Array<Symbol*> symbols = *(iter.value);
        for (int i = 0; i < symbols.size; i++) {
            Symbol* s = symbols[i];
            if (is_parent) {
                string_append_formated(string, "\t");
            }
            symbol_append_to_string(s, string);
            string_append_formated(string, "\n");
        }
    }
    // if (table->parent != 0) {
    //     symbol_table_append_to_string_with_parent_info(string, table->parent, true, print_root);
    // }
}

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root) {
    symbol_table_append_to_string_with_parent_info(string, table, false, print_root);
}
