#include "compiler.hpp"
#include "../../win32/timing.hpp"

Token_Range token_range_make(int start_index, int end_index)
{
    Token_Range result;
    result.start_index = start_index;
    result.end_index = end_index;
    return result;
}

Compiler compiler_create(Timer* timer)
{
    Compiler result;
    result.timer = timer;
    result.lexer = lexer_create();
    result.parser = ast_parser_create();
    result.lexer = lexer_create();
    result.type_system = type_system_create(&result.lexer);
    result.analyser = semantic_analyser_create();
    result.bytecode_generator = bytecode_generator_create();
    result.bytecode_interpreter = bytecode_intepreter_create();
    result.intermediate_generator = intermediate_generator_create();
    result.c_generator = c_generator_create();
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
    intermediate_generator_destroy(&compiler->intermediate_generator);
    c_generator_destroy(&compiler->c_generator);
}

bool enable_lexing = true;
bool enable_parsing = true;
bool enable_analysis = true;
bool enable_im_gen = false;
bool enable_bytecode_gen = true;
bool enable_execution = true;
bool enable_output = true;

bool output_lexing = false;
bool output_identifiers = false;
bool output_ast = false;
bool output_type_system = true;
bool output_im = false;
bool output_bytecode = false;

void compiler_compile(Compiler* compiler, String* source_code, bool generate_code)
{
    bool do_lexing = enable_lexing;
    bool do_parsing = do_lexing && enable_parsing;
    bool do_analysis = do_parsing && enable_analysis;
    bool do_im_gen = do_analysis && enable_im_gen && generate_code;
    bool do_bytecode_gen = do_im_gen && enable_bytecode_gen;

    double lexer_start_time = timer_current_time_in_seconds(compiler->timer);
    if (do_lexing) {
        lexer_parse_string(&compiler->lexer, source_code);
    }
    double parser_start_time = timer_current_time_in_seconds(compiler->timer);
    if (do_parsing) {
        ast_parser_parse(&compiler->parser, &compiler->lexer);
    }
    double semantic_analysis_start_time = timer_current_time_in_seconds(compiler->timer);

    if (do_analysis) {
        semantic_analyser_analyse(&compiler->analyser, compiler);
    }
    if (do_im_gen) {
        if (compiler->parser.errors.size == 0 && compiler->analyser.errors.size == 0)
        {
            // Generate Intermediate Code
            intermediate_generator_generate(&compiler->intermediate_generator, compiler);
            // Generate Bytecode from IM
            if (do_bytecode_gen) {
                bytecode_generator_generate(&compiler->bytecode_generator, compiler);
            }
        }
    }

    if (enable_output && generate_code)
    {
        //logg("\n\n\n\n\n\n\n\n\n\n\n\n--------SOURCE CODE--------: \n%s\n\n", source_code->characters);
        if (do_lexing) {
            if (output_lexing) {
                logg("\n\n\n\n--------LEXER RESULT--------:\n");
                lexer_print(&compiler->lexer);
            }
            if (output_identifiers) {
                logg("\n--------IDENTIFIERS:--------:\n");
                lexer_print_identifiers(&compiler->lexer);
            }
        }

        if (do_parsing && output_ast)
        { 
            String printed_ast = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&printed_ast));
            ast_parser_append_to_string(&compiler->parser, &printed_ast);
            logg("\n");
            logg("--------AST PARSE RESULT--------:\n");
            logg("\n%s\n", printed_ast.characters);
        }

        if (do_analysis && output_type_system) {
            logg("--------TYPE SYSTEM RESULT--------:\n");
            type_system_print(&compiler->type_system);
            logg("--------ROOT TABLE RESULT---------\n");
            String root_table = string_create_empty(1024);
            SCOPE_EXIT(string_destroy(&root_table));
            symbol_table_append_to_string(&root_table, compiler->analyser.root_table, &compiler->lexer, true);
            logg("%s", root_table.characters);

            logg("--------PROGRAM---------\n");
            string_reset(&root_table);
            ir_program_append_to_string(compiler->analyser.program, &root_table);
            logg("%s", root_table.characters);
        }

        if (compiler->analyser.errors.size == 0 && true)
        {
            String result_str = string_create_empty(32);
            SCOPE_EXIT(string_destroy(&result_str));
            if (do_im_gen && output_im) {
                intermediate_generator_append_to_string(&result_str, &compiler->intermediate_generator);
                logg("---------INTERMEDIATE_GENERATOR_RESULT----------\n%s\n\n", result_str.characters);
                string_reset(&result_str);
            }
            if (do_bytecode_gen && output_bytecode) {
                bytecode_generator_append_bytecode_to_string(&compiler->bytecode_generator, &result_str);
                logg("----------------BYTECODE_GENERATOR RESULT---------------: \n%s\n", result_str.characters);
            }
        }
    }
    /*
    double intermediate_generator_start_time = timer_current_time_in_seconds(compiler->timer);
    double bytecode_generator_start_time = timer_current_time_in_seconds(compiler->timer);
    if (compiler->parser.errors.size == 0 && compiler->analyser.errors.size == 0 && generate_code)
    {
        // Generate Intermediate Code
        intermediate_generator_generate(&compiler->intermediate_generator, compiler);
        bytecode_generator_start_time = timer_current_time_in_seconds(compiler->timer);
        // Generate Bytecode from IM
        bytecode_generator_generate(&compiler->bytecode_generator, compiler);
    }

    double debug_print_start_time = timer_current_time_in_seconds(compiler->timer);
    // Debug Print
    if (generate_code && false)
    {
        logg("\n\n\n\n\n\n\n\n\n\n\n\n--------SOURCE CODE--------: \n%s\n\n", source_code->characters);
        logg("\n\n\n\n--------LEXER RESULT--------:\n");
        lexer_print(&compiler->lexer);

        logg("\n--------IDENTIFIERS:--------:\n");
        lexer_print_identifiers(&compiler->lexer);

        String printed_ast = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&printed_ast));
        ast_parser_append_to_string(&compiler->parser, &printed_ast);
        logg("\n");
        logg("--------AST PARSE RESULT--------:\n");
        logg("\n%s\n", printed_ast.characters);

        logg("--------TYPE SYSTEM RESULT--------:\n");
        type_system_print(&compiler->type_system);
        if (compiler->analyser.errors.size == 0 && true)
        {
            String result_str = string_create_empty(32);
            SCOPE_EXIT(string_destroy(&result_str));
            intermediate_generator_append_to_string(&result_str, &compiler->intermediate_generator);
            logg("---------INTERMEDIATE_GENERATOR_RESULT----------\n%s\n\n", result_str.characters);
            string_reset(&result_str);

            bytecode_generator_append_bytecode_to_string(&compiler->bytecode_generator, &result_str);
            logg("----------------BYTECODE_GENERATOR RESULT---------------: \n%s\n", result_str.characters);
        }
    }

    double debug_print_end_time = timer_current_time_in_seconds(compiler->timer);
    logg(
        "--------- TIMINGS -----------\nlexer time: \t%3.2fms\nparser time: \t%3.2fms\nanalyser time: %3.2fms\nintermediate time: %3.2fms\nbytecode time: %3.2fms\ndebug print: %3.2fms\n",
        (float)(parser_start_time - lexer_start_time) * 1000.0f,
        (float)(semantic_analysis_start_time - parser_start_time) * 1000.0f,
        (float)(intermediate_generator_start_time - semantic_analysis_start_time) * 1000.0f,
        (float)(bytecode_generator_start_time - intermediate_generator_start_time) * 1000.0f,
        (float)(debug_print_start_time - bytecode_generator_start_time) * 1000.0f,
        (float)(debug_print_end_time - debug_print_start_time) * 1000.0f
    );
    */
}

void compiler_execute(Compiler* compiler)
{
    bool do_execution =
        enable_lexing &&
        enable_parsing &&
        enable_analysis &&
        enable_im_gen &&
        enable_bytecode_gen &&
        enable_execution;

    // Execute
    if (compiler->parser.errors.size == 0 && compiler->analyser.errors.size == 0 && do_execution)
    {
        double bytecode_start = timer_current_time_in_seconds(compiler->timer);
        bytecode_interpreter_execute_main(&compiler->bytecode_interpreter, compiler);
        double bytecode_end = timer_current_time_in_seconds(compiler->timer);
        float bytecode_time = (bytecode_end - bytecode_start);
        if (compiler->bytecode_interpreter.exit_code == Exit_Code::SUCCESS) {
            logg("Bytecode interpreter result: %d (%2.5f seconds)\n", *(int*)(byte*)&compiler->bytecode_interpreter.return_register[0], bytecode_time);
        }
        else {
            String tmp = string_create_empty(128);
            SCOPE_EXIT(string_destroy(&tmp));
            exit_code_append_to_string(&tmp, compiler->bytecode_interpreter.exit_code);
            logg("Bytecode interpreter error: %s\n", tmp.characters);
        }

        //c_generator_generate(&compiler->c_generator, &&compiler->intermediate_generator);
        //logg("C-Code:\n------------------\n%s\n", &compiler->c_generator.output_string.characters);
    }
}

Text_Slice token_range_to_text_slice(Token_Range range, Compiler* compiler)
{
    if (compiler->lexer.tokens.size == 0) {
        return text_slice_make(text_position_make(0, 0), text_position_make(0, 0));
    }
    range.end_index = math_clamp(range.end_index, 0, math_maximum(0, compiler->lexer.tokens.size - 1));
    return text_slice_make(
        compiler->lexer.tokens[range.start_index].position.start,
        compiler->lexer.tokens[math_maximum(0, range.end_index - 1)].position.end
    );
}


