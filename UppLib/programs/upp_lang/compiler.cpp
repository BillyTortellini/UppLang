#include "compiler.hpp"
#include "../../win32/timing.hpp"
#include "../../win32/windows_helper_functions.hpp"
#include "../../utility/file_io.hpp"

#include "semantic_analyser.hpp"
#include "dependency_analyser.hpp"
#include "semantic_analyser.hpp"
#include "ir_code.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "c_backend.hpp"
#include "parser.hpp"
#include "ast.hpp"
#include "lexer.hpp"

// Parser stages
bool enable_lexing = true;
bool enable_parsing = true;
bool enable_dependency_analysis = true;
bool enable_analysis = true;
bool enable_ir_gen = true;
bool enable_bytecode_gen = true;
bool enable_c_generation = false;
bool enable_c_compilation = false;

// Output stages
bool output_identifiers = false;
bool output_ast = true;
bool output_dependency_analysis = false;
bool output_type_system = false;
bool output_root_table = false;
bool output_ir = true;
bool output_bytecode = false;
bool output_timing = true;

// Testcases
bool enable_testcases = true;
bool enable_stresstest = false;
bool run_testcases_compiled = false;

// Execution
bool enable_output = true;
bool output_only_on_code_gen = true;
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
    result->source_parse = 0;
    result->analysed = false;
    result->analysis_items = dynamic_array_create_empty<Analysis_Item*>(1);
    result->item_dependencies = dynamic_array_create_empty<Item_Dependency>(1);
    result->file_path = file_path;
    dynamic_array_push_back(&compiler.code_sources, result);
    hashtable_insert_element(&compiler.cached_imports, file_path, result);
    return result;
}

void code_source_destroy(Code_Source* source)
{
    if (source->origin != Code_Origin::MAIN_PROJECT) {
        source_code_destroy(source->code);
    }
    string_destroy(&source->file_path);
    if (source->source_parse != 0) {
        Parser::source_parse_destroy(source->source_parse);
    }
    for (int i = 0; i < source->analysis_items.size; i++) {
        analysis_item_destroy(source->analysis_items[i]);
    }
    dynamic_array_destroy(&source->analysis_items);
    dynamic_array_destroy(&source->item_dependencies);
    delete source;
}



// COMPILER
Compiler* compiler_initialize(Timer* timer)
{
    compiler.timer = timer;
    compiler.identifier_pool = identifier_pool_create();
    compiler.constant_pool = constant_pool_create(&compiler.type_system);
    compiler.extern_sources = extern_sources_create();
    compiler.cached_imports = hashtable_create_empty<String, Code_Source*>(1, hash_string, string_equals);

    Parser::initialize();
    lexer_initialize(&compiler.identifier_pool);

    compiler.type_system = type_system_create(timer);
    compiler.dependency_analyser = dependency_analyser_initialize();
    compiler.semantic_analyser = semantic_analyser_initialize();
    compiler.ir_generator = ir_generator_initialize();
    compiler.bytecode_generator = new Bytecode_Generator;
    *compiler.bytecode_generator = bytecode_generator_create();
    compiler.bytecode_interpreter = new Bytecode_Interpreter;
    *compiler.bytecode_interpreter = bytecode_intepreter_create();
    compiler.c_generator = new C_Generator;
    *compiler.c_generator = c_generator_create();
    compiler.c_compiler = new C_Compiler;
    *compiler.c_compiler = c_compiler_create();

    compiler.code_sources = dynamic_array_create_empty<Code_Source*>(1);
    return &compiler;
}

void compiler_destroy()
{
    Parser::destroy();
    lexer_shutdown();

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

    dependency_analyser_destroy();
    semantic_analyser_destroy();
    ir_generator_destroy();
    bytecode_generator_destroy(compiler.bytecode_generator);
    delete compiler.bytecode_generator;
    bytecode_interpreter_destroy(compiler.bytecode_interpreter);
    delete compiler.bytecode_interpreter;
    c_generator_destroy(compiler.c_generator);
    delete compiler.c_generator;
    c_compiler_destroy(compiler.c_compiler);
    delete compiler.c_compiler;
}



// Compiling
void compiler_lex_code(Code_Source* source)
{
    bool do_lexing = enable_lexing;
    if (!do_lexing) return;

    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));
    compiler_switch_timing_task(Timing_Task::LEXING);

    source_code_tokenize(source->code);
}

void compiler_parse_code(Code_Source* source)
{
    bool do_lexing = enable_lexing;
    bool do_parsing = do_lexing && enable_parsing;
    if (!do_parsing) return;

    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));
    compiler_switch_timing_task(Timing_Task::PARSING);

    assert(source->source_parse == 0, "Hey");
    source->source_parse = Parser::execute_clean(source->code);

    if (output_ast && do_output)
    {
        compiler_switch_timing_task(Timing_Task::OUTPUT);
        logg("\n");
        logg("--------AST PARSE RESULT--------:\n");
        AST::base_print(&source->source_parse->root->base);
    }
}

void compiler_analyse_code(Code_Source* source)
{
    if (source->analysed) return;
    source->analysed = true;

    bool do_lexing = enable_lexing;
    bool do_parsing = do_lexing && enable_parsing;
    bool do_dependency_analysis = do_parsing && enable_analysis;
    if (!do_dependency_analysis) return;

    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));
    compiler_switch_timing_task(Timing_Task::RC_GEN);

    for (int i = 0; i < source->analysis_items.size; i++) {
        analysis_item_destroy(source->analysis_items[i]);
    }
    dynamic_array_reset(&source->analysis_items);
    dynamic_array_reset(&source->item_dependencies);

    dependency_analyser_analyse(source);
    compiler_switch_timing_task(Timing_Task::ANALYSIS);
    workload_executer_add_analysis_items(source);

    if (output_dependency_analysis && do_output)
    {
        compiler_switch_timing_task(Timing_Task::OUTPUT);
        String printed_items = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&printed_items));
        dependency_analyser_append_to_string(&printed_items);
        logg("\n");
        logg("--------RC_ANALYSIS_ITEMS--------:\n");
        logg("\n%s\n", printed_items.characters);
    }
}

void code_source_analyse_clean(Code_Source* source)
{
    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));
    compiler_switch_timing_task(Timing_Task::LEXING);
    compiler_lex_code(source);
    compiler_switch_timing_task(Timing_Task::PARSING);
    compiler_parse_code(source);
    compiler_switch_timing_task(Timing_Task::ANALYSIS);
    compiler_analyse_code(source);
}



void compiler_prepare_compile(bool incremental, Compile_Type compile_type)
{
    bool generate_code = compile_type == Compile_Type::BUILD_CODE;
    do_output = enable_output && !(output_only_on_code_gen && !generate_code);
    if (do_output) {
        logg("\n\n\n   COMPILING\n---------------\n");
    }
    compiler.time_compile_start = timer_current_time_in_seconds(compiler.timer);
    compiler.generate_code = generate_code;
    {
        compiler.time_analysing = 0;
        compiler.time_code_gen = 0;
        compiler.time_lexing = 0;
        compiler.time_parsing = 0;
        compiler.time_rc_gen = 0;
        compiler.time_reset = 0;
        compiler.time_code_exec = 0;
        compiler.time_output = 0;
        compiler.task_last_start_time = compiler.time_compile_start;
        compiler.task_current = Timing_Task::FINISH;
    }

    compiler_switch_timing_task(Timing_Task::RESET);
    {
        // NOTE: Identifier pool is not beeing reset because Syntax-Editor already does incremental lexing
        compiler.id_size = identifier_pool_add(&compiler.identifier_pool, string_create_static("size"));
        compiler.id_data = identifier_pool_add(&compiler.identifier_pool, string_create_static("data"));
        compiler.id_tag = identifier_pool_add(&compiler.identifier_pool, string_create_static("tag"));
        compiler.id_main = identifier_pool_add(&compiler.identifier_pool, string_create_static("main"));
        compiler.id_type_of = identifier_pool_add(&compiler.identifier_pool, string_create_static("type_of"));
        compiler.id_type_info = identifier_pool_add(&compiler.identifier_pool, string_create_static("type_info"));
        compiler.id_empty_string = identifier_pool_add(&compiler.identifier_pool, string_create_static(""));

        // FUTURE: When we have incremental compilation we cannot just reset everything anymore
        // Reset Data
        constant_pool_destroy(&compiler.constant_pool);
        compiler.constant_pool = constant_pool_create(&compiler.type_system);
        extern_sources_destroy(&compiler.extern_sources);
        compiler.extern_sources = extern_sources_create();

        if (!incremental) {
            compiler.main_source = 0;
        }
        for (int i = 0; i < compiler.code_sources.size; i++) {
            auto source = compiler.code_sources[i];
            if (incremental) {
                source->analysed = false;
            }
            else {
                code_source_destroy(compiler.code_sources[i]);
                compiler.code_sources[i] = 0;
            }
        }
        if (!incremental) {
            dynamic_array_reset(&compiler.code_sources);
            hashtable_reset(&compiler.cached_imports);
        }

        // Reset stages
        type_system_reset(&compiler.type_system);
        dependency_analyser_reset(&compiler);
        type_system_add_primitives(&compiler.type_system, &compiler.identifier_pool, &compiler.dependency_analyser->predefined_symbols);
        if (!incremental) {
            Parser::reset();
        }
        semantic_analyser_reset(&compiler);
        ir_generator_reset();
        bytecode_generator_reset(compiler.bytecode_generator, &compiler);
        bytecode_interpreter_reset(compiler.bytecode_interpreter, &compiler);
    }
}

void compiler_finish_compile()
{
    bool do_analysis = enable_lexing && enable_parsing && enable_dependency_analysis && enable_analysis;
    if (do_analysis) {
        compiler_switch_timing_task(Timing_Task::ANALYSIS);
        workload_executer_resolve();
        semantic_analyser_finish();
    }

    // Check for errors
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

    compiler_switch_timing_task(Timing_Task::OUTPUT);
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
            symbol_table_append_to_string(&root_table, compiler.dependency_analyser->root_symbol_table, false);
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
        if (enable_dependency_analysis) {
            logg("rc_gen      ... %3.2fms\n", (float)(compiler.time_rc_gen) * 1000);
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

void compiler_compile_clean(Source_Code* source_code, Compile_Type compile_type, String project_file) // Takes ownership of project path
{
    compiler_prepare_compile(false, compile_type);

    file_io_relative_to_full_path(&project_file);
    compiler.main_source = code_source_create_empty(Code_Origin::MAIN_PROJECT, source_code, project_file);
    code_source_analyse_clean(compiler.main_source);

    compiler_finish_compile();
}

void compiler_compile_incremental(Code_History* history, Compile_Type compile_type)
{
    compiler_prepare_compile(true, compile_type);
    auto source = compiler.main_source;
    assert(source != 0, "");
    assert(source->source_parse != 0, "");

    Timing_Task before = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before));
    compiler_switch_timing_task(Timing_Task::PARSING);
    Parser::execute_incremental(source->source_parse, history);
    compiler_switch_timing_task(Timing_Task::ANALYSIS);
    compiler_analyse_code(source);
    compiler_finish_compile();
}

bool compiler_add_project_import(AST::Project_Import* project_import)
{
    auto src = compiler_find_ast_code_source(&project_import->base);

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
        string_append_string(&path, project_import->filename);
        file_io_relative_to_full_path(&path);
    }

    // Check cache
    {
        Code_Source** cached_code = hashtable_find_element(&compiler.cached_imports, path);
        if (cached_code != 0) {
            // Project is already imported
            compiler_analyse_code(*cached_code);
            return true;
        }
    }

    Optional<String> file_content = file_io_load_text_file(path.characters);
    SCOPE_EXIT(file_io_unload_text_file(&file_content));
    if (file_content.available) {
        auto source_code = source_code_create();
        source_code_fill_from_string(source_code, file_content.value);

        Code_Source* code_source = code_source_create_empty(Code_Origin::LOADED_FILE, source_code, path);
        code_source_analyse_clean(code_source);
        success = true;
        return true;
    }
    return false;
}

Exit_Code compiler_execute()
{
    bool do_execution =
        enable_lexing &&
        enable_parsing &&
        enable_dependency_analysis &&
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
            double bytecode_start = timer_current_time_in_seconds(compiler.timer);
            compiler.bytecode_interpreter->instruction_limit_enabled = true;
            compiler.bytecode_interpreter->instruction_limit = 10000;
            bytecode_interpreter_run_function(compiler.bytecode_interpreter, compiler.bytecode_generator->entry_point_index);
            double bytecode_end = timer_current_time_in_seconds(compiler.timer);
            float bytecode_time = (bytecode_end - bytecode_start);
            return compiler.bytecode_interpreter->exit_code;
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
    case Timing_Task::RC_GEN: add_to = &compiler.time_rc_gen; break;
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
    if (compiler.semantic_analyser->errors.size > 0 || compiler.dependency_analyser->errors.size > 0) return true;
    for (int i = 0; i < compiler.code_sources.size; i++) {
        if (compiler.code_sources[i]->source_parse->error_messages.size > 0) return true;
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
        test_case_make("007_imports.upp", true),
        test_case_make("011_pointers.upp", true),
        test_case_make("012_new_delete.upp", true),
        test_case_make("013_structs.upp", true),
        //test_case_make("014_templates.upp", true),
        test_case_make("015_defer.upp", true),
        test_case_make("016_casting.upp", true),
        test_case_make("017_function_pointers.upp", true),
        test_case_make("018_modules.upp", true),
        test_case_make("019_scopes.upp", true),
        test_case_make("020_globals.upp", true),
        test_case_make("021_slices.upp", true),
        //test_case_make("022_dynamic_array.upp", true),
        //test_case_make("023_invalid_recursive_template.upp", false),
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
                    auto parser_errors = compiler.code_sources[i]->source_parse->error_messages;
                    for (int j = 0; j < parser_errors.size; j++) {
                        auto& e = parser_errors[i];
                        string_append_formated(&result, "    Parse Error: %s\n", e.msg);
                    }
                }

                auto& dependency_errors = compiler.dependency_analyser->errors;
                for (int i = 0; i < dependency_errors.size; i++) {
                    auto& e = dependency_errors[i];
                    string_append_formated(&result, "    Symbol Error: %s\n", e.existing_symbol->id->characters);
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
        //compiler_compile(&compiler, cut_code, false);
        if (i % (code.size / 10) == 0) {
            logg("Stresstest (Parenthesis): %d/%d characters\n", i, code.size);
        }
    }

    double time_stress_end = timer_current_time_in_seconds(timer);
    float ms_time = (time_stress_end - time_stress_start) * 1000.0f;
    logg("Stress test time: %3.2fms (%3.2fms per parse/analyse)\n", ms_time, ms_time / code.size / 2.0f);
}
