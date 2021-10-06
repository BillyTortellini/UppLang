#include "compiler.hpp"
#include "../../win32/timing.hpp"



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
    assert(range.start_index >= 0 && range.start_index <= compiler->lexer.tokens.size, "HEY");
    assert(range.end_index >= 0 && range.end_index <= compiler->lexer.tokens.size + 1, "HEY");
    assert(range.end_index >= range.start_index, "HEY");
    if (range.end_index > compiler->lexer.tokens.size) {
        range.end_index = compiler->lexer.tokens.size;
    }
    if (compiler->lexer.tokens.size == 0) {
        return text_slice_make(text_position_make(0, 0), text_position_make(0, 0));
    }

    range.end_index = math_clamp(range.end_index, 0, math_maximum(0, compiler->lexer.tokens.size));
    if (range.end_index == range.start_index) {
        return text_slice_make(
            compiler->lexer.tokens[range.start_index].position.start,
            compiler->lexer.tokens[range.start_index].position.end
        );
    }

    return text_slice_make(
        compiler->lexer.tokens[range.start_index].position.start,
        compiler->lexer.tokens[range.end_index - 1].position.end
    );
}

void exit_code_append_to_string(String* string, Exit_Code code)
{
    switch (code)
    {
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

String* identifier_pool_add(Identifier_Pool* lexer, String identifier)
{
    String** found = hashtable_find_element(&lexer->identifier_lookup_table, identifier);
    if (found != 0) {
        return *found;
    }
    else {
        String* copy = new String;
        *copy = string_create(identifier.characters);
        hashtable_insert_element(&lexer->identifier_lookup_table, *copy, copy);
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
}

void compiler_compile(Compiler* compiler, String* source_code, bool generate_code)
{
    bool do_lexing = enable_lexing;
    bool do_parsing = do_lexing && enable_parsing;
    bool do_analysis = do_parsing && enable_analysis;
    bool do_ir_gen = do_analysis && enable_ir_gen && generate_code;
    bool do_bytecode_gen = do_ir_gen && enable_bytecode_gen && generate_code;
    bool do_c_generation = do_ir_gen && enable_c_generation && generate_code;
    bool do_c_compilation = do_c_generation && enable_c_compilation && generate_code;

    // Reset data (FUTURE: Watch out for incremental compilation, pools should not be reset then)
    constant_pool_destroy(&compiler->constant_pool);
    compiler->constant_pool = constant_pool_create();
    extern_sources_destroy(&compiler->extern_sources);
    compiler->extern_sources = extern_sources_create();
    type_system_reset(&compiler->type_system);
    type_system_add_primitives(&compiler->type_system, &compiler->identifier_pool);

    double time_start_lexing = timer_current_time_in_seconds(compiler->timer);
    if (do_lexing) {
        lexer_lex(&compiler->lexer, source_code, &compiler->identifier_pool);
    }
    double time_end_lexing = timer_current_time_in_seconds(compiler->timer);

    double time_start_parsing = timer_current_time_in_seconds(compiler->timer);
    if (do_parsing) {
        ast_parser_parse(&compiler->parser, &compiler->lexer);
    }
    double time_end_parsing = timer_current_time_in_seconds(compiler->timer);

    double time_start_analysis = timer_current_time_in_seconds(compiler->timer);
    if (do_analysis) {
        semantic_analyser_analyse(&compiler->analyser, compiler);
    }
    double time_end_analysis = timer_current_time_in_seconds(compiler->timer);

    // Check for errors
    bool error_free = compiler->parser.errors.size == 0 && compiler->analyser.errors.size == 0;
    do_ir_gen = do_ir_gen && error_free;
    do_bytecode_gen = do_bytecode_gen && error_free;
    do_c_generation = do_c_generation && error_free;
    do_c_compilation = do_c_compilation && error_free;

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
        if (do_lexing) {
            if (output_lexing) {
                logg("\n\n\n\n--------LEXER RESULT--------:\n");
                lexer_print(&compiler->lexer);
            }
            if (output_identifiers) {
                logg("\n--------IDENTIFIERS:--------:\n");
                identifier_pool_print(&compiler->identifier_pool);
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
        if (enable_lexing) {
            logg("lexing       ... %3.2fms\n", (time_end_lexing - time_start_lexing) * 1000);
        }
        if (enable_parsing) {
            logg("parsing      ... %3.2fms\n", (time_end_parsing - time_start_parsing) * 1000);
        }
        if (enable_analysis) {
            logg("analysis     ... %3.2fms\n", (time_end_analysis - time_start_analysis) * 1000);
        }
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

void compiler_execute(Compiler* compiler)
{
    bool do_execution =
        enable_lexing &&
        enable_parsing &&
        enable_analysis &&
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
            c_compiler_execute(&compiler->c_compiler);
        }
        else
        {
            double bytecode_start = timer_current_time_in_seconds(compiler->timer);
            bytecode_interpreter_execute_main(&compiler->bytecode_interpreter, compiler);
            double bytecode_end = timer_current_time_in_seconds(compiler->timer);
            float bytecode_time = (bytecode_end - bytecode_start);
            if (compiler->bytecode_interpreter.exit_code == Exit_Code::SUCCESS) {
                logg("Interpreter: Exit SUCCESS\n");
                //logg("Bytecode interpreter result: %d (%2.5f seconds)\n", *(int*)(byte*)&compiler->bytecode_interpreter.return_register[0], bytecode_time);
            }
            else {
                String tmp = string_create_empty(128);
                SCOPE_EXIT(string_destroy(&tmp));
                exit_code_append_to_string(&tmp, compiler->bytecode_interpreter.exit_code);
                logg("Bytecode interpreter error: %s\n", tmp.characters);
            }
        }
    }
}

// TODO: Call this function at some point
void compiler_add_source_code(Compiler* compiler, String* source_code, Code_Origin origin)
{
    bool do_lexing = enable_lexing;
    bool do_parsing = do_lexing && enable_parsing;
    bool do_analysis = do_parsing && enable_analysis;

    Code_Source source;
    source.origin = origin;
    source.source_code = source_code;

    if (do_lexing) {
        lexer_lex(&compiler->lexer, source_code, &compiler->identifier_pool);
        source.tokens = compiler->lexer.tokens;
        source.tokens_with_decoration = compiler->lexer.tokens_with_decoration;
        compiler->lexer.tokens = dynamic_array_create_empty<Token>(source.tokens.size);
        compiler->lexer.tokens_with_decoration = dynamic_array_create_empty<Token>(source.tokens_with_decoration.size);
    }

    if (do_parsing) {
        ast_parser_parse(&compiler->parser, &compiler->lexer);
        source.parse_errors = compiler->parser.errors;
        source.nodes = compiler->parser.nodes;
        source.token_mapping = compiler->parser.token_mapping;
        compiler->parser.errors = dynamic_array_create_empty<Compiler_Error>(source.parse_errors.size);
        compiler->parser.nodes = dynamic_array_create_empty<AST_Node>(source.nodes.size);
        compiler->parser.token_mapping = dynamic_array_create_empty<Token_Range>(source.token_mapping.size);
    }

    if (do_analysis) 
    {
        // Add analysis workload
        semantic_analyser_analyse(&compiler->analyser, compiler);
    }

    // Check for errors
    bool error_free = compiler->parser.errors.size == 0 && compiler->analyser.errors.size == 0;
}

