#include "symbol_table.hpp"

#include "compiler_misc.hpp"
#include "compiler.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "ast.hpp"
#include "editor_analysis_info.hpp"

// SYMBOL TABLE FUNCTIONS
Symbol_Table* symbol_table_create()
{
    auto analyser = compiler.semantic_analyser;
    Symbol_Table* result = new Symbol_Table;
    dynamic_array_push_back(&compiler.analysis_data->allocated_symbol_tables, result);
    result->included_tables = dynamic_array_create<Included_Table>();
    result->symbols = hashtable_create_pointer_empty<String*, Dynamic_Array<Symbol*>>(1);
    result->operator_context = 0;
    return result;
}

Symbol_Table* symbol_table_create_with_parent(Symbol_Table* parent_table, Symbol_Access_Level access_level)
{
    Symbol_Table* result = symbol_table_create();
    symbol_table_add_include_table(result, parent_table, true, access_level, 0);
    result->operator_context = parent_table->operator_context;
    return result;
}

void symbol_table_destroy(Symbol_Table* symbol_table)
{
    for (auto iter = hashtable_iterator_create(&symbol_table->symbols); hashtable_iterator_has_next(&iter); hashtable_iterator_next(&iter)) {
        // Note: Symbols are allocated elsewhere...
        dynamic_array_destroy(iter.value);
    }
    hashtable_destroy(&symbol_table->symbols);
	dynamic_array_destroy(&symbol_table->included_tables);
    delete symbol_table;
}

void symbol_table_add_include_table(
    Symbol_Table* symbol_table, Symbol_Table* included_table, bool transitive, Symbol_Access_Level access_level, AST::Node* error_report_node)
{
    // Check for errors
    if (symbol_table == included_table) {
        log_semantic_error("Trying to include symbol table to itself!", error_report_node);
        return;
    }
    for (int i = 0; i < symbol_table->included_tables.size; i++) {
        auto include = symbol_table->included_tables[i];
        if (include.table == included_table) {
            log_semantic_error("Table is already included!", error_report_node);
            return;
        }
    }

    // Add include
    Included_Table included;
    included.access_level = access_level;
    included.transitive = transitive;
    included.table = included_table;
    dynamic_array_push_back(&symbol_table->included_tables, included);
}

void symbol_destroy(Symbol* symbol) {
    dynamic_array_destroy(&symbol->references);
    delete symbol;
}

Symbol* symbol_table_define_symbol(Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, Symbol_Access_Level access_level)
{
    assert(id != 0, "HEY");

    // Create new symbol
    Symbol* new_sym = new Symbol;
    dynamic_array_push_back(&compiler.analysis_data->allocated_symbols, new_sym);
    new_sym->id = id;
    new_sym->type = type;
    new_sym->origin_table = symbol_table;
    new_sym->access_level = access_level;
    new_sym->references = dynamic_array_create<AST::Symbol_Lookup*>();

    new_sym->definition_node = definition_node;
    if (definition_node != nullptr) {
        new_sym->definition_unit = compiler_find_ast_compilation_unit(new_sym->definition_node);
        new_sym->definition_text_index = token_index_to_text_index(definition_node->range.start, new_sym->definition_unit->code, true);
    }
    else {
        new_sym->definition_unit = nullptr;
        new_sym->definition_text_index = text_index_make(0, 0);
    }

    // Check if symbol is already defined
    bool add_to_symbol_table = true;
    Dynamic_Array<Symbol*>* symbols = hashtable_find_element(&symbol_table->symbols, id);
    if (symbols == 0) {
        Dynamic_Array<Symbol*> new_symbols = dynamic_array_create<Symbol*>(1);
        hashtable_insert_element(&symbol_table->symbols, id, new_symbols);
        symbols = hashtable_find_element(&symbol_table->symbols, id);
        assert(symbols != 0, "Just inserted!");
    }
    else {
        // Overloading is only allowed for functions or a type + module combo
		// Note that it may be possible to break this currently using Import or Alias symbols
		//		which may affect the way errors are reported (TODO: Think about this)
        auto symbol_disallows_overload = [&](Symbol* symbol) -> bool {
            return symbol->type == Symbol_Type::VARIABLE || 
                symbol->type == Symbol_Type::VARIABLE_UNDEFINED || 
                symbol->type == Symbol_Type::PARAMETER ||
                symbol->type == Symbol_Type::GLOBAL;
        };

        // Overloading is allowed for all types, except for variables
        bool overload_valid = true;
		if (symbol_disallows_overload(new_sym)) {
			overload_valid = false;
		}
		for (int i = 0; i < symbols->size; i++) {
			auto other = (*symbols)[i];
			if (symbol_disallows_overload(other)) {
				overload_valid = false;
				break;
			}
		}

		if (!overload_valid) {
			// Note: Here we still return a new symbol, but this symbol can never be referenced, because it isn't added in the symbol table
			log_semantic_error("Symbol already defined in this scope", definition_node);
			new_sym->id = compiler.identifier_pool.predefined_ids.invalid_symbol_name;
			return new_sym;
		}
	}

	// Add to symbol_table
	dynamic_array_push_back(symbols, new_sym);
	return new_sym;
}


void symbol_table_query_id_recursive(
	Symbol_Table* table, String* id, bool search_includes, Symbol_Access_Level access_level, Dynamic_Array<Symbol*>* results, Hashset<Symbol_Table*>* already_visited
)
{
	// Check if already visited
	if (hashset_contains(already_visited, table)) {
		return;
	}
	hashset_insert_element(already_visited, table);

	// Check if all symbols should be added (If id parameter == 0)
	bool stop_further_lookup = false;
	if (id != 0) {
		// Try to find symbol
		Dynamic_Array<Symbol*>* symbols = hashtable_find_element(&table->symbols, id);
		if (symbols != 0) {
			for (int i = 0; i < symbols->size; i++) {
				Symbol* symbol = (*symbols)[i];
				if ((int)symbol->access_level <= (int)access_level) {
					dynamic_array_push_back(results, symbol);
					if (symbol->access_level == Symbol_Access_Level::INTERNAL) {
						// Once a internal symbol is found (variable/parameter), stop the search. 
						// Update: Why is this the case? Probably because overloading for variables isn't available yet?
						stop_further_lookup = true;
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
			(Symbol_Access_Level)(math_minimum((int)access_level, (int)included.access_level)),
			results,
			already_visited
		);
	}
}

void symbol_table_query_id(
	Symbol_Table* table, String* id, bool search_includes, Symbol_Access_Level access_level, Dynamic_Array<Symbol*>* results, Hashset<Symbol_Table*>* already_visited)
{
	hashset_reset(already_visited);
	return symbol_table_query_id_recursive(table, id, search_includes, access_level, results, already_visited);
}

void symbol_type_append_to_string(Symbol_Type type, String* string)
{
	switch (type)
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
	case Symbol_Type::POLYMORPHIC_VALUE:
		string_append_formated(string, "Polymorphic value");
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
		string_append_formated(string, "Constant");
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

void symbol_append_to_string(Symbol* symbol, String* string)
{
	string_append_formated(string, "%s ", symbol->id->characters);
	symbol_type_append_to_string(symbol->type, string);
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
	//     symbol_table_append_to_string_with_parent_info(c_string, table->parent, true, print_root);
	// }
}

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root) {
	symbol_table_append_to_string_with_parent_info(string, table, false, print_root);
}
