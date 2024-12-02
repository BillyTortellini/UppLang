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
bool output_ir = false;
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



// COMPILER
Compiler* compiler_initialize(Timer* timer)
{
    compiler.add_compilation_unit_semaphore = semaphore_create(1, 1);
    compiler.timer = timer;
    compiler.identifier_pool = identifier_pool_create();
    compiler.analysis_data = nullptr;
    compiler.fiber_pool = fiber_pool_create();
    compiler.random = random_make_time_initalized();
    compiler.main_unit = 0;

    lexer_initialize();

    compiler.semantic_analyser = semantic_analyser_initialize();
    compiler.ir_generator = ir_generator_initialize();
    compiler.bytecode_generator = new Bytecode_Generator;
    *compiler.bytecode_generator = bytecode_generator_create();
    compiler.c_generator = c_generator_initialize();
    compiler.c_compiler = c_compiler_initialize();

    compiler.compilation_units = dynamic_array_create<Compilation_Unit*>();
    return &compiler;
}

void compilation_unit_destroy(Compilation_Unit* unit)
{
    source_code_destroy(unit->code);
    dynamic_array_destroy(&unit->parser_errors);
    for (int i = 0; i < unit->allocated_nodes.size; i++) {
        AST::base_destroy(unit->allocated_nodes[i]);
    }
    dynamic_array_destroy(&unit->allocated_nodes);
    string_destroy(&unit->filepath);
    delete unit;
}

void compiler_destroy()
{
    semaphore_destroy(compiler.add_compilation_unit_semaphore);
    lexer_shutdown();
    fiber_pool_destroy(compiler.fiber_pool);
    compiler.fiber_pool = 0;

    if (compiler.analysis_data != nullptr) {
        compiler_analysis_data_destroy(compiler.analysis_data);
    }
    identifier_pool_destroy(&compiler.identifier_pool);

    for (int i = 0; i < compiler.compilation_units.size; i++) {
        compilation_unit_destroy(compiler.compilation_units[i]);
    }
    dynamic_array_destroy(&compiler.compilation_units);

    semantic_analyser_destroy();
    ir_generator_destroy();
    bytecode_generator_destroy(compiler.bytecode_generator);
    delete compiler.bytecode_generator;
    c_generator_shutdown();
    c_compiler_shutdown();
}



// Compiling
Compilation_Unit* compiler_add_compilation_unit(String file_path_param, bool open_in_editor, bool is_import_file)
{
    String full_file_path = string_copy(file_path_param);
    file_io_relative_to_full_path(&full_file_path);
    SCOPE_EXIT(string_destroy(&full_file_path));

    semaphore_wait(compiler.add_compilation_unit_semaphore);
    SCOPE_EXIT(semaphore_increment(compiler.add_compilation_unit_semaphore, 1););

    // Check if file is already loaded
    Compilation_Unit* unit = nullptr;
    for (int i = 0; i < compiler.compilation_units.size; i++) {
        auto comp_unit = compiler.compilation_units[i];
        if (string_equals(&comp_unit->filepath, &full_file_path)) {
            unit = comp_unit;
            break;
        }
    }

    // Otherwise create new compilation unit
    if (unit == nullptr)
    {
        auto result = file_io_load_text_file(full_file_path.characters);
        SCOPE_EXIT(file_io_unload_text_file(&result));
        if (!result.available) {
            return 0;
        }

        auto source_code = source_code_create();
        source_code_fill_from_string(source_code, result.value);
        source_code_tokenize(source_code);

        unit = new Compilation_Unit;
        unit->code = source_code;
        unit->filepath = full_file_path;
        full_file_path.capacity = 0;
        unit->editor_tab_index = -1;
        unit->open_in_editor = open_in_editor;
        unit->used_in_last_compile = true;
        unit->allocated_nodes = dynamic_array_create<AST::Node*>();
        unit->module_progress = 0;
        unit->parser_errors = dynamic_array_create<Error_Message>();
        unit->root = 0;
        dynamic_array_push_back(&compiler.compilation_units, unit);
    }
    else 
    {
        if (open_in_editor) {
            unit->open_in_editor = true;
        }
        if (is_import_file) {
            unit->used_in_last_compile = true;
        }
    }

    return unit;
}

void compiler_parse_unit(Compilation_Unit* unit)
{
    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));

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
    compiler_switch_timing_task(Timing_Task::PARSING);
    Parser::execute_clean(unit);
}

void compiler_compile(Compilation_Unit* main_unit, Compile_Type compile_type)
{
    fiber_pool_set_current_fiber_to_main(compiler.fiber_pool);

    // Reset compiler data
    {
        compiler.main_unit = main_unit;
        main_unit->used_in_last_compile = true;
        compiler.generate_code = compile_type == Compile_Type::BUILD_CODE;

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
    }

    compiler_switch_timing_task(Timing_Task::RESET);
    {
        // FUTURE: When we have incremental compilation we cannot just reset everything anymore
        // Reset Data
        fiber_pool_check_all_handles_completed(compiler.fiber_pool);

        if (compiler.analysis_data != nullptr) {
            compiler_analysis_data_destroy(compiler.analysis_data);
        }
        compiler.analysis_data = compiler_analysis_data_create(compiler.timer);
        type_system_add_predefined_types(&compiler.analysis_data->type_system);

        // Remove/Delete sources that aren't used anymore
        for (int i = 0; i < compiler.compilation_units.size; i++)
        {
            auto unit = compiler.compilation_units[i];
            unit->module_progress = 0; // So that new modules are created

            bool should_delete = false;
            if (unit->used_in_last_compile) {
                unit->used_in_last_compile = false; // Reset
                continue;
            }
            else {
                should_delete = true;
            }

            if (unit->open_in_editor) {
                should_delete = false;
            }

            if (should_delete) {
                compilation_unit_destroy(unit);
                dynamic_array_swap_remove(&compiler.compilation_units, i);
                i -= 1;
            }
        }
        compiler.main_unit->used_in_last_compile = true;

        // Reset stages
        semantic_analyser_reset();
        ir_generator_reset();
        bytecode_generator_reset(compiler.bytecode_generator, &compiler);
    }

    // Parse all compilation units
    for (int i = 0; i < compiler.compilation_units.size; i++) {
        auto unit = compiler.compilation_units[i];
        compiler_parse_unit(unit);
    }

    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));

    // Semantic_Analysis (Workload Execution)
    compiler_switch_timing_task(Timing_Task::ANALYSIS);
    bool do_analysis = enable_lexing && enable_parsing && enable_analysis;
    if (do_analysis) {
        compiler.main_unit->module_progress = workload_executer_add_module_discovery(compiler.main_unit->root, true);
        workload_executer_resolve();
        semantic_analyser_finish();
    }

    // Code-Generation (IR-Generator + Interpreter/C-Backend)
    bool error_free = !compiler_errors_occured(compiler.analysis_data);
    bool generate_code = compiler.generate_code;
    bool do_ir_gen = do_analysis && enable_ir_gen && generate_code && error_free;
    bool do_bytecode_gen = do_ir_gen && enable_bytecode_gen && generate_code && error_free;
    bool do_c_generation = do_ir_gen && enable_c_generation && generate_code && error_free;
    bool do_c_compilation = do_c_generation && enable_c_compilation && generate_code && error_free;
    {
        compiler_switch_timing_task(Timing_Task::CODE_GEN);
        if (do_ir_gen) {
            ir_generator_finish(do_bytecode_gen);
        }
        if (do_bytecode_gen) {
            // INFO: Bytecode Gen is currently controlled by ir-generator
            bytecode_generator_set_entry_function(compiler.bytecode_generator);
        }
        if (do_c_generation) {
            c_generator_generate();
        }
        if (do_c_compilation) {
            c_compiler_compile();
        }
    }

    // Output
    {
        compiler_switch_timing_task(Timing_Task::OUTPUT);
        if (do_output && output_ast) {
            logg("\n");
            logg("--------AST PARSE RESULT--------:\n");
            AST::base_print(upcast(compiler.main_unit->root));
        }
        if (do_output && compiler.generate_code)
        {
            //logg("\n\n\n\n\n\n\n\n\n\n\n\n--------SOURCE CODE--------: \n%s\n\n", source_code->characters);
            if (do_analysis && output_type_system) {
                logg("\n--------TYPE SYSTEM RESULT--------:\n");
                type_system_print(&compiler.analysis_data->type_system);
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
                    ir_program_append_to_string(compiler.ir_generator->program, &tmp, false);
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

Module_Progress* compiler_import_and_queue_analysis_workload(AST::Import* import_node)
{
    assert(import_node->type == AST::Import_Type::FILE, "");

    // Resolve file-path (E.g. imports are relative from the file they are in)
    auto src = compiler_find_ast_compilation_unit(&import_node->base);
    String path = string_copy(src->filepath);
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
        string_append_string(&path, import_node->file_name);
        file_io_relative_to_full_path(&path);
    }

    // Add source (checks if file is already loaded)
    auto unit = compiler_add_compilation_unit(path, false, true);
    if (unit == nullptr) return nullptr;

    if (unit->module_progress == nullptr) {
        compiler_parse_unit(unit);
        unit->module_progress = workload_executer_add_module_discovery(unit->root, false);
    }
    return unit->module_progress;
}

Exit_Code compiler_execute(Compiler_Analysis_Data* analysis_data)
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
    if (!compiler_errors_occured(analysis_data) && do_execution)
    {
        if (execute_binary) {
            return c_compiler_execute();
        }
        else
        {
            Bytecode_Thread* thread = bytecode_thread_create(analysis_data, 10000);
            SCOPE_EXIT(bytecode_thread_destroy(thread));
            bytecode_thread_set_initial_state(thread, compiler.bytecode_generator->entry_point_index);
            bytecode_thread_execute(thread);
            return thread->exit_code;
        }
    }
    return exit_code_make(Exit_Code_Type::COMPILATION_FAILED);
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

bool compiler_errors_occured(Compiler_Analysis_Data* analysis_data)
{
    if (analysis_data == nullptr) return true;
    if (analysis_data->semantic_errors.size > 0) return true;
    for (int i = 0; i < compiler.compilation_units.size; i++) {
        auto code = compiler.compilation_units[i];
        if (!code->used_in_last_compile) continue;
        if (code->parser_errors.size > 0) return true;
    }
    return false;
}

Compilation_Unit* compiler_find_ast_compilation_unit(AST::Node* base)
{
    while (base->parent != 0) {
        base = base->parent;
    }
    for (int i = 0; i < compiler.compilation_units.size; i++) {
        auto code = compiler.compilation_units[i];
        if (upcast(code->root) == base) {
            return code;
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
        test_case_count += 1;

        logg("Testcase: %s\n", name.characters);
        String path = string_create_formated("upp_code/testcases/%s", name.characters);
        SCOPE_EXIT(string_destroy(&path));
        auto source_code = compiler_add_compilation_unit(path, false, true);
        if (source_code == 0) {
            string_append_formated(&result, "ERROR:   Test %s could not load test file\n", name.characters);
            errors_occured = true;
            continue;
        }

        compiler_compile(source_code, Compile_Type::BUILD_CODE);
        Exit_Code exit_code = compiler_execute(compiler.analysis_data);
        if (exit_code.type != Exit_Code_Type::SUCCESS && case_should_succeed)
        {
            string_append_formated(&result, "ERROR:   Test %s exited with Code ", name.characters);
            exit_code_append_to_string(&result, exit_code);
            string_append_formated(&result, "\n");
            if (exit_code.type == Exit_Code_Type::COMPILATION_FAILED)
            {
                for (int i = 0; i < compiler.compilation_units.size; i++) {
                    auto code = compiler.compilation_units[i];
                    if (code->open_in_editor && !code->used_in_last_compile) continue;
                    auto parser_errors = code->parser_errors;
                    for (int j = 0; j < parser_errors.size; j++) {
                        auto& e = parser_errors[j];
                        string_append_formated(&result, "    Parse Error: %s\n", e.msg);
                    }
                }

                semantic_analyser_append_semantic_errors_to_string(compiler.analysis_data, &result, 1);
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
