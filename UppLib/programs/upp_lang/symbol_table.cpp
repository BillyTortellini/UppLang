#include "symbol_table.hpp"

#include "compiler_misc.hpp"
#include "compiler.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "ast.hpp"
#include "editor_analysis_info.hpp"

// SYMBOL TABLE FUNCTIONS
Symbol_Table* symbol_table_create(Compilation_Data* compilation_data)
{
    Symbol_Table* result = new Symbol_Table;
    result->custom_operator_table = nullptr;
	result->parent_table = nullptr;
	result->parent_access_level = Symbol_Access_Level::GLOBAL;
    dynamic_array_push_back(&compilation_data->allocated_symbol_tables, result);
    result->imports = dynamic_array_create<Symbol_Table_Import>();
    result->symbols = hashtable_create_pointer_empty<String*, Dynamic_Array<Symbol*>>(1);
    return result;
}

Symbol_Table* symbol_table_create_with_parent(Symbol_Table* parent_table, Symbol_Access_Level parent_access_level, Compilation_Data* compilation_data)
{
    Symbol_Table* result = symbol_table_create(compilation_data);
	result->parent_table = parent_table;
	result->parent_access_level = parent_access_level;
    result->custom_operator_table = parent_table->custom_operator_table;
    return result;
}

void symbol_table_destroy(Symbol_Table* symbol_table)
{
    for (auto iter = hashtable_iterator_create(&symbol_table->symbols); hashtable_iterator_has_next(&iter); hashtable_iterator_next(&iter)) {
        // Note: Symbols are allocated elsewhere...
        dynamic_array_destroy(iter.value);
    }
    hashtable_destroy(&symbol_table->symbols);
	dynamic_array_destroy(&symbol_table->imports);
    delete symbol_table;
}

void symbol_table_add_import(
    Symbol_Table* symbol_table, Symbol_Table* imported_table, 
	Import_Type import_type, bool is_transitive, Symbol_Access_Level access_level, Semantic_Context* semantic_context,
    AST::Node* error_report_node, Node_Section error_report_section
)
{
	assert(import_type != Import_Type::NONE, "None should only be used for lookups!");

    // Check for errors
    if (symbol_table == imported_table) {
        log_semantic_error(semantic_context, "Trying to include symbol table to itself!", error_report_node, Node_Section::FIRST_TOKEN);
        return;
    }
    for (int i = 0; i < symbol_table->imports.size; i++) {
        auto& import = symbol_table->imports[i];
        if (import.table == imported_table) 
		{
			if (import.type == import_type) {
				log_semantic_error(semantic_context, "Table is already included!", error_report_node, Node_Section::FIRST_TOKEN);
				return;
			}
        }
    }

    // Add include
	Symbol_Table_Import table_import;
	table_import.table = imported_table;
	table_import.type = import_type;
	table_import.access_level = access_level;
	table_import.is_transitive = is_transitive;
    dynamic_array_push_back(&symbol_table->imports, table_import);
}

void symbol_destroy(Symbol* symbol) {
    dynamic_array_destroy(&symbol->references);
    delete symbol;
}

Symbol* symbol_table_define_symbol(
	Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Node* definition_node, Symbol_Access_Level access_level,
	Compilation_Data* compilation_data)
{
    assert(id != 0, "HEY");

    // Create new symbol
    Symbol* new_sym = new Symbol;
    dynamic_array_push_back(&compilation_data->allocated_symbols, new_sym);
    new_sym->id = id;
    new_sym->type = type;
    new_sym->origin_table = symbol_table;
    new_sym->access_level = access_level;
    new_sym->references = dynamic_array_create<AST::Symbol_Lookup*>();

    new_sym->definition_node = definition_node;
    if (definition_node != nullptr) {
        new_sym->definition_unit = compiler_find_ast_compilation_unit(compilation_data->compiler, new_sym->definition_node);
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

Symbol_Query_Info symbol_query_info_make(
    Symbol_Access_Level access_level, Import_Type import_search_type, bool search_parents
)
{
	Symbol_Query_Info result;
	result.access_level = access_level;
	result.import_search_type = import_search_type;
	result.search_parents = search_parents;
	return result;
}

void symbol_table_find_all_reachable_tables_recursive(
	Symbol_Table* symbol_table, Symbol_Query_Info query_info, DynArray<Reachable_Table>& reachable_tables, int depth)
{
	const bool search_imports = (query_info.import_search_type != Import_Type::NONE);

	// Check if already visited
	bool create_new = true;
	for (int i = 0; i < reachable_tables.size; i++)
	{
		auto& reachable_table = reachable_tables[i];
		if (reachable_table.table != symbol_table) continue;
		create_new = false;

		const bool import_search_improved = search_imports && !reachable_table.search_imports;
		const bool parent_search_improved = query_info.search_parents && !reachable_table.search_parents;
		const bool access_level_improved  = (int)query_info.access_level > (int)reachable_table.access_level;

		reachable_table.access_level       = math_maximum(query_info.access_level, reachable_table.access_level);
		reachable_table.search_imports     = reachable_table.search_imports || search_imports;
		reachable_table.search_parents     = reachable_table.search_parents || query_info.search_parents;
		reachable_table.depth              = math_minimum(depth, reachable_table.depth);

		// Return if we already had the same (or stronger) lookup
		if (!import_search_improved && !parent_search_improved && !access_level_improved) {
			return;
		}
		break;
	}

	// Create new reachable_table if not already visited 
	if (create_new) 
	{
		Reachable_Table table;
		table.access_level = query_info.access_level;
		table.search_imports = search_imports;
		table.search_parents = query_info.search_parents;
		table.depth = depth;
		table.table = symbol_table;
		reachable_tables.push_back(table);
	}

	if (query_info.search_parents && symbol_table->parent_table != nullptr) {
		Symbol_Access_Level new_level = math_minimum(query_info.access_level, symbol_table->parent_access_level);
		Symbol_Query_Info new_query = symbol_query_info_make(
			new_level, query_info.import_search_type, true
		);
		symbol_table_find_all_reachable_tables_recursive(symbol_table->parent_table, new_query, reachable_tables, depth + 1);
	}

	// Search imported tables
	if (!search_imports) return;
	for (int i = 0; i < symbol_table->imports.size; i++)
	{
		Symbol_Table_Import& import  = symbol_table->imports[i];
		// Skip other types
		if (import.type != query_info.import_search_type) {
			// Special exception: Import_Type::DOT_CALL also queries normal imports
			if (!(import.type == Import_Type::SYMBOLS && query_info.import_search_type == Import_Type::DOT_CALLS)) {
				continue;
			}
		}

		Symbol_Access_Level next_level = math_minimum(query_info.access_level, import.access_level);
		Import_Type next_type = query_info.import_search_type;
		if (!import.is_transitive) {
			next_type = Import_Type::NONE;
		}
		Symbol_Query_Info new_query_info = symbol_query_info_make(next_level, next_type, false);
		symbol_table_find_all_reachable_tables_recursive(import.table, new_query_info, reachable_tables, depth + 1);
	}
}

DynArray<Reachable_Table> symbol_table_query_all_reachable_tables(Symbol_Table* symbol_table, Symbol_Query_Info query_info, Arena* arena)
{
	DynArray<Reachable_Table> reachable_tables = DynArray<Reachable_Table>::create(arena);
	symbol_table_find_all_reachable_tables_recursive(symbol_table, query_info, reachable_tables, 0);
	return reachable_tables;
}

DynArray<Symbol*> symbol_table_query_id(Symbol_Table* symbol_table, String* id, Symbol_Query_Info query_info, Arena* arena)
{
	// Find all tables we are searching through
	DynArray<Reachable_Table> query_tables = symbol_table_query_all_reachable_tables(symbol_table, query_info, arena);
	DynArray<Symbol*> results = DynArray<Symbol*>::create(arena);

	// Add all symbols with the given id
	// Note: Internal symbols have priority over non-internal symbols (Parameters and variables shadow other symbols)
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

DynArray<Symbol*> symbol_table_query_all_symbols(Symbol_Table* symbol_table, Symbol_Query_Info query_info, Arena* arena)
{
	// Find all tables we are searching through
	DynArray<Reachable_Table> query_tables = symbol_table_query_all_reachable_tables(symbol_table, query_info, arena);
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



// CUSTOM OPERATORS
u64 hash_custom_operator(Custom_Operator* op)
{
	int type = (int)op->type;
	u64 hash = hash_i32(&type);
	switch (op->type)
	{
	case Custom_Operator_Type::CAST: 
	{
		auto& cast = op->options.custom_cast;
		hash = hash_combine(hash, hash_pointer(cast.function));
		hash = hash_bool(hash, cast.auto_cast);
		hash = hash_bool(hash, cast.call_by_reference);
		hash = hash_bool(hash, cast.return_by_reference);
		break;
	}
	}
	return hash;
}

bool equals_custom_operator(Custom_Operator* a_op, Custom_Operator* b_op)
{
	if (a_op->type != b_op->type) return false;

	switch (a_op->type)
	{
	case Custom_Operator_Type::CAST: 
	{
		auto& a = a_op->options.custom_cast;
		auto& b = b_op->options.custom_cast;
		return
			a.function == b.function &&
			a.call_by_reference == b.call_by_reference &&
			a.return_by_reference == b.return_by_reference &&
			a.auto_cast == b.auto_cast;
	}
	}

	return true;
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
