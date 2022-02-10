#include "rc_analyser.hpp"

#include "compiler_misc.hpp"
#include "compiler.hpp"
#include "ast_parser.hpp"
#include "semantic_analyser.hpp"

RC_Expression* rc_analyser_analyse_expression(RC_Analyser* analyser, AST_Node* expression_node);
void rc_analyser_analyse_symbol_definition_node(RC_Analyser* analyser, AST_Node* definition_node);

/*
SYMBOL TABLE FUNCTIONS
*/
Symbol_Table* symbol_table_create(RC_Analyser* analyser, Symbol_Table* parent, AST_Node* definition_node)
{
    Symbol_Table* result = new Symbol_Table;
    dynamic_array_push_back(&analyser->allocated_symbol_tables, result);
    result->parent = parent;
    result->symbols = hashtable_create_pointer_empty<String*, Symbol*>(4);
    if (definition_node != 0) {
        hashtable_insert_element(&analyser->mapping_ast_to_symbol_table, definition_node, result);
    }
    return result;
}

void symbol_destroy(Symbol* symbol) {
    dynamic_array_destroy(&symbol->references);
}

void symbol_table_destroy(Symbol_Table* symbol_table)
{
    auto it = hashtable_iterator_create(&symbol_table->symbols);
    while (hashtable_iterator_has_next(&it)) {
        Symbol* symbol = *it.value;
        symbol_destroy(symbol);
        delete symbol;
        hashtable_iterator_next(&it);
    }
    hashtable_destroy(&symbol_table->symbols);
    delete symbol_table;
}

Symbol* symbol_table_define_symbol(Symbol_Table* symbol_table, RC_Analyser* analyser, String* id, Symbol_Type type, AST_Node* definition_node)
{
    assert(id != 0, "HEY");

    // Check if already defined in same scope
    Symbol* found_symbol = symbol_table_find_symbol(symbol_table, id, false, 0);
    if (found_symbol != 0) {
        rc_analyser_log_error(analyser, found_symbol, definition_node);
        String temp_identifer = string_create_empty(128);
        SCOPE_EXIT(string_destroy(&temp_identifer));
        string_append_formated(&temp_identifer, "__temporary_%d", analyser->errors.size);
        id = identifier_pool_add(&analyser->compiler->identifier_pool, temp_identifer);
    }

    Symbol* new_sym = new Symbol;
    new_sym->definition_node = definition_node;
    new_sym->id = id;
    new_sym->type = type;
    new_sym->origin_table = symbol_table;
    new_sym->references = dynamic_array_create_empty<RC_Symbol_Read*>(2);
    new_sym->origin_item = analyser->analysis_item;

    hashtable_insert_element(&symbol_table->symbols, id, new_sym);
    return new_sym;
}

Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool only_current_scope, RC_Symbol_Read* symbol_read)
{
    if (symbol_read != 0 && symbol_read->symbol != 0) {
        panic("Symbol already found, I dont know if this path has a use case");
        return symbol_read->symbol;
    }
    Symbol** found = hashtable_find_element(&table->symbols, id);
    if (found == 0) {
        if (!only_current_scope && table->parent != 0) {
            return symbol_table_find_symbol(table->parent, id, only_current_scope, symbol_read);
        }
        return nullptr;
    }

    // Variables/Parameters need special treatment since we have inner functions that cannot 'see' outer function variables
    Symbol_Type sym_type = (*found)->type;
    if (symbol_read != 0 && 
        (sym_type == Symbol_Type::VARIABLE_UNDEFINED || sym_type == Symbol_Type::VARIABLE || sym_type == Symbol_Type::POLYMORPHIC_PARAMETER)) 
    {
        RC_Analysis_Item* read_item = symbol_read->item;
        RC_Analysis_Item* definition_item = (*found)->origin_item;
        if (read_item != definition_item && 
            !(definition_item->type == RC_Analysis_Item_Type::FUNCTION && definition_item->options.function.body_item == read_item)) 
        {
            return nullptr;
        }
    }
    if (symbol_read != 0) {
        dynamic_array_push_back(&((*found)->references), symbol_read);
    }
    return *found;
}

void symbol_append_to_string(Symbol* symbol, String* string)
{
    string_append_formated(string, "%s ", symbol->id->characters);
    if (symbol->type == Symbol_Type::UNRESOLVED) {
        string_append_formated(string, "Analysis not finished!");
        return;
    }
    switch (symbol->type)
    {
    case Symbol_Type::VARIABLE_UNDEFINED:
        if (symbol->options.variable_undefined.is_parameter) {
            string_append_formated(string, "Parameter Undefined (#%d)", symbol->options.variable_undefined.parameter_index);
        }
        else {
            string_append_formated(string, "Variable Undefined");
        }
        break;
    case Symbol_Type::UNRESOLVED:
        string_append_formated(string, "Unresolved");
        break;
    case Symbol_Type::POLYMORPHIC_PARAMETER:
        string_append_formated(string, "Polymorphic Parameter");
        break;
    case Symbol_Type::VARIABLE:
        string_append_formated(string, "Variable");
        //type_signature_append_to_string(string, symbol->data.options.variable->data_type);
        break;
    case Symbol_Type::TYPE:
        string_append_formated(string, "Type");
        break;
    case Symbol_Type::ERROR_SYMBOL:
        string_append_formated(string, "Error");
        break;
    case Symbol_Type::SYMBOL_ALIAS:
        string_append_formated(string, "Alias for %s", symbol->options.alias->id);
        break;
    case Symbol_Type::CONSTANT_VALUE:
        string_append_formated(string, "Constant %d", symbol->options.constant.constant_index);
        break;
    case Symbol_Type::HARDCODED_FUNCTION:
        string_append_formated(string, "Hardcoded Function");
        //string_append_formated(string, "Polymorphic Function");
        break;
    case Symbol_Type::EXTERN_FUNCTION:
        string_append_formated(string, "Extern Function");
        break;
    case Symbol_Type::FUNCTION:
        string_append_formated(string, "Function");
        /*
        switch (symbol->data.options.function->function_type) {
        case ModTree_Function_Type::FUNCTION: string_append_formated(string, "Function "); break;
        case ModTree_Function_Type::EXTERN_FUNCTION: string_append_formated(string, "Extern Function "); break;
        case ModTree_Function_Type::HARDCODED_FUNCTION: string_append_formated(string, "Hardcoded Function "); break;
        }
        //type_signature_append_to_string(string, symbol->data.options.function->signature);
        */
        break;
    case Symbol_Type::MODULE:
        string_append_formated(string, "Module");
        break;
    default: panic("What");
    }
}

void symbol_table_append_to_string_with_parent_info(String* string, Symbol_Table* table, bool is_parent, bool print_root)
{
    if (!print_root && table->parent == 0) return;
    if (!is_parent) {
        string_append_formated(string, "Symbols: \n");
    }
    auto iter = hashtable_iterator_create(&table->symbols);
    while (hashtable_iterator_has_next(&iter))
    {
        Symbol* s = *iter.value;
        if (is_parent) {
            string_append_formated(string, "\t");
        }
        symbol_append_to_string(s, string);
        string_append_formated(string, "\n");
        hashtable_iterator_next(&iter);
    }
    if (table->parent != 0) {
        symbol_table_append_to_string_with_parent_info(string, table->parent, true, print_root);
    }
}

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root) {
    symbol_table_append_to_string_with_parent_info(string, table, false, print_root);
}

/*
RC helper functions
*/
RC_Expression* rc_expression_create_empty(RC_Analyser* analyser, RC_Expression_Type type, AST_Node* origin_node)
{
    RC_Expression* expression = new RC_Expression;
    dynamic_array_push_back(&analyser->allocated_expressions, expression);
    expression->type = type;
    bool worked = hashtable_insert_element(&analyser->mapping_expressions_to_ast, expression, origin_node);
    assert(worked, "");
    return expression;
}

RC_Statement* rc_statement_create_empty(RC_Analyser* analyser, RC_Statement_Type type, AST_Node* origin_node)
{
    RC_Statement* statement = new RC_Statement;
    dynamic_array_push_back(&analyser->allocated_statements, statement);
    statement->type = type;
    bool worked = hashtable_insert_element(&analyser->mapping_statements_to_ast, statement, origin_node);
    assert(worked, "");
    return statement;
}

void rc_block_destroy(RC_Block* block)
{
    dynamic_array_destroy(&block->statements);
    delete block;
}

void rc_expression_destroy(RC_Expression* expression)
{
    switch (expression->type)
    {
    case RC_Expression_Type::ANALYSIS_ITEM: break;
    case RC_Expression_Type::ENUM:
        dynamic_array_destroy(&expression->options.enumeration.members);
        break;
    case RC_Expression_Type::FUNCTION_SIGNATURE:
        dynamic_array_destroy(&expression->options.function_signature.parameters);
        break;
    case RC_Expression_Type::FUNCTION_CALL:
        dynamic_array_destroy(&expression->options.function_call.arguments);
        break;
    case RC_Expression_Type::ARRAY_INITIALIZER:
        dynamic_array_destroy(&expression->options.array_initializer.element_initializers);
        break;
    case RC_Expression_Type::STRUCT_INITIALIZER:
        dynamic_array_destroy(&expression->options.struct_initializer.member_initializers);
        break;
    case RC_Expression_Type::SYMBOL_READ:
    case RC_Expression_Type::MODULE:
    case RC_Expression_Type::ARRAY_TYPE:
    case RC_Expression_Type::SLICE_TYPE:
    case RC_Expression_Type::BINARY_OPERATION:
    case RC_Expression_Type::UNARY_OPERATION:
    case RC_Expression_Type::LITERAL_READ:
    case RC_Expression_Type::NEW_EXPR:
    case RC_Expression_Type::ARRAY_ACCESS:
    case RC_Expression_Type::AUTO_ENUM:
    case RC_Expression_Type::MEMBER_ACCESS:
    case RC_Expression_Type::CAST:
    case RC_Expression_Type::TYPE_INFO:
    case RC_Expression_Type::TYPE_OF:
    case RC_Expression_Type::DEREFERENCE:
    case RC_Expression_Type::POINTER:
        break;
    default: panic("");
    }
    delete expression;
}

void rc_statement_destroy(RC_Statement* statement)
{
    switch (statement->type)
    {
    case RC_Statement_Type::SWITCH_STATEMENT:
        dynamic_array_destroy(&statement->options.switch_statement.cases);
        break;
    case RC_Statement_Type::VARIABLE_DEFINITION:
    case RC_Statement_Type::STATEMENT_BLOCK:
    case RC_Statement_Type::ASSIGNMENT_STATEMENT:
    case RC_Statement_Type::DEFER:
    case RC_Statement_Type::IF_STATEMENT:
    case RC_Statement_Type::WHILE_STATEMENT:
    case RC_Statement_Type::BREAK_STATEMENT:
    case RC_Statement_Type::CONTINUE_STATEMENT:
    case RC_Statement_Type::RETURN_STATEMENT:
    case RC_Statement_Type::EXPRESSION_STATEMENT:
    case RC_Statement_Type::DELETE_STATEMENT:
        break;
    default: panic("");
    }
    delete statement;
}


/*
ANALYSER FUNCTIONS
*/
RC_Analysis_Item* rc_analysis_item_create_empty(RC_Analyser* analyser, RC_Analysis_Item_Type type, RC_Analysis_Item* parent_item)
{
    RC_Analysis_Item* item = new RC_Analysis_Item;
    item->item_dependencies = dynamic_array_create_empty<RC_Item_Dependency>(1);
    item->symbol_dependencies = dynamic_array_create_empty<RC_Symbol_Read*>(1);
    item->type = type;
    if (parent_item != 0) {
        RC_Item_Dependency item_dependency;
        item_dependency.item = item;
        item_dependency.type = type == RC_Analysis_Item_Type::STRUCTURE ? analyser->dependency_type : RC_Dependency_Type::NORMAL;
        dynamic_array_push_back(&parent_item->item_dependencies, item_dependency);
    }
    return item;
}

void rc_analysis_item_destroy(RC_Analysis_Item* item)
{
    for (int i = 0; i < item->item_dependencies.size; i++) {
        rc_analysis_item_destroy(item->item_dependencies[i].item);
    }
    dynamic_array_destroy(&item->item_dependencies);
    for (int i = 0; i < item->symbol_dependencies.size; i++) {
        delete item->symbol_dependencies[i];
    }
    dynamic_array_destroy(&item->symbol_dependencies);

    switch (item->type)
    {
    case RC_Analysis_Item_Type::ROOT:
    case RC_Analysis_Item_Type::DEFINITION:
    case RC_Analysis_Item_Type::FUNCTION_BODY:
    case RC_Analysis_Item_Type::BAKE:
        break;
    case RC_Analysis_Item_Type::FUNCTION: {
        dynamic_array_destroy(&item->options.function.parameter_symbols);
        rc_analysis_item_destroy(item->options.function.body_item);
        break;
    }
    case RC_Analysis_Item_Type::STRUCTURE: {
        dynamic_array_destroy(&item->options.structure.members);
        break;
    }
    default: panic("");
    }

    delete item;
}

RC_Analyser rc_analyser_create()
{
    RC_Analyser analyser;
    analyser.errors = dynamic_array_create_empty<Symbol_Error>(16);
    analyser.mapping_ast_to_symbol_table = hashtable_create_pointer_empty<AST_Node*, Symbol_Table*>(16);
    analyser.mapping_expressions_to_ast = hashtable_create_pointer_empty<RC_Expression*, AST_Node*>(16);
    analyser.mapping_statements_to_ast = hashtable_create_pointer_empty<RC_Statement*, AST_Node*>(16);

    analyser.allocated_symbol_tables = dynamic_array_create_empty<Symbol_Table*>(16);
    analyser.allocated_expressions = dynamic_array_create_empty<RC_Expression*>(32);
    analyser.allocated_blocks = dynamic_array_create_empty<RC_Block*>(16);
    analyser.allocated_statements = dynamic_array_create_empty<RC_Statement*>(16);

    analyser.root_symbol_table = 0;
    analyser.compiler = 0;
    analyser.root_item = 0;
    return analyser;
}

void rc_analyser_destroy(RC_Analyser* analyser)
{
    // Destroy results
    dynamic_array_destroy(&analyser->errors);
    hashtable_destroy(&analyser->mapping_ast_to_symbol_table);
    hashtable_destroy(&analyser->mapping_expressions_to_ast);
    hashtable_destroy(&analyser->mapping_statements_to_ast);
    if (analyser->root_item != 0) {
        rc_analysis_item_destroy(analyser->root_item);
    }

    // Destroy allocations
    for (int i = 0; i < analyser->allocated_symbol_tables.size; i++) {
        symbol_table_destroy(analyser->allocated_symbol_tables[i]);
    }
    for (int i = 0; i < analyser->allocated_blocks.size; i++) {
        rc_block_destroy(analyser->allocated_blocks[i]);
    }
    for (int i = 0; i < analyser->allocated_expressions.size; i++) {
        rc_expression_destroy(analyser->allocated_expressions[i]);
    }
    for (int i = 0; i < analyser->allocated_statements.size; i++) {
        rc_statement_destroy(analyser->allocated_statements[i]);
    }
    dynamic_array_destroy(&analyser->allocated_symbol_tables);
    dynamic_array_destroy(&analyser->allocated_blocks);
    dynamic_array_destroy(&analyser->allocated_expressions);
    dynamic_array_destroy(&analyser->allocated_statements);
}

void rc_analyser_reset(RC_Analyser* analyser, Compiler* compiler)
{
    // Reset results
    dynamic_array_reset(&analyser->errors);
    hashtable_reset(&analyser->mapping_ast_to_symbol_table);
    hashtable_reset(&analyser->mapping_expressions_to_ast);
    hashtable_reset(&analyser->mapping_statements_to_ast);
    if (analyser->root_item != 0) {
        rc_analysis_item_destroy(analyser->root_item);
    }
    analyser->root_item = rc_analysis_item_create_empty(analyser, RC_Analysis_Item_Type::ROOT, 0);
    analyser->dependency_type = RC_Dependency_Type::NORMAL;

    // Reset allocations
    for (int i = 0; i < analyser->allocated_symbol_tables.size; i++) {
        symbol_table_destroy(analyser->allocated_symbol_tables[i]);
    }
    for (int i = 0; i < analyser->allocated_blocks.size; i++) {
        rc_block_destroy(analyser->allocated_blocks[i]);
    }
    for (int i = 0; i < analyser->allocated_expressions.size; i++) {
        rc_expression_destroy(analyser->allocated_expressions[i]);
    }
    for (int i = 0; i < analyser->allocated_statements.size; i++) {
        rc_statement_destroy(analyser->allocated_statements[i]);
    }
    dynamic_array_reset(&analyser->allocated_blocks);
    dynamic_array_reset(&analyser->allocated_expressions);
    dynamic_array_reset(&analyser->allocated_statements);
    dynamic_array_reset(&analyser->allocated_symbol_tables);

    analyser->compiler = compiler;
    analyser->root_symbol_table = symbol_table_create(analyser, 0, 0);
    analyser->analysis_item = analyser->root_item;
    analyser->symbol_table = analyser->root_symbol_table;
    // Set predefined symbols
    {
        String* id_int = identifier_pool_add(&compiler->identifier_pool, string_create_static("int"));
        String* id_bool = identifier_pool_add(&compiler->identifier_pool, string_create_static("bool"));
        String* id_float = identifier_pool_add(&compiler->identifier_pool, string_create_static("float"));
        String* id_u8 = identifier_pool_add(&compiler->identifier_pool, string_create_static("u8"));
        String* id_u16 = identifier_pool_add(&compiler->identifier_pool, string_create_static("u16"));
        String* id_u32 = identifier_pool_add(&compiler->identifier_pool, string_create_static("u32"));
        String* id_u64 = identifier_pool_add(&compiler->identifier_pool, string_create_static("u64"));
        String* id_i8 = identifier_pool_add(&compiler->identifier_pool, string_create_static("i8"));
        String* id_i16 = identifier_pool_add(&compiler->identifier_pool, string_create_static("i16"));
        String* id_i32 = identifier_pool_add(&compiler->identifier_pool, string_create_static("i32"));
        String* id_i64 = identifier_pool_add(&compiler->identifier_pool, string_create_static("i64"));
        String* id_f64 = identifier_pool_add(&compiler->identifier_pool, string_create_static("f64"));
        String* id_f32 = identifier_pool_add(&compiler->identifier_pool, string_create_static("f32"));
        String* id_byte = identifier_pool_add(&compiler->identifier_pool, string_create_static("byte"));
        String* id_void = identifier_pool_add(&compiler->identifier_pool, string_create_static("void"));
        String* id_string = identifier_pool_add(&compiler->identifier_pool, string_create_static("String"));
        String* id_type = identifier_pool_add(&compiler->identifier_pool, string_create_static("Type"));
        String* id_type_information = identifier_pool_add(&compiler->identifier_pool, string_create_static("Type_Information"));
        String* id_any = identifier_pool_add(&compiler->identifier_pool, string_create_static("Any"));
        String* id_empty = identifier_pool_add(&compiler->identifier_pool, string_create_static("_"));
        // This placeholder can never be an identifier, becuase it starts with a number
        String* id_error = identifier_pool_add(&compiler->identifier_pool, string_create_static("0_ERROR_SYMBOL"));

        analyser->predefined_symbols.error_symbol = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_error, Symbol_Type::ERROR_SYMBOL, 0);
        analyser->predefined_symbols.type_bool = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_bool, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_int = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_int, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_float = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_float, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_u8 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_u8, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_u16 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_u16, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_u32 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_u32, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_u64 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_u64, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_i8 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_i8, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_i16 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_i16, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_i32 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_i32, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_i64 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_i64, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_f32 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_f32, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_f64 = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_f64, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_byte = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_byte, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_void = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_void, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_string = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_string, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_type = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_type, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_type_information = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_type_information, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_any = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_any, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.type_empty = symbol_table_define_symbol(analyser->root_symbol_table, analyser, id_empty, Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_print_bool = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_bool")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_print_i32 = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_i32")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_print_f32 = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_f32")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_print_string = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_string")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_print_line = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_line")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_read_i32 = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("read_i32")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_read_f32 = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("read_f32")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_read_bool = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("read_bool")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.hardcoded_random_i32 = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("random_i32")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.function_assert = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("assert")), Symbol_Type::UNRESOLVED, 0);
        analyser->predefined_symbols.global_type_informations = symbol_table_define_symbol(analyser->root_symbol_table, analyser,
            identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("type_informations")), Symbol_Type::UNRESOLVED, 0);
    }
}

RC_Block* rc_analyser_analyse_statement_block(RC_Analyser* analyser, AST_Node* statement_block_node, RC_Block_Type block_type)
{
    assert(statement_block_node->type == AST_Node_Type::STATEMENT_BLOCK, "");
    RC_Block* rc_block = new RC_Block;
    rc_block->type = block_type;
    rc_block->symbol_table = symbol_table_create(analyser, analyser->symbol_table, statement_block_node);
    rc_block->statements = dynamic_array_create_empty<RC_Statement*>(1);
    rc_block->block_id = statement_block_node->id;
    dynamic_array_push_back(&analyser->allocated_blocks, rc_block);

    // Set new symbol table
    Symbol_Table* rewind_table = analyser->symbol_table;
    SCOPE_EXIT(analyser->symbol_table = rewind_table;);
    analyser->symbol_table = rc_block->symbol_table;

    AST_Node* statement_node = statement_block_node->child_start;
    while (statement_node != 0)
    {
        switch (statement_node->type)
        {
        case AST_Node_Type::COMPTIME_DEFINE_ASSIGN:
        case AST_Node_Type::COMPTIME_DEFINE_INFER: {
            rc_analyser_analyse_symbol_definition_node(analyser, statement_node);
            break;
        }
        case AST_Node_Type::VARIABLE_DEFINITION:
        case AST_Node_Type::VARIABLE_DEFINE_ASSIGN:
        case AST_Node_Type::VARIABLE_DEFINE_INFER:
        {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::VARIABLE_DEFINITION, statement_node);
            statement->options.variable_definition.symbol = symbol_table_define_symbol(
                analyser->symbol_table, analyser, statement_node->id, Symbol_Type::VARIABLE_UNDEFINED, statement_node
            );
            statement->options.variable_definition.symbol->options.variable_undefined.is_parameter = false;
            statement->options.variable_definition.symbol->options.variable_undefined.parameter_index = -1;

            if (statement_node->type == AST_Node_Type::VARIABLE_DEFINITION || statement_node->type == AST_Node_Type::VARIABLE_DEFINE_ASSIGN) {
                statement->options.variable_definition.type_expression = optional_make_success(
                    rc_analyser_analyse_expression(analyser, statement_node->child_start)
                );
            }
            else {
                statement->options.variable_definition.type_expression = optional_make_failure<RC_Expression*>();
            }
            if (statement_node->type != AST_Node_Type::VARIABLE_DEFINITION) {
                statement->options.variable_definition.value_expression = optional_make_success(
                    rc_analyser_analyse_expression(analyser, statement_node->child_end)
                );
            }
            else {
                statement->options.variable_definition.value_expression = optional_make_failure<RC_Expression*>();
            }
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_BLOCK: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::STATEMENT_BLOCK, statement_node);
            statement->options.statement_block = rc_analyser_analyse_statement_block(analyser, statement_node, RC_Block_Type::ANONYMOUS_BLOCK_CASE);
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_IF_ELSE:
        case AST_Node_Type::STATEMENT_IF: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::IF_STATEMENT, statement_node);
            statement->options.if_statement.condition = rc_analyser_analyse_expression(analyser, statement_node->child_start);
            statement->options.if_statement.true_block = rc_analyser_analyse_statement_block(
                analyser, statement_node->child_start->neighbor, RC_Block_Type::IF_TRUE_BLOCK
            );
            if (statement_node->type == AST_Node_Type::STATEMENT_IF_ELSE) {
                statement->options.if_statement.false_block = optional_make_success(rc_analyser_analyse_statement_block(
                    analyser, statement_node->child_end, RC_Block_Type::IF_ELSE_BLOCK
                ));
            }
            else {
                statement->options.if_statement.false_block = optional_make_failure<RC_Block*>();
            }
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_DEFER: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::DEFER, statement_node);
            statement->options.defer_block = rc_analyser_analyse_statement_block(
                analyser, statement_node->child_start, RC_Block_Type::DEFER_BLOCK
            );
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_WHILE: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::WHILE_STATEMENT, statement_node);
            statement->options.while_statement.condition = rc_analyser_analyse_expression(analyser, statement_node->child_start);
            statement->options.while_statement.body = rc_analyser_analyse_statement_block(
                analyser, statement_node->child_end, RC_Block_Type::WHILE_BODY
            );
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_SWITCH:
        {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::SWITCH_STATEMENT, statement_node);
            statement->options.switch_statement.condition = rc_analyser_analyse_expression(analyser, statement_node->child_start);
            statement->options.switch_statement.cases = dynamic_array_create_empty<RC_Switch_Case>(2);
            AST_Node* case_node = statement_node->child_start->neighbor;
            while (case_node != 0)
            {
                RC_Switch_Case switch_case;
                if (case_node->type == AST_Node_Type::SWITCH_CASE) {
                    switch_case.expression = optional_make_success(rc_analyser_analyse_expression(analyser, case_node->child_start));
                    switch_case.body = rc_analyser_analyse_statement_block(analyser, case_node->child_end, RC_Block_Type::SWITCH_CASE);
                }
                else {
                    assert(case_node->type == AST_Node_Type::SWITCH_DEFAULT_CASE, "");
                    switch_case.expression = optional_make_failure<RC_Expression*>();
                    switch_case.body = rc_analyser_analyse_statement_block(analyser, case_node->child_end, RC_Block_Type::SWITCH_DEFAULT);
                }
                dynamic_array_push_back(&statement->options.switch_statement.cases, switch_case);
                case_node = case_node->neighbor;
            }
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_BREAK: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::BREAK_STATEMENT, statement_node);
            statement->options.break_id = statement_node->id;
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_CONTINUE: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::CONTINUE_STATEMENT, statement_node);
            statement->options.continue_id = statement_node->id;
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_RETURN: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::RETURN_STATEMENT, statement_node);
            if (statement_node->child_count == 0) {
                statement->options.return_statement = optional_make_failure<RC_Expression*>();
            }
            else {
                statement->options.return_statement = optional_make_success(rc_analyser_analyse_expression(analyser, statement_node->child_start));
            }
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_EXPRESSION: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::EXPRESSION_STATEMENT, statement_node);
            statement->options.expression_statement = rc_analyser_analyse_expression(analyser, statement_node->child_start);
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_ASSIGNMENT: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::ASSIGNMENT_STATEMENT, statement_node);
            statement->options.assignment.left_expression = rc_analyser_analyse_expression(analyser, statement_node->child_start);
            statement->options.assignment.right_expression = rc_analyser_analyse_expression(analyser, statement_node->child_end);
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        case AST_Node_Type::STATEMENT_DELETE: {
            RC_Statement* statement = rc_statement_create_empty(analyser, RC_Statement_Type::DELETE_STATEMENT, statement_node);
            statement->options.delete_expression = rc_analyser_analyse_expression(analyser, statement_node->child_start);
            dynamic_array_push_back(&rc_block->statements, statement);
            break;
        }
        default: panic("");
        }

        statement_node = statement_node->neighbor;
    }

    return rc_block;
}

void rc_analyser_analyse_definitions(RC_Analyser* analyser, AST_Node* definitions_node);

RC_Expression* rc_analyser_analyse_expression(RC_Analyser* analyser, AST_Node* expression_node)
{
    RC_Dependency_Type backup_type = analyser->dependency_type;
    SCOPE_EXIT(analyser->dependency_type = backup_type);
    if (analyser->dependency_type != RC_Dependency_Type::NORMAL)
    {
        if (expression_node->type == AST_Node_Type::FUNCTION_SIGNATURE ||
            expression_node->type == AST_Node_Type::EXPRESSION_POINTER ||
            expression_node->type == AST_Node_Type::EXPRESSION_SLICE_TYPE
            ) {
            analyser->dependency_type = RC_Dependency_Type::MEMBER_REFERENCE;
        }
        else if (expression_node->type != AST_Node_Type::EXPRESSION_IDENTIFIER &&
            expression_node->type != AST_Node_Type::EXPRESSION_ARRAY_TYPE &&
            expression_node->type != AST_Node_Type::STRUCT &&
            expression_node->type != AST_Node_Type::UNION &&
            expression_node->type != AST_Node_Type::C_UNION)
        {
            analyser->dependency_type = RC_Dependency_Type::NORMAL;
        }
    }

    switch (expression_node->type)
    {
    case AST_Node_Type::MODULE:
    {
        Symbol_Table* module_table = symbol_table_create(analyser, analyser->symbol_table, expression_node);
        Symbol_Table* backup_table = analyser->symbol_table;
        analyser->symbol_table = module_table;
        SCOPE_EXIT(analyser->symbol_table = backup_table;);
        rc_analyser_analyse_definitions(analyser, expression_node->child_start);

        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::MODULE, expression_node);
        result_expr->options.module_table = module_table;
        return result_expr;
    }
    case AST_Node_Type::FUNCTION:
    {
        RC_Analysis_Item* function_item = rc_analysis_item_create_empty(analyser, RC_Analysis_Item_Type::FUNCTION, analyser->analysis_item);
        auto function = &function_item->options.function;
        RC_Analysis_Item* body_item = rc_analysis_item_create_empty(analyser, RC_Analysis_Item_Type::FUNCTION_BODY, 0);
        function->body_item = body_item;

        // Backup 
        Symbol_Table* backup_table = analyser->symbol_table;
        RC_Analysis_Item* backup_item = analyser->analysis_item;
        SCOPE_EXIT(analyser->symbol_table = backup_table; analyser->analysis_item = backup_item;);

        Symbol_Table* param_table = symbol_table_create(analyser, analyser->symbol_table, expression_node);
        analyser->analysis_item = function_item;
        analyser->symbol_table = param_table;

        // Analyse signature
        function->signature_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        assert(function->signature_expression->type == RC_Expression_Type::FUNCTION_SIGNATURE, "");

        // Create parameter Symbols
        function->parameter_symbols = dynamic_array_create_empty<Symbol*>(1);
        for (int i = 0; i < function->signature_expression->options.function_signature.parameters.size; i++) {
            RC_Parameter* parameter = &function->signature_expression->options.function_signature.parameters[i];
            Symbol* symbol = symbol_table_define_symbol(
                param_table, analyser, parameter->param_id, Symbol_Type::VARIABLE_UNDEFINED, parameter->param_node
            );
            symbol->options.variable_undefined.is_parameter = true;
            symbol->options.variable_undefined.parameter_index = i;
            dynamic_array_push_back(&function->parameter_symbols, symbol);
        }
        function->symbol = 0;

        // Analyse body
        analyser->analysis_item = body_item;
        body_item->options.function_body = rc_analyser_analyse_statement_block(analyser, expression_node->child_end, RC_Block_Type::FUNCTION_BODY);

        RC_Expression* result_expression = rc_expression_create_empty(analyser, RC_Expression_Type::ANALYSIS_ITEM, expression_node);
        result_expression->options.analysis_item = function_item;
        return result_expression;
    }
    case AST_Node_Type::FUNCTION_SIGNATURE:
    {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::FUNCTION_SIGNATURE, expression_node);
        AST_Node* parameter_block_node = expression_node->child_start;
        AST_Node* parameter_node = parameter_block_node->child_start;
        result_expr->options.function_signature.parameters = dynamic_array_create_empty<RC_Parameter>(1);
        while (parameter_node != 0)
        {
            assert(parameter_node->type == AST_Node_Type::PARAMETER || parameter_node->type == AST_Node_Type::PARAMETER_COMPTIME, "");
            RC_Parameter param;
            param.type_expression = rc_analyser_analyse_expression(analyser, parameter_node->child_start);
            param.param_id = parameter_node->id;
            param.param_node = parameter_node;
            param.is_comptime = parameter_node->type == AST_Node_Type::PARAMETER_COMPTIME;
            dynamic_array_push_back(&result_expr->options.function_signature.parameters, param);
            parameter_node = parameter_node->neighbor;
        }
        if (expression_node->child_count == 2) {
            result_expr->options.function_signature.return_type_expression =
                optional_make_success(rc_analyser_analyse_expression(analyser, expression_node->child_end));
        }
        else {
            result_expr->options.function_signature.return_type_expression = optional_make_failure<RC_Expression*>();
        }
        return result_expr;
    }
    case AST_Node_Type::UNION:
    case AST_Node_Type::C_UNION:
    case AST_Node_Type::STRUCT:
    {
        RC_Analysis_Item* struct_item = rc_analysis_item_create_empty(analyser, RC_Analysis_Item_Type::STRUCTURE, analyser->analysis_item);
        auto structure = &struct_item->options.structure;
        structure->symbol = 0;

        RC_Analysis_Item* backup_item = analyser->analysis_item;
        analyser->analysis_item = struct_item;
        SCOPE_EXIT(analyser->analysis_item = backup_item;);

        if (expression_node->type == AST_Node_Type::UNION) {
            structure->structure_type = Structure_Type::UNION;
        }
        else if (expression_node->type == AST_Node_Type::C_UNION) {
            structure->structure_type = Structure_Type::C_UNION;
        }
        else {
            assert(expression_node->type == AST_Node_Type::STRUCT, "");
            structure->structure_type = Structure_Type::STRUCT;
        }

        // Analyse members
        structure->members = dynamic_array_create_empty<RC_Struct_Member>(2);
        AST_Node* member_node = expression_node->child_start;
        while (member_node != 0) {
            RC_Struct_Member member;
            member.id = member_node->id;
            analyser->dependency_type = RC_Dependency_Type::MEMBER_IN_MEMORY;
            member.type_expression = rc_analyser_analyse_expression(analyser, member_node->child_start);
            dynamic_array_push_back(&structure->members, member);
            member_node = member_node->neighbor;
        }
        RC_Expression* result_expression = rc_expression_create_empty(analyser, RC_Expression_Type::ANALYSIS_ITEM, expression_node);
        result_expression->options.analysis_item = struct_item;
        return result_expression;
    }
    case AST_Node_Type::ENUM: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::ENUM, expression_node);
        result_expr->options.enumeration.members = dynamic_array_create_empty<RC_Enum_Member>(2);
        AST_Node* enum_member_node = expression_node->child_start;
        while (enum_member_node != 0)
        {
            RC_Enum_Member member;
            member.id = enum_member_node->id;
            member.node = enum_member_node;
            if (enum_member_node->child_start != 0) {
                member.value_expression = optional_make_success(rc_analyser_analyse_expression(analyser, enum_member_node->child_start));
            }
            else {
                member.value_expression = optional_make_failure<RC_Expression*>();
            }
            dynamic_array_push_back(&result_expr->options.enumeration.members, member);
            enum_member_node = enum_member_node->neighbor;
        }
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_POINTER: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::POINTER, expression_node);
        result_expr->options.pointer_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_IDENTIFIER:
    {
        RC_Symbol_Read* read = new RC_Symbol_Read;
        read->identifier_node = expression_node->child_start;
        read->symbol_table = analyser->symbol_table;
        read->symbol = 0;
        read->type = analyser->dependency_type;
        read->item = analyser->analysis_item;
        dynamic_array_push_back(&analyser->analysis_item->symbol_dependencies, read);

        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::SYMBOL_READ, expression_node);
        result_expr->options.symbol_read = read;
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_SLICE_TYPE: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::SLICE_TYPE, expression_node);
        result_expr->options.slice_type.element_type_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_ARRAY_TYPE: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::ARRAY_TYPE, expression_node);
        result_expr->options.array_type.element_type_expression = rc_analyser_analyse_expression(analyser, expression_node->child_end);

        analyser->dependency_type = RC_Dependency_Type::NORMAL; // Reset dependency type to normal, so that the size dependencies need to be finished
        result_expr->options.array_type.size_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_LITERAL: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::LITERAL_READ, expression_node);
        result_expr->options.literal_read = *expression_node->literal_token;
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_NEW: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::NEW_EXPR, expression_node);
        result_expr->options.new_expression.count_expression = optional_make_failure<RC_Expression*>();
        result_expr->options.new_expression.type_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_NEW_ARRAY: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::NEW_EXPR, expression_node);
        result_expr->options.new_expression.count_expression = optional_make_success(rc_analyser_analyse_expression(analyser, expression_node->child_start));
        result_expr->options.new_expression.type_expression = rc_analyser_analyse_expression(analyser, expression_node->child_end);
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::FUNCTION_CALL, expression_node);
        result_expr->options.function_call.call_expr = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        result_expr->options.function_call.arguments = dynamic_array_create_empty<RC_Expression*>(math_maximum(1, expression_node->child_count));
        AST_Node* arguments_block_node = expression_node->child_end;
        AST_Node* argument_node = arguments_block_node->child_start;
        while (argument_node != 0) {
            dynamic_array_push_back(&result_expr->options.function_call.arguments, rc_analyser_analyse_expression(analyser, argument_node->child_start));
            argument_node = argument_node->neighbor;
        }
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::ARRAY_ACCESS, expression_node);
        result_expr->options.array_access.array_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        result_expr->options.array_access.index_expression = rc_analyser_analyse_expression(analyser, expression_node->child_end);
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_ARRAY_INITIALIZER:
    case AST_Node_Type::EXPRESSION_AUTO_ARRAY_INITIALIZER:
    {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::ARRAY_INITIALIZER, expression_node);
        AST_Node* init_node = expression_node->child_start;
        if (expression_node->type == AST_Node_Type::EXPRESSION_ARRAY_INITIALIZER) {
            result_expr->options.array_initializer.type_expression =
                optional_make_success<RC_Expression*>(rc_analyser_analyse_expression(analyser, expression_node->child_start));
            init_node = init_node->neighbor;
        }
        else {
            result_expr->options.array_initializer.type_expression = optional_make_failure<RC_Expression*>();
        }
        result_expr->options.array_initializer.element_initializers = dynamic_array_create_empty<RC_Expression*>(expression_node->child_count + 1);
        while (init_node != 0) {
            dynamic_array_push_back(&result_expr->options.array_initializer.element_initializers, rc_analyser_analyse_expression(analyser, init_node));
            init_node = init_node->neighbor;
        }
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_STRUCT_INITIALIZER:
    case AST_Node_Type::EXPRESSION_AUTO_STRUCT_INITIALIZER:
    {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::STRUCT_INITIALIZER, expression_node);
        AST_Node* args_node = 0;
        if (expression_node->type == AST_Node_Type::EXPRESSION_STRUCT_INITIALIZER) {
            result_expr->options.struct_initializer.type_expression =
                optional_make_success<RC_Expression*>(rc_analyser_analyse_expression(analyser, expression_node->child_start));
            args_node = expression_node->child_end;
        }
        else {
            result_expr->options.struct_initializer.type_expression = optional_make_failure<RC_Expression*>();
            args_node = expression_node->child_start;
        }
        result_expr->options.struct_initializer.member_initializers = dynamic_array_create_empty<RC_Member_Initializer>(expression_node->child_count + 1);
        AST_Node* init_node = args_node->child_start;
        while (init_node != 0) {
            RC_Member_Initializer member_init;
            member_init.init_expression = rc_analyser_analyse_expression(analyser, init_node->child_start);
            if (init_node->type == AST_Node_Type::ARGUMENT_UNNAMED) {
                member_init.member_id = optional_make_failure<String*>();
            }
            else {
                member_init.member_id = optional_make_success(init_node->id);
            }
            dynamic_array_push_back(&result_expr->options.struct_initializer.member_initializers, member_init);
            init_node = init_node->neighbor;
        }
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_AUTO_ENUM: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::AUTO_ENUM, expression_node);
        result_expr->options.auto_enum_member_id = expression_node->id;
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::MEMBER_ACCESS, expression_node);
        result_expr->options.member_access.expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        result_expr->options.member_access.member_name = expression_node->id;
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_CAST_RAW:
    case AST_Node_Type::EXPRESSION_CAST:
    case AST_Node_Type::EXPRESSION_CAST_PTR: {
        RC_Cast_Type type;
        bool has_type_expression;
        if (expression_node->type == AST_Node_Type::EXPRESSION_CAST_RAW) {
            type = RC_Cast_Type::PTR_TO_RAW;
            has_type_expression = false;
        }
        else if (expression_node->type == AST_Node_Type::EXPRESSION_CAST) {
            if (expression_node->child_count == 1) {
                type = RC_Cast_Type::AUTO_CAST;
                has_type_expression = false;
            }
            else {
                type = RC_Cast_Type::TYPE_TO_TYPE;
                has_type_expression = true;
            }
        }
        else {
            type = RC_Cast_Type::RAW_TO_PTR;
            has_type_expression = true;
        }

        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::CAST, expression_node);
        result_expr->options.cast.type_expression = 0;
        if (has_type_expression) {
            result_expr->options.cast.type_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        }
        result_expr->options.cast.operand = rc_analyser_analyse_expression(analyser, expression_node->child_end);
        result_expr->options.cast.type = type;
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_BAKE:
    {
        RC_Analysis_Item* bake_item = rc_analysis_item_create_empty(analyser, RC_Analysis_Item_Type::BAKE, analyser->analysis_item);
        auto bake = &bake_item->options.bake;

        RC_Analysis_Item* backup_item = analyser->analysis_item;
        SCOPE_EXIT(analyser->analysis_item = backup_item;);
        analyser->analysis_item = bake_item;

        bake->type_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        bake->body = rc_analyser_analyse_statement_block(analyser, expression_node->child_end, RC_Block_Type::BAKE_BLOCK);

        RC_Expression* result_expression = rc_expression_create_empty(analyser, RC_Expression_Type::ANALYSIS_ITEM, expression_node);
        result_expression->options.analysis_item = bake_item;
        return result_expression;
    }
    case AST_Node_Type::EXPRESSION_TYPE_INFO: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::TYPE_INFO, expression_node);
        result_expr->options.type_info_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_TYPE_OF: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::TYPE_OF, expression_node);
        result_expr->options.type_of_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_NOT_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: {
        RC_Binary_Operation_Type op_type = (RC_Binary_Operation_Type)
            ((int)expression_node->type - (int)AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION + (int)RC_Binary_Operation_Type::ADDITION);
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::BINARY_OPERATION, expression_node);
        result_expr->options.binary_operation.left_operand = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        result_expr->options.binary_operation.right_operand = rc_analyser_analyse_expression(analyser, expression_node->child_end);
        result_expr->options.binary_operation.op_type = op_type;
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::UNARY_OPERATION, expression_node);
        result_expr->options.unary_expression.operand = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        result_expr->options.unary_expression.op_type = RC_Unary_Operation_Type::NEGATE;
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::UNARY_OPERATION, expression_node);
        result_expr->options.unary_expression.operand = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        result_expr->options.unary_expression.op_type = RC_Unary_Operation_Type::LOGICAL_NOT;
        return result_expr;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE: {
        RC_Expression* result_expr = rc_expression_create_empty(analyser, RC_Expression_Type::DEREFERENCE, expression_node);
        result_expr->options.dereference_expression = rc_analyser_analyse_expression(analyser, expression_node->child_start);
        return result_expr;
    }
    default: panic("");
    }
    panic("");
    return rc_expression_create_empty(analyser, RC_Expression_Type::BINARY_OPERATION, expression_node);
}

void rc_expression_find_symbol_reads(RC_Expression* expression, Dynamic_Array<RC_Symbol_Read*>* reads)
{
    switch (expression->type)
    {
    case RC_Expression_Type::MODULE:
        break;
    case RC_Expression_Type::ANALYSIS_ITEM:
        break;
    case RC_Expression_Type::SYMBOL_READ:
        dynamic_array_push_back(reads, expression->options.symbol_read);
        break;
    case RC_Expression_Type::ENUM: {
        for (int i = 0; i < expression->options.enumeration.members.size; i++) {
            auto mem = &expression->options.enumeration.members[i];
            if (mem->value_expression.available) {
                rc_expression_find_symbol_reads(mem->value_expression.value, reads);
            }
        }
        break;
    }
    case RC_Expression_Type::ARRAY_TYPE: {
        rc_expression_find_symbol_reads(expression->options.array_type.element_type_expression, reads);
        rc_expression_find_symbol_reads(expression->options.array_type.size_expression, reads);
        break;
    }
    case RC_Expression_Type::SLICE_TYPE: {
        rc_expression_find_symbol_reads(expression->options.slice_type.element_type_expression, reads);
        break;
    }
    case RC_Expression_Type::FUNCTION_SIGNATURE: {
        auto signature = &expression->options.function_signature;
        for (int i = 0; i < signature->parameters.size; i++) {
            rc_expression_find_symbol_reads(signature->parameters[i].type_expression, reads);
        }
        if (signature->return_type_expression.available) {
            rc_expression_find_symbol_reads(signature->return_type_expression.value, reads);
        }
        break;
    }
    case RC_Expression_Type::FUNCTION_CALL: {
        auto call = &expression->options.function_call;
        for (int i = 0; i < call->arguments.size; i++) {
            rc_expression_find_symbol_reads(call->arguments[i], reads);
        }
        rc_expression_find_symbol_reads(call->call_expr, reads);
        break;
    }
    case RC_Expression_Type::BINARY_OPERATION: {
        rc_expression_find_symbol_reads(expression->options.binary_operation.left_operand, reads);
        rc_expression_find_symbol_reads(expression->options.binary_operation.right_operand, reads);
        break;
    }
    case RC_Expression_Type::UNARY_OPERATION: {
        rc_expression_find_symbol_reads(expression->options.unary_expression.operand, reads);
        break;
    }
    case RC_Expression_Type::LITERAL_READ: {
        break;
    }
    case RC_Expression_Type::NEW_EXPR: {
        if (expression->options.new_expression.count_expression.available) {
            rc_expression_find_symbol_reads(expression->options.new_expression.count_expression.value, reads);
        }
        rc_expression_find_symbol_reads(expression->options.new_expression.type_expression, reads);
        break;
    }
    case RC_Expression_Type::ARRAY_ACCESS: {
        rc_expression_find_symbol_reads(expression->options.array_access.array_expression, reads);
        rc_expression_find_symbol_reads(expression->options.array_access.index_expression, reads);
        break;
    }
    case RC_Expression_Type::ARRAY_INITIALIZER: {
        auto init = &expression->options.array_initializer;
        for (int i = 0; i < init->element_initializers.size; i++) {
            rc_expression_find_symbol_reads(init->element_initializers[i], reads);
        }
        if (init->type_expression.available) {
            rc_expression_find_symbol_reads(init->type_expression.value, reads);
        }
        break;
    }
    case RC_Expression_Type::STRUCT_INITIALIZER: {
        auto init = &expression->options.struct_initializer;
        for (int i = 0; i < init->member_initializers.size; i++) {
            rc_expression_find_symbol_reads(init->member_initializers[i].init_expression, reads);
        }
        if (init->type_expression.available) {
            rc_expression_find_symbol_reads(init->type_expression.value, reads);
        }
        break;
    }
    case RC_Expression_Type::AUTO_ENUM: {
        break;
    }
    case RC_Expression_Type::MEMBER_ACCESS: {
        rc_expression_find_symbol_reads(expression->options.member_access.expression, reads);
        break;
    }
    case RC_Expression_Type::CAST: {
        if (expression->options.cast.type != RC_Cast_Type::AUTO_CAST) {
            rc_expression_find_symbol_reads(expression->options.cast.type_expression, reads);
        }
        rc_expression_find_symbol_reads(expression->options.cast.operand, reads);
        break;
    }
    case RC_Expression_Type::TYPE_INFO: {
        rc_expression_find_symbol_reads(expression->options.type_info_expression, reads);
        break;
    }
    case RC_Expression_Type::TYPE_OF: {
        rc_expression_find_symbol_reads(expression->options.type_info_expression, reads);
        break;
    }
    case RC_Expression_Type::DEREFERENCE: {
        rc_expression_find_symbol_reads(expression->options.dereference_expression, reads);
        break;
    }
    case RC_Expression_Type::POINTER: {
        rc_expression_find_symbol_reads(expression->options.pointer_expression, reads);
        break;
    }
    default: panic("");
    }
    return;
}


void rc_analyser_analyse_symbol_definition_node(RC_Analyser* analyser, AST_Node* definition_node)
{
    if (definition_node->type == AST_Node_Type::COMPTIME_DEFINE_INFER)
    {
        if (definition_node->child_start->type == AST_Node_Type::FUNCTION) {
            RC_Expression* value = rc_analyser_analyse_expression(analyser, definition_node->child_end);
            assert(value->type == RC_Expression_Type::ANALYSIS_ITEM, "");
            value->options.analysis_item->options.function.symbol = symbol_table_define_symbol(
                analyser->symbol_table, analyser, definition_node->id, Symbol_Type::UNRESOLVED, definition_node
            );
            return;
        }
        else if (definition_node->child_start->type == AST_Node_Type::STRUCT) {
            RC_Expression* value = rc_analyser_analyse_expression(analyser, definition_node->child_end);
            assert(value->type == RC_Expression_Type::ANALYSIS_ITEM, "");
            value->options.analysis_item->options.structure.symbol = symbol_table_define_symbol(
                analyser->symbol_table, analyser, definition_node->id, Symbol_Type::UNRESOLVED, definition_node
            );
            return;
        }
    }

    RC_Analysis_Item* item = rc_analysis_item_create_empty(analyser, RC_Analysis_Item_Type::DEFINITION, analyser->analysis_item);
    auto definition = &item->options.definition;
    RC_Analysis_Item* backup_item = analyser->analysis_item;
    analyser->analysis_item = item;
    SCOPE_EXIT(analyser->analysis_item = backup_item;);

    definition->symbol = symbol_table_define_symbol(
        analyser->symbol_table, analyser, definition_node->id, Symbol_Type::UNRESOLVED, definition_node
    );
    definition->is_comptime_definition =
        definition_node->type == AST_Node_Type::COMPTIME_DEFINE_ASSIGN || definition_node->type == AST_Node_Type::COMPTIME_DEFINE_INFER;
    if (definition_node->type == AST_Node_Type::COMPTIME_DEFINE_ASSIGN || definition_node->type == AST_Node_Type::VARIABLE_DEFINITION ||
        definition_node->type == AST_Node_Type::VARIABLE_DEFINE_ASSIGN) {
        definition->type_expression = optional_make_success(rc_analyser_analyse_expression(analyser, definition_node->child_start));
    }
    else {
        definition->type_expression = optional_make_failure<RC_Expression*>();
    }
    if (definition_node->type == AST_Node_Type::COMPTIME_DEFINE_ASSIGN || definition_node->type == AST_Node_Type::COMPTIME_DEFINE_INFER ||
        definition_node->type == AST_Node_Type::VARIABLE_DEFINE_ASSIGN || definition_node->type == AST_Node_Type::VARIABLE_DEFINE_INFER)
    {
        RC_Expression* value = rc_analyser_analyse_expression(analyser, definition_node->child_end);
        if (value->type == RC_Expression_Type::ANALYSIS_ITEM)
        {
            if (value->options.analysis_item->type == RC_Analysis_Item_Type::STRUCTURE) {
                value->options.analysis_item->options.structure.symbol = definition->symbol;
            }
            else if (value->options.analysis_item->type == RC_Analysis_Item_Type::FUNCTION) {
                value->options.analysis_item->options.function.symbol = definition->symbol;
            }
        }
        definition->value_expression = optional_make_success(value);
    }
    else {
        definition->value_expression = optional_make_failure<RC_Expression*>();
    }
}

void rc_analyser_analyse_definitions(RC_Analyser* analyser, AST_Node* definitions_node)
{
    assert(definitions_node->type == AST_Node_Type::DEFINITIONS, "");
    AST_Node* top_level_node = definitions_node->child_start;

    while (top_level_node != 0)
    {
        SCOPE_EXIT(top_level_node = top_level_node->neighbor);
        switch (top_level_node->type)
        {
        case AST_Node_Type::COMPTIME_DEFINE_ASSIGN:
        case AST_Node_Type::COMPTIME_DEFINE_INFER:
        case AST_Node_Type::VARIABLE_DEFINE_ASSIGN:
        case AST_Node_Type::VARIABLE_DEFINE_INFER:
        case AST_Node_Type::VARIABLE_DEFINITION: {
            rc_analyser_analyse_symbol_definition_node(analyser, top_level_node);
            break;
        }
        case AST_Node_Type::EXTERN_FUNCTION_DECLARATION:
            /*
            {
                // Create Workload
                Analysis_Workload workload;
                workload.type = Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION;
                workload.options.extern_function.node = top_level_node;
                workload.options.extern_function.parent_module = module;
                dynamic_array_push_back(&analyser->active_workloads, workload);
                break;
            }
            */
            break;
        case AST_Node_Type::EXTERN_HEADER_IMPORT:
            /*
        {
            // Create Workload
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::EXTERN_HEADER_IMPORT;
            workload.options.extern_header.parent_module = module;
            workload.options.extern_header.node = top_level_node;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        */
            break;
        case AST_Node_Type::LOAD_FILE:
            /*
        {
            if (hashset_contains(&analyser->loaded_filenames, top_level_node->id)) {
                break;
            }
            hashset_insert_element(&analyser->loaded_filenames, top_level_node->id);

            Optional<String> file_content = file_io_load_text_file(top_level_node->id->characters);
            if (file_content.available)
            {
                String content = file_content.value;
                Code_Origin origin;
                origin.type = Code_Origin_Type::LOADED_FILE;
                origin.load_node = top_level_node;
                compiler_add_source_code(analyser->compiler, content, origin);
            }
            else {
                Semantic_Error error;
                error.type = Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE;
                error.error_node = top_level_node;
                semantic_analyser_log_error(analyser, error);
            }
            break;
        }
        */
            break;
        case AST_Node_Type::EXTERN_LIB_IMPORT:
            /*
            {
                dynamic_array_push_back(&analyser->compiler->extern_sources.lib_files, top_level_node->id);
                break;
            }
            */
            break;
        default: panic("HEy");
        }
    }
}

void rc_analyser_analyse(RC_Analyser* analyser, AST_Node* root_node)
{
    assert(root_node->type == AST_Node_Type::ROOT, "");
    analyser->symbol_table = analyser->root_symbol_table;
    analyser->analysis_item = analyser->root_item;
    rc_analyser_analyse_definitions(analyser, root_node->child_start);
}

void rc_analyser_log_error(RC_Analyser* analyser, Symbol* existing_symbol, AST_Node* error_node)
{
    Symbol_Error error;
    error.error_node = error_node;
    error.existing_symbol = existing_symbol;
    dynamic_array_push_back(&analyser->errors, error);
}

void ast_identifier_node_append_to_string(String* string, AST_Node* node)
{
    assert(node->type == AST_Node_Type::IDENTIFIER_NAME || node->type == AST_Node_Type::IDENTIFIER_PATH, "");
    string_append_formated(string, node->id->characters);
    if (node->type == AST_Node_Type::IDENTIFIER_PATH) {
        string_append_formated(string, "~");
        ast_identifier_node_append_to_string(string, node->child_start);
    }
}

void string_set_indentation(String* string, int indentation)
{
    for (int i = 0; i < indentation; i++) {
        string_append_formated(string, "  ");
    }
}

void rc_analysis_item_append_to_string(RC_Analysis_Item* item, String* string, int indentation)
{
    string_set_indentation(string, indentation);
    switch (item->type)
    {
    case RC_Analysis_Item_Type::DEFINITION:
        string_append_formated(string, "Symbol \"%s\"", item->options.definition.symbol->id->characters);
        if (!item->options.definition.is_comptime_definition) {
            string_append_formated(string, ", Global_Variable");
        }
        else {
            string_append_formated(string, ", Definition");
        }
        break;
    case RC_Analysis_Item_Type::FUNCTION:
        if (item->options.function.symbol != 0) {
            string_append_formated(string, "Symbol \"%s\"", item->options.function.symbol->id->characters);
            string_append_formated(string, ", ");
        }
        string_append_formated(string, "Function");
        break;
    case RC_Analysis_Item_Type::FUNCTION_BODY:
        string_append_formated(string, "Body");
        break;
    case RC_Analysis_Item_Type::ROOT:
        string_append_formated(string, "Root");
        break;
    case RC_Analysis_Item_Type::STRUCTURE:
        if (item->options.structure.symbol != 0) {
            string_append_formated(string, "Symbol \"%s\"", item->options.structure.symbol->id->characters);
            string_append_formated(string, ", ");
        }
        string_append_formated(string, "Structure");
        break;
    default: panic("");
    }

    if (item->symbol_dependencies.size != 0) {
        string_append_formated(string, ": ", item->symbol_dependencies.size);
    }
    for (int i = 0; i < item->symbol_dependencies.size; i++)
    {
        RC_Symbol_Read* read = item->symbol_dependencies[i];
        ast_identifier_node_append_to_string(string, read->identifier_node);
        switch (read->type)
        {
        case RC_Dependency_Type::NORMAL: break;
        case RC_Dependency_Type::MEMBER_IN_MEMORY: string_append_formated(string, "(Member_In_Memory)"); break;
        case RC_Dependency_Type::MEMBER_REFERENCE: string_append_formated(string, "(Member_Reference)"); break;
        default: panic("");
        }
        if (i != item->symbol_dependencies.size - 1) {
            string_append_formated(string, ", ");
        }
    }
    if (item->type == RC_Analysis_Item_Type::FUNCTION) {
        string_append_formated(string, "\n");
        rc_analysis_item_append_to_string(item->options.function.body_item, string, indentation + 1);
    }
    for (int i = 0; i < item->item_dependencies.size; i++) {
        string_append_formated(string, "\n");
        rc_analysis_item_append_to_string(item->item_dependencies[i].item, string, indentation + 1);
    }
    string_append_formated(string, "\n");
}
