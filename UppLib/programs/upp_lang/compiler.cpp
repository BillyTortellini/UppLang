#include "compiler.hpp"
#include "../../win32/timing.hpp"
#include "../../utility/file_io.hpp"

#include "semantic_analyser.hpp"
#include "lexer.hpp"
#include "ast_parser.hpp"
#include "rc_analyser.hpp"
#include "semantic_analyser.hpp"
#include "ir_code.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "c_backend.hpp"
#include "c_importer.hpp"



// Parser stages
bool enable_lexing = true;
bool enable_parsing = true;
bool enable_rc_gen = true;
bool enable_analysis = true;
bool enable_ir_gen = true;
bool enable_bytecode_gen = true;
bool enable_c_generation = false;
bool enable_c_compilation = true;

// Output stages
bool output_lexing = false;
bool output_identifiers = false;
bool output_ast = false;
bool output_rc = false;
bool output_type_system = false;
bool output_root_table = false;
bool output_ir = false;
bool output_bytecode = false;
bool output_timing = true;

// Testcases
bool enable_testcases = false;
bool enable_stresstest = false;
bool run_testcases_compiled = false;

// Execution
bool enable_output = true;
bool enable_execution = true;
bool execute_binary = false;



/*
HELPERS
*/

Token_Range token_range_make(int start_index, int end_index)
{
    Token_Range result;
    result.start_index = start_index;
    result.end_index = end_index;
    return result;
}

Text_Slice token_range_to_text_slice(Token_Range range, Compiler* compiler)
{
    Code_Source* source = compiler->main_source;
    if (source->tokens.size == 0) return text_slice_make(text_position_make(0, 0), text_position_make(0, 0));

    assert(range.start_index >= 0 && range.start_index <= source->tokens.size, "HEY");
    assert(range.end_index >= 0, "HEY");
    assert(range.end_index >= range.start_index, "HEY");
    if (range.end_index > source->tokens.size) {
        range.end_index = source->tokens.size;
    }
    if (range.start_index >= source->tokens.size) {
        return text_slice_make(text_position_make(0, 0), text_position_make(0, 0));
    }

    range.end_index = math_clamp(range.end_index, 0, math_maximum(0, source->tokens.size));
    if (range.end_index == range.start_index) {
        return text_slice_make(
            source->tokens[range.start_index].position.start,
            source->tokens[range.start_index].position.end
        );
    }

    return text_slice_make(
        source->tokens[range.start_index].position.start,
        source->tokens[range.end_index - 1].position.end
    );
}

bool exit_code_is_valid(int value)
{
    return value >= (int)Exit_Code::SUCCESS && value <= (int)Exit_Code::INVALID_SWITCH_CASE;
}

void exit_code_append_to_string(String* string, Exit_Code code)
{
    switch (code)
    {
    case Exit_Code::ASSERTION_FAILED:
        string_append_formated(string, "ASSERTION_FAILED");
        break;
    case Exit_Code::OUT_OF_BOUNDS:
        string_append_formated(string, "OUT_OF_BOUNDS");
        break;
    case Exit_Code::RETURN_VALUE_OVERFLOW:
        string_append_formated(string, "RETURN_VALUE_OVERFLOW");
        break;
    case Exit_Code::STACK_OVERFLOW:
        string_append_formated(string, "STACK_OVERFLOW");
        break;
    case Exit_Code::SUCCESS:
        string_append_formated(string, "SUCCESS");
        break;
    case Exit_Code::COMPILATION_FAILED:
        string_append_formated(string, "COMPILATION_FAILED");
        break;
    case Exit_Code::EXTERN_FUNCTION_CALL_NOT_IMPLEMENTED:
        string_append_formated(string, "EXTERN_FUNCTION_CALL_NOT_IMPLEMENTED");
        break;
    case Exit_Code::ANY_CAST_INVALID:
        string_append_formated(string, "ANY_CAST_INVALID");
        break;
    case Exit_Code::INSTRUCTION_LIMIT_REACHED:
        string_append_formated(string, "INSTRUCTION_LIMIT_REACHED");
        break;
    case Exit_Code::INVALID_SWITCH_CASE:
        string_append_formated(string, "INVALID_SWITCH_CASE");
        break;
    case Exit_Code::CODE_ERROR_OCCURED:
        string_append_formated(string, "CODE_ERROR_OCCURED");
        break;
    default: panic("Hey");
    }
}

void hardcoded_function_type_append_to_string(String* string, Hardcoded_Function_Type hardcoded)
{
    switch (hardcoded)
    {
    case Hardcoded_Function_Type::PRINT_I32:
        string_append_formated(string, "PRINT_I32");
        break;
    case Hardcoded_Function_Type::PRINT_F32:
        string_append_formated(string, "PRINT_F32");
        break;
    case Hardcoded_Function_Type::PRINT_BOOL:
        string_append_formated(string, "PRINT_BOOL");
        break;
    case Hardcoded_Function_Type::PRINT_LINE:
        string_append_formated(string, "PRINT_LINE");
        break;
    case Hardcoded_Function_Type::PRINT_STRING:
        string_append_formated(string, "PRINT_STRING");
        break;
    case Hardcoded_Function_Type::READ_I32:
        string_append_formated(string, "READ_I32");
        break;
    case Hardcoded_Function_Type::READ_F32:
        string_append_formated(string, "READ_F32");
        break;
    case Hardcoded_Function_Type::READ_BOOL:
        string_append_formated(string, "READ_BOOL");
        break;
    case Hardcoded_Function_Type::RANDOM_I32:
        string_append_formated(string, "RANDOM_I32");
        break;
    case Hardcoded_Function_Type::MALLOC_SIZE_I32:
        string_append_formated(string, "MALLOC_SIZE_I32");
        break;
    case Hardcoded_Function_Type::FREE_POINTER:
        string_append_formated(string, "FREE_POINTER");
        break;
    default: panic("Should not happen");
    }
}



/*
    COMPILER MEMBERS
*/
Constant_Pool constant_pool_create(Type_System* type_system)
{
    Constant_Pool result;
    result.buffer = dynamic_array_create_empty<byte>(2048);
    result.constants = dynamic_array_create_empty<Upp_Constant>(2048);
    result.references = dynamic_array_create_empty<Upp_Constant_Reference>(128);
    result.saved_pointers = hashtable_create_pointer_empty<void*, int>(32);
    result.type_system = type_system;
    result.max_buffer_size = 1024 * 1024; // 1 MB of constant buffer is allowed per default
    return result;
}

void constant_pool_destroy(Constant_Pool* pool) {
    dynamic_array_destroy(&pool->buffer);
    dynamic_array_destroy(&pool->constants);
    dynamic_array_destroy(&pool->references);
    hashtable_destroy(&pool->saved_pointers);
}

struct Offset_Result
{
    Constant_Status status;
    int offset;
};

Offset_Result offset_result_make_success(int offset) {
    Offset_Result result;
    result.status = Constant_Status::SUCCESS;
    result.offset = offset;
    return result;
}

Offset_Result offset_result_make_error(Constant_Status error_status) {
    Offset_Result result;
    result.status = error_status;
    result.offset = -1;
    return result;
}

Offset_Result constant_pool_add_constant_internal(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes);
const char* constant_status_to_string(Constant_Status status)
{
    switch (status)
    {
    case Constant_Status::SUCCESS: return "SUCCESS";
    case Constant_Status::CONTAINS_VOID_TYPE: return "CONTAINS_VOID_TYPE";
    case Constant_Status::CONTAINS_INVALID_POINTER_NOT_NULL: return "CONTAINS_INVALID_POINTER_NOT_NULL";
    case Constant_Status::CANNOT_SAVE_FUNCTIONS_YET: return "CANNOT_SAVE_FUNCTIONS_YET";
    case Constant_Status::CANNOT_SAVE_C_UNIONS_CONTAINING_REFERENCES: return "CANNOT_SAVE_C_UNIONS_CONTAINING_REFERENCES";
    case Constant_Status::CONTAINS_INVALID_UNION_TAG: return "CONTAINS_INVALID_UNION_TAG";
    case Constant_Status::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case Constant_Status::INVALID_SLICE_SIZE: return "INVALID_SLICE_SIZE";
    default: panic("");
    }
    return 0;
}

bool type_signature_contains_references(Type_Signature* signature)
{
    switch (signature->type)
    {
    case Signature_Type::VOID_TYPE: return false;
    case Signature_Type::PRIMITIVE: return false;
    case Signature_Type::POINTER: return true;
    case Signature_Type::FUNCTION: return true;
    case Signature_Type::STRUCT: 
    {
        for (int i = 0; i < signature->options.structure.members.size; i++) {
            Struct_Member* member = &signature->options.structure.members[i];
            if (type_signature_contains_references(member->type)) {
                return true;
            }
        }
        return false;
    }
    case Signature_Type::ENUM: return false;
    case Signature_Type::ARRAY: return type_signature_contains_references(signature->options.array.element_type);
    case Signature_Type::SLICE: return true;
    case Signature_Type::TEMPLATE_TYPE: return false;
    case Signature_Type::TYPE_TYPE: return false;
    case Signature_Type::UNKNOWN_TYPE: return false;
    default: panic("");
    }

    return false;
}

bool constant_pool_compare_constants(Constant_Pool* pool, Upp_Constant a, Upp_Constant b)
{
    /*
        It has to be assured that Struct-Constant Memory is initialized to zero,
        otherwise memory_compare cannot be used here, since the padding bytes may be uninitialized and therefore random

        Also, I could implement a deep comparison, but the use cases for that seem unclear
    */

    if (a.type != b.type) return false;
    if (a.constant_index == b.constant_index || a.offset == b.offset) return true;

    byte* pool_data = (byte*) pool->buffer.data;
    byte* raw_data_a = &pool_data[a.offset];
    byte* raw_data_b = &pool_data[b.offset];
    Type_Signature* signature = a.type;
    return memory_compare(raw_data_a, raw_data_b, signature->size);
}

Constant_Status constant_pool_search_references(Constant_Pool* pool, int data_offset, Type_Signature* signature)
{
    void* raw_data = &pool->buffer[data_offset];
    switch (signature->type)
    {
    case Signature_Type::VOID_TYPE:
        return Constant_Status::CONTAINS_VOID_TYPE;
    case Signature_Type::PRIMITIVE:
        break;
    case Signature_Type::POINTER:
    {
        void* ptr_value = *(void**)raw_data;
        if (ptr_value == nullptr) {
            break;
        }
        if (memory_is_readable(ptr_value, signature->options.pointer_child->size)) 
        {
            Upp_Constant_Reference reference;
            reference.ptr_offset = data_offset;
            Offset_Result ptr_result = constant_pool_add_constant_internal(
                pool, signature->options.pointer_child, array_create_static_as_bytes((byte*)ptr_value, signature->options.pointer_child->size)
            );
            if (ptr_result.status != Constant_Status::SUCCESS) return ptr_result.status;
            reference.buffer_destination_offset = ptr_result.offset;
            dynamic_array_push_back(&pool->references, reference);
        }
        else {
            return Constant_Status::CONTAINS_INVALID_POINTER_NOT_NULL;
        }
        break;
    }
    case Signature_Type::FUNCTION: {
        return Constant_Status::CANNOT_SAVE_FUNCTIONS_YET;
    }
    case Signature_Type::STRUCT:
    {
        // Loop over each member and call this function
        switch (signature->options.structure.struct_type)
        {
        case Structure_Type::STRUCT: 
        {
            for (int i = 0; i < signature->options.structure.members.size; i++) {
                Struct_Member* member = &signature->options.structure.members[i];
                Constant_Status member_status = constant_pool_search_references(pool, data_offset + member->offset, member->type);
                if (member_status != Constant_Status::SUCCESS) return member_status;
            }
            break;
        }
        case Structure_Type::C_UNION: {
            if (type_signature_contains_references(signature)) {
                return Constant_Status::CANNOT_SAVE_C_UNIONS_CONTAINING_REFERENCES;
            }
            break;
        }
        case Structure_Type::UNION: 
        {
            Type_Signature* tag_type = signature->options.structure.tag_member.type;
            assert(tag_type->type == Signature_Type::ENUM, "");
            int tag_value = *(int*)((byte*)raw_data + signature->options.structure.tag_member.offset);
            int found_member_index = -1;
            for (int i = 0; i < tag_type->options.enum_type.members.size; i++) 
            {
                Enum_Member* member = &tag_type->options.enum_type.members[i];
                if (member->value == tag_value) {
                    found_member_index = tag_value - 1;
                }
            }
            if (found_member_index != -1) {
                Struct_Member* member = &signature->options.structure.members[found_member_index];
                Constant_Status member_status = constant_pool_search_references(pool, data_offset + member->offset, member->type);
                if (member_status != Constant_Status::SUCCESS) return member_status;
            }
            else {
                return Constant_Status::CONTAINS_INVALID_UNION_TAG;
                //panic("Could not find active member of given tag value, union seems not to be initialized");
            }
            break;
        }
        default: panic("");
        }
        break;
    }
    case Signature_Type::ENUM: {
        break;
    }
    case Signature_Type::ARRAY: {
        if (type_signature_contains_references(signature->options.array.element_type)) {
            for (int i = 0; i < signature->options.array.element_count; i++) {
                int element_offset = i * signature->options.array.element_type->size;
                Constant_Status element_status = constant_pool_search_references(pool, data_offset + element_offset, signature->options.array.element_type);
                if (element_status != Constant_Status::SUCCESS) return element_status;
            }
        }
        break;
    }
    case Signature_Type::SLICE: 
    {
        // Check if pointer is valid, if true, save slice data
        Upp_Slice_Base slice = *(Upp_Slice_Base*)raw_data;
        if (slice.data_ptr == nullptr || slice.size == 0) {
            break;
        }
        if (slice.size <= 0) {
            return Constant_Status::INVALID_SLICE_SIZE;
        }
        if (memory_is_readable(slice.data_ptr, signature->options.slice.element_type->size * slice.size)) 
        {
            Upp_Constant_Reference reference;
            reference.ptr_offset = data_offset;
            Offset_Result data_result = constant_pool_add_constant_internal(
                pool, type_system_make_array(pool->type_system, signature->options.slice.element_type, true, slice.size), 
                array_create_static_as_bytes((byte*)slice.data_ptr, signature->options.slice.element_type->size * slice.size)
            );
            if (data_result.status != Constant_Status::SUCCESS) return data_result.status;
            reference.buffer_destination_offset = data_result.offset;
            dynamic_array_push_back(&pool->references, reference);
        }
        else {
            return Constant_Status::CONTAINS_INVALID_POINTER_NOT_NULL;
        }
    }
    case Signature_Type::TEMPLATE_TYPE:
    case Signature_Type::TYPE_TYPE:
    case Signature_Type::UNKNOWN_TYPE:
        break;
    default: panic("");
    }
    return Constant_Status::SUCCESS;
}

/*
    1. Reset Pointer Hashtable
    2. Push bytes into pool
    3. Add given pointer to hashtable
    4. Search through bytes for references
    5. If reference found, call add_constant with new reference, but don't reset hashtable
*/
Offset_Result constant_pool_add_constant_internal(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes)
{
    {
        int* found_offset = hashtable_find_element(&pool->saved_pointers, (void*)bytes.data);
        if (found_offset != 0) {
            return offset_result_make_success(*found_offset);
        }
    }
    if (pool->buffer.size + signature->alignment + signature->size > pool->max_buffer_size) {
        return offset_result_make_error(Constant_Status::OUT_OF_MEMORY);
    }

    // Reserve enough memory in pool
    dynamic_array_reserve(&pool->buffer, pool->buffer.size + signature->alignment + signature->size);

    // Align pool to type alignment
    while (pool->buffer.size % signature->alignment != 0) {
        dynamic_array_push_back(&pool->buffer, (byte)0);
    }

    int start_offset = pool->buffer.size;
    for (int i = 0; i < bytes.size; i++) {
        dynamic_array_push_back(&pool->buffer, bytes[i]);
    }
    hashtable_insert_element(&pool->saved_pointers, (void*)bytes.data, start_offset);
    Constant_Status status = constant_pool_search_references(pool, start_offset, signature);
    if (status != Constant_Status::SUCCESS) {
        return offset_result_make_error(status);
    }
    return offset_result_make_success(start_offset);
}

Constant_Result constant_pool_add_constant(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes)
{
    int rewind_index = pool->buffer.size;
    hashtable_reset(&pool->saved_pointers);
    Offset_Result offset_result = constant_pool_add_constant_internal(pool, signature, bytes);
    if (offset_result.status != Constant_Status::SUCCESS) {
        Constant_Result result;
        result.status = offset_result.status;
        result.constant.constant_index = -1;
        result.constant.offset = -1;
        result.constant.type = 0;
        return result;
    }

    Upp_Constant constant;
    constant.type = signature;
    constant.offset = offset_result.offset;
    constant.constant_index = pool->constants.size;
    dynamic_array_push_back(&pool->constants, constant);

    Constant_Result result;
    result.status = Constant_Status::SUCCESS;
    result.constant = constant;
    return result;
}

Extern_Sources extern_sources_create()
{
    Extern_Sources result;
    result.extern_functions = dynamic_array_create_empty<Extern_Function_Identifier>(8);
    result.headers_to_include = dynamic_array_create_empty<String*>(8);
    result.source_files_to_compile = dynamic_array_create_empty<String*>(8);
    result.lib_files = dynamic_array_create_empty<String*>(8);
    result.extern_type_signatures = hashtable_create_pointer_empty<Type_Signature*, String*>(8);
    return result;
}

void extern_sources_destroy(Extern_Sources* sources)
{
    dynamic_array_destroy(&sources->extern_functions);
    dynamic_array_destroy(&sources->headers_to_include);
    dynamic_array_destroy(&sources->source_files_to_compile);
    dynamic_array_destroy(&sources->lib_files);
    hashtable_destroy(&sources->extern_type_signatures);
}



Identifier_Pool identifier_pool_create()
{
    Identifier_Pool result;
    result.identifier_lookup_table = hashtable_create_empty<String, String*>(128, hash_string, string_equals);
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
        *copy = string_create(identifier.characters);
        hashtable_insert_element(&pool->identifier_lookup_table, *copy, copy);
        return copy;
    }
}

void identifier_pool_print(Identifier_Pool* pool)
{
    String msg = string_create_empty(256);
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

Code_Source* code_source_create(Code_Origin origin, String source_code)
{
    Code_Source* result = new Code_Source;
    result->origin = origin;
    result->source_code = source_code;
    result->tokens.data = 0;
    result->tokens_with_decoration.data = 0;
    result->root_node = 0;
    return result;
}

void code_source_destroy(Code_Source* source)
{
    string_destroy(&source->source_code);
    if (source->tokens.data != 0) {
        dynamic_array_destroy(&source->tokens);
    }
    if (source->tokens_with_decoration.data != 0) {
        dynamic_array_destroy(&source->tokens);
    }
    delete source;
}



/*
COMPILER
*/
Compiler compiler_create(Timer* timer)
{
    Compiler result;
    result.timer = timer;
    result.identifier_pool = identifier_pool_create();
    result.constant_pool = constant_pool_create(&result.type_system);
    result.extern_sources = extern_sources_create();

    result.type_system = type_system_create(timer);
    result.lexer = new Lexer;
    *result.lexer = lexer_create();
    result.parser = new AST_Parser;
    *result.parser = ast_parser_create();
    result.rc_analyser = new RC_Analyser;
    *result.rc_analyser = rc_analyser_create();
    result.analyser = semantic_analyser_create();
    result.ir_generator = new IR_Generator;
    *result.ir_generator = ir_generator_create();
    result.bytecode_generator = new Bytecode_Generator;
    *result.bytecode_generator = bytecode_generator_create();
    result.bytecode_interpreter = new Bytecode_Interpreter;
    *result.bytecode_interpreter = bytecode_intepreter_create();
    result.c_generator = new C_Generator;
    *result.c_generator = c_generator_create();
    result.c_compiler = new C_Compiler;
    *result.c_compiler = c_compiler_create();
    result.c_importer = new C_Importer;
    *result.c_importer = c_importer_create();

    result.code_sources = dynamic_array_create_empty<Code_Source*>(16);
    return result;
}

void compiler_destroy(Compiler* compiler)
{
    type_system_destroy(&compiler->type_system);
    identifier_pool_destroy(&compiler->identifier_pool);
    extern_sources_destroy(&compiler->extern_sources);
    constant_pool_destroy(&compiler->constant_pool);

    for (int i = 0; i < compiler->code_sources.size; i++) {
        code_source_destroy(compiler->code_sources[i]);
    }
    dynamic_array_destroy(&compiler->code_sources);

    lexer_destroy(compiler->lexer);
    delete compiler->lexer;
    ast_parser_destroy(compiler->parser);
    delete compiler->parser;
    rc_analyser_destroy(compiler->rc_analyser);
    delete compiler->rc_analyser;
    semantic_analyser_destroy(compiler->analyser);
    ir_generator_destroy(compiler->ir_generator);
    delete compiler->ir_generator;
    bytecode_generator_destroy(compiler->bytecode_generator);
    delete compiler->bytecode_generator;
    bytecode_interpreter_destroy(compiler->bytecode_interpreter);
    delete compiler->bytecode_interpreter;
    c_generator_destroy(compiler->c_generator);
    delete compiler->c_generator;
    c_importer_destroy(compiler->c_importer);
    delete compiler->c_importer;
    c_compiler_destroy(compiler->c_compiler);
    delete compiler->c_compiler;
}

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

void compiler_switch_timing_task(Compiler* compiler, Timing_Task task)
{
    if (task == compiler->task_current) return;
    double* add_to = 0;
    switch (compiler->task_current)
    {
    case Timing_Task::LEXING: add_to = &compiler->time_lexing; break;
    case Timing_Task::PARSING: add_to = &compiler->time_parsing; break;
    case Timing_Task::RC_GEN: add_to = &compiler->time_rc_gen; break;
    case Timing_Task::ANALYSIS: add_to = &compiler->time_analysing; break;
    case Timing_Task::CODE_GEN: add_to = &compiler->time_code_gen; break;
    case Timing_Task::CODE_EXEC: add_to = &compiler->time_code_exec; break;
    case Timing_Task::RESET: add_to = &compiler->time_reset; break;
    case Timing_Task::OUTPUT: add_to = &compiler->time_output; break;
    case Timing_Task::FINISH: {
        compiler->task_current = task;
        return;
    }
    default: panic("");
    }
    double now = timer_current_time_in_seconds(compiler->timer);
    double time_spent = now - compiler->task_last_start_time;
    *add_to = *add_to + time_spent;
    //logg("Spent %3.2fms on: %s\n", time_spent, timing_task_to_string(compiler->task_current));
    compiler->task_last_start_time = now;
    compiler->task_current = task;
}

bool compiler_errors_occured(Compiler* compiler) {
    return !(compiler->parser->errors.size == 0 && compiler->analyser->errors.size == 0 &&
        compiler->rc_analyser->errors.size == 0 && compiler->analyser->error_flag_count == 0);
}

void compiler_compile(Compiler* compiler, String source_code, bool generate_code)
{
    logg("\n\n\n   COMPILING\n---------------\n");
    double time_compile_start = timer_current_time_in_seconds(compiler->timer);
    compiler->generate_code = generate_code;
    {
        compiler->time_analysing = 0;
        compiler->time_code_gen = 0;
        compiler->time_lexing = 0;
        compiler->time_parsing = 0;
        compiler->time_rc_gen = 0;
        compiler->time_reset = 0;
        compiler->time_code_exec = 0;
        compiler->time_output = 0;
        compiler->task_last_start_time = time_compile_start;
        compiler->task_current = Timing_Task::FINISH;
    }

    compiler_switch_timing_task(compiler, Timing_Task::RESET);
    {
        // Reset data (FUTURE: Watch out for incremental compilation, pools should not be reset then)
        constant_pool_destroy(&compiler->constant_pool);
        compiler->constant_pool = constant_pool_create(&compiler->type_system);
        extern_sources_destroy(&compiler->extern_sources);
        compiler->extern_sources = extern_sources_create();

        for (int i = 0; i < compiler->code_sources.size; i++) {
            code_source_destroy(compiler->code_sources[i]);
        }
        dynamic_array_reset(&compiler->code_sources);

        // Reset stages
        type_system_reset(&compiler->type_system);
        rc_analyser_reset(compiler->rc_analyser, compiler);
        type_system_add_primitives(&compiler->type_system, &compiler->identifier_pool, &compiler->rc_analyser->predefined_symbols);
        ast_parser_reset(compiler->parser, &compiler->identifier_pool);
        semantic_analyser_reset(compiler->analyser, compiler);
        ir_generator_reset(compiler->ir_generator, compiler);
        bytecode_generator_reset(compiler->bytecode_generator, compiler);
        bytecode_interpreter_reset(compiler->bytecode_interpreter, compiler);
    }

    Code_Origin origin;
    origin.type = Code_Origin_Type::MAIN_PROJECT;
    compiler_add_source_code(compiler, source_code, origin);
    bool do_analysis = enable_lexing && enable_parsing && enable_analysis;

    compiler_switch_timing_task(compiler, Timing_Task::ANALYSIS);
    if (do_analysis) {
        dependency_graph_resolve(&compiler->analyser->dependency_graph);
        semantic_analyser_finish(compiler->analyser);
    }

    // Check for errors
    bool error_free = !compiler_errors_occured(compiler);
    bool do_ir_gen = do_analysis && enable_ir_gen && generate_code && error_free;
    bool do_bytecode_gen = do_ir_gen && enable_bytecode_gen && generate_code && error_free;
    bool do_c_generation = do_ir_gen && enable_c_generation && generate_code && error_free;
    bool do_c_compilation = do_c_generation && enable_c_compilation && generate_code && error_free;

    compiler_switch_timing_task(compiler, Timing_Task::CODE_GEN);
    if (do_ir_gen) {
        ir_generator_queue_and_generate_all(compiler->ir_generator);
    }
    if (do_bytecode_gen) {
        //bytecode_generator_generate(&compiler->bytecode_generator, compiler);
        bytecode_generator_set_entry_function(compiler->bytecode_generator);
    }
    if (do_c_generation) {
        c_generator_generate(compiler->c_generator, compiler);
    }
    if (do_c_compilation) {
        c_compiler_add_source_file(compiler->c_compiler, string_create_static("backend/src/main.cpp"));
        c_compiler_add_source_file(compiler->c_compiler, string_create_static("backend/src/hello_world.cpp"));
        c_compiler_add_source_file(compiler->c_compiler, string_create_static("backend/hardcoded/hardcoded_functions.cpp"));
        c_compiler_compile(compiler->c_compiler);
    }

    compiler_switch_timing_task(compiler, Timing_Task::OUTPUT);

    if (enable_output && generate_code)
    {
        //logg("\n\n\n\n\n\n\n\n\n\n\n\n--------SOURCE CODE--------: \n%s\n\n", source_code->characters);
        if (do_analysis && output_type_system) {
            logg("\n--------TYPE SYSTEM RESULT--------:\n");
            type_system_print(&compiler->type_system);
        }

        if (do_analysis && output_root_table)
        {
            logg("\n--------ROOT TABLE RESULT---------\n");
            String root_table = string_create_empty(1024);
            SCOPE_EXIT(string_destroy(&root_table));
            symbol_table_append_to_string(&root_table, compiler->rc_analyser->root_symbol_table, false);
            logg("%s", root_table.characters);
        }

        if (error_free)
        {
            if (do_analysis && output_ir)
            {
                logg("\n--------IR_PROGRAM---------\n");
                String tmp = string_create_empty(1024);
                SCOPE_EXIT(string_destroy(&tmp));
                ir_program_append_to_string(compiler->ir_generator->program, &tmp);
                logg("%s", tmp.characters);
            }

            if (do_bytecode_gen && output_bytecode)
            {
                String result_str = string_create_empty(32);
                SCOPE_EXIT(string_destroy(&result_str));
                if (do_bytecode_gen && output_bytecode) {
                    bytecode_generator_append_bytecode_to_string(compiler->bytecode_generator, &result_str);
                    logg("\n----------------BYTECODE_GENERATOR RESULT---------------: \n%s\n", result_str.characters);
                }
            }
        }
    }

    compiler_switch_timing_task(compiler, Timing_Task::FINISH);
    if (enable_output && output_timing && generate_code)
    {
        logg("\n-------- TIMINGS ---------\n");
        logg("reset       ... %3.2fms\n", (float)(compiler->time_reset) * 1000);
        if (enable_lexing) {
            logg("lexing      ... %3.2fms\n", (float)(compiler->time_lexing) * 1000);
        }
        if (enable_parsing) {
            logg("parsing     ... %3.2fms\n", (float)(compiler->time_parsing) * 1000);
        }
        if (enable_rc_gen) {
            logg("rc_gen      ... %3.2fms\n", (float)(compiler->time_rc_gen) * 1000);
        }
        if (enable_analysis) {
            logg("analysis    ... %3.2fms\n", (float)(compiler->time_analysing) * 1000);
            logg("code_exec   ... %3.2fms\n", (float)(compiler->time_code_exec) * 1000);
        }
        if (enable_bytecode_gen) {
            logg("code_gen    ... %3.2fms\n", (float)(compiler->time_code_gen) * 1000);
        }
        if (enable_output) {
            logg("output      ... %3.2fms\n", (float)(compiler->time_output) * 1000);
        }
        double sum = timer_current_time_in_seconds(compiler->timer) - time_compile_start;
        logg("--------------------------\n");
        logg("sum         ... %3.2fms\n", (float)(sum) * 1000);
        logg("--------------------------\n");
    }
}

Exit_Code compiler_execute(Compiler* compiler)
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
    if (!compiler_errors_occured(compiler) && do_execution)
    {
        if (execute_binary) {
            return c_compiler_execute(compiler->c_compiler);
        }
        else
        {
            double bytecode_start = timer_current_time_in_seconds(compiler->timer);
            compiler->bytecode_interpreter->instruction_limit_enabled = false;
            bytecode_interpreter_run_function(compiler->bytecode_interpreter, compiler->bytecode_generator->entry_point_index);
            double bytecode_end = timer_current_time_in_seconds(compiler->timer);
            float bytecode_time = (bytecode_end - bytecode_start);
            return compiler->bytecode_interpreter->exit_code;
        }
    }
    return Exit_Code::COMPILATION_FAILED;
}

void compiler_add_source_code(Compiler* compiler, String source_code, Code_Origin origin)
{
    bool do_lexing = enable_lexing;
    bool do_parsing = do_lexing && enable_parsing;
    bool do_rc_gen = do_parsing && enable_rc_gen;

    Timing_Task before = compiler->task_current;
    SCOPE_EXIT(compiler_switch_timing_task(compiler, before));

    Code_Source* code_source = code_source_create(origin, source_code);
    dynamic_array_push_back(&compiler->code_sources, code_source);
    if (origin.type == Code_Origin_Type::MAIN_PROJECT) {
        compiler->main_source = code_source;
    }

    if (do_lexing)
    {
        compiler_switch_timing_task(compiler, Timing_Task::LEXING);

        lexer_lex(compiler->lexer, &source_code, &compiler->identifier_pool);
        if (output_lexing) {
            logg("\n\n\n\n--------LEXER RESULT--------:\n");
            lexer_print(compiler->lexer);
        }
        if (output_identifiers) {
            logg("\n--------IDENTIFIERS:--------:\n");
            identifier_pool_print(&compiler->identifier_pool);
        }

        code_source->tokens = compiler->lexer->tokens;
        code_source->tokens_with_decoration = compiler->lexer->tokens_with_decoration;
        compiler->lexer->tokens = dynamic_array_create_empty<Token>(code_source->tokens.size);
        compiler->lexer->tokens_with_decoration = dynamic_array_create_empty<Token>(code_source->tokens_with_decoration.size);
    }

    if (do_parsing)
    {
        compiler_switch_timing_task(compiler, Timing_Task::PARSING);

        ast_parser_parse(compiler->parser, code_source);
        if (output_ast)
        {
            String printed_ast = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&printed_ast));
            ast_node_append_to_string(compiler->main_source, compiler->main_source->root_node, &printed_ast, 0);
            logg("\n");
            logg("--------AST PARSE RESULT--------:\n");
            logg("\n%s\n", printed_ast.characters);
        }
    }

    if (do_rc_gen)
    {
        compiler_switch_timing_task(compiler, Timing_Task::RC_GEN);
        rc_analyser_analyse(compiler->rc_analyser, code_source->root_node);
        compiler_switch_timing_task(compiler, Timing_Task::ANALYSIS);
        dependency_graph_add_workload_from_item(&compiler->analyser->dependency_graph, compiler->rc_analyser->root_item);

        if (output_rc)
        {
            String printed_items = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&printed_items));
            rc_analysis_item_append_to_string(compiler->rc_analyser->root_item, &printed_items, 0);
            logg("\n");
            logg("--------RC_ANALYSIS_ITEMS--------:\n");
            logg("\n%s\n", printed_items.characters);
        }
    }
}

Code_Source* compiler_ast_node_to_code_source(Compiler* compiler, AST_Node* node)
{
    for (int i = 1; i < compiler->code_sources.size; i++) {
        if (node->alloc_index < compiler->code_sources[i]->root_node->alloc_index) {
            return compiler->code_sources[i - 1];
        }
    }
    return compiler->code_sources[compiler->code_sources.size - 1];
}

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

void compiler_run_testcases(Timer* timer)
{
    if (!enable_testcases) return;
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
    bool i_output_lexing = output_lexing;
    SCOPE_EXIT(output_lexing = i_output_lexing;);
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
    enable_c_generation = true;
    enable_c_compilation = run_testcases_compiled;
    enable_output = false;
    enable_execution = true;
    execute_binary = run_testcases_compiled;

    output_lexing = false;
    output_identifiers = false;
    output_ast = false;
    output_type_system = false;
    output_root_table = false;
    output_ir = false;
    output_bytecode = false;
    output_timing = false;

    logg("STARTING ALL TESTS:\n-----------------------------\n");

    Compiler compiler = compiler_create(timer);
    SCOPE_EXIT(compiler_destroy(&compiler));

    // Create testcases with expected result
    Test_Case test_cases[] = {
        test_case_make("000_empty.upp", false),
        test_case_make("001_main.upp", true),
        test_case_make("002_comments.upp", true),
        test_case_make("003_valid_comment.upp", true),
        test_case_make("004_invalid_comment.upp", false),
        test_case_make("005_variable_definition.upp", true),
        test_case_make("006_primitive_types.upp", true),
        test_case_make("007_pointers_and_arrays.upp", true),
        test_case_make("008_operator_precedence.upp", true),
        test_case_make("009_function_calls.upp", true),
        test_case_make("010_file_loads.upp", true),
        test_case_make("011_pointers.upp", true),
        test_case_make("012_new_delete.upp", true),
        test_case_make("013_structs.upp", true),
        test_case_make("014_templates.upp", true),
        test_case_make("015_defer.upp", true),
        test_case_make("016_casting.upp", true),
        test_case_make("017_function_pointers.upp", true),
        test_case_make("018_modules.upp", true),
        test_case_make("019_scopes.upp", true),
        test_case_make("020_globals.upp", true),
        test_case_make("021_slices.upp", true),
        test_case_make("022_dynamic_array.upp", true),
        test_case_make("023_invalid_recursive_template.upp", false),
        test_case_make("024_expression_context.upp", true),
        test_case_make("025_expression_context_limit.upp", false),
        test_case_make("026_auto_cast.upp", true),
        test_case_make("027_enums.upp", true),
        test_case_make("028_invalid_enum.upp", false),
        test_case_make("029_switch.upp", true),
        test_case_make("030_invalid_switch_cases_missing.upp", false),
        test_case_make("031_invalid_switch_case_not_constant.upp", false),
        test_case_make("032_invalid_switch_value_not_in_range.upp", false),
        test_case_make("033_constant_propagation.upp", true),
        test_case_make("034_constant_propagation_invalid_reference.upp", false),
        test_case_make("035_constant_propagation_control_flow.upp", false),
        test_case_make("036_bake.upp", true),
        test_case_make("037_bake_instruction_limit.upp", false),
        test_case_make("038_bake_exception.upp", false),
        test_case_make("039_struct_initializer.upp", true),
        test_case_make("040_struct_initializer_exhaustive_error.upp", false),
        test_case_make("041_struct_initializer_double_set_error.upp", false),
        test_case_make("042_array_initializer.upp", true),
        test_case_make("043_auto_syntax.upp", true),
        test_case_make("044_c_unions.upp", true),
        test_case_make("045_unions.upp", true),
        test_case_make("046_types_as_values.upp", true),
        test_case_make("047_type_info.upp", true),
        test_case_make("048_any_type.upp", true),
        test_case_make("049_any_error.upp", false),
        test_case_make("050_named_break_continue.upp", true),
        test_case_make("051_invalid_continue_no_loop.upp", false),
        test_case_make("052_invalid_lables.upp", false),
        test_case_make("053_named_flow_defer.upp", true),
    };
    int test_case_count = sizeof(test_cases) / sizeof(Test_Case);

    bool errors_occured = false;
    String result = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&result));
    for (int i = 0; i < test_case_count; i++)
    {
        Test_Case* test_case = &test_cases[i];
        String path = string_create_formated("upp_code/testcases/%s", test_case->name);
        SCOPE_EXIT(string_destroy(&path));
        Optional<String> code = file_io_load_text_file(path.characters);
        if (!code.available) {
            string_append_formated(&result, "ERROR:   Test %s could not load test file\n", test_case->name);
            errors_occured = true;
            continue;
        }

        compiler_compile(&compiler, code.value, true);
        Exit_Code exit_code = compiler_execute(&compiler);
        if (exit_code != Exit_Code::SUCCESS && test_case->should_succeed)
        {
            string_append_formated(&result, "ERROR:   Test %s exited with Code ", test_case->name);
            exit_code_append_to_string(&result, exit_code);
            string_append_formated(&result, "\n");
            if (exit_code == Exit_Code::COMPILATION_FAILED)
            {
                for (int i = 0; i < compiler.parser->errors.size; i++) {
                    Compiler_Error e = compiler.parser->errors[i];
                    string_append_formated(&result, "    Parse Error: %s\n", e.message);
                }
                if (compiler.parser->errors.size == 0)
                {
                    String tmp = string_create_empty(256);
                    SCOPE_EXIT(string_destroy(&tmp));
                    for (int i = 0; i < compiler.analyser->errors.size; i++)
                    {
                        Semantic_Error e = compiler.analyser->errors[i];
                        string_append_formated(&result, "    Semantic Error: ");
                        semantic_error_append_to_string(compiler.analyser, e, &result);
                        string_append_formated(&result, "\n");
                    }
                }
            }
            errors_occured = true;
        }
        else {
            string_append_formated(&result, "SUCCESS: Test %s\n", test_case->name);
        }
    }

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
    Optional<String> text = file_io_load_text_file("upp_code/testcases/022_dynamic_array.upp");
    SCOPE_EXIT(file_io_unload_text_file(&text););
    if (!text.available) {
        return;
    }

    double time_stress_start = timer_current_time_in_seconds(timer);

    String code = text.value;
    for (int i = 0; i < code.size; i++)
    {
        String cut_code = string_create_empty(i + 10);
        for (int j = 0; j < i; j++) {
            char c = code.characters[j];
            string_append_character(&cut_code, c);
        }

        //logg("Cut code:\n-----------------------\n%s", cut_code.characters);
        compiler_compile(&compiler, cut_code, false);
        if (i % (code.size / 10) == 0) {
            logg("Stresstest (Simple): %d/%d characters\n", i, code.size);
        }
    }

    // Stress testing again but with correct parenthesis order
    Dynamic_Array<char> stack_parenthesis = dynamic_array_create_empty<char>(256);
    SCOPE_EXIT(dynamic_array_destroy(&stack_parenthesis));
    for (int i = 0; i < code.size; i++)
    {
        dynamic_array_reset(&stack_parenthesis);
        String cut_code = string_create_empty(i + 10);
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
        compiler_compile(&compiler, cut_code, false);
        if (i % (code.size / 10) == 0) {
            logg("Stresstest (Parenthesis): %d/%d characters\n", i, code.size);
        }
    }

    double time_stress_end = timer_current_time_in_seconds(timer);
    float ms_time = (time_stress_end - time_stress_start) * 1000.0f;
    logg("Stress test time: %3.2fms (%3.2fms per parse/analyse)\n", ms_time, ms_time / code.size / 2.0f);
}
