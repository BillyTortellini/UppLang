#include "compiler.hpp"
#include "../../win32/timing.hpp"
#include "../../utility/file_io.hpp"



bool enable_lexing = true;
bool enable_parsing = true;
bool enable_analysis = true;
bool enable_ir_gen = true;
bool enable_bytecode_gen = true;
bool enable_c_generation = false;
bool enable_c_compilation = true;
bool enable_output = true;
bool enable_execution = true;
bool execute_binary = false;

bool output_lexing = false;
bool output_identifiers = false;
bool output_ast = false;
bool output_type_system = false;
bool output_root_table = false;
bool output_ir = false;
bool output_bytecode = false;
bool output_timing = true;



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
    assert(range.start_index >= 0 && range.start_index <= source->tokens.size, "HEY");
    assert(range.end_index >= 0 && range.end_index <= source->tokens.size + 1, "HEY");
    assert(range.end_index >= range.start_index, "HEY");
    if (range.end_index > source->tokens.size) {
        range.end_index = source->tokens.size;
    }
    if (source->tokens.size == 0) {
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
Constant_Pool constant_pool_create()
{
    Constant_Pool result;
    result.buffer = dynamic_array_create_empty<byte>(2048);
    result.constants = dynamic_array_create_empty<Upp_Constant>(2048);
    return result;
}

void constant_pool_destroy(Constant_Pool* pool) {
    dynamic_array_destroy(&pool->buffer);
    dynamic_array_destroy(&pool->constants);
}

int constant_pool_add_constant(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes)
{
    dynamic_array_reserve(&pool->buffer, pool->buffer.size + signature->alignment + signature->size);
    while (pool->buffer.size % signature->alignment != 0) {
        dynamic_array_push_back(&pool->buffer, (byte)0);
    }

    Upp_Constant constant;
    constant.type = signature;
    constant.offset = pool->buffer.size;
    dynamic_array_push_back(&pool->constants, constant);

    for (int i = 0; i < bytes.size; i++) {
        dynamic_array_push_back(&pool->buffer, bytes[i]);
    }
    return pool->constants.size - 1;
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
    result.constant_pool = constant_pool_create();
    result.extern_sources = extern_sources_create();

    result.lexer = lexer_create();
    result.parser = ast_parser_create();
    result.type_system = type_system_create();
    result.analyser = semantic_analyser_create();
    result.bytecode_generator = bytecode_generator_create();
    result.bytecode_interpreter = bytecode_intepreter_create();
    result.c_generator = c_generator_create();
    result.c_compiler = c_compiler_create();
    result.c_importer = c_importer_create();
    result.ir_generator = ir_generator_create();

    result.code_sources = dynamic_array_create_empty<Code_Source*>(16);
    return result;
}

void compiler_destroy(Compiler* compiler)
{
    ast_parser_destroy(&compiler->parser);
    lexer_destroy(&compiler->lexer);
    type_system_destroy(&compiler->type_system);
    semantic_analyser_destroy(&compiler->analyser);
    bytecode_generator_destroy(&compiler->bytecode_generator);
    bytecode_interpreter_destroy(&compiler->bytecode_interpreter);
    c_generator_destroy(&compiler->c_generator);
    c_compiler_destroy(&compiler->c_compiler);
    c_importer_destroy(&compiler->c_importer);
    ir_generator_destroy(&compiler->ir_generator);
    identifier_pool_destroy(&compiler->identifier_pool);
    extern_sources_destroy(&compiler->extern_sources);
    constant_pool_destroy(&compiler->constant_pool);
    for (int i = 0; i < compiler->code_sources.size; i++) {
        code_source_destroy(compiler->code_sources[i]);
    }
    dynamic_array_destroy(&compiler->code_sources);
}

void compiler_compile(Compiler* compiler, String source_code, bool generate_code)
{
    compiler->generate_code = generate_code;
    bool do_analysis = enable_lexing && enable_parsing && enable_analysis;

    // Reset data (FUTURE: Watch out for incremental compilation, pools should not be reset then)
    constant_pool_destroy(&compiler->constant_pool);
    compiler->constant_pool = constant_pool_create();
    extern_sources_destroy(&compiler->extern_sources);
    compiler->extern_sources = extern_sources_create();

    type_system_reset(&compiler->type_system);
    type_system_add_primitives(&compiler->type_system, &compiler->identifier_pool);
    ast_parser_reset(&compiler->parser, &compiler->identifier_pool);
    semantic_analyser_reset(&compiler->analyser, compiler);

    Code_Origin origin;
    origin.type = Code_Origin_Type::MAIN_PROJECT;
    compiler_add_source_code(compiler, source_code, origin);
    semantic_analyser_execute_workloads(&compiler->analyser);

    // Check for errors
    bool error_free = compiler->parser.errors.size == 0 && compiler->analyser.errors.size == 0;
    bool do_ir_gen = do_analysis && enable_ir_gen && generate_code && error_free;
    bool do_bytecode_gen = do_ir_gen && enable_bytecode_gen && generate_code && error_free;
    bool do_c_generation = do_ir_gen && enable_c_generation && generate_code && error_free;
    bool do_c_compilation = do_c_generation && enable_c_compilation && generate_code && error_free;

    double time_start_ir_gen = timer_current_time_in_seconds(compiler->timer);
    if (do_ir_gen) {
        ir_generator_generate(&compiler->ir_generator, compiler);
    }
    double time_end_ir_gen = timer_current_time_in_seconds(compiler->timer);

    double time_start_codegen = timer_current_time_in_seconds(compiler->timer);
    if (do_bytecode_gen) {
        bytecode_generator_generate(&compiler->bytecode_generator, compiler);
    }
    double time_end_codegen = timer_current_time_in_seconds(compiler->timer);

    double time_start_c_gen = timer_current_time_in_seconds(compiler->timer);
    if (do_c_generation) {
        c_generator_generate(&compiler->c_generator, compiler);
    }
    double time_end_c_gen = timer_current_time_in_seconds(compiler->timer);

    double time_start_c_comp = timer_current_time_in_seconds(compiler->timer);
    if (do_c_compilation) {
        c_compiler_add_source_file(&compiler->c_compiler, string_create_static("backend/src/main.cpp"));
        c_compiler_add_source_file(&compiler->c_compiler, string_create_static("backend/src/hello_world.cpp"));
        c_compiler_add_source_file(&compiler->c_compiler, string_create_static("backend/hardcoded/hardcoded_functions.cpp"));
        c_compiler_compile(&compiler->c_compiler);
    }
    double time_end_c_comp = timer_current_time_in_seconds(compiler->timer);

    double time_start_output = timer_current_time_in_seconds(compiler->timer);
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
            symbol_table_append_to_string(&root_table, compiler->analyser.program->root_module->symbol_table, &compiler->analyser, false);
            logg("%s", root_table.characters);
        }

        if (compiler->analyser.errors.size == 0 && compiler->parser.errors.size == 0)
        {
            if (do_analysis && output_ir)
            {
                logg("\n--------IR_PROGRAM---------\n");
                String tmp = string_create_empty(1024);
                SCOPE_EXIT(string_destroy(&tmp));
                ir_program_append_to_string(compiler->ir_generator.program, &tmp);
                logg("%s", tmp.characters);
            }

            if (do_bytecode_gen && output_bytecode)
            {
                String result_str = string_create_empty(32);
                SCOPE_EXIT(string_destroy(&result_str));
                if (do_bytecode_gen && output_bytecode) {
                    bytecode_generator_append_bytecode_to_string(&compiler->bytecode_generator, &result_str);
                    logg("\n----------------BYTECODE_GENERATOR RESULT---------------: \n%s\n", result_str.characters);
                }
            }
        }
    }
    double time_end_output = timer_current_time_in_seconds(compiler->timer);

    if (enable_output && output_timing && generate_code)
    {
        logg("\n--------- TIMINGS -----------\n");
        if (enable_ir_gen) {
            logg("ir_gen       ... %3.2fms\n", (time_end_ir_gen - time_start_ir_gen) * 1000);
        }
        if (enable_bytecode_gen) {
            logg("bytecode_gen ... %3.2fms\n", (time_end_codegen - time_start_codegen) * 1000);
        }
        if (enable_output) {
            logg("output       ... %3.2fms\n", (time_end_output - time_start_output) * 1000);
        }
        if (enable_c_generation) {
            logg("c_gen        ... %3.2fms\n", (time_end_c_gen - time_start_c_gen) * 1000);
        }
        if (enable_c_compilation) {
            logg("g_comp       ... %3.2fms\n", (time_end_c_comp - time_start_c_comp) * 1000);
        }
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
    if (compiler->parser.errors.size == 0 && compiler->analyser.errors.size == 0 && do_execution)
    {
        if (execute_binary) {
            return c_compiler_execute(&compiler->c_compiler);
        }
        else
        {
            double bytecode_start = timer_current_time_in_seconds(compiler->timer);
            bytecode_interpreter_execute_main(&compiler->bytecode_interpreter, compiler);
            double bytecode_end = timer_current_time_in_seconds(compiler->timer);
            float bytecode_time = (bytecode_end - bytecode_start);
            return compiler->bytecode_interpreter.exit_code;
        }
    }
    return Exit_Code::COMPILATION_FAILED;
}

void compiler_add_source_code(Compiler* compiler, String source_code, Code_Origin origin)
{
    bool do_lexing = enable_lexing;
    bool do_parsing = do_lexing && enable_parsing;
    bool do_analysis = do_parsing && enable_analysis;

    Code_Source* code_source = code_source_create(origin, source_code);
    dynamic_array_push_back(&compiler->code_sources, code_source);
    if (origin.type == Code_Origin_Type::MAIN_PROJECT) {
        compiler->main_source = code_source;
    }

    if (do_lexing) 
    {
        lexer_lex(&compiler->lexer, &source_code, &compiler->identifier_pool);
        if (output_lexing) {
            logg("\n\n\n\n--------LEXER RESULT--------:\n");
            lexer_print(&compiler->lexer);
        }
        if (output_identifiers) {
            logg("\n--------IDENTIFIERS:--------:\n");
            identifier_pool_print(&compiler->identifier_pool);
        }

        code_source->tokens = compiler->lexer.tokens;
        code_source->tokens_with_decoration = compiler->lexer.tokens_with_decoration;
        compiler->lexer.tokens = dynamic_array_create_empty<Token>(code_source->tokens.size);
        compiler->lexer.tokens_with_decoration = dynamic_array_create_empty<Token>(code_source->tokens_with_decoration.size);
    }

    if (do_parsing) 
    {
        ast_parser_parse(&compiler->parser, code_source);
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

    if (do_analysis)
    {
        // Add analysis workload
        Analysis_Workload workload;
        workload.type = Analysis_Workload_Type::MODULE_ANALYSIS;
        workload.options.module_analysis.root_node = code_source->root_node;
        dynamic_array_push_back(&compiler->analyser.active_workloads, workload);
    }
}

Code_Source* compiler_ast_node_to_code_source(Compiler* compiler, AST_Node* node)
{
    for (int i = 1; i < compiler->code_sources.size; i++) {
        if (node->alloc_index < compiler->code_sources[i]->root_node->alloc_index) {
            return compiler->code_sources[i-1];
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
    bool i_enable_lexing = enable_lexing;
    SCOPE_EXIT(enable_lexing = i_enable_lexing;);
    bool i_enable_parsing = enable_parsing;
    SCOPE_EXIT(enable_parsing = i_enable_parsing ;);
    bool i_enable_analysis = enable_analysis;
    SCOPE_EXIT(enable_analysis = i_enable_analysis ;);
    bool i_enable_ir_gen = enable_ir_gen;
    SCOPE_EXIT(enable_ir_gen = i_enable_ir_gen ;);
    bool i_enable_bytecode_gen = enable_bytecode_gen;
    SCOPE_EXIT(enable_bytecode_gen = i_enable_bytecode_gen ;);
    bool i_enable_c_generation = enable_c_generation;
    SCOPE_EXIT(enable_c_generation = i_enable_c_generation ;);
    bool i_enable_c_compilation = enable_c_compilation;
    SCOPE_EXIT(enable_c_compilation = i_enable_c_compilation ;);
    bool i_enable_output = enable_output;
    SCOPE_EXIT(enable_output = i_enable_output ;);
    bool i_enable_execution = enable_execution;
    SCOPE_EXIT(enable_execution = i_enable_execution ;);
    bool i_execute_binary = execute_binary;
    SCOPE_EXIT(execute_binary = i_execute_binary ;);
    bool i_output_lexing = output_lexing;
    SCOPE_EXIT(output_lexing = i_output_lexing ;);
    bool i_output_identifiers = output_identifiers;
    SCOPE_EXIT(output_identifiers = i_output_identifiers ;);
    bool i_output_ast = output_ast;
    SCOPE_EXIT(output_ast = i_output_ast ;);
    bool i_output_type_system = output_type_system;
    SCOPE_EXIT(output_type_system = i_output_type_system ;);
    bool i_output_root_table = output_root_table;
    SCOPE_EXIT(output_root_table = i_output_root_table ;);
    bool i_output_ir = output_ir;
    SCOPE_EXIT(output_ir = i_output_ir ;);
    bool i_output_bytecode = output_bytecode;
    SCOPE_EXIT(output_bytecode = i_output_bytecode ;);
    bool i_output_timing = output_timing;
    SCOPE_EXIT(output_timing = i_output_timing ;);

    enable_lexing = true;
    enable_parsing = true;
    enable_analysis = true;
    enable_ir_gen = true;
    enable_bytecode_gen = true;
    enable_c_generation = true;
    enable_c_compilation = false;
    enable_output = true;
    enable_execution = true;
    execute_binary = false;

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
                for (int i = 0; i < compiler.parser.errors.size; i++) {
                    Compiler_Error e = compiler.parser.errors[i];
                    string_append_formated(&result, "    Parse Error: %s\n", e.message);
                }
                if (compiler.parser.errors.size == 0)
                {
                    String tmp = string_create_empty(256);
                    SCOPE_EXIT(string_destroy(&tmp));
                    for (int i = 0; i < compiler.analyser.errors.size; i++)
                    {
                        Semantic_Error e = compiler.analyser.errors[i];
                        string_append_formated(&result, "    Semantic Error: ");
                        semantic_error_append_to_string(&compiler.analyser, e, &result);
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
}
