#include "compilation_data.hpp"

#include "constant_pool.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "source_code.hpp"
#include "semantic_analyser.hpp"
#include "syntax_colors.hpp"
#include "../../win32/timing.hpp"
#include "../../utility/rich_text.hpp"
#include "ir_code.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "c_backend.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/character_info.hpp"
#include "../../utility/directory_crawler.hpp"



// Parser stages
bool enable_lexing = true;
bool enable_parsing = true;
bool enable_analysis = true;
bool enable_ir_gen = true;
bool enable_bytecode_gen = true;
bool enable_c_generation = false;
bool enable_c_compilation = true;

// Output stages
bool output_identifiers = false;
bool output_ast = true;
bool output_type_system = false;
bool output_root_table = false;
bool output_ir = true;
bool output_bytecode = true;
bool output_timing = true;

// Testcases
bool enable_testcases = false;
bool enable_stresstest = false;
bool run_testcases_compiled = false;

// Execution
bool enable_output = true;
bool output_only_on_code_gen = false;
bool enable_execution = true;
bool execute_binary = false;



// HELPERS
struct Semantic_Info_Comparator
{
	bool operator()(const Editor_Info& a, const Editor_Info& b)
	{
		if (a.analysis_item_index == b.analysis_item_index) {
			return a.pass < b.pass;
		}
		return a.analysis_item_index < b.analysis_item_index;
	}
};

u64 ast_info_key_hash(AST_Info_Key* key) {
	return hash_combine(hash_pointer(key->base), hash_pointer(key->pass));
}

bool ast_info_equals(AST_Info_Key* a, AST_Info_Key* b) {
	return a->base == b->base && a->pass == b->pass;
}

void compilation_unit_parse_ast(Compilation_Unit* unit, Compilation_Data* compilation_data)
{
    Timing_Task before = compilation_data->task_current;
    SCOPE_EXIT(compilation_data_switch_timing_task(compilation_data, before));

	// Already parsed
	if (unit->root != nullptr) {
		return;
	}

    unit->root = nullptr;
    if (!enable_parsing) {
        return;
    }

    // Parse code
    compilation_data_switch_timing_task(compilation_data, Timing_Task::PARSING);
    Parser::execute_clean(unit, &compilation_data->identifier_pool, &compilation_data->arena, &compilation_data->tmp_arena);
}

void call_signature_destroy(Call_Signature* signature)
{
	dynamic_array_destroy(&signature->parameters);
	delete signature;
}

u64 hash_call_signature(Call_Signature** callable_p)
{
	Call_Signature* signature = *callable_p;
	u64 hash = hash_i32(&signature->return_type_index);
	hash = hash_combine(hash, hash_i32(&signature->parameters.size));
	for (int i = 0; i < signature->parameters.size; i++)
	{
		auto& param = signature->parameters[i];
		hash = hash_combine(hash, hash_pointer(param.datatype));
		hash = hash_combine(hash, hash_pointer(param.name));
		hash = hash_bool(hash, param.required);
		hash = hash_bool(hash, param.requires_named_addressing);
		hash = hash_bool(hash, param.must_not_be_set);
		hash = hash_combine(hash, hash_i32(&param.pattern_variable_index));
	}
	return hash;
}

bool equals_call_signature(Call_Signature** app, Call_Signature** bpp)
{
	Call_Signature* a = *app;
	Call_Signature* b = *bpp;

	if (a->return_type_index != b->return_type_index) return false;
	if (a->parameters.size != b->parameters.size) return false;
	for (int i = 0; i < a->parameters.size; i++)
	{
		auto& pa = a->parameters[i];
		auto& pb = b->parameters[i];

		if (!types_are_equal(pa.datatype, pb.datatype)) return false;

		if (pa.name != pb.name || pa.required != pb.required ||
			pa.requires_named_addressing != pb.requires_named_addressing ||
			pa.must_not_be_set != pb.must_not_be_set ||
			pa.pattern_variable_index != pb.pattern_variable_index) return false;
	}

	return true;
}

Text_Range text_index_to_word_range(Text_Index pos, Source_Code* code)
{
    auto line = source_code_get_line(code, pos.line);
	Text_Range error_range = text_range_make(pos, pos);
	if (line == nullptr) return error_range;
    auto text = line->text;
	if (pos.character >= text.size) return error_range;
	if (!char_is_valid_identifier(text[pos.character])) return error_range;

	int start_index = pos.character;
	int end_index = pos.character;

	while (start_index > 0)
	{
		char prev = text[start_index - 1];
		if (!char_is_valid_identifier(prev)) {
			break;
		}
		start_index -= 1;
	}

	while (end_index < text.size)
	{
		char curr = text[end_index];
		if (!char_is_valid_identifier(curr)) {
			break;
		}
		end_index += 1;
	}

	return text_range_make(text_index_make(pos.line, start_index), text_index_make(pos.line, end_index));
}







// COMPILATION_DATA
Compilation_Data* compilation_data_create(Fiber_Pool* fiber_pool)
{
	Compilation_Data* result = new Compilation_Data;
	result->fiber_pool = fiber_pool;

	// Create datastructures
	{
		result->compiler_errors = dynamic_array_create<Compiler_Error_Info>(); // List of parser and semantic errors
		result->identifier_pool = identifier_pool_create();

		// Initialize Data
		result->semantic_errors = dynamic_array_create<Semantic_Error>();

		result->ast_to_pass_mapping = hashtable_create_pointer_empty<AST::Node*, Node_Passes>(16);
		result->ast_to_info_mapping = hashtable_create_empty<AST_Info_Key, Analysis_Info*>(16, ast_info_key_hash, ast_info_equals);

		result->error_symbol = nullptr; // Initialized after this block
		result->code_block_comptimes = hashtable_create_pointer_empty<AST::Code_Block*, Symbol_Table*>(1);
		result->functions = dynamic_array_create<Upp_Function*>();
		result->globals = dynamic_array_create<Upp_Global*>();

		// Allocations
		result->compilation_units = dynamic_array_create<Compilation_Unit*>();
		result->arena = Arena::create(2048);
		result->tmp_arena = Arena::create(2048);
		result->allocated_symbol_tables = dynamic_array_create<Symbol_Table*>();
		result->allocated_symbols = dynamic_array_create<Symbol*>();
		result->allocated_passes = dynamic_array_create<Analysis_Pass*>();
		result->allocated_custom_operator_tables = dynamic_array_create<Custom_Operator_Table*>();
		result->call_signatures = hashset_create_empty<Call_Signature*>(0, hash_call_signature, equals_call_signature);
		result->custom_operator_deduplication = hashtable_create_empty<Custom_Operator, Custom_Operator*>(16, hash_custom_operator, equals_custom_operator);
		result->bytecode = DynArray<Bytecode_Instruction>::create(&result->arena);

		result->semantic_infos = dynamic_array_create<Editor_Info>();
		result->next_analysis_item_index = 0;

		// Initialize stages
		result->constant_pool = constant_pool_create(result);
		result->type_system = type_system_create(result);
		result->extern_sources = extern_sources_create();
		result->workload_executer = workload_executer_create(result);
		result->c_generator = c_generator_create(result);
		c_compiler_initialize(); // Initializiation is cached, so calling this multiple times doesn't matter
	}

	// Initialize default objects
	{
		Compilation_Data* compilation_data = result;
		auto& type_system = compilation_data->type_system;
		auto& types = type_system->predefined_types;
		auto identifier_pool = &compilation_data->identifier_pool;
		Identifier_Pool* id_pool = identifier_pool;

		// Create root tables and predefined Symbols 
		{
			// Create root table and root table symbols
			{
				auto root_table = symbol_table_create(compilation_data);
				compilation_data->root_symbol_table = root_table;

				// Define int, float, bool, string
				Symbol* result = symbol_table_define_symbol(
					root_table, identifier_pool_add(id_pool, string_create_static("int")), Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.datatype = upcast(types.i32_type);

				result = symbol_table_define_symbol(
					root_table, identifier_pool_add(id_pool, string_create_static("float")), Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.datatype = upcast(types.f32_type);

				result = symbol_table_define_symbol(
					root_table, identifier_pool_add(id_pool, string_create_static("bool")), Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.datatype = upcast(types.bool_type);

				result = symbol_table_define_symbol(
					root_table, identifier_pool_add(id_pool, string_create_static("string")), Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.datatype = upcast(types.string);
			}

			// Create builtin-module
			{
				Symbol_Table* builtin_table = symbol_table_create(compilation_data);
				Symbol* builtin_symbol = symbol_table_define_symbol(
					builtin_table, identifier_pool_add(id_pool, string_create_static("_BUILTIN_MODULE_")), 
					Symbol_Type::MODULE, nullptr, Symbol_Access_Level::GLOBAL,
					compilation_data
				);

				Upp_Module* builtin_module = compilation_data->arena.allocate<Upp_Module>();
				builtin_module->node = nullptr;
				builtin_module->is_file_module = false;
				builtin_module->options.module_symbol = builtin_symbol;
				builtin_module->symbol_table = builtin_table;

				builtin_symbol->options.upp_module = builtin_module;
				compilation_data->builtin_module = builtin_module;
			}

			// Create symbols in builtin table
			Symbol_Table* builtin_table = compilation_data->builtin_module->symbol_table;
			auto define_type_symbol = [&](const char* name, Datatype* type) -> Symbol* {
				Symbol* result = symbol_table_define_symbol(
					builtin_table, 
					identifier_pool_add(id_pool, string_create_static(name)), 
					Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL, compilation_data
				);
				result->options.datatype = type;
				return result;
			};

			define_type_symbol("c_char", upcast(types.c_char));
			define_type_symbol("c_string", upcast(types.c_string));
			define_type_symbol("u8", upcast(types.u8_type));
			define_type_symbol("u16", upcast(types.u16_type));
			define_type_symbol("u32", upcast(types.u32_type));
			define_type_symbol("u64", upcast(types.u64_type));
			define_type_symbol("i8", upcast(types.i8_type));
			define_type_symbol("i16", upcast(types.i16_type));
			define_type_symbol("i32", upcast(types.i32_type));
			define_type_symbol("i64", upcast(types.i64_type));
			define_type_symbol("f32", upcast(types.f32_type));
			define_type_symbol("f64", upcast(types.f64_type));
			define_type_symbol("string", types.string);
			define_type_symbol("Allocator", upcast(types.allocator));
			define_type_symbol("Type_Handle", types.type_handle);
			define_type_symbol("Type_Info", upcast(types.type_information_type));
			define_type_symbol("Any", upcast(types.any_type));
			define_type_symbol("_", upcast(types.empty_struct_type));
			define_type_symbol("address", upcast(types.address));
			define_type_symbol("isize", upcast(types.isize));
			define_type_symbol("usize", upcast(types.usize));
			define_type_symbol("bytes", upcast(types.bytes));

			auto define_hardcoded_symbol = [&](const char* name, Hardcoded_Type type) -> Symbol* {
				Symbol* result = symbol_table_define_symbol(
					builtin_table, identifier_pool_add(id_pool, string_create_static(name)), 
					Symbol_Type::HARDCODED_FUNCTION, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.hardcoded = type;
				return result;
			};
			define_hardcoded_symbol("print_bool", Hardcoded_Type::PRINT_BOOL);
			define_hardcoded_symbol("print_i32", Hardcoded_Type::PRINT_I32);
			define_hardcoded_symbol("print_f32", Hardcoded_Type::PRINT_F32);
			define_hardcoded_symbol("print_string", Hardcoded_Type::PRINT_STRING);
			define_hardcoded_symbol("print_line", Hardcoded_Type::PRINT_LINE);
			define_hardcoded_symbol("read_i32", Hardcoded_Type::PRINT_I32);
			define_hardcoded_symbol("read_f32", Hardcoded_Type::READ_F32);
			define_hardcoded_symbol("read_bool", Hardcoded_Type::READ_BOOL);
			define_hardcoded_symbol("type_of", Hardcoded_Type::TYPE_OF);
			define_hardcoded_symbol("type_info", Hardcoded_Type::TYPE_INFO);

			define_hardcoded_symbol("memory_copy", Hardcoded_Type::MEMORY_COPY);
			define_hardcoded_symbol("memory_zero", Hardcoded_Type::MEMORY_ZERO);
			define_hardcoded_symbol("memory_compare", Hardcoded_Type::MEMORY_COMPARE);

			define_hardcoded_symbol("assert", Hardcoded_Type::ASSERT_FN);
			define_hardcoded_symbol("panic", Hardcoded_Type::PANIC_FN);
			define_hardcoded_symbol("size_of", Hardcoded_Type::SIZE_OF);
			define_hardcoded_symbol("align_of", Hardcoded_Type::ALIGN_OF);
			define_hardcoded_symbol("return_type", Hardcoded_Type::RETURN_TYPE);
			define_hardcoded_symbol("struct_tag", Hardcoded_Type::STRUCT_TAG);

			define_hardcoded_symbol("bitwise_not", Hardcoded_Type::BITWISE_NOT);
			define_hardcoded_symbol("bitwise_and", Hardcoded_Type::BITWISE_AND);
			define_hardcoded_symbol("bitwise_or", Hardcoded_Type::BITWISE_OR);
			define_hardcoded_symbol("bitwise_xor", Hardcoded_Type::BITWISE_XOR);
			define_hardcoded_symbol("bitwise_shift_left", Hardcoded_Type::BITWISE_SHIFT_LEFT);
			define_hardcoded_symbol("bitwise_shift_right", Hardcoded_Type::BITWISE_SHIFT_RIGHT);

			// NOTE: Error symbol is required so that unresolved symbol-reads can point to something,
			//       but it shouldn't be possible to reference the error symbol by name, so the 
			//       current little 'hack' is to use the identifier 0_ERROR and because it starts with a 0, it
			//       can never be used as a symbol read. Other approaches would be to have a custom symbol-table for the error symbol
			//       that isn't connected to anything or not adding the error symbol to any table.
			compilation_data->error_symbol = define_type_symbol("0_ERROR_SYMBOL", types.unknown_type);
			compilation_data->error_symbol->type = Symbol_Type::ERROR_SYMBOL;
		}

		// Predefined Callables (Hardcoded and context change)
		{
			auto& hardcoded_signatures = compilation_data->hardcoded_function_signatures;
			auto& context_signatures   = compilation_data->context_change_type_signatures;
			auto& ids = identifier_pool->predefined_ids;
			auto make_id = [&](const char* name) -> String* {
				return identifier_pool_add(id_pool, string_create_static(name));
			};

			Call_Signature* call_signature = call_signature_create_empty();
			compilation_data->empty_call_signature = call_signature_register(call_signature, compilation_data);



			// CAST
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("to"), types.type_handle, false, false, false);
			call_signature_add_parameter(call_signature, make_id("from"), types.type_handle, false, false, false);
			call_signature_add_return_type(call_signature, upcast(types.empty_pattern_variable), compilation_data);
			compilation_data->cast_signature = call_signature;



			// Context functions
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("function"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("auto_cast"), upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("call_by_reference"), upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("return_by_reference"), upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::CAST] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("binop"), upcast(types.string), true, false, false);
			call_signature_add_parameter(call_signature, make_id("function"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("commutative"), upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer_for_left"),  upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer_for_right"), upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::BINOP] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("unop"), upcast(types.string), true, false, false);
			call_signature_add_parameter(call_signature, make_id("function"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer"),  upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::UNOP] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("function"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer"), upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer_for_index"), upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::ARRAY_ACCESS] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("create"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("has_next"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("next"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("get_value"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer"), upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::ITERATOR] = call_signature_register(call_signature, compilation_data);



			// HARDCODED FUNCTIONS
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("condition"), upcast(types.bool_type), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::ASSERT_FN] = call_signature_register(call_signature, compilation_data);

			hardcoded_signatures[(int)Hardcoded_Type::PANIC_FN] = compilation_data->empty_call_signature;

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_return_type(call_signature, types.type_handle, compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::TYPE_OF] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("type"), upcast(types.type_handle), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.usize), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::SIZE_OF] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("type"), upcast(types.type_handle), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.u32_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::ALIGN_OF] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_return_type(call_signature, upcast(types.empty_pattern_variable), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::RETURN_TYPE] = call_signature;

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("type"), upcast(types.type_handle), true, false, false);
			call_signature_add_return_type(call_signature, upcast(type_system_make_pointer(type_system, upcast(types.type_information_type))), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::TYPE_INFO] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.empty_pattern_variable), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::STRUCT_TAG] = call_signature_register(call_signature, compilation_data);



			// Memory functions
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("destination"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("source"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("size"), upcast(types.usize), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::MEMORY_COPY] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("destination"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("size"), upcast(types.usize), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::MEMORY_ZERO] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("a"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("b"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("size"), upcast(types.usize), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.bool_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::MEMORY_COMPARE] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("size"), upcast(types.usize), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.address), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::SYSTEM_ALLOC] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("data"), upcast(types.address), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::SYSTEM_FREE] = call_signature_register(call_signature, compilation_data);


			// Basic IO-Functions
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.bool_type), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_BOOL] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.i32_type), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_I32] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.f32_type), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_F32] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.string), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_STRING] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_LINE] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_return_type(call_signature, upcast(types.i32_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::READ_I32] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_return_type(call_signature, upcast(types.f32_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::READ_F32] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_return_type(call_signature, upcast(types.bool_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::READ_BOOL] = call_signature_register(call_signature, compilation_data);



			// Bitwise functions
			auto bitwise_binop = call_signature_create_empty();
			call_signature_add_parameter(bitwise_binop, make_id("a"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(bitwise_binop, make_id("b"), upcast(types.empty_pattern_variable), true, false, false);
			bitwise_binop = call_signature_register(bitwise_binop, compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_AND] = bitwise_binop;
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_OR] = bitwise_binop;
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_XOR] = bitwise_binop;
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_SHIFT_LEFT] = bitwise_binop;
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_SHIFT_RIGHT] = bitwise_binop;

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.empty_pattern_variable), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_NOT] = call_signature_register(call_signature, compilation_data);
		}
	}

	result->ir_generator = ir_generator_create(result); // Was moved because this requires system allocator and global allocator to be available
	result->root_workload = workload_executer_add_root_workload(result);

	return result;
}

void compilation_data_destroy(Compilation_Data* data)
{
	ir_generator_destroy(data->ir_generator);
	workload_executer_destroy(data->workload_executer);

	dynamic_array_destroy(&data->compiler_errors);
	constant_pool_destroy(data->constant_pool);
	extern_sources_destroy(&data->extern_sources);
	c_generator_destroy(data->c_generator);

	for (int i = 0; i < data->functions.size; i++) {
		Upp_Function* function = data->functions[i];
		if (function->ir_block != nullptr) {
			ir_code_block_destroy(function->ir_block);
			function->ir_block = nullptr;
		}
	}
	dynamic_array_destroy(&data->functions);
	dynamic_array_destroy(&data->globals);

	dynamic_array_destroy(&data->semantic_infos);
	hashtable_destroy(&data->custom_operator_deduplication);
	hashtable_destroy(&data->code_block_comptimes);

	for (int i = 0; i < data->semantic_errors.size; i++) {
		dynamic_array_destroy(&data->semantic_errors[i].information);
	}
	dynamic_array_destroy(&data->semantic_errors);

	{
		auto iter = hashtable_iterator_create(&data->ast_to_info_mapping);
		while (hashtable_iterator_has_next(&iter)) {
			Analysis_Info* info = *iter.value;
			AST_Info_Key* key = iter.key;
			delete info;
			hashtable_iterator_next(&iter);
		}
		hashtable_destroy(&data->ast_to_info_mapping);
	}
	{
		auto iter = hashtable_iterator_create(&data->ast_to_pass_mapping);
		while (hashtable_iterator_has_next(&iter)) {
			Node_Passes* workloads = iter.value;
			dynamic_array_destroy(&workloads->passes);
			hashtable_iterator_next(&iter);
		}
		hashtable_destroy(&data->ast_to_pass_mapping);
	}
	{
		for (int i = 0; i < data->allocated_passes.size; i++) {
			delete data->allocated_passes[i];
		}
		dynamic_array_destroy(&data->allocated_passes);
	}

	for (int i = 0; i < data->allocated_custom_operator_tables.size; i++) {
		auto context = data->allocated_custom_operator_tables[i];
		hashtable_destroy(&context->installed_operators);
		delete data->allocated_custom_operator_tables[i];
	}
	dynamic_array_destroy(&data->allocated_custom_operator_tables);

	data->arena.destroy();
	data->tmp_arena.destroy();

	// Symbol tables + workloads
	for (int i = 0; i < data->allocated_symbol_tables.size; i++) {
		symbol_table_destroy(data->allocated_symbol_tables[i]);
	}
	dynamic_array_destroy(&data->allocated_symbol_tables);

	for (int i = 0; i < data->allocated_symbols.size; i++) {
		symbol_destroy(data->allocated_symbols[i]);
	}
	dynamic_array_destroy(&data->allocated_symbols);

	for (auto iter = data->call_signatures.make_iter(); iter.has_next(); iter.next()) {
		call_signature_destroy(*iter.value);
	}
	hashset_destroy(&data->call_signatures);

	for (int i = 0; i < data->compilation_units.size; i++) {
		Compilation_Unit* unit = data->compilation_units[i];
		if (unit->code != nullptr) {
		    source_code_destroy(unit->code);
		    unit->code = nullptr;
		}
		string_destroy(&unit->filepath);
		delete unit;
	}
	dynamic_array_destroy(&data->compilation_units);

	delete data;
}

Compilation_Unit* compilation_data_add_compilation_unit_unique(Compilation_Data* compilation_data, String filepath, bool load_file_if_new, bool parse_ast)
{
    String full_file_path = string_copy(filepath);
    file_io_relative_to_full_path(&full_file_path);
    SCOPE_EXIT(string_destroy(&full_file_path)); // On success capacity is set to 0, so this won't do anything

	// Check if filename alreay exists
	for (int i = 0; i < compilation_data->compilation_units.size; i++) {
		auto other = compilation_data->compilation_units[i];
		if (string_equals(&other->filepath, &full_file_path)) {
			if (parse_ast) {
				compilation_unit_parse_ast(other, compilation_data);
			}
			return other;
		}
	}

	Source_Code* source_code = nullptr;
	if (load_file_if_new)
	{
		source_code = source_code_load_from_file(full_file_path);
		if (source_code == nullptr) {
		    return nullptr;
		}
	}

	// Otherwise create new compilation-unit
	Compilation_Unit* unit = new Compilation_Unit;
	unit->filepath = full_file_path;
	full_file_path.capacity = 0;
	unit->parser_errors = array_create_empty<Error_Message>();

	unit->code = source_code;
	unit->root = nullptr;
	unit->upp_module = nullptr;
	if (parse_ast) {
		compilation_unit_parse_ast(unit, compilation_data);
	}

	dynamic_array_push_back(&compilation_data->compilation_units, unit);

	return unit;
}

void compilation_data_compile(Compilation_Data* compilation_data, Compilation_Unit* main_unit, Compile_Type compile_type)
{
    fiber_pool_set_current_fiber_to_main(compilation_data->fiber_pool);
    fiber_pool_check_all_handles_completed(compilation_data->fiber_pool);

    compilation_data->main_unit = main_unit;
    compilation_data->compile_type = compile_type;

    // Reset timing data
    {
        compilation_data->time_compile_start = timer_current_time_in_seconds();
        compilation_data->time_analysing = 0;
        compilation_data->time_code_gen = 0;
        compilation_data->time_lexing = 0;
        compilation_data->time_parsing = 0;
        compilation_data->time_reset = 0;
        compilation_data->time_code_exec = 0;
        compilation_data->time_output = 0;
        compilation_data->task_last_start_time = compilation_data->time_compile_start;
        compilation_data->task_current = Timing_Task::FINISH;
        compilation_data_switch_timing_task(compilation_data, Timing_Task::RESET);
    }

    // Parse main unit
    compilation_unit_parse_ast(main_unit, compilation_data);

    Timing_Task before = compilation_data->task_current;
    SCOPE_EXIT(compilation_data_switch_timing_task(compilation_data, before));

    // Semantic_Analysis (Workload Execution)
    compilation_data_switch_timing_task(compilation_data, Timing_Task::ANALYSIS);
    bool do_analysis = enable_lexing && enable_parsing && enable_analysis;
    if (do_analysis) 
    {
        workload_executer_add_module_discovery(main_unit->root, compilation_data);
        workload_executer_resolve(compilation_data->workload_executer, compilation_data);
		semantic_analyser_finish_analysis(compilation_data);
    }

    // Code-Generation (IR-Generator + Interpreter/C-Backend)
    bool error_free = !compilation_data_errors_occured(compilation_data);
    bool generate_code = compilation_data->compile_type == Compile_Type::BUILD_CODE;
    bool do_ir_gen = do_analysis && enable_ir_gen && generate_code && error_free;
    bool do_bytecode_gen = do_ir_gen && enable_bytecode_gen && generate_code && error_free;
    bool do_c_generation = do_ir_gen && enable_c_generation && generate_code && error_free;
    bool do_c_compilation = do_c_generation && enable_c_compilation && generate_code && error_free;
    {
        compilation_data_switch_timing_task(compilation_data, Timing_Task::CODE_GEN);
        if (do_ir_gen) 
        {
            for (int i = 0; i < compilation_data->functions.size; i++) {
                Upp_Function* function = compilation_data->functions[i];
                ir_generator_generate_function(function, compilation_data);
            }
            ir_generator_finish(compilation_data);
        }
        if (do_bytecode_gen) 
        {
            for (int i = 0; i < compilation_data->functions.size; i++) {
                Upp_Function* function = compilation_data->functions[i];
                if (function->ir_block != nullptr) {
                    bytecode_generator_compile_function(compilation_data, function);
                }
            }
        }
        if (do_c_generation) {
            c_generator_generate(compilation_data->c_generator);
        }
        if (do_c_compilation) {
            c_compiler_compile(compilation_data);
        }
    }

    // Output
    {
        compilation_data_switch_timing_task(compilation_data, Timing_Task::OUTPUT);
        if (output_ast) {
            logg("\n");
            logg("--------AST PARSE RESULT--------:\n");
            AST::base_print(upcast(compilation_data->main_unit->root));
        }
        if (compilation_data->compile_type == Compile_Type::BUILD_CODE)
        {
            //logg("\n\n\n\n\n\n\n\n\n\n\n\n--------SOURCE CODE--------: \n%s\n\n", source_code->characters);
            if (do_analysis && output_type_system) {
                logg("\n--------TYPE SYSTEM RESULT--------:\n");
                type_system_print(compilation_data->type_system);
            }

            if (do_analysis && output_root_table)
            {
                logg("\n--------ROOT TABLE RESULT---------\n");
                String root_table = string_create(1024);
                SCOPE_EXIT(string_destroy(&root_table));
                symbol_table_append_to_string(&root_table, compilation_data->root_symbol_table, false);
                logg("%s", root_table.characters);
            }

            if (error_free)
            {
                if (do_ir_gen && output_ir)
                {
                    logg("\n--------IR_PROGRAM---------\n");
                    String tmp = string_create(1024);
                    SCOPE_EXIT(string_destroy(&tmp));
                    ir_program_append_to_string(&tmp, false, compilation_data);
                    string_style_remove_codes(&tmp);
                    logg("%s", tmp.characters);
                }

                if (do_bytecode_gen && output_bytecode)
                {
                    String result_str = string_create(32);
                    SCOPE_EXIT(string_destroy(&result_str));
                    if (do_bytecode_gen && output_bytecode) {
                        bytecode_generator_append_bytecode_to_string(compilation_data, &result_str);
                        logg("\n----------------BYTECODE_GENERATOR RESULT---------------: \n%s\n", result_str.characters);
                    }
                }
            }
        }

        compilation_data_switch_timing_task(compilation_data, Timing_Task::FINISH);
        if (output_timing && generate_code)
        {
            double sum = timer_current_time_in_seconds() - compilation_data->time_compile_start;
            logg("\n-------- TIMINGS ---------\n");
            logg("reset       ... %3.2fms\n", (float)(compilation_data->time_reset) * 1000);
            if (enable_lexing) {
                logg("lexing      ... %3.2fms\n", (float)(compilation_data->time_lexing) * 1000);
            }
            if (enable_parsing) {
                logg("parsing     ... %3.2fms\n", (float)(compilation_data->time_parsing) * 1000);
            }
            if (enable_analysis) {
                logg("analysis    ... %3.2fms\n", (float)(compilation_data->time_analysing) * 1000);
                logg("code_exec   ... %3.2fms\n", (float)(compilation_data->time_code_exec) * 1000);
            }
            if (enable_bytecode_gen) {
                logg("code_gen    ... %3.2fms\n", (float)(compilation_data->time_code_gen) * 1000);
            }
            if (true) {
                logg("output      ... %3.2fms\n", (float)(compilation_data->time_output) * 1000);
            }
            logg("--------------------------\n");
            logg("sum         ... %3.2fms\n", (float)(sum) * 1000);
            logg("--------------------------\n");
        }
    }
}

void add_semantic_info(
	int analysis_item_index, Editor_Info_Type type, Editor_Info_Option option, Analysis_Pass* pass, Compilation_Data* compilation_data)
{
	// Add Editor_Info
	Editor_Info info;
	info.type = type;
	info.options = option;
	info.analysis_item_index = analysis_item_index;
	info.pass = pass;
	dynamic_array_push_back(&compilation_data->semantic_infos, info);
}

int add_code_analysis_item(Text_Range range, Source_Code* code, int tree_depth, Compilation_Data* compilation_data)
{
	int analysis_item_index = compilation_data->next_analysis_item_index;
	compilation_data->next_analysis_item_index += 1;

    for (int i = range.start.line; i <= range.end.line; i++)
    {
        auto line = source_code_get_line(code, i);
        int start_index = range.start.character;
        int end_index = range.end.character;
        if (i != range.start.line) {
            start_index = 0;
        }
        if (i != range.end.line) {
            end_index = line->text.size;
        }

        Editor_Info_Reference result;
        result.start_char = start_index;
        result.end_char = end_index;
        result.tree_depth = tree_depth;
		// These values are calculated afterwards
		result.editor_info_mapping_count = 0;
		result.editor_info_mapping_start_index = -1;
		result.item_index = analysis_item_index;
        dynamic_array_push_back(&line->item_infos, result);
    }

	return analysis_item_index;
};

void add_markup(Text_Range range, Source_Code* code, int tree_depth, Syntax_Color color, Compilation_Data* compilation_data)
{
	int analysis_item_index = add_code_analysis_item(range, code, tree_depth, compilation_data);
	Editor_Info_Option option;
	option.markup_color = color;
	add_semantic_info(analysis_item_index, Editor_Info_Type::MARKUP, option, nullptr, compilation_data);
}

void find_editor_infos_recursive(
	AST::Node* node, Compilation_Unit* unit, DynArray<Analysis_Pass*>& active_passes, int tree_depth, Compilation_Data* compilation_data)
{
	auto code = unit->code;
	auto type_system = compilation_data->type_system;

	// Add additional passes to active-passes array
	auto node_passes_opt = hashtable_find_element(&compilation_data->ast_to_pass_mapping, node);
	int active_pass_count_before = active_passes.size;
	if (node_passes_opt != nullptr) {
		auto new_passes = node_passes_opt->passes;
		for (int i = 0; i < new_passes.size; i++) {
			active_passes.push_back(new_passes[i]);
		}
	}

	switch (node->type)
	{
	case AST::Node_Type::DEFINITION:
	{
		AST::Definition* definition = downcast<AST::Definition>(node);
		if (definition->type != AST::Definition_Type::MODULE) break;
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];

			Symbol_Table* symbol_table = nullptr;
			switch (definition->type)
			{
			case AST::Definition_Type::STRUCT: 
			{
				if (pass->origin_workload->type == Analysis_Workload_Type::STRUCT_BODY) {
					symbol_table = ((Workload_Structure_Body*)(pass->origin_workload))->symbol_table;
				}
				else if (pass->origin_workload->type == Analysis_Workload_Type::STRUCT_HEADER) {
					symbol_table = ((Workload_Structure_Header*)(pass->origin_workload))->symbol_table;
				}
				break;
			}
			case AST::Definition_Type::FUNCTION: 
			{
				if (pass->origin_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
					symbol_table = ((Workload_Function_Header*)(pass->origin_workload))->symbol_table;
				}
				else if (pass->origin_workload->type == Analysis_Workload_Type::FUNCTION_BODY) {
					symbol_table = ((Workload_Function_Body*)(pass->origin_workload))->parameter_table;
				}
				break;
			}
			case AST::Definition_Type::MODULE: 
			{
				auto info = pass_get_node_info(pass, definition, Info_Query::TRY_READ, compilation_data);
				if (info == nullptr) { continue; }
				if (info->upp_module == nullptr) { continue; }
				symbol_table = info->upp_module->symbol_table;
				break;
			}
			default: break;
			}

			if (symbol_table != nullptr)
			{
				Symbol_Table* table = ((Workload_Function_Header*)(pass->origin_workload))->symbol_table;
				Symbol_Table_Range table_range;
				table_range.range = node->bounding_range;
				table_range.symbol_table = table;
				table_range.tree_depth = tree_depth;
				table_range.pass = pass;
				dynamic_array_push_back(&code->symbol_table_ranges, table_range);
			}
		}
		break;
	}
	case AST::Node_Type::CODE_BLOCK:
	{
		auto block_node = AST::downcast<AST::Code_Block>(node);
		if (block_node->block_id.available) {
			Block_ID_Range id_range;
			id_range.range = node->bounding_range;
			id_range.block_id = block_node->block_id.value;
			dynamic_array_push_back(&code->block_id_range, id_range);
		}

		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];
			auto block = pass_get_node_info(pass, block_node, Info_Query::TRY_READ, compilation_data);
			if (block == 0) { continue; }
			Symbol_Table_Range table_range;
			table_range.range = node->bounding_range;
			table_range.symbol_table = block->symbol_table;
			table_range.tree_depth = tree_depth;
			table_range.pass = pass;
			dynamic_array_push_back(&code->symbol_table_ranges, table_range);
		}
		break;
	}
	case AST::Node_Type::EXPRESSION:
	{
		auto expr = downcast<AST::Expression>(node);
		if (expr->type == AST::Expression_Type::AUTO_ENUM) {
			add_markup(node->range, code, tree_depth, Syntax_Color::ENUM_MEMBER, compilation_data);
		}
		if (active_passes.size == 0) break;

		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth, compilation_data);
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];

			Expression_Info* info = pass_get_node_info(pass, expr, Info_Query::TRY_READ, compilation_data);
			if (info == nullptr) continue;
			// Special Case: Member-Accesses should always generate an Expression_Info, so we can still have code-completion even with errors
			if (!info->is_valid && expr->type != AST::Expression_Type::MEMBER_ACCESS) {
				continue;
			}

			Editor_Info_Option option;
			option.expression.expr = expr;
			option.expression.info = info;
			option.expression.is_member_access = false;
			option.expression.analysis_pass = pass;

			if (expr->type == AST::Expression_Type::AUTO_ENUM)
			{
				auto type = expression_info_get_type(info, true, type_system);
				if (type->type == Datatype_Type::ENUM) {
					option.expression.is_member_access = true;
					option.expression.member_access_info.value_type = type;
					option.expression.member_access_info.has_definition = false;
					option.expression.member_access_info.member_definition_unit = nullptr;
					Datatype_Enum* enum_type = downcast<Datatype_Enum>(type);
					if (enum_type->definition_node != nullptr)
					{
						option.expression.member_access_info.has_definition = true;
						option.expression.member_access_info.member_definition_unit = compilation_data_ast_node_to_compilation_unit(
							compilation_data, enum_type->definition_node
						);
						option.expression.member_access_info.definition_index = enum_type->definition_node->range.start;
					}
				}
			}
			else if (expr->type == AST::Expression_Type::MEMBER_ACCESS)
			{
				option.expression.is_member_access = true;
				option.expression.member_access_info.has_definition = false;
				option.expression.member_access_info.value_type = nullptr;

				auto value_info = pass_get_node_info(pass, expr->options.member_access.expr, Info_Query::TRY_READ, compilation_data);
				if (value_info != nullptr && value_info->is_valid) {
					option.expression.member_access_info.value_type = value_info->cast_info.result_type;
					if (value_info->result_type == Expression_Result_Type::DATATYPE) {
						option.expression.member_access_info.value_type = value_info->options.datatype;
					}
				}
				// Note: Info->is_valid does not really mean a lot anymore
				// if (!info->is_valid) break;

				auto& access_info = info->specifics.member_access;
				AST::Node* goto_node = nullptr;
				switch (access_info.type)
				{
				case Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS:
					break;
				case Member_Access_Type::STRUCT_MEMBER_ACCESS:
				{
					if (access_info.options.member.definition_node == nullptr) break;
					goto_node = access_info.options.member.definition_node;
					break;
				}
				case Member_Access_Type::ENUM_MEMBER_ACCESS: {
					Datatype* type = option.expression.member_access_info.value_type;
					if (type == nullptr) break;
					if (type->type != Datatype_Type::ENUM) break;
					auto enum_type = downcast<Datatype_Enum>(type);
					goto_node = enum_type->definition_node;
					break;
				}
				default: panic("");
				}

				if (goto_node != nullptr)
				{
					option.expression.member_access_info.has_definition = true;
					option.expression.member_access_info.member_definition_unit = compilation_data_ast_node_to_compilation_unit(compilation_data, goto_node);
					option.expression.member_access_info.definition_index = goto_node->range.start;
				}
			}

			// Expression info
			add_semantic_info(analysis_item_index, Editor_Info_Type::EXPRESSION_INFO, option, pass, compilation_data);
		}

		break;
	}
	case AST::Node_Type::STRUCT_MEMBER: {
		auto member = downcast<AST::Structure_Member_Node>(node);
		add_markup(
			node->range, code, tree_depth, 
			member->is_expression ? Syntax_Color::MEMBER : Syntax_Color::SUBTYPE, 
			compilation_data
		);
		break;
	}
	case AST::Node_Type::ENUM_MEMBER: {
		add_markup(text_index_to_word_range(node->range.start, code), code, tree_depth, Syntax_Color::ENUM_MEMBER, compilation_data);
		break;
	}
	case AST::Node_Type::CALL_NODE:
	{
		if (active_passes.size == 0) break;
		auto arguments = downcast<AST::Call_Node>(node);

		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth, compilation_data);
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];
			Call_Info* call_info = pass_get_node_info(pass, arguments, Info_Query::TRY_READ, compilation_data);
			if (call_info != nullptr) {
				Editor_Info_Option option;
				option.call_info.callable_call = call_info;
				option.call_info.call_node = arguments;
				add_semantic_info(analysis_item_index, Editor_Info_Type::CALL_INFORMATION, option, pass, compilation_data);
			}
		}
		break;
	}
	case AST::Node_Type::ARGUMENT:
	{
		// Add named argument highlighting
		auto arg = downcast<AST::Argument>(node);
		if (arg->name.available) {
			add_markup(text_index_to_word_range(node->range.start, code), code, tree_depth, Syntax_Color::VARIABLE, compilation_data);
		}

		// Find argument index
		auto arguments = downcast<AST::Call_Node>(node->parent);
		int arg_index = -1;
		for (int i = 0; i < arguments->arguments.size; i++) {
			if (upcast(arguments->arguments[i]) == node) {
				arg_index = i;
				break;
			}
		}

		Editor_Info_Option option;
		option.argument_info.call_node = arguments;
		option.argument_info.argument_index = arg_index;
		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth, compilation_data);
		add_semantic_info(analysis_item_index, Editor_Info_Type::ARGUMENT, option, nullptr, compilation_data);
		break;
	}
	case AST::Node_Type::SYMBOL_NODE:
	{
		AST::Symbol_Node* symbol_node = downcast<AST::Symbol_Node>(node);
		bool is_definition = symbol_node->is_definition;
		Text_Range range = text_index_to_word_range(node->range.start, code);

		int analysis_item_index = add_code_analysis_item(range, code, tree_depth, compilation_data);
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];

			Symbol* symbol = nullptr;
			Symbol_Node_Info* info = pass_get_node_info(pass, symbol_node, Info_Query::TRY_READ, compilation_data);
			if (info != nullptr){
				symbol = info->symbol;
			}

			if (symbol != nullptr) 
			{
				// Add symbol lookup info
				Editor_Info_Option option;
				option.symbol_info.symbol = symbol;
				option.symbol_info.is_definition = is_definition;
				option.symbol_info.pass = pass;
				option.symbol_info.add_color = !symbol_node->is_root_lookup;
				add_semantic_info(analysis_item_index, Editor_Info_Type::SYMBOL_LOOKUP, option, pass, compilation_data);
			}
			else if (symbol_node->is_definition) 
			{
				Editor_Info_Option option;
				option.markup_color = Syntax_Color::VALUE_DEFINITION;
				add_semantic_info(analysis_item_index, Editor_Info_Type::MARKUP, option, pass, compilation_data);
			}
		}
		break;
	}
	}

	// Recurse to children
	int index = 0;
	auto child = AST::base_get_child(node, index);
	while (child != 0)
	{
		find_editor_infos_recursive(child, unit, active_passes, tree_depth + 1, compilation_data);
		index += 1;
		child = AST::base_get_child(node, index);
	}
	active_passes.rollback_to_size(active_pass_count_before);
}

void compilation_data_update_source_code_information(Compilation_Data* compilation_data)
{
	compilation_data->next_analysis_item_index = 0;
	auto& errors = compilation_data->compiler_errors;
	dynamic_array_reset(&errors);

	for (int i = 0; i < compilation_data->compilation_units.size; i++)
	{
		auto unit = compilation_data->compilation_units[i];

		// Reset analysis data for unit
		dynamic_array_reset(&unit->code->block_id_range);
		dynamic_array_reset(&unit->code->symbol_table_ranges);
		unit->code->root_table = nullptr;
		for (int i = 0; i < unit->code->line_count; i++) {
			dynamic_array_reset(&source_code_get_line(unit->code, i)->item_infos);
		}

		// Store nodes
		if (unit->upp_module == nullptr) {
			continue;
		}

		auto checkpoint = compilation_data->tmp_arena.make_checkpoint();
		SCOPE_EXIT(checkpoint.rewind());
		DynArray<Analysis_Pass*> active_passes = DynArray<Analysis_Pass*>::create(&compilation_data->tmp_arena);
		find_editor_infos_recursive(upcast(unit->root), unit, active_passes, 0, compilation_data);

		// Add parser errors
		for (int i = 0; i < unit->parser_errors.size; i++)
		{
			const auto& error = unit->parser_errors[i];
			Text_Range range = error.range;

			Compiler_Error_Info error_info;
			error_info.message = error.msg;
			error_info.unit = unit;
			error_info.semantic_error_index = -1;
			error_info.text_index = range.start;
			dynamic_array_push_back(&errors, error_info);

			int analysis_item_index = add_code_analysis_item(range, unit->code, 0, compilation_data);
			Editor_Info_Option option;
			option.error_index = errors.size - 1;
			add_semantic_info(analysis_item_index, Editor_Info_Type::ERROR_ITEM, option, nullptr, compilation_data);
		}
	}

	// Add semantic errors
	Arena temp_arena = Arena::create();
	SCOPE_EXIT(temp_arena.destroy());
	for (int i = 0; i < compilation_data->semantic_errors.size; i++)
	{
		const auto& error = compilation_data->semantic_errors[i];

		auto unit = compilation_data_ast_node_to_compilation_unit(compilation_data, error.error_node);

		temp_arena.reset(true);
		DynArray<Text_Range> ranges = Parser::ast_base_get_section_token_range(unit->code, error.error_node, error.section, &temp_arena);
		assert(ranges.size != 0, "");

		Compiler_Error_Info error_info;
		error_info.message = error.msg;
		error_info.unit = unit;
		error_info.semantic_error_index = i;
		error_info.text_index = ranges[0].start;
		dynamic_array_push_back(&errors, error_info);

		// Add visible ranges
		for (int j = 0; j < ranges.size; j++) {
			int analysis_item_index = add_code_analysis_item(ranges[j], unit->code, 0, compilation_data);
			Editor_Info_Option option;
			option.error_index = errors.size - 1;
			add_semantic_info(analysis_item_index, Editor_Info_Type::ERROR_ITEM, option, nullptr, compilation_data);
		}
	}

	// Set mapping infos between Analysis-Items and Semantic_Infos
	{
		// Sort semantic-infos, so that same analysis-items are next to each other
		auto& semantic_infos = compilation_data->semantic_infos;
		const int analysis_item_count = compilation_data->next_analysis_item_index;
		dynamic_array_sort(&compilation_data->semantic_infos, Semantic_Info_Comparator());

		// Find start and length of all semantic infos
		Array<int> item_info_start_indices = array_create<int>(analysis_item_count);
		Array<int> item_info_count = array_create<int>(analysis_item_count);
		SCOPE_EXIT(array_destroy(&item_info_start_indices));
		SCOPE_EXIT(array_destroy(&item_info_count));
		for (int i = 0; i < analysis_item_count; i++) {
			item_info_start_indices[i] = -1;
			item_info_count[i] = 0;
		}

		int last_index = -1;
		for (int i = 0; i < semantic_infos.size; i++) {
			auto& info = semantic_infos[i];
			if (info.analysis_item_index != last_index) {
				item_info_start_indices[info.analysis_item_index] = i;
				assert(item_info_count[info.analysis_item_index] == 0, "After sorting this should be the case");
			}
			last_index = info.analysis_item_index;
			item_info_count[info.analysis_item_index] += 1;
		}

		// Update analysis-item infos in source-code
		for (int i = 0; i < compilation_data->compilation_units.size; i++)
		{
			auto unit = compilation_data->compilation_units[i];
			for (int j = 0; j < unit->code->line_count; j++) 
			{
				auto& analysis_items = source_code_get_line(unit->code, j)->item_infos;
				for (int k = 0; k < analysis_items.size; k++) 
				{
					auto& item = analysis_items[k];
					item.editor_info_mapping_start_index = item_info_start_indices[item.item_index];
					item.editor_info_mapping_count = item_info_count[item.item_index];
					// Remove item if mapping count is null (Not sure if this is wanted behavior)
					if (item.editor_info_mapping_count == 0) {
						dynamic_array_swap_remove(&analysis_items, k);
						k -= 1;
					}
				}
			}
		}
	}
}

Exit_Code compiler_execute(Compilation_Data* compilation_data)
{
    bool do_execution =
        enable_lexing &&
        enable_parsing &&
        enable_analysis &&
        enable_ir_gen &&
        enable_execution;
    if (execute_binary) {
        do_execution = do_execution && enable_c_compilation;
    }
    else {
        do_execution = do_execution && enable_bytecode_gen;
    }

    // Execute
    if (!compilation_data_errors_occured(compilation_data) && do_execution)
    {
        if (execute_binary) {
            return c_compiler_execute();
        }
        else
        {
            Arena* scratch_arena = &compilation_data->tmp_arena;
            auto checkpoint = scratch_arena->make_checkpoint();
            SCOPE_EXIT(checkpoint.rewind());

            Bytecode_Thread* thread = bytecode_thread_create(compilation_data, scratch_arena, 10000, 1024 * 64, 1024 * 8, true);
            bytecode_thread_set_initial_state(thread, compilation_data->entry_function);
            return bytecode_thread_execute(thread);
        }
    }
    return exit_code_make(Exit_Code_Type::COMPILATION_FAILED);
}

bool compilation_data_is_configured_for_c_compilation(Compilation_Data* compilation_data)
{
    return
        enable_lexing &&
        enable_parsing &&
        enable_analysis &&
        enable_ir_gen &&
        enable_c_generation &&
        enable_c_compilation &&
        !compilation_data_errors_occured(compilation_data);
}

bool compilation_data_errors_occured(Compilation_Data* compilation_data)
{
    if (compilation_data->semantic_errors.size > 0) return true;

    for (int i = 0; i < compilation_data->compilation_units.size; i++) 
    {
        auto unit = compilation_data->compilation_units[i];
        if (unit->upp_module == nullptr) continue; // Checking if unit was analysed
        if (unit->parser_errors.size > 0) return true;
    }
    return false;
}

Compilation_Unit* compilation_data_ast_node_to_compilation_unit(Compilation_Data* compilation_data, AST::Node* base)
{
    while (base->parent != nullptr) {
        base = base->parent;
    }
    assert(base->type == AST::Node_Type::DEFINITION, "Root must be definition module");
    AST::Definition* definition = downcast<AST::Definition>(base);
    assert(definition->type == AST::Definition_Type::MODULE, "Root must be definition module");

    for (int i = 0; i < compilation_data->compilation_units.size; i++) {
        auto unit = compilation_data->compilation_units[i];
        if (unit->root == &definition->options.module) {
            return unit;
        }
    }
    return nullptr;
}

void compilation_data_switch_timing_task(Compilation_Data* compilation_data, Timing_Task task)
{
    if (task == compilation_data->task_current) return;
    double* add_to = 0;
    switch (compilation_data->task_current)
    {
    case Timing_Task::LEXING: add_to = &compilation_data->time_lexing; break;
    case Timing_Task::PARSING: add_to = &compilation_data->time_parsing; break;
    case Timing_Task::ANALYSIS: add_to = &compilation_data->time_analysing; break;
    case Timing_Task::CODE_GEN: add_to = &compilation_data->time_code_gen; break;
    case Timing_Task::CODE_EXEC: add_to = &compilation_data->time_code_exec; break;
    case Timing_Task::RESET: add_to = &compilation_data->time_reset; break;
    case Timing_Task::OUTPUT: add_to = &compilation_data->time_output; break;
    case Timing_Task::FINISH: {
        compilation_data->task_current = task;
        return;
    }
    default: panic("");
    }
    double now = timer_current_time_in_seconds();
    double time_spent = now - compilation_data->task_last_start_time;
    *add_to = *add_to + time_spent;
    //logg("Spent %3.2fms on: %s\n", time_spent, timing_task_to_string(compilation_data->task_current));
    compilation_data->task_last_start_time = now;
    compilation_data->task_current = task;
}



// TEST CASES
struct Test_Case
{
    const char* name;
    bool should_succeed;
};

Test_Case test_case_make(const char* name, bool should_success)
{
    Test_Case result;
    result.name = name;
    result.should_succeed = should_success;
    return result;
}

void compiler_run_testcases(bool force_run)
{
    if (!enable_testcases && !force_run) return;
    bool i_enable_lexing = enable_lexing;
    SCOPE_EXIT(enable_lexing = i_enable_lexing;);
    bool i_enable_parsing = enable_parsing;
    SCOPE_EXIT(enable_parsing = i_enable_parsing;);
    bool i_enable_analysis = enable_analysis;
    SCOPE_EXIT(enable_analysis = i_enable_analysis;);
    bool i_enable_ir_gen = enable_ir_gen;
    SCOPE_EXIT(enable_ir_gen = i_enable_ir_gen;);
    bool i_enable_bytecode_gen = enable_bytecode_gen;
    SCOPE_EXIT(enable_bytecode_gen = i_enable_bytecode_gen;);
    bool i_enable_c_generation = enable_c_generation;
    SCOPE_EXIT(enable_c_generation = i_enable_c_generation;);
    bool i_enable_c_compilation = enable_c_compilation;
    SCOPE_EXIT(enable_c_compilation = i_enable_c_compilation;);
    bool i_enable_output = enable_output;
    SCOPE_EXIT(enable_output = i_enable_output;);
    bool i_enable_execution = enable_execution;
    SCOPE_EXIT(enable_execution = i_enable_execution;);
    bool i_execute_binary = execute_binary;
    SCOPE_EXIT(execute_binary = i_execute_binary;);
    bool i_output_identifiers = output_identifiers;
    SCOPE_EXIT(output_identifiers = i_output_identifiers;);
    bool i_output_ast = output_ast;
    SCOPE_EXIT(output_ast = i_output_ast;);
    bool i_output_type_system = output_type_system;
    SCOPE_EXIT(output_type_system = i_output_type_system;);
    bool i_output_root_table = output_root_table;
    SCOPE_EXIT(output_root_table = i_output_root_table;);
    bool i_output_ir = output_ir;
    SCOPE_EXIT(output_ir = i_output_ir;);
    bool i_output_bytecode = output_bytecode;
    SCOPE_EXIT(output_bytecode = i_output_bytecode;);
    bool i_output_timing = output_timing;
    SCOPE_EXIT(output_timing = i_output_timing;);

    enable_lexing = true;
    enable_parsing = true;
    enable_analysis = true;
    enable_ir_gen = true;
    enable_bytecode_gen = true;
    enable_c_generation = run_testcases_compiled;
    enable_c_compilation = run_testcases_compiled;
    enable_output = false;
    enable_execution = true;
    execute_binary = run_testcases_compiled;

    output_identifiers = false;
    output_ast = false;
    output_type_system = false;
    output_root_table = false;
    output_ir = false;
    output_bytecode = false;
    output_timing = false;

    logg("STARTING ALL TESTS:\n-----------------------------\n");

    Fiber_Pool* fiber_pool = fiber_pool_create();
    SCOPE_EXIT(fiber_pool_destroy(fiber_pool));

    // Create testcases with expected result
    Directory_Crawler* crawler = directory_crawler_create();
    SCOPE_EXIT(directory_crawler_destroy(crawler));
    directory_crawler_set_path(crawler, string_create_static("upp_code/testcases"));
    auto files = directory_crawler_get_content(crawler);

    bool errors_occured = false;
    int test_case_count = 0;
    String result = string_create(256);
    SCOPE_EXIT(string_destroy(&result));
    for (int i = 0; i < files.size; i++)
    {
        const auto& file = files[i];
        if (file.is_directory) continue;

        auto name = files[i].name;
        bool case_should_succeed = string_contains_substring(name, 0, string_create_static("error")) == -1;
        bool skip_file = string_contains_substring(name, 0, string_create_static("notest")) != -1;
        if (skip_file) {
            continue;
        }

        logg("Testcase #%4d: %s\n", test_case_count, name.characters);
        test_case_count += 1;

        Compilation_Data* compilation_data = compilation_data_create(fiber_pool);
        SCOPE_EXIT(compilation_data_destroy(compilation_data));

        String path = string_create();
        path.append_formated("upp_code/testcases/%s", name.characters);
        SCOPE_EXIT(string_destroy(&path));
        Compilation_Unit* main_unit = compilation_data_add_compilation_unit_unique(compilation_data, path, true, false);
        if (main_unit == nullptr) {
            string_append_formated(&result, "ERROR:   Test %s could not load test file\n", name.characters);
            errors_occured = true;
            continue;
        }

        compilation_data_compile(compilation_data, main_unit, Compile_Type::BUILD_CODE);
        Exit_Code exit_code = compiler_execute(compilation_data);
        if (exit_code.type != Exit_Code_Type::SUCCESS && case_should_succeed)
        {
            string_append_formated(&result, "ERROR:   Test %s exited with Code ", name.characters);
            exit_code_append_to_string(&result, exit_code);
            string_append_formated(&result, "\n");
            if (exit_code.type == Exit_Code_Type::COMPILATION_FAILED)
            {
                for (int i = 0; i < compilation_data->compilation_units.size; i++) 
                {
                    auto unit = compilation_data->compilation_units[i];
                    auto parser_errors = unit->parser_errors;
                    for (int j = 0; j < parser_errors.size; j++) {
                        auto& e = parser_errors[j];
                        string_append_formated(&result, "    Parse Error: %s\n", e.msg);
                    }
                }

                compilation_data_append_semantic_errors_to_string(compilation_data, &result, 1);
                string_append_character(&result, '\n');
            }
            errors_occured = true;
        }
        else if (exit_code.type == Exit_Code_Type::SUCCESS && !case_should_succeed) {
            string_append_formated(&result, "ERROR:   Test %s successfull, but should fail!\n", name.characters);
            errors_occured = true;
        }
        else {
            string_append_formated(&result, "SUCCESS: Test %s\n", name.characters);
        }
    }

    string_style_remove_codes(&result);
    logg(result.characters);
    if (errors_occured) {
        logg("-------------------------------\nSummary: There were errors!\n-----------------------------\n");
    }
    else {
        logg("-------------------------------\nSummary: All Tests Successfull!\n-----------------------------\n");
    }


    if (!enable_stresstest) return;
    /*
    Parser/Analyser Stresstest
    --------------------------
    Each character gets typed one by one, then the text is parsed and analysed
    */
    Optional<String> text = file_io_load_text_file("upp_code/testcases/045_unions.upp");
    SCOPE_EXIT(file_io_unload_text_file(&text););
    if (!text.available) {
        logg("Couldn't execute stresstest, file not found\n");
        return;
    }

    double time_stress_start = timer_current_time_in_seconds();

    String code = text.value;
    for (int i = 0; i < code.size; i++)
    {
        String cut_code = string_create(i + 10);
        for (int j = 0; j < i; j++) {
            char c = code.characters[j];
            string_append_character(&cut_code, c);
        }

        //logg("Cut code:\n-----------------------\n%s", cut_code.characters);
        //compiler_compile(&compiler, cut_code, false);
        if (i % (code.size / 10) == 0) {
            logg("Stresstest (Simple): %d/%d characters\n", i, code.size);
        }
    }

    // Stress testing again but with correct parenthesis order
    Dynamic_Array<char> stack_parenthesis = dynamic_array_create<char>(256);
    SCOPE_EXIT(dynamic_array_destroy(&stack_parenthesis));
    for (int i = 0; i < code.size; i++)
    {
        dynamic_array_reset(&stack_parenthesis);
        String cut_code = string_create(i + 10);
        for (int j = 0; j < i; j++)
        {
            char c = code.characters[j];
            bool is_parenthesis = true;
            bool is_open = true;
            char counter_type = '}';
            switch (c)
            {
            case '{': is_open = true; counter_type = '}'; break;
            case '}': is_open = false; counter_type = '{'; break;
            case '[': is_open = true; counter_type = ']'; break;
            case ']': is_open = false; counter_type = '['; break;
            case '(': is_open = true; counter_type = ')'; break;
            case ')': is_open = false; counter_type = '('; break;
            default: is_parenthesis = false;
            }

            char last_on_stack = '!';
            if (stack_parenthesis.size > 0) {
                last_on_stack = stack_parenthesis.data[stack_parenthesis.size - 1];
            }

            if (is_parenthesis)
            {
                if (is_open)
                {
                    string_append_character(&cut_code, counter_type);
                    string_append_character(&cut_code, c);
                    dynamic_array_push_back(&stack_parenthesis, counter_type);
                }
                else
                {
                    assert(last_on_stack == c, "Wrong parenthesis order");
                    string_append_character(&cut_code, c);
                    dynamic_array_rollback_to_size(&stack_parenthesis, math_maximum(0, stack_parenthesis.size - 1));
                }
            }
        }

        //logg("Cut code:\n-----------------------\n%s", cut_code.characters);
        //compiler_compile(&compiler, cut_code, false);
        if (i % (code.size / 10) == 0) {
            logg("Stresstest (Parenthesis): %d/%d characters\n", i, code.size);
        }
    }

    double time_stress_end = timer_current_time_in_seconds();
    float ms_time = (time_stress_end - time_stress_start) * 1000.0f;
    logg("Stress test time: %3.2fms (%3.2fms per parse/analyse)\n", ms_time, ms_time / code.size / 2.0f);
}



Call_Signature* call_signature_create_empty()
{
	Call_Signature* result = new Call_Signature;
	result->parameters = dynamic_array_create<Call_Parameter>();
	result->return_type_index = -1;
	result->is_registered = false;
	return result;
}

Call_Parameter* call_signature_add_parameter(
	Call_Signature* signature, String* name, Datatype* datatype,
	bool required, bool requires_named_addressing, bool must_not_be_set)
{
	assert(!signature->is_registered, "");

	Call_Parameter param;
	param.name = name;
	param.datatype = datatype;
	param.required = required;
	param.requires_named_addressing = requires_named_addressing;
	param.must_not_be_set = must_not_be_set;
	param.pattern_variable_index = -1;

	dynamic_array_push_back(&signature->parameters, param);
	return &signature->parameters[signature->parameters.size - 1];
}

Call_Parameter* call_signature_add_return_type(Call_Signature* signature, Datatype* datatype, Compilation_Data* compilation_data)
{
	auto& ids = compilation_data->identifier_pool.predefined_ids;
	call_signature_add_parameter(signature, ids.return_type_name, datatype, false, false, true);
	assert(signature->return_type_index == -1, "");
	signature->return_type_index = signature->parameters.size - 1;
	return &signature->parameters[signature->parameters.size - 1];
}

Call_Signature* call_signature_register(Call_Signature* signature, Compilation_Data* compilation_data)
{
	assert(!signature->is_registered, "");

	// Deduplicate
	Call_Signature** dedup = hashset_find(&compilation_data->call_signatures, signature);
	if (dedup != nullptr) {
		call_signature_destroy(signature);
		return *dedup;
	}

	signature->is_registered = true;
	hashset_insert_element(&compilation_data->call_signatures, signature);
	return signature;
}

void call_signature_append_to_string(Call_Signature* signature, String* string, Type_System* type_system, Datatype_Format format)
{
	auto& parameters = signature->parameters;
	string->append("(");

	int highlight_index = format.highlight_parameter_index;
	format.highlight_parameter_index = -1;
	bool require_colon = false;
	for (int i = 0; i < parameters.size; i++)
	{
		auto& param = parameters[i];

		// Skip parameters that shouldn't be printed
		if (signature->return_type_index == i) continue;
		if (param.must_not_be_set) continue;

		if (require_colon) {
			string->append(", ");
		}
		require_colon = true;

		// Print param name + type with possible highlight
		if (highlight_index == i) {
			string_style_push(string, Mark_Type::BACKGROUND_COLOR, format.highlight_color);
		}
		if (param.pattern_variable_index != -1) {
			string->append('$');
		}
		string_style_push(string, Mark_Type::TEXT_COLOR, Syntax_Color::VALUE_DEFINITION);
		string->append(param.name);
		string_style_pop(string);
		string->append(": ");

		auto param_type = param.datatype;
		datatype_append_to_string(param_type, string, type_system, format);

		if (highlight_index == i) {
			string_style_pop(string);
		}
	}
	string->append(")");

	if (signature->return_type_index != -1) {
		string->append(" => ");
		datatype_append_to_string(signature->parameters[signature->return_type_index].datatype, string, type_system, format);
	}
}
