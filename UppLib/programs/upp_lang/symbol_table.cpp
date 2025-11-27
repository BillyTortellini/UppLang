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
    symbol_table_add_include_table(result, parent_table, Include_Type::PARENT, access_level, nullptr, Node_Section::FIRST_TOKEN);
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
    Symbol_Table* symbol_table, Symbol_Table* included_table, Include_Type include_type, Symbol_Access_Level access_level, 
    AST::Node* error_report_node, Node_Section error_report_section)
{
    // Check for errors
    if (symbol_table == included_table) {
        log_semantic_error_outside("Trying to include symbol table to itself!", error_report_node, Node_Section::FIRST_TOKEN);
        return;
    }
    for (int i = 0; i < symbol_table->included_tables.size; i++) {
        auto include = symbol_table->included_tables[i];
        if (include.table == included_table) {
            log_semantic_error_outside("Table is already included!", error_report_node, Node_Section::FIRST_TOKEN);
            return;
        }
    }

    // Add include
    Included_Table included;
    included.access_level = access_level;
    included.include_type = include_type;
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

	// Add to symbol_table
	dynamic_array_push_back(symbols, new_sym);
	return new_sym;
}

struct Query_Table
{
	Symbol_Table* table;
	Lookup_Type lookup_type;
	Symbol_Access_Level access_level;
	int depth; // How many includes were traversed to find this query-table
};

void find_all_query_tables_recursive(
	Symbol_Table* symbol_table, Lookup_Type lookup_type, Symbol_Access_Level access_level, DynArray<Query_Table>& query_tables, int depth)
{
	// Check if already visited
	bool create_new = true;
	for (int i = 0; i < query_tables.size; i++) 
	{
		auto& query_table = query_tables[i];
		if (query_table.table != symbol_table) continue;
		create_new = false;

		query_table.access_level = (Symbol_Access_Level)math_maximum((int)access_level, (int)query_table.access_level);
		query_table.lookup_type  = (Lookup_Type)math_maximum((int)lookup_type, (int)query_table.lookup_type);
		query_table.depth        = math_minimum(depth, query_table.depth);

		// Return if we already had the same (or stronger) lookup
		const bool access_level_increased = (int)access_level > (int)query_table.access_level;
		const bool lookup_level_increased = (int)lookup_type > (int)query_table.lookup_type;
		if (!access_level_increased && !lookup_level_increased) {
			return;
		}
		break;
	}

	// Create new query_table if not already visited 
	if (create_new) 
	{
		Query_Table table;
		table.access_level = access_level;
		table.lookup_type = lookup_type;
		table.depth = depth;
		table.table = symbol_table;
		query_tables.push_back(table);
	}

	// Recurse to include tables
	if (lookup_type == Lookup_Type::LOCAL_SEARCH) return;
	for (int i = 0; i < symbol_table->included_tables.size; i++)
	{
		Included_Table& included = symbol_table->included_tables[i];
		if (lookup_type == Lookup_Type::SEARCH_PARENT && included.include_type != Include_Type::PARENT) continue;
		if (included.include_type == Include_Type::DOT_CALL_INCLUDE && lookup_type != Lookup_Type::DOT_CALL_LOOKUP) continue;

		Lookup_Type next_lookup_type = lookup_type;
		if (included.include_type == Include_Type::NORMAL || included.include_type == Include_Type::DOT_CALL_INCLUDE) {
			next_lookup_type = Lookup_Type::LOCAL_SEARCH;
		}
		find_all_query_tables_recursive(
			included.table, next_lookup_type, math_minimum(included.access_level, access_level), query_tables, depth + 1
		);
	}
}

DynArray<Symbol*> symbol_table_query_id(
	Symbol_Table* symbol_table, String* id, Lookup_Type lookup_type, Symbol_Access_Level access_level, Arena* arena)
{
	assert(id != nullptr, "We added another function to query all symbols");

	// Find all tables we are searching through
	DynArray<Query_Table> query_tables = DynArray<Query_Table>::create(arena);
	find_all_query_tables_recursive(symbol_table, lookup_type, access_level, query_tables, 0);

	// Add all symbols with the given id
	// Note: Internal symbols have priority over non-internal symbols (Parameters and variables shadow other symbols)
	DynArray<Symbol*> results = DynArray<Symbol*>::create(arena);
	bool found_internal = false;
	int min_internal_depth = INT_MAX;
	for (int i = 0; i < query_tables.size; i++)
	{
		auto& query_table = query_tables[i];
		// Try to find symbol
		Dynamic_Array<Symbol*>* symbols = hashtable_find_element(&query_table.table->symbols, id);
		if (symbols == nullptr) continue;
		for (int i = 0; i < symbols->size; i++) 
		{
			Symbol* symbol = (*symbols)[i];
			if (((int)symbol->access_level > (int)query_table.access_level)) continue;

			const bool is_internal = symbol->access_level == Symbol_Access_Level::INTERNAL;
			const int depth = query_table.depth;
			if (found_internal) 
			{
				if (!is_internal) continue;
				if (depth > min_internal_depth) continue;
				else if (depth < min_internal_depth) {
					min_internal_depth = depth;
					results.reset();
				}
			}
			else if (is_internal)
			{
				found_internal = true;
				results.reset();
				min_internal_depth = depth;
			}

			results.push_back(symbol);
		}
	}

	return results;
}

DynArray<Symbol*> symbol_table_query_all_symbols(
	Symbol_Table* symbol_table, Lookup_Type lookup_type, Symbol_Access_Level access_level, Arena* arena)
{
	DynArray<Query_Table> query_tables = DynArray<Query_Table>::create(arena);
	find_all_query_tables_recursive(symbol_table, lookup_type, access_level, query_tables, 0);
	DynArray<Symbol*> results = DynArray<Symbol*>::create(arena);
	for (int i = 0; i < query_tables.size; i++)
	{
		auto& query_table = query_tables[i];
		for (auto iter = hashtable_iterator_create(&query_table.table->symbols); hashtable_iterator_has_next(&iter); hashtable_iterator_next(&iter))
		{
			Dynamic_Array<Symbol*>* symbols = iter.value;
			for (int j = 0; j < symbols->size; j++) {
				Symbol* symbol = (*symbols)[j];
				if ((int)symbol->access_level > (int)query_table.access_level) continue;
				results.push_back(symbol);
			}
		}
	}
	return results;
}

void symbol_table_query_resolve_aliases(DynArray<Symbol*>& symbols)
{
	// Resolve aliases
	for (int i = 0; i < symbols.size; i++) 
	{
		Symbol* symbol = symbols[i];

		// Note: Unresolved aliases are removed. 
		// This special code-path is required during module-analysis, where aliases are not yet analysed
		// Specifically, this happens during module-imports, e.g. import Foo~* as something
		if (symbol->type == Symbol_Type::ALIAS_UNFINISHED) {
			symbols.swap_remove(i);
			i -= 1;
			continue;
		}

		if (symbol->type != Symbol_Type::ALIAS) continue;

		// Find what the symbol aliases (Aliases can be chained!)
		int count = 0;
		while (symbol->type == Symbol_Type::ALIAS) {
			symbol = symbol->options.alias_for;
			count += 1;
			assert(count < 300, "I assume this is a alias circular dependency, which shouldn't happen");
		}

		// Check if the alias is already in symbols array
		bool already_contained = false;
		for (int j = 0; j < symbols.size; j++) {
			auto& other = symbols[j];
			if (symbol == other) {
				already_contained = true;
				break;
			}
		}

		// Update symbols array
		if (already_contained) {
			symbols.swap_remove(i);
			i -= 1;
		}
		else {
			symbols[i] = symbol;
		}
	}
}




// PRINTING
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
	case Symbol_Type::PATTERN_VARIABLE:
		string_append_formated(string, "Pattern value");
		break;
	case Symbol_Type::ALIAS_UNFINISHED:
		string_append_formated(string, "Alias not yet defined");
		break;
	case Symbol_Type::ALIAS:
		string_append_formated(string, "Alias or imported symbol");
		break;
	case Symbol_Type::VARIABLE:
		string_append_formated(string, "Variable");
		break;
	case Symbol_Type::GLOBAL:
		string_append_formated(string, "Global");
		break;
	case Symbol_Type::DATATYPE:
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
