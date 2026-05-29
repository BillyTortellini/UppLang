#include "compiler_misc.hpp"

#include "type_system.hpp"
#include "../../win32/process.hpp"
#include "../../win32/thread.hpp"

const char* timing_task_to_string(Timing_Task task)
{
	switch (task)
	{
	case Timing_Task::LEXING: return "LEXING";
	case Timing_Task::PARSING: return "PARSING";
	case Timing_Task::ANALYSIS: return "ANALYSIS";
	case Timing_Task::CODE_GEN: return "CODE_GEN";
	case Timing_Task::CODE_EXEC: return "CODE_EXEC";
	case Timing_Task::RESET: return "RESET";
	case Timing_Task::OUTPUT: return "OUTPUT";
	case Timing_Task::FINISH: return "FINISH";
	default: panic("");
	}
	return "";
}

Hardcoded_Type_Info hardcoded_type_get_info(Hardcoded_Type type)
{
	auto make_info = [&](
		Hardcoded_Type_Class type_class, const char* symbol_name, 
		const char* cstring, const char* c_impl_name) -> Hardcoded_Type_Info 
	{
		Hardcoded_Type_Info info;
		info.type_class = type_class;
		info.cstring = cstring;
		info.symbol_name = symbol_name;
		info.c_impl_name = c_impl_name;
		return info;
	};

	switch (type)
	{
	case Hardcoded_Type::ASSERT_FN: return make_info(Hardcoded_Type_Class::UTILITY, "assert", "ASSERT_FN", "_");
	case Hardcoded_Type::TYPE_INFO: return make_info(Hardcoded_Type_Class::UTILITY, "type_info", "TYPE_INFO", "_");
	case Hardcoded_Type::TYPE_OF: return make_info(Hardcoded_Type_Class::UTILITY, "type_of", "TYPE_OF", "_");
	case Hardcoded_Type::SIZE_OF: return make_info(Hardcoded_Type_Class::UTILITY, "size_of", "SIZE_OF", "_");
	case Hardcoded_Type::ALIGN_OF: return make_info(Hardcoded_Type_Class::UTILITY, "align_of", "ALIGN_OF", "_");
	case Hardcoded_Type::PANIC_FN: return make_info(Hardcoded_Type_Class::UTILITY, "panic", "PANIC_FN", "_");
	case Hardcoded_Type::RETURN_TYPE: return make_info(Hardcoded_Type_Class::UTILITY, "return_type", "RETURN_TYPE", "_");
	case Hardcoded_Type::STRUCT_TAG: return make_info(Hardcoded_Type_Class::UTILITY, "struct_tag", "STRUCT_TAG", "_");

	case Hardcoded_Type::ENUM_VALUE_AS_STRING: return make_info(Hardcoded_Type_Class::UTILITY, "enum_value_as_string", "ENUM_VALUE_AS_STRING", "_");
	case Hardcoded_Type::ENUM_TYPE_MIN_VALUE: return make_info(Hardcoded_Type_Class::UTILITY, "enum_type_min_value", "ENUM_TYPE_MIN_VALUE", "_");
	case Hardcoded_Type::ENUM_TYPE_MAX_VALUE: return make_info(Hardcoded_Type_Class::UTILITY, "enum_type_max_value", "ENUM_TYPE_MAX_VALUE", "_");
	case Hardcoded_Type::ENUM_TYPE_IS_CONTINOUS: return make_info(Hardcoded_Type_Class::UTILITY, "enum_type_is_continous", "ENUM_TYPE_IS_CONTINOUS", "_");

	case Hardcoded_Type::CAST_PRIMITIVE: return make_info(Hardcoded_Type_Class::UTILITY, "cast_primitive", "CAST_PRIMITIVE", "_");
	case Hardcoded_Type::CAST_POINTER: return make_info(Hardcoded_Type_Class::UTILITY, "cast_pointer", "CAST_POINTER", "_");

	case Hardcoded_Type::MEMORY_COPY: return make_info(Hardcoded_Type_Class::UTILITY, "memory_copy", "MEMORY_COPY", "memory_copy");
	case Hardcoded_Type::MEMORY_COPY_NO_OVERLAP: return make_info(
		Hardcoded_Type_Class::UTILITY, "memory_copy_no_overlap", "MEMORY_COPY_NO_OVERLAP", "memory_copy_no_overlap");
	case Hardcoded_Type::MEMORY_COMPARE: return make_info(Hardcoded_Type_Class::UTILITY, "memory_compare", "MEMORY_COMPARE", "memory_compare");
	case Hardcoded_Type::MEMORY_ZERO: return make_info(Hardcoded_Type_Class::UTILITY, "memory_zero", "MEMORY_ZERO", "memory_zero");

	case Hardcoded_Type::SYSTEM_ALLOC: return make_info(Hardcoded_Type_Class::UTILITY, "system_allocate", "SYSTEM_ALLOC", "malloc_size_u64");
	case Hardcoded_Type::SYSTEM_FREE: return make_info(Hardcoded_Type_Class::UTILITY, "system_free", "SYSTEM_FREE", "free_pointer");

	case Hardcoded_Type::BITWISE_NOT: return make_info(Hardcoded_Type_Class::BITWISE_OPERATION, "bitwise_not", "BITWISE_NOT", "_");
	case Hardcoded_Type::BITWISE_AND: return make_info(Hardcoded_Type_Class::BITWISE_OPERATION, "bitwise_and", "BITWISE_AND", "_");
	case Hardcoded_Type::BITWISE_OR: return make_info(Hardcoded_Type_Class::BITWISE_OPERATION, "bitwise_or", "BITWISE_OR", "_");
	case Hardcoded_Type::BITWISE_XOR: return make_info(Hardcoded_Type_Class::BITWISE_OPERATION, "bitwise_xor", "BITWISE_XOR", "_");
	case Hardcoded_Type::BITWISE_SHIFT_LEFT: return make_info(Hardcoded_Type_Class::BITWISE_OPERATION, "bitwise_shift_left", "BITWISE_SHIFT_LEFT", "_");
	case Hardcoded_Type::BITWISE_SHIFT_RIGHT: return make_info(Hardcoded_Type_Class::BITWISE_OPERATION, "bitwise_shift_right", "BITWISE_SHIFT_RIGHT", "_");

	case Hardcoded_Type::PRINT_I32: return make_info(Hardcoded_Type_Class::OUTPUT, "print_i32", "PRINT_I32", "print_i32");
	case Hardcoded_Type::PRINT_F32: return make_info(Hardcoded_Type_Class::OUTPUT, "print_f32", "PRINT_F32", "print_f32");
	case Hardcoded_Type::PRINT_BOOL: return make_info(Hardcoded_Type_Class::OUTPUT, "print_bool", "PRINT_BOOL", "print_bool");
	case Hardcoded_Type::PRINT_LINE: return make_info(Hardcoded_Type_Class::OUTPUT, "print_line", "PRINT_LINE", "print_line");
	case Hardcoded_Type::PRINT_STRING: return make_info(Hardcoded_Type_Class::OUTPUT, "print_string", "PRINT_STRING", "print_string");
	case Hardcoded_Type::READ_I32: return make_info(Hardcoded_Type_Class::INPUT, "read_i32", "READ_I32", "read_i32");
	case Hardcoded_Type::READ_F32: return make_info(Hardcoded_Type_Class::INPUT, "read_f32", "READ_F32", "read_f32");
	case Hardcoded_Type::READ_BOOL: return make_info(Hardcoded_Type_Class::INPUT, "read_bool", "READ_BOOL", "read_bool");

	case Hardcoded_Type::F32_ABSOLUTE: return make_info(Hardcoded_Type_Class::F32_UNARY, "absolute", "F32_ABSOLUTE", "fabsf");
	case Hardcoded_Type::F32_MODULO: return make_info(Hardcoded_Type_Class::F32_BINARY, "modulo", "F32_MODULO", "fmodf");
	case Hardcoded_Type::F32_REMAINDER: return make_info(Hardcoded_Type_Class::F32_BINARY, "remainder", "F32_REMAINDER", "remainderf");
	case Hardcoded_Type::F32_CEIL: return make_info(Hardcoded_Type_Class::F32_UNARY, "round_up", "F32_CEIL", "ceilf");
	case Hardcoded_Type::F32_FLOOR: return make_info(Hardcoded_Type_Class::F32_UNARY, "round_down", "F32_FLOOR", "floorf");
	case Hardcoded_Type::F32_TRUNCATE: return make_info(Hardcoded_Type_Class::F32_UNARY, "round_towards_zero", "F32_TRUNCATE", "truncf");
	case Hardcoded_Type::F32_ROUND_NEAREST: return make_info(Hardcoded_Type_Class::F32_UNARY, "round_nearest", "F32_ROUND_NEAREST", "roundf");
	case Hardcoded_Type::F32_EXP: return make_info(Hardcoded_Type_Class::F32_UNARY, "expr", "F32_EXP", "expf");
	case Hardcoded_Type::F32_LN: return make_info(Hardcoded_Type_Class::F32_UNARY, "ln", "F32_LN", "logf");
	case Hardcoded_Type::F32_LOG10: return make_info(Hardcoded_Type_Class::F32_UNARY, "log10", "F32_LOG10", "log10f");
	case Hardcoded_Type::F32_LOG2: return make_info(Hardcoded_Type_Class::F32_UNARY, "log2", "F32_LOG2", "log2f");
	case Hardcoded_Type::F32_POW: return make_info(Hardcoded_Type_Class::F32_BINARY, "pow", "F32_POW", "powf");
	case Hardcoded_Type::F32_SQUARE_ROOT: return make_info(Hardcoded_Type_Class::F32_UNARY, "sqare_root", "F32_SQUARE_ROOT", "sqrtf");
	case Hardcoded_Type::F32_CUBE_ROOT: return make_info(Hardcoded_Type_Class::F32_UNARY, "cube_root", "F32_CUBE_ROOT", "cbrtf");
	case Hardcoded_Type::F32_SIN: return make_info(Hardcoded_Type_Class::F32_UNARY, "sin", "F32_SIN", "sinf");
	case Hardcoded_Type::F32_COS: return make_info(Hardcoded_Type_Class::F32_UNARY, "cos", "F32_COS", "cosf");
	case Hardcoded_Type::F32_TAN: return make_info(Hardcoded_Type_Class::F32_UNARY, "tan", "F32_TAN", "tanf");
	case Hardcoded_Type::F32_ASIN: return make_info(Hardcoded_Type_Class::F32_UNARY, "asin", "F32_ASIN", "asinf");
	case Hardcoded_Type::F32_ACOS: return make_info(Hardcoded_Type_Class::F32_UNARY, "acos", "F32_ACOS", "acosf");
	case Hardcoded_Type::F32_ATAN: return make_info(Hardcoded_Type_Class::F32_UNARY, "atan", "F32_ATAN", "atanf");
	case Hardcoded_Type::F32_ATAN2: return make_info(Hardcoded_Type_Class::F32_BINARY, "atan2", "F32_ATAN2", "atan2f");
	case Hardcoded_Type::F32_SINH: return make_info(Hardcoded_Type_Class::F32_UNARY, "sinh", "F32_SINH", "sinhf");
	case Hardcoded_Type::F32_COSH: return make_info(Hardcoded_Type_Class::F32_UNARY, "cosh", "F32_COSH", "coshf");
	case Hardcoded_Type::F32_TANH: return make_info(Hardcoded_Type_Class::F32_UNARY, "tanh", "F32_TANH", "tanhf");
	case Hardcoded_Type::F32_ASINH: return make_info(Hardcoded_Type_Class::F32_UNARY, "asinh", "F32_ASINH", "asinhf");
	case Hardcoded_Type::F32_ACOSH: return make_info(Hardcoded_Type_Class::F32_UNARY, "acosh", "F32_ACOSH", "acoshf");
	case Hardcoded_Type::F32_ATANH: return make_info(Hardcoded_Type_Class::F32_UNARY, "atanh", "F32_ATANH", "atanhf");
	case Hardcoded_Type::F32_IS_NAN: return make_info(Hardcoded_Type_Class::F32_PREDICATE, "is_nan", "F32_IS_NAN", "isnan");
	case Hardcoded_Type::F32_IS_FINITE: return make_info(Hardcoded_Type_Class::F32_PREDICATE, "is_finite", "F32_IS_FINITE", "isfinite");
	case Hardcoded_Type::F32_IS_INFINITE: return make_info(Hardcoded_Type_Class::F32_PREDICATE, "is_infinite", "F32_IS_INFINITE", "isinf");

	case Hardcoded_Type::F64_ABSOLUTE: return make_info(Hardcoded_Type_Class::F64_UNARY, "absolute", "F64_ABSOLUTE", "fabs");
	case Hardcoded_Type::F64_MODULO: return make_info(Hardcoded_Type_Class::F64_BINARY, "modulo", "F64_MODULO", "fmod");
	case Hardcoded_Type::F64_REMAINDER: return make_info(Hardcoded_Type_Class::F64_BINARY, "remainder", "F64_REMAINDER", "remainder");
	case Hardcoded_Type::F64_CEIL: return make_info(Hardcoded_Type_Class::F64_UNARY, "round_up", "F64_CEIL", "ceil");
	case Hardcoded_Type::F64_FLOOR: return make_info(Hardcoded_Type_Class::F64_UNARY, "round_down", "F64_FLOOR", "floor");
	case Hardcoded_Type::F64_TRUNCATE: return make_info(Hardcoded_Type_Class::F64_UNARY, "round_towards_zero", "F64_TRUNCATE", "trunc");
	case Hardcoded_Type::F64_ROUND_NEAREST: return make_info(Hardcoded_Type_Class::F64_UNARY, "round_nearest", "F64_ROUND_NEAREST", "round");
	case Hardcoded_Type::F64_EXP: return make_info(Hardcoded_Type_Class::F64_UNARY, "expr", "F64_EXP", "exp");
	case Hardcoded_Type::F64_LN: return make_info(Hardcoded_Type_Class::F64_UNARY, "ln", "F64_LN", "log");
	case Hardcoded_Type::F64_LOG10: return make_info(Hardcoded_Type_Class::F64_UNARY, "log10", "F64_LOG10", "log10");
	case Hardcoded_Type::F64_LOG2: return make_info(Hardcoded_Type_Class::F64_UNARY, "log2", "F64_LOG2", "log2");
	case Hardcoded_Type::F64_POW: return make_info(Hardcoded_Type_Class::F64_BINARY, "pow", "F64_POW", "pow");
	case Hardcoded_Type::F64_SQUARE_ROOT: return make_info(Hardcoded_Type_Class::F64_UNARY, "sqare_root", "F64_SQUARE_ROOT", "sqrt");
	case Hardcoded_Type::F64_CUBE_ROOT: return make_info(Hardcoded_Type_Class::F64_UNARY, "cube_root", "F64_CUBE_ROOT", "cbrt");
	case Hardcoded_Type::F64_SIN: return make_info(Hardcoded_Type_Class::F64_UNARY, "sin", "F64_SIN", "sin");
	case Hardcoded_Type::F64_COS: return make_info(Hardcoded_Type_Class::F64_UNARY, "cos", "F64_COS", "cos");
	case Hardcoded_Type::F64_TAN: return make_info(Hardcoded_Type_Class::F64_UNARY, "tan", "F64_TAN", "tan");
	case Hardcoded_Type::F64_ASIN: return make_info(Hardcoded_Type_Class::F64_UNARY, "asin", "F64_ASIN", "asin");
	case Hardcoded_Type::F64_ACOS: return make_info(Hardcoded_Type_Class::F64_UNARY, "acos", "F64_ACOS", "acos");
	case Hardcoded_Type::F64_ATAN: return make_info(Hardcoded_Type_Class::F64_UNARY, "atan", "F64_ATAN", "atan");
	case Hardcoded_Type::F64_ATAN2: return make_info(Hardcoded_Type_Class::F64_BINARY, "atan2", "F64_ATAN2", "atan2");
	case Hardcoded_Type::F64_SINH: return make_info(Hardcoded_Type_Class::F64_UNARY, "sinh", "F64_SINH", "sinh");
	case Hardcoded_Type::F64_COSH: return make_info(Hardcoded_Type_Class::F64_UNARY, "cosh", "F64_COSH", "cosh");
	case Hardcoded_Type::F64_TANH: return make_info(Hardcoded_Type_Class::F64_UNARY, "tanh", "F64_TANH", "tanh");
	case Hardcoded_Type::F64_ASINH: return make_info(Hardcoded_Type_Class::F64_UNARY, "asinh", "F64_ASINH", "asinh");
	case Hardcoded_Type::F64_ACOSH: return make_info(Hardcoded_Type_Class::F64_UNARY, "acosh", "F64_ACOSH", "acosh");
	case Hardcoded_Type::F64_ATANH: return make_info(Hardcoded_Type_Class::F64_UNARY, "atanh", "F64_ATANH", "atanh");
	case Hardcoded_Type::F64_IS_NAN: return make_info(Hardcoded_Type_Class::F64_PREDICATE, "is_nan", "F64_IS_NAN", "isnan");
	case Hardcoded_Type::F64_IS_FINITE: return make_info(Hardcoded_Type_Class::F64_PREDICATE, "is_finite", "F64_IS_FINITE", "isfinite");
	case Hardcoded_Type::F64_IS_INFINITE: return make_info(Hardcoded_Type_Class::F64_PREDICATE, "is_infinite", "F64_IS_INFINITE", "isinf");
	default: panic("Should not happen");
	}

	return make_info((Hardcoded_Type_Class)-1, "INVALID", "INVALID", "INVALID");
}

Exit_Code exit_code_make(Exit_Code_Type type, const char* error_msg)
{
	Exit_Code result;
	result.type = type;
	result.options.error_msg = error_msg;
	return result;
}

const char* exit_code_type_as_string(Exit_Code_Type type)
{
	switch (type)
	{
	case Exit_Code_Type::SUCCESS: return "SUCCESS";
	case Exit_Code_Type::RUNNING: return "RUNNING";
	case Exit_Code_Type::COMPILATION_FAILED: return "COMPILATION_FAILED";
	case Exit_Code_Type::CODE_ERROR: return "CODE_ERROR";
	case Exit_Code_Type::EXECUTION_ERROR: return "EXECUTION_ERROR";
	case Exit_Code_Type::INSTRUCTION_LIMIT_REACHED: return "INSTRUCTION_LIMIT_REACHED";
	case Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED: return "TYPE_INFO_WAITING_FOR_TYPE_FINISH";
	case Exit_Code_Type::CALL_TO_UNFINISHED_FUNCTION: return "CALL_TO_UNFINISHED_FUNCTION";
	default: panic("");
	}
	return "";
}

void exit_code_append_to_string(String* string, Exit_Code code)
{
	string_append_formated(string, exit_code_type_as_string(code.type));
	if (code.type == Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED || code.type == Exit_Code_Type::CALL_TO_UNFINISHED_FUNCTION) {
		return;
	}
	if (code.options.error_msg != 0) {
		string_append_formated(string, ", %s", code.options.error_msg);
	}
}



// Extern sources
Extern_Sources extern_sources_create()
{
	Extern_Sources result;
	result.extern_functions = dynamic_array_create<Upp_Function*>();
	for (int i = 0; i < (int)Extern_Compiler_Setting::MAX_ENUM_VALUE; i++) {
		result.compiler_settings[i] = dynamic_array_create<String*>();
	}
	return result;
}

void extern_sources_destroy(Extern_Sources* sources)
{
	dynamic_array_destroy(&sources->extern_functions);
	for (int i = 0; i < (int)Extern_Compiler_Setting::MAX_ENUM_VALUE; i++) {
		dynamic_array_destroy(&sources->compiler_settings[i]);
	}
}



// Identifier Pool
Identifier_Pool identifier_pool_create()
{
	Identifier_Pool result;
	result.identifier_lookup_table = hashtable_create_empty<String, String*>(128, hash_string, string_equals);

	// Add predefined IDs
	{
		auto& ids = result.predefined_ids;
		auto add_id = [&](const char* id) -> String* {
			return identifier_pool_add(&result, string_create_static(id));
		};

		ids.size = add_id("size");
		ids.data = add_id("data");
		ids.tag = add_id("tag");
		ids.anon_struct = add_id("Anonymous");
		ids.anon_enum = add_id("Anon_Enum");
		ids.main = add_id("main");
		ids.type_of = add_id("type_of");
		ids.type_info = add_id("type_info");
		ids.empty_string = add_id("");
		ids.root_module = add_id("_ROOT_");
		ids.invalid_symbol_name = add_id("__INVALID_SYMBOL_NAME");
		ids.id_struct = add_id("Struct");
		ids.byte = add_id("byte");
		ids.value = add_id("value");
		ids.is_available = add_id("is_available");
		ids.uninitialized_token = add_id("_");
		ids.return_type_name = add_id("!return_type_name");
		ids.operators = add_id("operators");
		ids.dot_calls = add_id("dot_calls");
		ids.c_string = add_id("c_string");
		ids.string = add_id("string");
		ids.allocator = add_id("Allocator");
		ids.bytes = add_id("bytes");
		ids.lambda_function = add_id("lambda_function");
		ids.bake_function = add_id("bake_function");

		ids.hashtag_instanciate = add_id("#instanciate");
		ids.hashtag_bake = add_id("#bake");
		ids.hashtag_get_overload = add_id("#get_overload");
		ids.hashtag_get_overload_poly = add_id("#get_overload_poly");

		ids.custom_operator_function_names[(int)Custom_Operator_Type::ARRAY_ACCESS]      = add_id("add_array_access");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::AUTO_CAST]         = add_id("add_auto_cast");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::ITERATOR]          = add_id("add_iterator");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::DEFAULT_VALUE]     = add_id("add_default_value");

		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_ADDITION]      = add_id("add_binop_addition");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_SUBTRACTION]   = add_id("add_binop_subtraction");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_MULTIPLY]      = add_id("add_binop_multiply");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_DIVIDE]        = add_id("add_binop_divide");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_MODULO]        = add_id("add_binop_modulo");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_EQUAL]         = add_id("add_binop_equal");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_NOT_EQUAL]     = add_id("add_binop_not_equal");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_LESS]          = add_id("add_binop_less");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_LESS_EQUAL]    = add_id("add_binop_less_equal");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_GREATER]       = add_id("add_binop_greater");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::BINOP_GREATER_EQUAL] = add_id("add_binop_greater_equal");

		ids.custom_operator_function_names[(int)Custom_Operator_Type::UNOP_NOT]          = add_id("add_unop_not");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::UNOP_NEGATE]       = add_id("add_unop_negate");
		ids.custom_operator_function_names[(int)Custom_Operator_Type::INVALID]           = add_id("!custom_op_invalid!");

		ids.defer_restore = add_id("defer_restore");
		ids.cast = add_id("cast");
		ids.defer = add_id("defer");
		ids.from = add_id("from");
		ids.to = add_id("to");

		ids.function = add_id("function");
		ids.create_fn = add_id("create_fn");
		ids.next_fn = add_id("next_fn");
		ids.has_next_fn = add_id("has_next_fn");
		ids.value_fn = add_id("value_fn");
		ids.name = add_id("name");
		ids.as_member_access = add_id("as_member_access");
		ids.commutative = add_id("commutative");
		ids.binop = add_id("binop");
		ids.unop = add_id("unop");
		ids.option = add_id("option");
		ids.global = add_id("global");
		ids.lib = add_id("lib");
		ids.lib_dir = add_id("lib_dir");
		ids.source = add_id("source");
		ids.header = add_id("header");
		ids.header_dir = add_id("header_dir");
		ids.definition = add_id("definition");
	}

	return result;
}

void identifier_pool_destroy(Identifier_Pool* pool)
{
	auto iter = hashtable_iterator_create(&pool->identifier_lookup_table);
	while (hashtable_iterator_has_next(&iter)) {
		String* str = *iter.value;
		string_destroy(str);
		delete str;
		hashtable_iterator_next(&iter);
	}
	hashtable_destroy(&pool->identifier_lookup_table);
}

String* identifier_pool_add(Identifier_Pool* pool, String identifier)
{
	String** found = hashtable_find_element(&pool->identifier_lookup_table, identifier);
	if (found != 0) {
		return *found;
	}
	else {
		String* copy = new String;
		*copy = string_create(identifier.size);
		string_append_string(copy, &identifier);
		hashtable_insert_element(&pool->identifier_lookup_table, *copy, copy);
		return copy;
	}
}

void identifier_pool_print(Identifier_Pool* pool)
{
	String msg = string_create(256);
	SCOPE_EXIT(string_destroy(&msg));
	string_append_formated(&msg, "Identifiers: ");

	auto iter = hashtable_iterator_create(&pool->identifier_lookup_table);
	int i = 0;
	while (hashtable_iterator_has_next(&iter)) {
		String* str = *iter.value;
		string_append_formated(&msg, "\n\t%d: %s", i, str->characters);
		hashtable_iterator_next(&iter);
		i++;
	}
	string_append_formated(&msg, "\n");
	logg("%s", msg.characters);
}



// FIBER POOL
struct Fiber_Startup_Info // Just a helper struct for change up initial fibers in pool
{
	Fiber_Pool* pool;
	int index_in_pool;
};

struct Fiber_Info
{
	Fiber_Handle handle;
	bool has_task_to_run; // If the fiber is currently executing a task or if its waiting for a new one
	fiber_entry_fn next_entry;
	void* next_userdata;
};

struct Fiber_Pool
{
	Fiber_Handle main_fiber; // Fiber that created the fiber pool
	Dynamic_Array<Fiber_Info> allocated_fibers;
	Dynamic_Array<int> next_free_index;
};

Fiber_Pool* fiber_pool_create() {
	Fiber_Pool* pool = new Fiber_Pool;
	pool->allocated_fibers = dynamic_array_create<Fiber_Info>(1);
	pool->next_free_index = dynamic_array_create<int>(1);
	if (!fiber_initialize()) {
		panic("Couldn't create fiber_pool, fiber initialization failed!\n");
	}
	pool->main_fiber = fiber_get_current();
	return pool;
}

void fiber_pool_set_current_fiber_to_main(Fiber_Pool* pool)
{
	pool->main_fiber = fiber_get_current();
}

void fiber_pool_destroy(Fiber_Pool* pool) {
	for (int i = 0; i < pool->allocated_fibers.size; i++) {
		fiber_delete(pool->allocated_fibers[i].handle);
	}
	dynamic_array_destroy(&pool->allocated_fibers);
	dynamic_array_destroy(&pool->next_free_index);
	delete pool;
}

void fiber_pool_instance_entry(void* userdata) 
{
	// Copy startup info to local stack
	Fiber_Startup_Info startup = *((Fiber_Startup_Info*)userdata);
	auto pool = startup.pool;
	// Switch to main fiber after copy and wait for commands
	fiber_switch_to(pool->main_fiber);

	while (true)
	{
		Fiber_Info info = pool->allocated_fibers[startup.index_in_pool];
		assert(info.has_task_to_run, "Trying to run a fiber without a given task!");
		if (info.next_entry == 0) {
			panic("Fiber pool instance started without properly setting the entryfn, should never happen!");
			break;
		}
		info.next_entry(info.next_userdata);
		pool->allocated_fibers[startup.index_in_pool].has_task_to_run = false;

		// When the fiber's job finishes, we add it back to the pool, and switch to the main fiber
		dynamic_array_push_back(&pool->next_free_index, startup.index_in_pool);
		pool->allocated_fibers[startup.index_in_pool].next_entry = 0; // Just for error detection
		pool->allocated_fibers[startup.index_in_pool].next_userdata = 0;
		fiber_switch_to(pool->main_fiber);
	}
}

Fiber_Pool_Handle fiber_pool_get_handle(Fiber_Pool* pool, fiber_entry_fn entry_fn, void* userdata) 
{
	// Setup new fiber if no free one is in pool
	if (pool->next_free_index.size == 0)
	{
		Fiber_Startup_Info startup;
		Fiber_Info info;
		info.handle = fiber_create(fiber_pool_instance_entry, &startup); // Note: startup is not filled out yet, but the pointer is still valid!
		info.next_entry = 0;
		info.next_userdata = 0;
		info.has_task_to_run = false;
		dynamic_array_push_back(&pool->allocated_fibers, info);
		dynamic_array_push_back(&pool->next_free_index, pool->allocated_fibers.size - 1);

		// Switch to fiber so it can grab its startup info, see 'fiber_pool_instance_entry'
		startup.pool = pool;
		startup.index_in_pool = pool->allocated_fibers.size - 1;
		fiber_switch_to(info.handle);
	}

	// Grab fiber from pool and setup to run the given function
	Fiber_Pool_Handle handle;
	handle.pool = pool;
	handle.pool_index = pool->next_free_index[pool->next_free_index.size - 1];
	pool->next_free_index.size = pool->next_free_index.size - 1; // Pop

	Fiber_Info* info = &pool->allocated_fibers[handle.pool_index];
	assert(!info->has_task_to_run, "We seem to be grabbing a function thats currently running!");
	info->next_entry = entry_fn;
	info->next_userdata = userdata;
	info->has_task_to_run = true;

	return handle;
}

bool fiber_pool_switch_to_handel(Fiber_Pool_Handle handle)
{
	Fiber_Info* info = &handle.pool->allocated_fibers[handle.pool_index];
	assert(info->has_task_to_run, "Fiber_Pool_Handle seems to be invalid, e.g. the task was already finished\n");
	fiber_switch_to(info->handle);
	// After return, return if task has completed
	info = &handle.pool->allocated_fibers[handle.pool_index]; // Refresh info pointer, just in case 
	return !info->has_task_to_run;
}

void fiber_pool_check_all_handles_completed(Fiber_Pool* pool) 
{
	for (int i = 0; i < pool->allocated_fibers.size; i++) {
		auto& info = pool->allocated_fibers[i];
		assert(!info.has_task_to_run, "Task must be completed!\n");
		info.next_entry = 0;
		info.next_userdata = 0;

		// Check that fiber is in next_free_entry list
		bool found = false;
		for (int j = 0; j < pool->next_free_index.size; j++) {
			if (pool->next_free_index[j] == i) {
				found = true;
				break;
			}
		}
		assert(found, "Finished fiber must be in next_free_entry list!\n");
	}

	// Reset next_free entry list
	assert(pool->next_free_index.size == pool->allocated_fibers.size, "Must be the same, since all fibers should be completed!\n");
	dynamic_array_reset(&pool->next_free_index);
	for (int i = 0; i < pool->allocated_fibers.size; i++) {
		dynamic_array_push_back(&pool->next_free_index, i);
	}
}

void fiber_pool_switch_to_main_fiber(Fiber_Pool* pool)
{
	fiber_switch_to(pool->main_fiber);
}



// Fiber Pool test
void test_print_int_task(void* userdata)
{
	int value = *((int*)userdata);
	logg("Fiber with userdata #%d working\n", value);
}

void test_pause_3_task(void* userdata)
{
	Fiber_Pool* pool = (Fiber_Pool*)userdata;
	logg("Wait 1\n");
	fiber_pool_switch_to_main_fiber(pool);
	logg("Wait 2\n");
	fiber_pool_switch_to_main_fiber(pool);
	logg("Wait 3\n");
	fiber_pool_switch_to_main_fiber(pool);
	logg("Finish\n");
}

void fiber_pool_test()
{
	Fiber_Pool* pool = fiber_pool_create();
	SCOPE_EXIT(fiber_pool_destroy(pool));

	int a = 1;
	int b = 2;
	int c = 3;
	Fiber_Pool_Handle handle1 = fiber_pool_get_handle(pool, test_print_int_task, &a);
	Fiber_Pool_Handle handle2 = fiber_pool_get_handle(pool, test_print_int_task, &b);
	bool finished = fiber_pool_switch_to_handel(handle1);
	assert(finished, "Must be finished now\n");
	finished = fiber_pool_switch_to_handel(handle2);
	assert(finished, "Must be finished now\n");

	Fiber_Pool_Handle pausing = fiber_pool_get_handle(pool, test_pause_3_task, pool);
	finished = false;
	while (!finished) {
		logg("switch to pausing\n");
		finished = fiber_pool_switch_to_handel(pausing);
	}
	logg("Returned from pausing!\n");

	assert(pool->allocated_fibers.size == 2, "Must not have allocated 3, since only max of 2 fibers at a time were active\n");
}

Optional<Datatype*> Call_Signature::return_type()
{
	assert(is_registered, "");
	if (return_type_index == -1) return optional_make_failure<Datatype*>();
	return optional_make_success(parameters[return_type_index].datatype);
}

int Call_Signature::param_count(bool with_return) 
{
	int param_count = parameters.size;
	if (return_type_index != -1) {
		param_count -= 1;
	}
	return param_count;
}
