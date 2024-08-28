#include "compiler.hpp"
#include "../../win32/timing.hpp"
#include "../../win32/windows_helper_functions.hpp"
#include "../../utility/file_io.hpp"

#include "semantic_analyser.hpp"
#include "ir_code.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "c_backend.hpp"
#include "parser.hpp"
#include "ast.hpp"
#include "lexer.hpp"
#include "symbol_table.hpp"

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
bool output_ast = false;
bool output_type_system = false;
bool output_root_table = false;
bool output_ir = true;
bool output_bytecode = false;
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


// This variable gets written to in compiler_compile
bool do_output;

// GLOBALS
Compiler compiler;



// Code_Source
Code_Source* code_source_create_empty(Code_Origin origin, Source_Code* code, String file_path)
{
    Code_Source* result = new Code_Source;
    result->origin = origin;
    result->code = code;
    result->parsed_code = nullptr;
    result->module_progress = nullptr;
    result->file_path = file_path;
    dynamic_array_push_back(&compiler.code_sources, result);
    hashtable_insert_element(&compiler.cached_imports, file_path, result);
    return result;
}

void code_source_destroy(Code_Source* source)
{
    if (source->origin != Code_Origin::MAIN_PROJECT) { // Main project code gets destroyed by syntax_editor_destroy
        source_code_destroy(source->code);
    }
    string_destroy(&source->file_path);
    if (source->parsed_code != 0) {
        Parser::source_parse_destroy(source->parsed_code);
    }
    delete source;
}



// COMPILER
Compiler* compiler_initialize(Timer* timer)
{
    compiler.timer = timer;
    compiler.identifier_pool = identifier_pool_create();
    compiler.constant_pool = constant_pool_create();
    compiler.extern_sources = extern_sources_create();
    compiler.cached_imports = hashtable_create_empty<String, Code_Source*>(1, hash_string, string_equals);
    compiler.fiber_pool = fiber_pool_create();
    compiler.random = random_make_time_initalized();

    Parser::initialize();
    lexer_initialize(&compiler.identifier_pool);

    compiler.type_system = type_system_create(timer);
    compiler.semantic_analyser = semantic_analyser_initialize();
    compiler.ir_generator = ir_generator_initialize();
    compiler.bytecode_generator = new Bytecode_Generator;
    *compiler.bytecode_generator = bytecode_generator_create();
    compiler.c_generator = new C_Generator;
    *compiler.c_generator = c_generator_create();
    compiler.c_compiler = new C_Compiler;
    *compiler.c_compiler = c_compiler_create();

    compiler.code_sources = dynamic_array_create<Code_Source*>(1);
    return &compiler;
}

void compiler_destroy()
{
    Parser::destroy();
    lexer_shutdown();
    fiber_pool_destroy(compiler.fiber_pool);
    compiler.fiber_pool = 0;

    type_system_destroy(&compiler.type_system);
    identifier_pool_destroy(&compiler.identifier_pool);
    extern_sources_destroy(&compiler.extern_sources);
    constant_pool_destroy(&compiler.constant_pool);

    for (int i = 0; i < compiler.code_sources.size; i++) {
        code_source_destroy(compiler.code_sources[i]);
        compiler.code_sources[i] = 0;
    }
    dynamic_array_destroy(&compiler.code_sources);
    hashtable_destroy(&compiler.cached_imports);

    semantic_analyser_destroy();
    ir_generator_destroy();
    bytecode_generator_destroy(compiler.bytecode_generator);
    delete compiler.bytecode_generator;
    c_generator_destroy(compiler.c_generator);
    delete compiler.c_generator;
    c_compiler_destroy(compiler.c_compiler);
    delete compiler.c_compiler;
}



// Compiling
void compiler_lex_parse_and_add_workload_for_code_source(Code_Source* source, Code_History* history_for_incremental, bool is_root_module)
{
    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));

    // Lex code if required
    compiler_switch_timing_task(Timing_Task::LEXING);
    if (!enable_lexing) {
        return;
    }
    if (history_for_incremental == 0) {
        source_code_tokenize(source->code);
    }

    // Parse code
    compiler_switch_timing_task(Timing_Task::PARSING);
    if (!enable_parsing) {
        return;
    }
    if (history_for_incremental != 0) {
        assert(source->parsed_code != 0, "Not quite sure about this condition yet");
        Parser::execute_incremental(source->parsed_code, history_for_incremental);
    }
    else {
        assert(source->parsed_code == 0, "Hey");
        source->parsed_code = Parser::execute_clean(source->code);
    }

    compiler_switch_timing_task(Timing_Task::ANALYSIS);
    if (!enable_analysis) {
        return;
    }
    assert(source->module_progress == 0, "At this point this has to be 0 me thinks");
    source->module_progress = workload_executer_add_module_discovery(source->parsed_code->root, is_root_module);
}



void compiler_reset_data(bool keep_data_for_incremental_compile, Compile_Type compile_type)
{
    bool generate_code = compile_type == Compile_Type::BUILD_CODE;
    do_output = enable_output && !(output_only_on_code_gen && !generate_code);
    if (do_output) {
        // logg("\n\n\n   COMPILING\n---------------\n");
    }
    compiler.time_compile_start = timer_current_time_in_seconds(compiler.timer);
    compiler.generate_code = generate_code;
    {
        compiler.time_analysing = 0;
        compiler.time_code_gen = 0;
        compiler.time_lexing = 0;
        compiler.time_parsing = 0;
        compiler.time_reset = 0;
        compiler.time_code_exec = 0;
        compiler.time_output = 0;
        compiler.task_last_start_time = compiler.time_compile_start;
        compiler.task_current = Timing_Task::FINISH;
    }

    compiler_switch_timing_task(Timing_Task::RESET);
    {
        // NOTE: Identifier pool is not beeing reset because Syntax-Editor already does incremental lexing
        // Predefined ids
        {
            auto& ids = compiler.predefined_ids;
            auto add_id = [&](const char* id) -> String* {
                return identifier_pool_add(&compiler.identifier_pool, string_create_static(id));
            };

            ids.size =                add_id("size");
            ids.data =                add_id("data");
            ids.tag =                 add_id("tag");
            ids.main =                add_id("main");
            ids.type_of =             add_id("type_of");
            ids.type_info =           add_id("type_info");
            ids.empty_string =        add_id("");
            ids.invalid_symbol_name = add_id("__INVALID_SYMBOL_NAME");
            ids.id_struct =           add_id("Struct");
            ids.byte =                add_id("byte");
            ids.value =               add_id("value");

            ids.cast_mode =          add_id("Cast_Mode");
            ids.cast_mode_none =     add_id("NONE");
            ids.cast_mode_explicit = add_id("EXPLICIT");
            ids.cast_mode_inferred = add_id("INFERRED");
            ids.cast_mode_implicit = add_id("IMPLICIT");

            ids.id_import = add_id("import");
            ids.set_option = add_id("set_option");
            ids.set_cast_option = add_id("set_cast_option");
            ids.add_binop = add_id("add_binop");
            ids.add_unop = add_id("add_unop");
            ids.add_cast = add_id("add_cast");
            ids.add_array_access = add_id("add_array_access");
            ids.add_dot_call = add_id("add_dot_call");
            ids.add_iterator = add_id("add_iterator");

            ids.cast_option = add_id("Cast_Option");
            ids.cast_option_enum_values[(int)Cast_Option::ARRAY_TO_SLICE] = add_id("ARRAY_TO_SLICE");
            ids.cast_option_enum_values[(int)Cast_Option::INTEGER_SIZE_UPCAST] = add_id("INTEGER_SIZE_UPCAST");
            ids.cast_option_enum_values[(int)Cast_Option::INTEGER_SIZE_DOWNCAST] = add_id("INTEGER_SIZE_DOWNCAST");
            ids.cast_option_enum_values[(int)Cast_Option::INTEGER_SIGNED_TO_UNSIGNED] = add_id("INTEGER_SIGNED_TO_UNSIGNED");
            ids.cast_option_enum_values[(int)Cast_Option::INTEGER_UNSIGNED_TO_SIGNED] = add_id("INTEGER_UNSIGNED_TO_SIGNED");
            ids.cast_option_enum_values[(int)Cast_Option::FLOAT_SIZE_UPCAST] = add_id("FLOAT_SIZE_UPCAST");
            ids.cast_option_enum_values[(int)Cast_Option::FLOAT_SIZE_DOWNCAST] = add_id("FLOAT_SIZE_DOWNCAST");
            ids.cast_option_enum_values[(int)Cast_Option::INT_TO_FLOAT] = add_id("INT_TO_FLOAT");
            ids.cast_option_enum_values[(int)Cast_Option::FLOAT_TO_INT] = add_id("FLOAT_TO_INT");
            ids.cast_option_enum_values[(int)Cast_Option::POINTER_TO_POINTER] = add_id("POINTER_TO_POINTER");
            ids.cast_option_enum_values[(int)Cast_Option::FROM_BYTE_POINTER] = add_id("FROM_BYTE_POINTER");
            ids.cast_option_enum_values[(int)Cast_Option::TO_BYTE_POINTER] = add_id("TO_BYTE_POINTER");
            ids.cast_option_enum_values[(int)Cast_Option::POINTER_NULL_CHECK] = add_id("POINTER_NULL_CHECK");
            ids.cast_option_enum_values[(int)Cast_Option::TO_ANY] = add_id("TO_ANY");
            ids.cast_option_enum_values[(int)Cast_Option::FROM_ANY] = add_id("FROM_ANY");
            ids.cast_option_enum_values[(int)Cast_Option::ENUM_TO_INT] = add_id("ENUM_TO_INT");
            ids.cast_option_enum_values[(int)Cast_Option::INT_TO_ENUM] = add_id("INT_TO_ENUM");
            ids.cast_option_enum_values[(int)Cast_Option::ARRAY_TO_SLICE] = add_id("ARRAY_TO_SLICE");
            ids.cast_option_enum_values[(int)Cast_Option::TO_SUBTYPE] = add_id("TO_SUBTYPE");
        }

        // FUTURE: When we have incremental compilation we cannot just reset everything anymore
        // Reset Data
        fiber_pool_check_all_handles_completed(compiler.fiber_pool);
        constant_pool_destroy(&compiler.constant_pool);
        compiler.constant_pool = constant_pool_create();
        extern_sources_destroy(&compiler.extern_sources);
        compiler.extern_sources = extern_sources_create();

        if (!keep_data_for_incremental_compile) {
            compiler.main_source = 0;
        }
        for (int i = 0; i < compiler.code_sources.size; i++) {
            auto source = compiler.code_sources[i];
            if (keep_data_for_incremental_compile) {
                source->module_progress = 0;
            }
            else {
                code_source_destroy(compiler.code_sources[i]);
                compiler.code_sources[i] = 0;
            }
        }
        if (!keep_data_for_incremental_compile) {
            dynamic_array_reset(&compiler.code_sources);
            hashtable_reset(&compiler.cached_imports);
        }

        // Reset stages
        type_system_reset(&compiler.type_system);
        type_system_add_predefined_types(&compiler.type_system);
        if (!keep_data_for_incremental_compile) {
            Parser::reset();
        }

        semantic_analyser_reset();
        ir_generator_reset();
        bytecode_generator_reset(compiler.bytecode_generator, &compiler);
    }
}

void compiler_execute_analysis_workloads_and_code_generation()
{
    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));

    // ANALYSE CODE
    compiler_switch_timing_task(Timing_Task::ANALYSIS);
    bool do_analysis = enable_lexing && enable_parsing && enable_analysis;
    if (do_analysis) {
        workload_executer_resolve();
        semantic_analyser_finish();
    }

    // CODE_GENERATION
    bool error_free = !compiler_errors_occured();
    bool generate_code = compiler.generate_code;
    bool do_ir_gen = do_analysis && enable_ir_gen && generate_code && error_free;
    bool do_bytecode_gen = do_ir_gen && enable_bytecode_gen && generate_code && error_free;
    bool do_c_generation = do_ir_gen && enable_c_generation && generate_code && error_free;
    bool do_c_compilation = do_c_generation && enable_c_compilation && generate_code && error_free;

    compiler_switch_timing_task(Timing_Task::CODE_GEN);
    if (do_ir_gen) {
        ir_generator_finish(do_bytecode_gen);
    }
    if (do_bytecode_gen) {
        // INFO: Bytecode Gen is currently controlled by ir-generator
        bytecode_generator_set_entry_function(compiler.bytecode_generator);
    }
    if (do_c_generation) {
        c_generator_generate(compiler.c_generator, &compiler);
    }
    if (do_c_compilation) {
        c_compiler_add_source_file(compiler.c_compiler, string_create_static("backend/src/main.cpp"));
        c_compiler_add_source_file(compiler.c_compiler, string_create_static("backend/src/hello_world.cpp"));
        c_compiler_add_source_file(compiler.c_compiler, string_create_static("backend/hardcoded/hardcoded_functions.cpp"));
        c_compiler_compile(compiler.c_compiler);
    }

    // OUTPUT
    {
        compiler_switch_timing_task(Timing_Task::OUTPUT);
        if (do_output && output_ast) {
            logg("\n");
            logg("--------AST PARSE RESULT--------:\n");
            AST::base_print(upcast(compiler.main_source->parsed_code->root));
        }
        if (do_output && generate_code)
        {
            //logg("\n\n\n\n\n\n\n\n\n\n\n\n--------SOURCE CODE--------: \n%s\n\n", source_code->characters);
            if (do_analysis && output_type_system) {
                logg("\n--------TYPE SYSTEM RESULT--------:\n");
                type_system_print(&compiler.type_system);
            }

            if (do_analysis && output_root_table)
            {
                logg("\n--------ROOT TABLE RESULT---------\n");
                String root_table = string_create_empty(1024);
                SCOPE_EXIT(string_destroy(&root_table));
                symbol_table_append_to_string(&root_table, compiler.semantic_analyser->root_symbol_table, false);
                logg("%s", root_table.characters);
            }

            if (error_free)
            {
                if (do_ir_gen && output_ir)
                {
                    logg("\n--------IR_PROGRAM---------\n");
                    String tmp = string_create_empty(1024);
                    SCOPE_EXIT(string_destroy(&tmp));
                    ir_program_append_to_string(compiler.ir_generator->program, &tmp);
                    logg("%s", tmp.characters);
                }

                if (do_bytecode_gen && output_bytecode)
                {
                    String result_str = string_create_empty(32);
                    SCOPE_EXIT(string_destroy(&result_str));
                    if (do_bytecode_gen && output_bytecode) {
                        bytecode_generator_append_bytecode_to_string(compiler.bytecode_generator, &result_str);
                        logg("\n----------------BYTECODE_GENERATOR RESULT---------------: \n%s\n", result_str.characters);
                    }
                }
            }
        }

        compiler_switch_timing_task(Timing_Task::FINISH);
        if (do_output && output_timing && generate_code)
        {
            double sum = timer_current_time_in_seconds(compiler.timer) - compiler.time_compile_start;
            logg("\n-------- TIMINGS ---------\n");
            logg("reset       ... %3.2fms\n", (float)(compiler.time_reset) * 1000);
            if (enable_lexing) {
                logg("lexing      ... %3.2fms\n", (float)(compiler.time_lexing) * 1000);
            }
            if (enable_parsing) {
                logg("parsing     ... %3.2fms\n", (float)(compiler.time_parsing) * 1000);
            }
            if (enable_analysis) {
                logg("analysis    ... %3.2fms\n", (float)(compiler.time_analysing) * 1000);
                logg("code_exec   ... %3.2fms\n", (float)(compiler.time_code_exec) * 1000);
            }
            if (enable_bytecode_gen) {
                logg("code_gen    ... %3.2fms\n", (float)(compiler.time_code_gen) * 1000);
            }
            if (do_output) {
                logg("output      ... %3.2fms\n", (float)(compiler.time_output) * 1000);
            }
            logg("--------------------------\n");
            logg("sum         ... %3.2fms\n", (float)(sum) * 1000);
            logg("--------------------------\n");
        }
    }
}

void compiler_compile_clean(Source_Code* source_code, Compile_Type compile_type, String project_file) // Takes ownership of project path
{
    compiler_reset_data(false, compile_type);

    file_io_relative_to_full_path(&project_file);
    compiler.main_source = code_source_create_empty(Code_Origin::MAIN_PROJECT, source_code, project_file);
    compiler_lex_parse_and_add_workload_for_code_source(compiler.main_source, nullptr, true);
    compiler_execute_analysis_workloads_and_code_generation();
}

void compiler_compile_incremental(Code_History* history, Compile_Type compile_type)
{
    compiler_reset_data(true, compile_type);
    auto source = compiler.main_source;
    assert(source != 0, "");
    assert(source->parsed_code != 0, "");
    assert(source->module_progress == 0, "Should be after reset");

    compiler_lex_parse_and_add_workload_for_code_source(compiler.main_source, history, true);
    compiler_execute_analysis_workloads_and_code_generation();
}

Module_Progress* compiler_import_and_queue_analysis_workload(AST::Import* import_node)
{
    assert(import_node->type == AST::Import_Type::FILE, "");
    auto src = compiler_find_ast_code_source(&import_node->base);
    
    String path = string_create(src->file_path.characters);
    file_io_relative_to_full_path(&path);
    bool success = false;
    SCOPE_EXIT(if (!success) { string_destroy(&path); });
    
    // Convert relative to full path, taking the folder of the import into account
    {
        Optional<int> last_pos = string_find_character_index_reverse(&path, '/', path.size - 1);
        if (last_pos.available) {
            string_truncate(&path, last_pos.value + 1);
        }
        else {
            string_reset(&path);
        }
        string_append_string(&path, import_node->file_name);
        file_io_relative_to_full_path(&path);
    }
    
    // Check cache
    {
        Code_Source** cached_code = hashtable_find_element(&compiler.cached_imports, path);
        if (cached_code != 0) {
            auto code = *cached_code;
            // Source was imported before (So already lexed and parsed)
            if (code->module_progress == 0) {
                code->module_progress = workload_executer_add_module_discovery(code->parsed_code->root, false);
            }
            return code->module_progress;
        }
    }
    
    // Otherwise load file
    Optional<String> file_content = file_io_load_text_file(path.characters);
    SCOPE_EXIT(file_io_unload_text_file(&file_content));
    if (file_content.available) {
        auto source_code = source_code_create();
        source_code_fill_from_string(source_code, file_content.value);
    
        Code_Source* code_source = code_source_create_empty(Code_Origin::LOADED_FILE, source_code, path);
        compiler_lex_parse_and_add_workload_for_code_source(code_source, nullptr, false);
        success = true;
        return code_source->module_progress;
    }
    return 0;
}

Exit_Code compiler_execute()
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
    if (!compiler_errors_occured() && do_execution)
    {
        if (execute_binary) {
            return c_compiler_execute(compiler.c_compiler);
        }
        else
        {
            Bytecode_Thread* thread = bytecode_thread_create(10000);
            SCOPE_EXIT(bytecode_thread_destroy(thread));
            bytecode_thread_set_initial_state(thread, compiler.bytecode_generator->entry_point_index);
            bytecode_thread_execute(thread);
            return thread->exit_code;
        }
    }
    return Exit_Code::COMPILATION_FAILED;
}




// UTILITY
void compiler_switch_timing_task(Timing_Task task)
{
    if (task == compiler.task_current) return;
    double* add_to = 0;
    switch (compiler.task_current)
    {
    case Timing_Task::LEXING: add_to = &compiler.time_lexing; break;
    case Timing_Task::PARSING: add_to = &compiler.time_parsing; break;
    case Timing_Task::ANALYSIS: add_to = &compiler.time_analysing; break;
    case Timing_Task::CODE_GEN: add_to = &compiler.time_code_gen; break;
    case Timing_Task::CODE_EXEC: add_to = &compiler.time_code_exec; break;
    case Timing_Task::RESET: add_to = &compiler.time_reset; break;
    case Timing_Task::OUTPUT: add_to = &compiler.time_output; break;
    case Timing_Task::FINISH: {
        compiler.task_current = task;
        return;
    }
    default: panic("");
    }
    double now = timer_current_time_in_seconds(compiler.timer);
    double time_spent = now - compiler.task_last_start_time;
    *add_to = *add_to + time_spent;
    //logg("Spent %3.2fms on: %s\n", time_spent, timing_task_to_string(compiler.task_current));
    compiler.task_last_start_time = now;
    compiler.task_current = task;
}

bool compiler_errors_occured() {
    if (compiler.semantic_analyser->errors.size > 0) return true;
    for (int i = 0; i < compiler.code_sources.size; i++) {
        if (compiler.code_sources[i]->parsed_code->error_messages.size > 0) return true;
    }
    return false;
}

Source_Code* compiler_find_ast_source_code(AST::Node* base) {
    if (base->range.start.type == AST::Node_Position_Type::TOKEN_INDEX) {
        return base->range.start.options.block_index.code;
    }
    return base->range.start.options.token_index.line_index.block_index.code;
}

Code_Source* compiler_find_ast_code_source(AST::Node* base)
{
    auto code = compiler_find_ast_source_code(base);
    for (int i = 0; i < compiler.code_sources.size; i++) {
        auto src = compiler.code_sources[i];
        if (src->code == code) {
            return src;
        }
    }
    return 0;
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

void compiler_run_testcases(Timer* timer, bool force_run)
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
    enable_c_generation = false;
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

    // Create testcases with expected result
    Test_Case test_cases[] = {
        test_case_make("000_empty.upp", false),
        test_case_make("001_main.upp", true),
        test_case_make("002_comments.upp", true),
        test_case_make("002_comments_invalid.upp", false),
        test_case_make("002_comments_valid.upp", true),
        test_case_make("003_variables.upp", true),
        test_case_make("004_types_pointers_arrays.upp", true),
        test_case_make("004_types_primitive.upp", true),
        test_case_make("005_operator_precedence.upp", true),
        test_case_make("006_function_calls.upp", true),
        test_case_make("007_modules.upp", true),
        test_case_make("008_imports_simple.upp", true),
        test_case_make("008_imports_aliases.upp", true),
        test_case_make("008_imports_star.upp", true),
        test_case_make("008_imports_star_star.upp", true),
        test_case_make("008_imports_import_order.upp", true),
        test_case_make("008_imports_invalid_import_order.upp", false),
        test_case_make("008_imports_as_statement.upp", true),
        test_case_make("011_pointers.upp", true),
        test_case_make("012_new_delete.upp", true),
        test_case_make("013_structs.upp", true),

        test_case_make("014_01_casts.upp", true),
        test_case_make("014_02_casts_operator_context.upp", true),
        test_case_make("014_03_casts_cast_mode_error1.upp", false),
        test_case_make("014_04_casts_cast_mode_error2.upp", false),
        test_case_make("014_05_casts_pointer_arithmetic.upp", true),
        test_case_make("014_06_casts_auto_address_of.upp", true),
        test_case_make("014_07_casts_auto_dereference.upp", true),
        test_case_make("014_09_casts_auto_operations_and_casts.upp", true),
        test_case_make("014_10_casts_auto_operations_and_casts_error.upp", false),
        test_case_make("014_11_casts_more_context_options.upp", true),
        test_case_make("014_12_casts_custom_casts.upp", true),
        test_case_make("014_13_casts_custom_casts_error1.upp", false),
        test_case_make("014_14_casts_custom_casts_error2.upp", false),
        test_case_make("014_15_casts_custom_polymorphic_casts.upp", true),
        test_case_make("014_16_casts_custom_polymorphic_cast_error.upp", false),
        test_case_make("014_17_casts_operator_context_imports.upp", true),
        test_case_make("014_18_casts_optional_example.upp", true),

        test_case_make("015_defer.upp", true),
        test_case_make("017_function_pointers.upp", true),
        test_case_make("019_scopes.upp", true),
        test_case_make("020_globals.upp", true),
        test_case_make("021_slices.upp", true),
        //test_case_make("022_dynamic_array.upp", true),
        //test_case_make("023_invalid_recursive_template.upp", false),
        test_case_make("024_expression_context.upp", true),
        test_case_make("025_expression_context_limit.upp", false),
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
        //test_case_make("044_c_unions.upp", true),
        test_case_make("045_unions.upp", true),
        test_case_make("046_types_as_values.upp", true),
        test_case_make("047_type_info.upp", true),
        test_case_make("048_any_type.upp", true),
        test_case_make("049_any_error.upp", false),
        test_case_make("050_named_break_continue.upp", true),
        test_case_make("051_invalid_continue_no_loop.upp", false),
        test_case_make("052_invalid_lables.upp", false),
        test_case_make("053_named_flow_defer.upp", true),
        test_case_make("054_1_polymorphic_empty_function.upp", true),
        test_case_make("054_2_polymorphic_simple_call.upp", true),
        test_case_make("054_3_polymorphic_multiple_calls.upp", true),
        test_case_make("054_4_polymorphic_using_polymorphic_values.upp", true),
        test_case_make("054_5_polymorphic_polymorphic_calculation.upp", true),
        test_case_make("054_6_polymorphic_parameter_dependencies.upp", true),
        test_case_make("054_7_polymorphic_implicit_parameters.upp", true),
        test_case_make("054_8_polymorphic_return_value.upp", true),
        test_case_make("054_9_polymorphic_error_cyclic_dependency.upp", false),
        test_case_make("054_10_polymorphic_error_recursive_instanciation.upp", false),
        test_case_make("054_11_polymorphic_explicit_implicit.upp", true),
        test_case_make("054_12_polymorphic_struct_instance.upp", true),
        test_case_make("054_13_polymorphic_error_recursive_struct.upp", false),
        test_case_make("054_14_polymorphic_recursive_struct.upp", true),
        test_case_make("054_15_polymorphic_struct_templates.upp", true),
        test_case_make("054_16_polymorphic_struct_value_access.upp", true),
        test_case_make("054_17_polymorphic_struct_nested_templates.upp", true),
        test_case_make("054_18_polymorphic_struct_nested_returns.upp", true),
        test_case_make("054_19_polymorphic_parameter_self_dependency.upp", true),
        test_case_make("054_20_polymorphic_error_self_dependency.upp", false),
        test_case_make("054_21_polymorphic_anonymous_structs.upp", true),
        test_case_make("054_22_polymorphic_lambdas.upp", true),
        test_case_make("054_23_polymorphic_comptime_function_pointer.upp", true),
        test_case_make("054_24_polymorphic_bake.upp", true),

        test_case_make("055_01_custom_operators_binop.upp", true),
        test_case_make("055_02_custom_operators_binop_errors.upp", false),
        test_case_make("055_03_custom_operators_unop.upp", true),
        test_case_make("055_04_custom_operators_unop_errors.upp", false),
        test_case_make("055_05_custom_operators_array_access.upp", true),
        test_case_make("055_06_custom_operators_array_access_error.upp", false),
        test_case_make("055_07_custom_operators_array_access_poly.upp", true),
        test_case_make("055_08_custom_operators_dot_call.upp", true),
        test_case_make("055_09_custom_operators_dot_call_poly.upp", true),
        test_case_make("055_10_custom_operators_iterator.upp", true),
        test_case_make("055_11_custom_operators_iterator_poly.upp", true),
    };
    int test_case_count = sizeof(test_cases) / sizeof(Test_Case);

    bool errors_occured = false;
    String result = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&result));
    for (int i = 0; i < test_case_count; i++)
    {
        Test_Case* test_case = &test_cases[i];
        logg("Testcase: %s\n", test_case->name);
        String path = string_create_formated("upp_code/testcases/%s", test_case->name);
        Optional<String> code = file_io_load_text_file(path.characters);
        SCOPE_EXIT(file_io_unload_text_file(&code));
        if (!code.available) {
            string_append_formated(&result, "ERROR:   Test %s could not load test file\n", test_case->name);
            errors_occured = true;
            string_destroy(&path);
            continue;
        }

        auto source_code = source_code_create();
        source_code_fill_from_string(source_code, code.value);
        SCOPE_EXIT(source_code_destroy(source_code));
        compiler_compile_clean(source_code, Compile_Type::BUILD_CODE, path);
        Exit_Code exit_code = compiler_execute();
        if (exit_code != Exit_Code::SUCCESS && test_case->should_succeed)
        {
            string_append_formated(&result, "ERROR:   Test %s exited with Code ", test_case->name);
            exit_code_append_to_string(&result, exit_code);
            string_append_formated(&result, "\n");
            if (exit_code == Exit_Code::COMPILATION_FAILED)
            {
                for (int i = 0; i < compiler.code_sources.size; i++) {
                    auto parser_errors = compiler.code_sources[i]->parsed_code->error_messages;
                    for (int j = 0; j < parser_errors.size; j++) {
                        auto& e = parser_errors[i];
                        string_append_formated(&result, "    Parse Error: %s\n", e.msg);
                    }
                }

                String tmp = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&tmp));
                for (int i = 0; i < compiler.semantic_analyser->errors.size; i++)
                {
                    Semantic_Error e = compiler.semantic_analyser->errors[i];
                    string_append_formated(&result, "    Semantic Error: ");
                    semantic_error_append_to_string(e, &result);
                    string_append_formated(&result, "\n");
                }
            }
            errors_occured = true;
        }
        else if (exit_code == Exit_Code::SUCCESS && !test_case->should_succeed) {
            string_append_formated(&result, "ERROR:   Test %s successfull, but should fail!\n", test_case->name);
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
    Optional<String> text = file_io_load_text_file("upp_code/testcases/045_unions.upp");
    SCOPE_EXIT(file_io_unload_text_file(&text););
    if (!text.available) {
        logg("Couldn't execute stresstest, file not found\n");
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
        //compiler_compile(&compiler, cut_code, false);
        if (i % (code.size / 10) == 0) {
            logg("Stresstest (Parenthesis): %d/%d characters\n", i, code.size);
        }
    }

    double time_stress_end = timer_current_time_in_seconds(timer);
    float ms_time = (time_stress_end - time_stress_start) * 1000.0f;
    logg("Stress test time: %3.2fms (%3.2fms per parse/analyse)\n", ms_time, ms_time / code.size / 2.0f);
}
