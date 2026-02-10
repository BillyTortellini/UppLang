#include "compiler.hpp"
#include "../../win32/timing.hpp"
#include "../../win32/windows_helper_functions.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/directory_crawler.hpp"

#include "semantic_analyser.hpp"
#include "ir_code.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "c_backend.hpp"
#include "parser.hpp"
#include "ast.hpp"
#include "lexer.hpp"
#include "symbol_table.hpp"
#include "editor_analysis_info.hpp"

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



// COMPILER
Compiler* compiler_create()
{
    Compiler* compiler = new Compiler;
    compiler->identifier_pool = identifier_pool_create();
    compiler->fiber_pool = fiber_pool_create();

    tokenizer_initialize();

    return compiler;
}

void compilation_unit_destroy(Compilation_Unit* unit)
{
    if (unit->code != nullptr) {
        source_code_destroy(unit->code);
        unit->code = nullptr;
    }
    dynamic_array_destroy(&unit->parser_errors);
    for (int i = 0; i < unit->allocated_nodes.size; i++) {
        AST::base_destroy(unit->allocated_nodes[i]);
    }
    dynamic_array_destroy(&unit->allocated_nodes);
    string_destroy(&unit->filepath);
    delete unit;
}

void compiler_destroy(Compiler* compiler)
{
    tokenizer_shutdown();
    fiber_pool_destroy(compiler->fiber_pool);
    compiler->fiber_pool = 0;

    identifier_pool_destroy(&compiler->identifier_pool);
}



// Compiling
void compiler_parse_unit(Compilation_Unit* unit, Compilation_Data* compilation_data)
{
    Timing_Task before = compilation_data->task_current;
    SCOPE_EXIT(compilation_data_switch_timing_task(compilation_data, before));

    // Reset parsing data
    for (int i = 0; i < unit->allocated_nodes.size; i++) {
        AST::base_destroy(unit->allocated_nodes[i]);
    }
    dynamic_array_reset(&unit->allocated_nodes);
    dynamic_array_reset(&unit->parser_errors);
    unit->root = 0;

    if (!enable_parsing) {
        return;
    }

    // Parse code
    compilation_data_switch_timing_task(compilation_data, Timing_Task::PARSING);
    Parser::execute_clean(unit, &compilation_data->compiler->identifier_pool.predefined_ids);
}

void compilation_data_compile(Compilation_Data* compilation_data, Compilation_Unit* main_unit, Compile_Type compile_type)
{
    Compiler* compiler = compilation_data->compiler;
    fiber_pool_set_current_fiber_to_main(compiler->fiber_pool);
    fiber_pool_check_all_handles_completed(compiler->fiber_pool);

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
    compiler_parse_unit(main_unit, compilation_data);

    Timing_Task before = compilation_data->task_current;
    SCOPE_EXIT(compilation_data_switch_timing_task(compilation_data, before));

    // Semantic_Analysis (Workload Execution)
    compilation_data_switch_timing_task(compilation_data, Timing_Task::ANALYSIS);
    bool do_analysis = enable_lexing && enable_parsing && enable_analysis;
    if (do_analysis) 
    {
        workload_executer_add_module_discovery(main_unit->root, compilation_data);
        workload_executer_resolve(compilation_data->workload_executer, compilation_data);
        compilation_data_finish_semantic_analysis(compilation_data);
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
        if (do_ir_gen) {
            ir_generator_finish(compilation_data, do_bytecode_gen);
        }
        if (do_bytecode_gen) {
            // INFO: Bytecode Gen is currently controlled by ir-generator
            bytecode_generator_set_entry_function(compilation_data->bytecode_generator);
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
        if (do_output && output_ast) {
            logg("\n");
            logg("--------AST PARSE RESULT--------:\n");
            AST::base_print(upcast(compilation_data->main_unit->root));
        }
        if (do_output && compilation_data->compile_type == Compile_Type::BUILD_CODE)
        {
            //logg("\n\n\n\n\n\n\n\n\n\n\n\n--------SOURCE CODE--------: \n%s\n\n", source_code->characters);
            if (do_analysis && output_type_system) {
                logg("\n--------TYPE SYSTEM RESULT--------:\n");
                type_system_print(compilation_data->type_system);
            }

            if (do_analysis && output_root_table)
            {
                logg("\n--------ROOT TABLE RESULT---------\n");
                String root_table = string_create_empty(1024);
                SCOPE_EXIT(string_destroy(&root_table));
                symbol_table_append_to_string(&root_table, compilation_data->root_symbol_table, false);
                logg("%s", root_table.characters);
            }

            if (error_free)
            {
                if (do_ir_gen && output_ir)
                {
                    logg("\n--------IR_PROGRAM---------\n");
                    String tmp = string_create_empty(1024);
                    SCOPE_EXIT(string_destroy(&tmp));
                    ir_program_append_to_string(compilation_data->ir_generator->program, &tmp, false, compilation_data);
                    logg("%s", tmp.characters);
                }

                if (do_bytecode_gen && output_bytecode)
                {
                    String result_str = string_create_empty(32);
                    SCOPE_EXIT(string_destroy(&result_str));
                    if (do_bytecode_gen && output_bytecode) {
                        bytecode_generator_append_bytecode_to_string(compilation_data->bytecode_generator, &result_str);
                        logg("\n----------------BYTECODE_GENERATOR RESULT---------------: \n%s\n", result_str.characters);
                    }
                }
            }
        }

        compilation_data_switch_timing_task(compilation_data, Timing_Task::FINISH);
        if (do_output && output_timing && generate_code)
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
            if (do_output) {
                logg("output      ... %3.2fms\n", (float)(compilation_data->time_output) * 1000);
            }
            logg("--------------------------\n");
            logg("sum         ... %3.2fms\n", (float)(sum) * 1000);
            logg("--------------------------\n");
        }
    }
}

Compilation_Unit* compiler_import_file(Compilation_Data* compilation_data, AST::Import* import_node)
{
    assert(import_node->operator_type == AST::Import_Operator::FILE_IMPORT, "");
    String* filename = import_node->options.file_name;

    // Resolve file-path (E.g. imports are relative from the file they are in)
    auto current_unit = compiler_find_ast_compilation_unit(compilation_data, &import_node->base);
    String path = string_copy(current_unit->filepath);
    SCOPE_EXIT(string_destroy(&path));
    file_io_relative_to_full_path(&path);

    // Replace filename in path with import string (All imports are currently relative)
    {
        Optional<int> last_pos = string_find_character_index_reverse(&path, '/', path.size - 1);
        if (last_pos.available) {
            string_truncate(&path, last_pos.value + 1);
        }
        else {
            string_reset(&path);
        }
        string_append_string(&path, filename);
        file_io_relative_to_full_path(&path);
    }

    return compilation_data_add_compilation_unit_unique(compilation_data, path, true);
}

bool compiler_can_execute_c_compiled(Compilation_Data* compilation_data)
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
            Bytecode_Thread* thread = bytecode_thread_create(compilation_data, 10000);
            SCOPE_EXIT(bytecode_thread_destroy(thread));
            bytecode_thread_set_initial_state(thread, compilation_data->bytecode_generator->entry_point_index);
            bytecode_thread_execute(thread);
            return thread->exit_code;
        }
    }
    return exit_code_make(Exit_Code_Type::COMPILATION_FAILED);
}




// UTILITY
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

bool compilation_data_errors_occured(Compilation_Data* compilation_data)
{
    if (compilation_data->semantic_errors.size > 0) return true;

    for (int i = 0; i < compilation_data->compilation_units.size; i++) 
    {
        auto unit = compilation_data->compilation_units[i];
        if (unit->module == nullptr) continue; // Checking if unit was analysed
        if (unit->parser_errors.size > 0) return true;
    }
    return false;
}

Compilation_Unit* compiler_find_ast_compilation_unit(Compilation_Data* compilation_data, AST::Node* base)
{
    while (base->parent != nullptr) {
        base = base->parent;
    }
    for (int i = 0; i < compilation_data->compilation_units.size; i++) {
        auto unit = compilation_data->compilation_units[i];
        if (upcast(unit->root) == base) {
            return unit;
        }
    }
    return 0;
}

bool compilation_unit_was_used_in_compile(Compilation_Unit* compilation_unit, Compilation_Data* compilation_data)
{
    if (compilation_unit->root == nullptr) return false;
	return hashtable_find_element(&compilation_data->ast_to_pass_mapping, upcast(compilation_unit->root)) != nullptr;
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

    Compiler* compiler = compiler_create();
    SCOPE_EXIT(compiler_destroy(compiler));

    // Create testcases with expected result
    Directory_Crawler* crawler = directory_crawler_create();
    SCOPE_EXIT(directory_crawler_destroy(crawler));
    directory_crawler_set_path(crawler, string_create_static("upp_code/testcases"));
    auto files = directory_crawler_get_content(crawler);

    bool errors_occured = false;
    int test_case_count = 0;
    String result = string_create_empty(256);
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

        Compilation_Data* compilation_data = compilation_data_create(compiler);
        SCOPE_EXIT(compilation_data_destroy(compilation_data));

        String path = string_create_formated("upp_code/testcases/%s", name.characters);
        SCOPE_EXIT(string_destroy(&path));
        Compilation_Unit* main_unit = compilation_data_add_compilation_unit_unique(compilation_data, path, true);
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
                    if (!compilation_unit_was_used_in_compile(unit, compilation_data)) continue;
                    auto parser_errors = unit->parser_errors;
                    for (int j = 0; j < parser_errors.size; j++) {
                        auto& e = parser_errors[j];
                        string_append_formated(&result, "    Parse Error: %s\n", e.msg);
                    }
                }

                semantic_analyser_append_semantic_errors_to_string(compilation_data, &result, 1);
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

    double time_stress_end = timer_current_time_in_seconds();
    float ms_time = (time_stress_end - time_stress_start) * 1000.0f;
    logg("Stress test time: %3.2fms (%3.2fms per parse/analyse)\n", ms_time, ms_time / code.size / 2.0f);
}
