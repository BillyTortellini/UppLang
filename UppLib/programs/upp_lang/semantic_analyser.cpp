#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"
#include "compiler.hpp"
#include "type_system.hpp"
#include "../../utility/file_io.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"

bool PRINT_DEPENDENCIES = false;

/*
MOD_TREE
*/
ModTree_Variable modtree_variable_make(ModTree_Variable_Origin origin, Symbol* symbol, Type_Signature* var_type)
{
    ModTree_Variable var;
    var.data_type = var_type;
    var.origin = origin;
    var.symbol = symbol;
    var.value.address_was_taken = false;
    var.value.last_write_block = 0;
    var.value.value.data = 0;
    var.value.value.signature = var_type;
    return var;
}

Expression_Value expression_value_make_unknown() {
    Expression_Value result;
    result.signature = 0;
    result.data = 0;
    return result;
}

Expression_Value expression_value_make_type(Semantic_Analyser* analyser, Type_Signature* signature) {
    Expression_Value result;
    result.signature = signature;
    if (!(signature->type == Signature_Type::PRIMITIVE ||
        (signature->type == Signature_Type::POINTER && signature->options.pointer_child->type == Signature_Type::FUNCTION))) return expression_value_make_unknown();
    result.data = stack_allocator_allocate_size(&analyser->allocator_values, signature->size, signature->alignment);
    return result;
}

void constant_analysis_assign_variable_value(ModTree_Variable* variable, ModTree_Expression* expr, ModTree_Block* write_block)
{
    if (expr->value.data == 0) return;
    if (variable->origin.type != ModTree_Variable_Origin_Type::LOCAL) return;
    if (variable->value.address_was_taken) return;
    variable->value.last_write_block = write_block;
    variable->value.value = expr->value; // Is this legal?
}

void constant_analysis_assign_value(ModTree_Expression* left_expr, ModTree_Expression* right_expr, ModTree_Block* write_block)
{
    if (right_expr->value.data == 0) return;
    if (left_expr->expression_type != ModTree_Expression_Type::VARIABLE_READ) return;
    ModTree_Variable* variable = left_expr->options.variable_read;
    if (variable->value.address_was_taken) return;
    variable->value.value.data = right_expr->value.data;
    variable->value.last_write_block = write_block;
}

Expression_Value constant_analysis_get_variable_value(ModTree_Variable* variable, ModTree_Block* read_block)
{
    if (variable->value.value.data == 0) return variable->value.value;
    if (variable->value.last_write_block != 0 && variable->value.last_write_block == read_block) return variable->value.value;
    return expression_value_make_unknown();
}

void constant_analysis_take_address_of(ModTree_Variable* variable) 
{
    variable->value.address_was_taken = true;
    variable->value.last_write_block = 0;
    variable->value.value.data = 0;
}

void modtree_block_destroy(ModTree_Block* block);
void modtree_expression_destroy(ModTree_Expression* expression)
{
    switch (expression->expression_type)
    {
    case ModTree_Expression_Type::BINARY_OPERATION:
        modtree_expression_destroy(expression->options.binary_operation.left_operand);
        modtree_expression_destroy(expression->options.binary_operation.right_operand);
        break;
    case ModTree_Expression_Type::UNARY_OPERATION:
        modtree_expression_destroy(expression->options.unary_operation.operand);
        break;
    case ModTree_Expression_Type::CONSTANT_READ:
        break;
    case ModTree_Expression_Type::FUNCTION_CALL:
        for (int i = 0; i < expression->options.function_call.arguments.size; i++) {
            modtree_expression_destroy(expression->options.function_call.arguments[i]);
        }
        dynamic_array_destroy(&expression->options.function_call.arguments);
        if (expression->options.function_call.is_pointer_call) {
            modtree_expression_destroy(expression->options.function_call.pointer_expression);
        }
        break;
    case ModTree_Expression_Type::VARIABLE_READ:
        break;
    case ModTree_Expression_Type::ARRAY_ACCESS: {
        modtree_expression_destroy(expression->options.array_access.array_expression);
        modtree_expression_destroy(expression->options.array_access.index_expression);
        break;
    }
    case ModTree_Expression_Type::MEMBER_ACCESS: {
        modtree_expression_destroy(expression->options.member_access.structure_expression);
        break;
    }
    case ModTree_Expression_Type::CAST: {
        modtree_expression_destroy(expression->options.cast.cast_argument);
        break;
    }
    case ModTree_Expression_Type::FUNCTION_POINTER_READ: {
        //modtree_expression_destroy(expression->options.function_pointer_read);
        break;
    }
    case ModTree_Expression_Type::NEW_ALLOCATION: {
        if (expression->options.new_allocation.element_count.available) {
            modtree_expression_destroy(expression->options.new_allocation.element_count.value);
        }
        break;
    }
    default: panic("HEY");
    }

    delete expression;
}

void modtree_statement_destroy(ModTree_Statement* statement)
{
    switch (statement->type)
    {
    case ModTree_Statement_Type::BLOCK:
        modtree_block_destroy(statement->options.block);
        break;
    case ModTree_Statement_Type::IF:
        modtree_expression_destroy(statement->options.if_statement.condition);
        modtree_block_destroy(statement->options.if_statement.if_block);
        modtree_block_destroy(statement->options.if_statement.else_block);
        break;
    case ModTree_Statement_Type::WHILE:
        modtree_expression_destroy(statement->options.while_statement.condition);
        modtree_block_destroy(statement->options.while_statement.while_block);
        break;
    case ModTree_Statement_Type::EXIT:
    case ModTree_Statement_Type::BREAK:
    case ModTree_Statement_Type::CONTINUE:
        break;
    case ModTree_Statement_Type::RETURN:
        if (statement->options.return_value.available) {
            modtree_expression_destroy(statement->options.return_value.value);
        }
        break;
    case ModTree_Statement_Type::SWITCH:
        modtree_expression_destroy(statement->options.switch_statement.condition);
        for (int i = 0; i < statement->options.switch_statement.cases.size; i++) {
            modtree_block_destroy(statement->options.switch_statement.cases[i].body);
            modtree_expression_destroy(statement->options.switch_statement.cases[i].expression);
        }
        if (statement->options.switch_statement.default_block != 0) {
            modtree_block_destroy(statement->options.switch_statement.default_block);
        }
        break;
    case ModTree_Statement_Type::EXPRESSION:
        modtree_expression_destroy(statement->options.expression);
        break;
    case ModTree_Statement_Type::DELETION:
        modtree_expression_destroy(statement->options.deletion.expression);
        break;
    case ModTree_Statement_Type::ASSIGNMENT:
        modtree_expression_destroy(statement->options.assignment.destination);
        modtree_expression_destroy(statement->options.assignment.source);
        break;
    default: panic("HEY");
    }

    delete statement;
}

void modtree_block_destroy(ModTree_Block* block)
{
    for (int i = 0; i < block->statements.size; i++) {
        modtree_statement_destroy(block->statements[i]);
    }
    dynamic_array_destroy(&block->statements);
    delete block;
}

void modtree_function_destroy(ModTree_Function* function)
{
    switch (function->function_type)
    {
    case ModTree_Function_Type::FUNCTION:
        modtree_block_destroy(function->options.function.body);
        for (int i = 0; i < function->options.function.parameters.size; i++) {
            delete function->options.function.parameters[i];
        }
        dynamic_array_destroy(&function->options.function.parameters);
        dynamic_array_destroy(&function->options.function.dependency_functions);
        dynamic_array_destroy(&function->options.function.dependency_globals);
        break;
    case ModTree_Function_Type::EXTERN_FUNCTION:
        break;
    case ModTree_Function_Type::HARDCODED_FUNCTION:
        break;
    default: panic("HEY");
    }
    delete function;
}

ModTree_Module* modtree_module_create(Semantic_Analyser* analyser, Symbol_Table* symbol_table, ModTree_Module* parent, Symbol* symbol)
{
    ModTree_Module* result = new ModTree_Module;
    result->functions = dynamic_array_create_empty<ModTree_Function*>(4);
    result->globals = dynamic_array_create_empty<ModTree_Variable*>(4);
    result->modules = dynamic_array_create_empty<ModTree_Module*>(2);
    result->symbol_table = symbol_table;
    result->parent_module = parent;
    if (result->parent_module != 0) {
        dynamic_array_push_back(&parent->modules, result);
    }
    result->symbol = symbol;
    return result;
}

void modtree_module_destroy(ModTree_Module* module)
{
    for (int i = 0; i < module->functions.size; i++) {
        modtree_function_destroy(module->functions[i]);
    }
    dynamic_array_destroy(&module->functions);
    for (int i = 0; i < module->globals.size; i++) {
        delete module->globals[i];
    }
    dynamic_array_destroy(&module->globals);
    for (int i = 0; i < module->modules.size; i++) {
        modtree_module_destroy(module->modules[i]);
    }
    dynamic_array_destroy(&module->modules);
    delete module;
}

ModTree_Program* modtree_program_create(Semantic_Analyser* analyser, Symbol_Table* base_table)
{
    ModTree_Program* result = new ModTree_Program();
    result->entry_function = 0;
    result->root_module = modtree_module_create(analyser, symbol_table_create(analyser, base_table, nullptr), 0, 0);
    return result;
}

void modtree_program_destroy(ModTree_Program* program)
{
    modtree_module_destroy(program->root_module);
    delete program;
}

bool modtree_expression_result_is_temporary(ModTree_Expression* expression)
{
    switch (expression->expression_type)
    {
    case ModTree_Expression_Type::BINARY_OPERATION: return true;
    case ModTree_Expression_Type::UNARY_OPERATION: 
    {
        switch (expression->options.unary_operation.operation_type) 
        {
        case ModTree_Unary_Operation_Type::ADDRESS_OF: return true; // The pointer is always temporary
        case ModTree_Unary_Operation_Type::DEREFERENCE: return false; // There are special cases where memory loss is not detected, e.g. &(new int) 
        case ModTree_Unary_Operation_Type::LOGICAL_NOT: return true;
        case ModTree_Unary_Operation_Type::NEGATE: return true;
        default: panic("");
        }
        return true;
    }
    case ModTree_Expression_Type::CONSTANT_READ: return true;
    case ModTree_Expression_Type::FUNCTION_CALL: return true;
    case ModTree_Expression_Type::VARIABLE_READ: return false;
    case ModTree_Expression_Type::FUNCTION_POINTER_READ: return true;
    case ModTree_Expression_Type::ARRAY_ACCESS: return modtree_expression_result_is_temporary(expression->options.array_access.array_expression);
    case ModTree_Expression_Type::MEMBER_ACCESS: return modtree_expression_result_is_temporary(expression->options.member_access.structure_expression);
    case ModTree_Expression_Type::NEW_ALLOCATION: return true;
    case ModTree_Expression_Type::CAST: return true;
    default: panic("");
    }

    panic("");
    return false;
}

ModTree_Block* modtree_block_create_block_child(Symbol_Table* table, ModTree_Block* parent, Block_Type type)
{
    ModTree_Block* result = new ModTree_Block;
    result->statements = dynamic_array_create_empty<ModTree_Statement*>(2);
    result->variables = dynamic_array_create_empty<ModTree_Variable*>(2);
    result->symbol_table = table;
    result->type = type;
    result->origin.parent = parent;
    return result;
}

ModTree_Block* modtree_block_create_function_body(Symbol_Table* table, ModTree_Function* function)
{
    ModTree_Block* result = new ModTree_Block;
    result->statements = dynamic_array_create_empty<ModTree_Statement*>(2);
    result->variables = dynamic_array_create_empty<ModTree_Variable*>(2);
    result->symbol_table = table;
    result->type = Block_Type::FUNCTION_BODY;
    result->origin.function = function;
    return result;
}

Block_Origin modtree_block_origin_make_function(ModTree_Function* function) {
    Block_Origin origin;
    origin.function = function;
    return origin;
}

Block_Origin modtree_block_origin_make_block(ModTree_Block* parent, Block_Type type) {
    Block_Origin origin;
    origin.parent = parent;
    return origin;
}

ModTree_Function* modtree_function_make_empty(
    Semantic_Analyser* analyser, ModTree_Module* module, Symbol_Table* symbol_table, Type_Signature* signature, Symbol* symbol, AST_Node* symbol_table_origin_node)
{
    ModTree_Function* function = new ModTree_Function;
    function->function_type = ModTree_Function_Type::FUNCTION;
    function->parent_module = module;
    function->signature = signature;
    function->symbol = symbol;
    function->options.function.body = modtree_block_create_function_body(
        symbol_table_create(analyser, symbol_table, symbol_table_origin_node), function
    );
    function->options.function.parameters = dynamic_array_create_empty<ModTree_Variable*>(2);
    function->options.function.dependency_functions = dynamic_array_create_empty<ModTree_Function*>(2);
    function->options.function.dependency_globals = dynamic_array_create_empty<ModTree_Variable*>(2);

    dynamic_array_push_back(&module->functions, function);
    return function;
}



/*
    Symbol Table
*/
Symbol_Table* symbol_table_create(Semantic_Analyser* analyser, Symbol_Table* parent, AST_Node* node)
{
    Symbol_Table* table = new Symbol_Table();
    table->parent = parent;
    table->symbols = hashtable_create_pointer_empty<String*, Symbol*>(4);
    dynamic_array_push_back(&analyser->symbol_tables, table);
    if (node != 0) {
        hashtable_insert_element(&analyser->ast_to_symbol_table, node, table);
    }
    return table;
}

void symbol_destroy(Symbol* symbol);
void symbol_template_data_destroy(Symbol_Template_Data* data)
{
    if (data->is_templated)
    {
        dynamic_array_destroy(&data->parameter_names);
        for (int i = 0; i < data->instances.size; i++) {
            symbol_destroy(data->instances[i]->instance_symbol);
            delete data->instances[i]->instance_symbol;
            dynamic_array_destroy(&data->instances[i]->arguments);
            delete data->instances[i];
        }
        dynamic_array_destroy(&data->instances);
    }
}

void symbol_destroy(Symbol* symbol)
{
    symbol_template_data_destroy(&symbol->template_data);
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

void symbol_add_reference(Semantic_Analyser* analyser, Symbol* symbol, Symbol_Reference reference);
Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool only_current_scope, Symbol_Reference reference, Semantic_Analyser* analyser)
{
    Symbol** found = hashtable_find_element(&table->symbols, id);
    if (found == 0) {
        if (!only_current_scope && table->parent != 0) {
            return symbol_table_find_symbol(table->parent, id, only_current_scope, reference, analyser);
        }
        return nullptr;
    }
    symbol_add_reference(analyser, *found, reference);
    return *found;
}

void symbol_append_to_string(Symbol* symbol, String* string)
{
    string_append_formated(string, "%s ", symbol->id->characters);
    switch (symbol->type)
    {
    case Symbol_Type::VARIABLE:
        type_signature_append_to_string(string, symbol->options.variable->data_type);
        break;
    case Symbol_Type::TYPE:
        type_signature_append_to_string(string, symbol->options.type);
        break;
    case Symbol_Type::FUNCTION:
        /*
        switch (symbol->options.function->function_type) {
        case ModTree_Function_Type::FUNCTION: string_append_formated(string, "Function "); break;
        case ModTree_Function_Type::EXTERN_FUNCTION: string_append_formated(string, "Extern Function "); break;
        case ModTree_Function_Type::HARDCODED_FUNCTION: string_append_formated(string, "Hardcoded Function "); break;
        }
        */
        type_signature_append_to_string(string, symbol->options.function->signature);
        break;
    case Symbol_Type::MODULE:
        string_append_formated(string, "Module");
        break;
    default: panic("What");
    }
}

void symbol_table_append_to_string_with_parent_info(String* string, Symbol_Table* table, Semantic_Analyser* analyser, bool is_parent, bool print_root)
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
        symbol_table_append_to_string_with_parent_info(string, table->parent, analyser, true, print_root);
    }
}

void symbol_table_append_to_string(String* string, Symbol_Table* table, Semantic_Analyser* analyser, bool print_root) {
    symbol_table_append_to_string_with_parent_info(string, table, analyser, false, print_root);
}

Symbol_Template_Data symbol_template_data_make_inherited(Symbol_Template_Data* other) 
{
    Symbol_Template_Data data;
    data.is_templated = other->is_templated;
    if (data.is_templated) {
        data.instances = dynamic_array_create_empty<Symbol_Template_Instance*>(2);
        data.parameter_names = dynamic_array_create_copy(other->parameter_names.data, other->parameter_names.size);
    }
    else {
        data.instances.data = 0;
        data.instances.size = 0;
        data.instances.capacity = 0;
        data.parameter_names.size = 0;
        data.parameter_names.capacity = 0;
        data.parameter_names.data = 0;
    }
    return data;
}

Symbol_Template_Data symbol_template_data_make_no_template()
{
    Symbol_Template_Data data;
    data.is_templated = false;
    data.instances.data = 0;
    data.instances.size = 0;
    data.instances.capacity = 0;
    data.parameter_names.size = 0;
    data.parameter_names.capacity = 0;
    data.parameter_names.data = 0;
    return data;
}

Symbol_Options symbol_options_make_function(ModTree_Function* function) {
    Symbol_Options options;
    options.function = function;
    return options;
}

Symbol_Options symbol_options_make_type(Type_Signature* type) {
    Symbol_Options options;
    options.type = type;
    return options;
}

Symbol_Options symbol_options_make_variable(ModTree_Variable* variable) {
    Symbol_Options options;
    options.variable = variable;
    return options;
}

Symbol_Options symbol_options_make_module(ModTree_Module* module) {
    Symbol_Options options;
    options.module = module;
    return options;
}

Symbol_Reference symbol_reference_make_ignore() {
    Symbol_Reference reference;
    reference.type = Usage_Type::IGNORE_REFERENCE;
    return reference;
}

Symbol_Reference symbol_reference_make(Usage_Type type, AST_Node* node)
{
    Symbol_Reference reference;
    reference.type = type;
    reference.reference_node = node;
    return reference;
}

ModTree_Function* modtree_block_get_function(ModTree_Block* block)
{
    while (block->type != Block_Type::FUNCTION_BODY) {
        block = block->origin.parent;
    }
    return block->origin.function;
}

void symbol_add_reference(Semantic_Analyser* analyser, Symbol* symbol, Symbol_Reference reference) 
{
    if (reference.type == Usage_Type::IGNORE_REFERENCE) return;
    // Collect function dependencies, so compile time code execution is possible
    if (analyser->current_workload != 0) 
    {
        if (analyser->current_workload->type == Analysis_Workload_Type::CODE)
        {
            Analysis_Workload_Code* work = &analyser->current_workload->options.code_block;
            ModTree_Function* function = work->function;
            if (symbol->type == Symbol_Type::FUNCTION) {
                if (symbol->options.function->function_type == ModTree_Function_Type::FUNCTION) {
                    dynamic_array_push_back(&function->options.function.dependency_functions, symbol->options.function);
                }
            }
            else if (symbol->type == Symbol_Type::VARIABLE) {
                if (symbol->options.variable->origin.type == ModTree_Variable_Origin_Type::GLOBAL) {
                    dynamic_array_push_back(&function->options.function.dependency_globals, symbol->options.variable);
                }
            }
        }
    }
    dynamic_array_push_back(&symbol->references, reference);
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, Semantic_Error error);
Symbol* symbol_table_define_symbol(
    Semantic_Analyser* analyser, Symbol_Table* symbol_table, String* id,
    AST_Node* definition_node, Symbol_Type type, Symbol_Options options, Symbol_Template_Data template_data)
{
    assert(id != 0, "HEY");

    if (PRINT_DEPENDENCIES) {
        logg("Defining symbol: %s\n", id);
    }

    // Check if already defined in same scope
    Symbol* found_symbol = symbol_table_find_symbol(symbol_table, id, true, symbol_reference_make_ignore(), analyser);
    if (found_symbol != 0)
    {
        // Two symbols with the same name in one scope is not possible without Overloading
        Semantic_Error error;
        error.type = Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED;
        error.error_node = definition_node;
        error.id = id;
        error.symbol = found_symbol;
        semantic_analyser_log_error(analyser, error);
        symbol_template_data_destroy(&template_data);
        return 0;
    }

    // Check if already defined in outer scopes
    found_symbol = symbol_table_find_symbol(symbol_table, id, false, symbol_reference_make_ignore(), analyser);
    if (found_symbol != 0)
    {
        bool shadowing_allowed = false;
        // Check if shadowing rules apply
        switch (type)
        {
        case Symbol_Type::FUNCTION: {
            break;
        }
        case Symbol_Type::VARIABLE: {
            shadowing_allowed = found_symbol->type == Symbol_Type::VARIABLE; // Variables may only shadow other variables
            break;
        }
        case Symbol_Type::TYPE: {
            shadowing_allowed = found_symbol->type == Symbol_Type::MODULE;
            break;
        }
        case Symbol_Type::MODULE: {
            break;
        }
        default: panic("HEY");
        }
        // Special rule for templates
        if (!shadowing_allowed && found_symbol->type == Symbol_Type::TYPE) {
            if (found_symbol->options.type->type == Signature_Type::TEMPLATE_TYPE) {
                shadowing_allowed = true;
            }
        }
        if (!shadowing_allowed) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED;
            error.error_node = definition_node;
            error.id = id;
            error.symbol = found_symbol;
            semantic_analyser_log_error(analyser, error);
            symbol_template_data_destroy(&template_data);
            return 0;
        }
    }

    Symbol* new_sym = new Symbol;
    new_sym->definition_node = definition_node;
    new_sym->id = id;
    new_sym->options = options;
    new_sym->symbol_table = symbol_table;
    new_sym->template_data = template_data;
    new_sym->type = type;
    new_sym->references = dynamic_array_create_empty<Symbol_Reference>(2);

    hashtable_insert_element(&symbol_table->symbols, id, new_sym);
    return new_sym;
}


/*
    SEMANTIC ANALYSER
*/
// Helpers
struct Type_Analysis_Result
{
    Analysis_Result_Type type;
    union {
        Type_Signature* result_type;
        Workload_Dependency dependency;
    } options;
};

struct Expression_Analysis_Result
{
    Analysis_Result_Type type;
    union
    {
        ModTree_Expression* expression;
        Workload_Dependency dependency;
    } options;
};

struct Variable_Creation_Analysis_Result
{
    Analysis_Result_Type type;
    union {
        Workload_Dependency dependency;
        ModTree_Variable* variable;
    } options;
};

Workload_Dependency workload_dependency_make_code_block_finished(ModTree_Block* code_block, AST_Node* node)
{
    Workload_Dependency dependency;
    dependency.node = node;
    dependency.type = Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED;
    dependency.options.code_block = code_block;
    return dependency;
}

Workload_Dependency workload_dependency_make_type_size_unknown(Type_Signature* type, AST_Node* node)
{
    Workload_Dependency dependency;
    dependency.node = node;
    dependency.type = Workload_Dependency_Type::TYPE_SIZE_UNKNOWN;
    dependency.options.type_signature = type;
    return dependency;
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, Semantic_Error error) {
    dynamic_array_push_back(&analyser->errors, error);
}

void semantic_analyser_define_type_symbol(Semantic_Analyser* analyser, Symbol_Table* table, String* id, Type_Signature* type, AST_Node* definition_node)
{
    Symbol_Options options;
    options.type = type;
    symbol_table_define_symbol(analyser, table, id, definition_node, Symbol_Type::TYPE, options, symbol_template_data_make_no_template());
}

Type_Analysis_Result type_analysis_result_make_success(Type_Signature* result_type)
{
    Type_Analysis_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    result.options.result_type = result_type;
    return result;
}

Type_Analysis_Result type_analysis_result_make_dependency(Workload_Dependency dependency)
{
    Type_Analysis_Result result;
    result.type = Analysis_Result_Type::DEPENDENCY;
    result.options.dependency = dependency;
    return result;
}

Type_Analysis_Result type_analysis_result_make_error()
{
    Type_Analysis_Result result;
    result.type = Analysis_Result_Type::ERROR_OCCURED;
    return result;
}

Expression_Analysis_Result expression_analysis_result_make_success(ModTree_Expression expression)
{
    Expression_Analysis_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    result.options.expression = new ModTree_Expression(expression);
    return result;
}

Expression_Analysis_Result expression_analysis_result_make_error()
{
    Expression_Analysis_Result result;
    result.type = Analysis_Result_Type::ERROR_OCCURED;
    return result;
}

Expression_Analysis_Result expression_analysis_result_make_dependency(Workload_Dependency dependency)
{
    Expression_Analysis_Result result;
    result.type = Analysis_Result_Type::DEPENDENCY;
    result.options.dependency = dependency;
    return result;
}

void analysis_workload_code_block_add_block(Analysis_Workload_Code* code_workload, ModTree_Block* block, AST_Node* block_node)
{
    Block_Analysis progress;
    progress.block = block;
    progress.block_node = block_node;
    progress.current_statement_node = block_node->child_start;
    progress.block_flow = Block_Control_Flow::NO_RETURN;

    progress.inside_defer = code_workload->active_block->inside_defer || block->type == Block_Type::DEFER_BLOCK;
    progress.inside_loop = code_workload->active_block->inside_loop || block->type == Block_Type::WHILE_BODY;
    progress.defer_count_block_start = code_workload->defer_nodes.size;
    if (block->type == Block_Type::WHILE_BODY) {
        progress.defer_count_surrounding_loop = code_workload->defer_nodes.size;
    }
    else {
        progress.defer_count_surrounding_loop = code_workload->active_block->defer_count_surrounding_loop;
    }

    progress.last_analysed_node = 0;
    progress.last_analysed_statement = 0;

    list_add_at_end(&code_workload->block_queue, progress);
}

Analysis_Workload analysis_workload_make_code_block(ModTree_Block* block, AST_Node* block_node)
{
    Analysis_Workload workload;
    workload.type = Analysis_Workload_Type::CODE;
    workload.options.code_block.defer_nodes = dynamic_array_create_empty<AST_Node*>(4);
    workload.options.code_block.block_queue = list_create<Block_Analysis>();
    workload.options.code_block.active_switches = hashtable_create_pointer_empty<AST_Node*, ModTree_Statement*>(2);
    assert(block->type == Block_Type::FUNCTION_BODY, "");
    workload.options.code_block.function = block->origin.function;

    Block_Analysis progress;
    progress.block = block;
    progress.block_node = block_node;
    progress.current_statement_node = block_node->child_start;
    progress.block_flow = Block_Control_Flow::NO_RETURN;

    progress.inside_defer = false;
    progress.inside_loop = false;
    progress.defer_count_block_start = 0;
    progress.defer_count_surrounding_loop = 0;

    progress.last_analysed_node = 0;
    progress.last_analysed_statement = 0;
    list_add_at_end(&workload.options.code_block.block_queue, progress);

    return workload;
}






// Analysis functions
Identifier_Analysis_Result semantic_analyser_instanciate_template(Semantic_Analyser* analyser, Symbol_Table* table, Symbol* symbol,
    Dynamic_Array<Type_Signature*> template_arguments, AST_Node* instance_node)
{
    if (symbol->type == Symbol_Type::MODULE) {
        // NOTE: I could just throw an error here, because this is never usefull currently
        Identifier_Analysis_Result result;
        result.options.symbol = symbol;
        result.type = Analysis_Result_Type::SUCCESS;
        return result;
    }
    assert(symbol->template_data.is_templated, "HEY");
    // Check if arguments match
    if (symbol->template_data.parameter_names.size != template_arguments.size) {
        Semantic_Error error;
        error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT;
        error.symbol = symbol;
        error.error_node = instance_node;
        error.invalid_argument_count.expected = symbol->template_data.parameter_names.size;
        error.invalid_argument_count.given = template_arguments.size;
        semantic_analyser_log_error(analyser, error);
        Identifier_Analysis_Result result;
        result.type = Analysis_Result_Type::ERROR_OCCURED;
        return result;
    }

    // Arguments must have size calculated (Prevents Templates circulary creating new templates, e.g. Struct Node with member x: Node<Node<T>>)
    for (int i = 0; i < template_arguments.size; i++) {
        if (template_arguments[i]->size == 0 && template_arguments[i]->alignment == 0) {
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = workload_dependency_make_type_size_unknown(template_arguments[i], instance_node);
            return result;
        }
    }

    // Search for already instanciated template
    Symbol_Template_Instance* found_instance = 0;
    for (int i = 0; i < symbol->template_data.instances.size; i++)
    {
        Symbol_Template_Instance* instance = symbol->template_data.instances[i];
        bool matches = true;
        for (int j = 0; j < instance->arguments.size; j++) {
            if (instance->arguments[j] != template_arguments[j]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            found_instance = instance;
            break;
        }
    }

    // Instanciate template if necessary
    if (found_instance == 0)
    {
        if (PRINT_DEPENDENCIES)
        {
            String tmp = string_create_empty(64);
            SCOPE_EXIT(string_destroy(&tmp));
            string_append_formated(&tmp, "No instance of template found, instanciating: %s<", symbol->id->characters);
            for (int i = 0; i < template_arguments.size; i++) {
                type_signature_append_to_string(&tmp, template_arguments[i]);
                if (i != template_arguments.size - 1) {
                    string_append_formated(&tmp, ", ");
                }
            }
            string_append_formated(&tmp, ">\n");
            logg("%s", tmp.characters);
        }

        // Create Instance;
        {
            Symbol_Template_Instance* instance = new Symbol_Template_Instance;
            instance->arguments = dynamic_array_create_copy(template_arguments.data, template_arguments.size);
            instance->instance_symbol = new Symbol();
            *instance->instance_symbol = *symbol; // Copy original symbol infos
            instance->instance_symbol->references = dynamic_array_create_empty<Symbol_Reference>(2);
            instance->instance_symbol->template_data.is_templated = false;
            instance->instance_symbol->symbol_table = 0;

            // Create instance template table, where template names are filled out
            instance->template_symbol_table = symbol_table_create(analyser, symbol->symbol_table, 0);
            for (int i = 0; i < symbol->template_data.parameter_names.size; i++)
            {
                symbol_table_define_symbol(
                    analyser, instance->template_symbol_table, symbol->template_data.parameter_names[i], instance_node,
                    Symbol_Type::TYPE, symbol_options_make_type(template_arguments[i]), symbol_template_data_make_no_template()
                );
            }
            dynamic_array_push_back(&symbol->template_data.instances, instance);
            found_instance = instance;
        }

        // Create workload
        switch (symbol->type)
        {
        case Symbol_Type::VARIABLE:
            panic("What");
        case Symbol_Type::FUNCTION:
        {
            assert(symbol->options.function->function_type == ModTree_Function_Type::FUNCTION, "WHAT");

            // Create new function
            ModTree_Function* function = modtree_function_make_empty(
                analyser, symbol->options.function->parent_module, found_instance->template_symbol_table, 0, found_instance->instance_symbol, 0
            );

            // Update instance symbol to differentiate it from original
            found_instance->instance_symbol->options.function = function;

            // Create header workload
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::FUNCTION_HEADER;
            workload.options.function_header.function_node = symbol->definition_node;
            workload.options.function_header.symbol_table = function->options.function.body->symbol_table;
            workload.options.function_header.function = function;
            workload.options.function_header.next_parameter_node = symbol->definition_node->child_start->child_start->child_start;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case Symbol_Type::TYPE:
        {
            assert(symbol->options.type->type == Signature_Type::STRUCT || symbol->options.type->type == Signature_Type::ENUM, "");

            if (symbol->options.type->type == Signature_Type::STRUCT)
            {
                // Update instance symbol to differentiate it from original
                AST_Node* struct_node = symbol->definition_node;
                found_instance->instance_symbol->options.type = type_system_make_struct_empty(
                    &analyser->compiler->type_system, struct_node, symbol->options.type->options.structure.is_union
                );

                // Create body workload
                Analysis_Workload workload;
                workload.type = Analysis_Workload_Type::STRUCT_BODY;
                workload.options.struct_body.symbol_table = found_instance->template_symbol_table;
                workload.options.struct_body.struct_signature = found_instance->instance_symbol->options.type;
                workload.options.struct_body.current_member_node = struct_node->child_start;
                workload.options.struct_body.offset = 0;
                workload.options.struct_body.alignment = 0;
                dynamic_array_push_back(&analyser->active_workloads, workload);
            }
            else
            {
                // Update instance symbol to differentiate it from original
                AST_Node* enum_node = symbol->definition_node;
                found_instance->instance_symbol->options.type = type_system_make_enum_empty(&analyser->compiler->type_system, enum_node->id);

                // Create body workload
                Analysis_Workload workload;
                workload.type = Analysis_Workload_Type::ENUM_BODY;
                workload.options.enum_body.symbol_table = found_instance->template_symbol_table;
                workload.options.enum_body.enum_type = found_instance->instance_symbol->options.type;
                workload.options.enum_body.current_member = enum_node->child_start;
                workload.options.enum_body.next_integer_value = 1;
                dynamic_array_push_back(&analyser->active_workloads, workload);

            }
            break;
        }
        default: panic("Hey"); break;
        }
    }

    // Create success
    Identifier_Analysis_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    result.options.symbol = found_instance->instance_symbol;
    return result;
}

Type_Analysis_Result semantic_analyser_analyse_type(Semantic_Analyser* analyser, Symbol_Table* table, AST_Node* type_node, Usage_Type usage);
Identifier_Analysis_Result semantic_analyser_analyse_identifier_node_with_template_arguments(
    Semantic_Analyser* analyser, Symbol_Table* table, AST_Node* node, bool only_current_scope,
    Usage_Type reference_type, Dynamic_Array<Type_Signature*> template_arguments)
{
    assert(node->type == AST_Node_Type::IDENTIFIER_NAME ||
        node->type == AST_Node_Type::IDENTIFIER_PATH ||
        node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED ||
        node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED, "Cannot lookup symbol of non identifer node");

    bool is_path = node->type == AST_Node_Type::IDENTIFIER_PATH || node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED;
    bool is_templated = node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED || node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED;

    Symbol* symbol = symbol_table_find_symbol(table, node->id, only_current_scope, symbol_reference_make(reference_type, node), analyser);
    if (symbol == 0)
    {
        Identifier_Analysis_Result result;
        result.type = Analysis_Result_Type::DEPENDENCY;
        result.options.dependency.type = Workload_Dependency_Type::IDENTIFER_NOT_FOUND;
        result.options.dependency.node = node;
        result.options.dependency.options.identifier_not_found.current_scope_only = only_current_scope;
        result.options.dependency.options.identifier_not_found.symbol_table = table;
        return result;
    }
    if (is_path && symbol->type != Symbol_Type::MODULE) {
        Semantic_Error error;
        error.type = Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH;
        error.error_node = node;
        error.symbol = symbol;
        semantic_analyser_log_error(analyser, error);
        Identifier_Analysis_Result result;
        result.type = Analysis_Result_Type::ERROR_OCCURED;
        return result;
    }

    // Check template parameters
    bool delete_parameter = false;
    SCOPE_EXIT(if (delete_parameter) dynamic_array_destroy(&template_arguments););
    if (is_templated)
    {
        if (!symbol->template_data.is_templated)
        {
            Semantic_Error error;
            error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE;
            error.symbol = symbol;
            error.error_node = node;
            semantic_analyser_log_error(analyser, error);
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::ERROR_OCCURED;
            return result;
        }

        // Create parameters array if not already created
        if (template_arguments.data == 0) {
            delete_parameter = true;
            template_arguments = dynamic_array_create_empty<Type_Signature*>(2);
        }

        // Analyse arguments, add to parameters
        AST_Node* unnamed_parameter_node = node->child_start;
        if (unnamed_parameter_node->child_count != symbol->template_data.parameter_names.size) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT;
            error.error_node = node;
            error.symbol = symbol;
            error.invalid_argument_count.expected = symbol->template_data.parameter_names.size;
            error.invalid_argument_count.given = template_arguments.size;
            semantic_analyser_log_error(analyser, error);
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::ERROR_OCCURED;
            return result;
        }
        AST_Node* parameter = unnamed_parameter_node->child_start;
        while (parameter != 0)
        {
            SCOPE_EXIT(parameter = parameter->neighbor;);
            Type_Analysis_Result type_result = semantic_analyser_analyse_type(analyser, table, parameter, Usage_Type::TEMPLATE_ARGUMENT);
            switch (type_result.type)
            {
            case Analysis_Result_Type::SUCCESS:
                dynamic_array_push_back(&template_arguments, type_result.options.result_type);
                break;
            case Analysis_Result_Type::DEPENDENCY: {
                Identifier_Analysis_Result result;
                result.type = Analysis_Result_Type::DEPENDENCY;
                result.options.dependency = type_result.options.dependency;
                return result;
            }
            case Analysis_Result_Type::ERROR_OCCURED: {
                Identifier_Analysis_Result result;
                result.type = Analysis_Result_Type::ERROR_OCCURED;
                return result;
            }
            default: panic("HEY");
            }
        }
    }

    if (symbol->template_data.is_templated && template_arguments.data == 0) {
        Semantic_Error error;
        error.symbol = symbol;
        error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED;
        error.error_node = node;
        semantic_analyser_log_error(analyser, error);
        Identifier_Analysis_Result result;
        result.type = Analysis_Result_Type::ERROR_OCCURED;
        return result;
    }

    if (is_path)
    {
        return semantic_analyser_analyse_identifier_node_with_template_arguments(
            analyser, symbol->options.module->symbol_table,
            is_templated ? node->child_start->neighbor : node->child_start,
            true, reference_type, template_arguments
        );
    }
    else
    {
        if (symbol->template_data.is_templated) {
            return semantic_analyser_instanciate_template(analyser, table, symbol, template_arguments, node);
        }
        else {
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::SUCCESS;
            result.options.symbol = symbol;
            return result;
        }
    }
}

Identifier_Analysis_Result semantic_analyser_analyse_identifier_node(
    Semantic_Analyser* analyser, Symbol_Table* table, AST_Node* node, bool only_current_scope, Usage_Type reference_type)
{
    Dynamic_Array<Type_Signature*> template_arguments;
    template_arguments.data = 0;
    template_arguments.size = 0;
    template_arguments.capacity = 0;
    Identifier_Analysis_Result result = semantic_analyser_analyse_identifier_node_with_template_arguments(
        analyser, table, node, only_current_scope, reference_type, template_arguments
    );
    if (result.type == Analysis_Result_Type::DEPENDENCY) {
        result.options.dependency.type = Workload_Dependency_Type::IDENTIFER_NOT_FOUND;
        result.options.dependency.node = node;
        result.options.dependency.options.identifier_not_found.current_scope_only = only_current_scope;
        result.options.dependency.options.identifier_not_found.symbol_table = table;
    }
    if (result.type == Analysis_Result_Type::SUCCESS)
    {
        Symbol* sym = result.options.symbol;
        if (sym->type == Symbol_Type::FUNCTION) {
            if (sym->options.function->signature == 0) {
                result.type = Analysis_Result_Type::DEPENDENCY;
                result.options.dependency.type = Workload_Dependency_Type::FUNCTION_HEADER_NOT_ANALYSED;
                result.options.dependency.node = sym->definition_node;
                result.options.dependency.options.function_header_not_analysed = sym->options.function;
            }
        }
    }
    return result;
}

Expression_Analysis_Result semantic_analyser_analyse_expression(Semantic_Analyser* analyser, Symbol_Table* symbol_table, AST_Node* expression_node);
Type_Analysis_Result semantic_analyser_analyse_type(Semantic_Analyser* analyser, Symbol_Table* table, AST_Node* type_node, Usage_Type usage)
{
    Type_System* type_system = &analyser->compiler->type_system;
    switch (type_node->type)
    {
    case AST_Node_Type::TYPE_IDENTIFIER:
    {
        Symbol* symbol = 0;
        Identifier_Analysis_Result identifier_result = semantic_analyser_analyse_identifier_node(analyser, table, type_node->child_start, false, usage);
        switch (identifier_result.type)
        {
        case Analysis_Result_Type::SUCCESS:
            symbol = identifier_result.options.symbol;
            break;
        case Analysis_Result_Type::DEPENDENCY: {
            Type_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = identifier_result.options.dependency;
            return result;
        }
        case Analysis_Result_Type::ERROR_OCCURED: {
            return type_analysis_result_make_error();
        }
        default: {
            panic("What");
            return type_analysis_result_make_error();
        }
        }

        if (symbol->type == Symbol_Type::TYPE) {
            if (symbol->options.type == analyser->compiler->type_system.error_type) {
                return type_analysis_result_make_error();
            }
        }
        else {
            Semantic_Error error;
            error.error_node = type_node;
            error.symbol = symbol;
            error.type = Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER;
            semantic_analyser_log_error(analyser, error);
            return type_analysis_result_make_error();
        }
        return type_analysis_result_make_success(symbol->options.type);
    }
    case AST_Node_Type::TYPE_POINTER_TO: {
        Type_Analysis_Result result = semantic_analyser_analyse_type(analyser, table, type_node->child_start, usage);
        if (result.type == Analysis_Result_Type::SUCCESS) {
            return type_analysis_result_make_success(type_system_make_pointer(&analyser->compiler->type_system, result.options.result_type));
        }
        else {
            return result;
        }
    }
    case AST_Node_Type::TYPE_ARRAY:
    {
        AST_Node* node_array_size = type_node->child_start;
        int array_size = 1;
        Expression_Analysis_Result result = semantic_analyser_analyse_expression(analyser, table, node_array_size);
        switch (result.type)
        {
        case Analysis_Result_Type::SUCCESS:
        {
            ModTree_Expression* expr = result.options.expression;
            SCOPE_EXIT(modtree_expression_destroy(expr););
            if (expr->result_type != type_system->i32_type) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE;
                error.expected_type = type_system->i32_type;
                error.given_type = expr->result_type;
                error.error_node = type_node;
                semantic_analyser_log_error(analyser, error);
            }
            if (expr->value.data == 0) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::OTHERS_ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN;
                error.expected_type = type_system->i32_type;
                error.given_type = expr->result_type;
                error.error_node = type_node;
                semantic_analyser_log_error(analyser, error);
            }
            else {
                array_size = *(int*)expr->value.data;
            }
            break;
        }
        case Analysis_Result_Type::DEPENDENCY: return type_analysis_result_make_dependency(result.options.dependency);
        case Analysis_Result_Type::ERROR_OCCURED: break; // Just go ahead with array size 1
        }

        if (array_size <= 0) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::OTHERS_ARRAY_SIZE_MUST_BE_GREATER_ZERO;
            error.error_node = type_node;
            semantic_analyser_log_error(analyser, error);
            array_size = 1;
        }

        Type_Analysis_Result element_result = semantic_analyser_analyse_type(analyser, table, node_array_size->neighbor, usage);
        if (element_result.type != Analysis_Result_Type::SUCCESS) {
            return element_result;
        }

        Type_Signature* element_type = element_result.options.result_type;
        if (element_type == analyser->compiler->type_system.void_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
            error.error_node = node_array_size;
            semantic_analyser_log_error(analyser, error);
            return type_analysis_result_make_error();
        }

        Type_Signature array_type;
        array_type.type = Signature_Type::ARRAY;
        array_type.options.array.element_type = element_type;
        array_type.options.array.element_count = array_size;
        array_type.alignment = 0;
        array_type.size = 0;
        Type_Signature* final_type = type_system_register_type(&analyser->compiler->type_system, array_type);

        if (element_type->size != 0 && element_type->alignment != 0)
        {
            // Just calculate the size now
            final_type->alignment = final_type->options.array.element_type->alignment;
            final_type->size = math_round_next_multiple(final_type->options.array.element_type->size, final_type->options.array.element_type->alignment) *
                final_type->options.array.element_count;
        }
        else {
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::ARRAY_SIZE;
            workload.options.array_size.array_signature = final_type;

            Waiting_Workload waiting;
            waiting.workload = workload;
            waiting.dependency = workload_dependency_make_type_size_unknown(final_type->options.array.element_type, type_node);
            dynamic_array_push_back(&analyser->waiting_workload, waiting);
        }

        return type_analysis_result_make_success(final_type);
    }
    case AST_Node_Type::TYPE_SLICE: {
        Type_Analysis_Result element_result = semantic_analyser_analyse_type(analyser, table, type_node->child_start, usage);
        if (element_result.type != Analysis_Result_Type::SUCCESS) {
            return element_result;
        }

        Type_Signature* element_type = element_result.options.result_type;
        if (element_type == analyser->compiler->type_system.void_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
            error.error_node = type_node->child_start;
            semantic_analyser_log_error(analyser, error);
            return type_analysis_result_make_error();
        }
        return type_analysis_result_make_success(type_system_make_slice(&analyser->compiler->type_system, element_type));
    }
    case AST_Node_Type::TYPE_FUNCTION_POINTER:
    {
        AST_Node* parameter_block = type_node->child_start;
        AST_Node* return_type_node = type_node->child_end;

        Type_Signature* return_type;
        if (type_node->child_count == 2)
        {
            Type_Analysis_Result return_type_result = semantic_analyser_analyse_type(analyser, table, return_type_node, usage);
            if (return_type_result.type != Analysis_Result_Type::SUCCESS) {
                return return_type_result;
            }
            return_type = return_type_result.options.result_type;
        }
        else {
            return_type = analyser->compiler->type_system.void_type;
        }

        Dynamic_Array<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(parameter_block->child_count);
        AST_Node* param_node = parameter_block->child_start;
        while (param_node != 0)
        {
            SCOPE_EXIT(param_node = param_node->neighbor);
            Type_Analysis_Result param_result = semantic_analyser_analyse_type(analyser, table, param_node, usage);
            if (param_result.type != Analysis_Result_Type::SUCCESS) {
                dynamic_array_destroy(&parameter_types);
                return param_result;
            }
            dynamic_array_push_back(&parameter_types, param_result.options.result_type);
        }

        Type_Signature* function_type = type_system_make_function(&analyser->compiler->type_system, parameter_types, return_type);
        return type_analysis_result_make_success(type_system_make_pointer(&analyser->compiler->type_system, function_type));
    }
    }

    panic("This should not happen, this means that the child was not a type!\n");
    return type_analysis_result_make_error();
}

Optional<ModTree_Cast_Type> semantic_analyser_check_if_cast_possible(
    Semantic_Analyser* analyser, Type_Signature* source_type, Type_Signature* destination_type, bool implicit_cast)
{
    bool cast_valid = false;
    ModTree_Cast_Type cast_type;
    // Pointer casting
    if (source_type->type == Signature_Type::POINTER || destination_type->type == Signature_Type::POINTER)
    {
        if (source_type->type == Signature_Type::POINTER && destination_type->type == Signature_Type::POINTER)
        {
            cast_type = ModTree_Cast_Type::POINTERS;
            cast_valid = true;
            if (implicit_cast) {
                cast_valid = source_type == analyser->compiler->type_system.void_ptr_type ||
                    destination_type == analyser->compiler->type_system.void_ptr_type;
            }
        }
        // U64 to Pointer
        if (source_type == analyser->compiler->type_system.u64_type && destination_type->type == Signature_Type::POINTER) {
            cast_valid = !implicit_cast;
            cast_type = ModTree_Cast_Type::U64_TO_POINTER;
        }
        // Pointer to U64
        if (source_type->type == Signature_Type::POINTER && destination_type == analyser->compiler->type_system.u64_type) {
            cast_valid = !implicit_cast;
            cast_type = ModTree_Cast_Type::POINTER_TO_U64;
        }
    }
    // Primitive Casting:
    else if (source_type->type == Signature_Type::PRIMITIVE && destination_type->type == Signature_Type::PRIMITIVE)
    {
        if (source_type->options.primitive.type == Primitive_Type::INTEGER && destination_type->options.primitive.type == Primitive_Type::INTEGER)
        {
            cast_type = ModTree_Cast_Type::INTEGERS;
            cast_valid = true;
            if (implicit_cast) {
                cast_valid = source_type->options.primitive.is_signed == destination_type->options.primitive.is_signed &&
                    source_type->size < destination_type->size;
            }
        }
        else if (destination_type->options.primitive.type == Primitive_Type::FLOAT && source_type->options.primitive.type == Primitive_Type::INTEGER)
        {
            cast_type = ModTree_Cast_Type::INT_TO_FLOAT;
            cast_valid = true;
        }
        else if (destination_type->options.primitive.type == Primitive_Type::INTEGER && source_type->options.primitive.type == Primitive_Type::FLOAT)
        {
            cast_type = ModTree_Cast_Type::FLOAT_TO_INT;
            cast_valid = !implicit_cast;
        }
        else if (destination_type->options.primitive.type == Primitive_Type::FLOAT && source_type->options.primitive.type == Primitive_Type::FLOAT)
        {
            cast_type = ModTree_Cast_Type::FLOATS;
            cast_valid = true;
            if (implicit_cast) {
                cast_valid = source_type->size < destination_type->size;
            }
        }
        else { // Booleans can never be cast
            cast_valid = false;
        }
    }
    // Array casting
    else if (source_type->type == Signature_Type::ARRAY && destination_type->type == Signature_Type::SLICE)
    {
        if (source_type->options.array.element_type == destination_type->options.array.element_type) {
            cast_type = ModTree_Cast_Type::ARRAY_SIZED_TO_UNSIZED;
            cast_valid = true;
        }
    }
    // Enum casting
    else if (source_type->type == Signature_Type::ENUM || destination_type->type == Signature_Type::ENUM)
    {
        bool enum_is_source = false;
        Type_Signature* other = 0;
        if (source_type->type == Signature_Type::ENUM) {
            enum_is_source = true;
            other = destination_type;
        }
        else {
            enum_is_source = false;
            other = source_type;
        }

        if (other->type == Signature_Type::PRIMITIVE && other->options.primitive.type == Primitive_Type::INTEGER) {
            cast_type = enum_is_source ? ModTree_Cast_Type::ENUM_TO_INT : ModTree_Cast_Type::INT_TO_ENUM;
            cast_valid = !implicit_cast;
        }
    }

    if (cast_valid) return optional_make_success(cast_type);
    return optional_make_failure<ModTree_Cast_Type>();
}

// If not possible, null is returned, otherwise an expression pointer is returned and the expression argument ownership is given to this pointer
ModTree_Expression* semantic_analyser_cast_implicit_if_possible(Semantic_Analyser* analyser, ModTree_Expression* expression, Type_Signature* destination_type)
{
    Type_Signature* source_type = expression->result_type;
    if (source_type == analyser->compiler->type_system.error_type || destination_type == analyser->compiler->type_system.error_type) {
        return expression;
    }
    if (source_type == destination_type) {
        return expression;
    }
    Optional<ModTree_Cast_Type> cast_type = semantic_analyser_check_if_cast_possible(analyser, expression->result_type, destination_type, true);
    if (cast_type.available) {
        ModTree_Expression cast_expr;
        cast_expr.expression_type = ModTree_Expression_Type::CAST;
        cast_expr.options.cast.cast_argument = expression;
        cast_expr.options.cast.type = cast_type.value;
        cast_expr.result_type = destination_type;
        cast_expr.value = expression_value_make_unknown();
        return new ModTree_Expression(cast_expr);
    }

    return 0;
}

ModTree_Expression* modtree_expression_create_constant(Semantic_Analyser* analyser, Type_Signature* signature, Array<byte> bytes)
{
    ModTree_Expression* expression = new ModTree_Expression;
    expression->expression_type = ModTree_Expression_Type::CONSTANT_READ;
    expression->options.literal_read.constant_index = constant_pool_add_constant(&analyser->compiler->constant_pool, signature, bytes);
    expression->result_type = signature;
    expression->value = expression_value_make_unknown(); // TODO: For compile time known stuff
    return expression;
}

ModTree_Expression* modtree_expression_create_constant_i32(Semantic_Analyser* analyser, i32 value) {
    return modtree_expression_create_constant(analyser, analyser->compiler->type_system.i32_type, array_create_static((byte*)&value, sizeof(i32)));
}

Partial_Compile_Result partial_compilation_queue_functions_for_bake_recursively(Semantic_Analyser* analyser, ModTree_Function* function, AST_Node* bake_node)
{
    if (hashtable_find_element(&analyser->compiler->ir_generator.function_mapping, function) != 0) {
        Partial_Compile_Result result;
        result.type = Analysis_Result_Type::SUCCESS;
        return result;
    }

    ir_generator_queue_function(&analyser->compiler->ir_generator, function);
    hashset_insert_element(&analyser->visited_functions, function);

    if (function->options.function.dependency_globals.size > 0)
    {
        Semantic_Error error;
        error.type = Semantic_Error_Type::BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS;
        error.function = function;
        error.error_node = bake_node;
        semantic_analyser_log_error(analyser, error);

        Partial_Compile_Result result;
        result.type = Analysis_Result_Type::ERROR_OCCURED;
        return result;
    }

    for (int i = 0; i < function->options.function.dependency_functions.size; i++)
    {
        ModTree_Function* dependent_fn = function->options.function.dependency_functions[i];
        if (dependent_fn->signature == 0)
        {
            Partial_Compile_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.dependency.type = Workload_Dependency_Type::FUNCTION_HEADER_NOT_ANALYSED;
            result.dependency.node = bake_node;
            result.dependency.options.function_header_not_analysed = dependent_fn;
            return result;
        }
        if (hashtable_find_element(&analyser->finished_code_blocks, dependent_fn->options.function.body) == 0)
        {
            Partial_Compile_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.dependency.type = Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED;
            result.dependency.node = bake_node;
            result.dependency.options.code_block = dependent_fn->options.function.body;
            return result;
        }
        Partial_Compile_Result result = partial_compilation_queue_functions_for_bake_recursively(analyser, dependent_fn, bake_node);
        if (result.type != Analysis_Result_Type::SUCCESS) return result;
    }

    Partial_Compile_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    return result;
}

Partial_Compile_Result partial_compilation_compile_function_for_bake(Semantic_Analyser* analyser, ModTree_Function* function, AST_Node* bake_node)
{
    if (analyser->errors.size != 0 || analyser->compiler->parser.errors.size != 0) {
        Partial_Compile_Result result;
        result.type = Analysis_Result_Type::ERROR_OCCURED;
        return result;
    }

    hashset_reset(&analyser->visited_functions);
    Partial_Compile_Result result = partial_compilation_queue_functions_for_bake_recursively(analyser, function, bake_node);
    if (result.type != Analysis_Result_Type::SUCCESS) return result;

    ir_generator_generate_queued_items(&analyser->compiler->ir_generator);
    return result;
}

Expression_Analysis_Result semantic_analyser_analyse_expression_without_value(Semantic_Analyser* analyser, Symbol_Table* symbol_table, AST_Node* expression_node)
{
    Type_System* type_system = &analyser->compiler->type_system;
    bool error_exit = false;
    bool is_binary_op = false;
    ModTree_Binary_Operation_Type binary_op_type;

    switch (expression_node->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
    {
        Expression_Analysis_Result identifier_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->child_start
        );
        if (identifier_expr_result.type != Analysis_Result_Type::SUCCESS) {
            return identifier_expr_result;
        }
        SCOPE_EXIT(if (error_exit && identifier_expr_result.options.expression != 0) modtree_expression_destroy(identifier_expr_result.options.expression););

        ModTree_Expression expression;
        expression.expression_type = ModTree_Expression_Type::FUNCTION_CALL;
        expression.value = expression_value_make_unknown();
        Type_Signature* signature = identifier_expr_result.options.expression->result_type;
        if (signature->type == Signature_Type::FUNCTION)
        {
            // See Expression_Variable_Read for how we differentiate between Function pointer calls and function calls
            expression.options.function_call.is_pointer_call = false;
            expression.options.function_call.function = identifier_expr_result.options.expression->options.function_pointer_read;
            modtree_expression_destroy(identifier_expr_result.options.expression);
            identifier_expr_result.options.expression = 0;
            if (expression.options.function_call.function == analyser->program->entry_function) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::OTHERS_NO_CALLING_TO_MAIN;
                error.error_node = expression_node;
                semantic_analyser_log_error(analyser, error);
                return expression_analysis_result_make_error();
            }
        }
        else if (signature->type == Signature_Type::POINTER && signature->options.pointer_child->type == Signature_Type::FUNCTION) {
            expression.options.function_call.is_pointer_call = true;
            expression.options.function_call.pointer_expression = identifier_expr_result.options.expression;
            signature = signature->options.pointer_child;
        }
        else {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL;
            error.given_type = signature;
            error.error_node = expression_node->child_start;
            semantic_analyser_log_error(analyser, error);
            return expression_analysis_result_make_error();
        }
        expression.result_type = signature->options.function.return_type;

        // Analyse arguments
        AST_Node* arguments_node = expression_node->child_start->neighbor;
        if (arguments_node->child_count != signature->options.function.parameter_types.size) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH;
            error.function_type = signature;
            error.invalid_argument_count.expected = signature->options.function.parameter_types.size;
            error.invalid_argument_count.given = arguments_node->child_count;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
        }

        expression.options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(arguments_node->child_count);
        SCOPE_EXIT(
            if (error_exit) {
                for (int i = 0; i < expression.options.function_call.arguments.size; i++) {
                    modtree_expression_destroy(expression.options.function_call.arguments[i]);
                }
                dynamic_array_destroy(&expression.options.function_call.arguments);
            }
        );
        AST_Node* argument_node = arguments_node->child_start;
        int i = 0;
        while (argument_node != 0 && i < signature->options.function.parameter_types.size)
        {
            SCOPE_EXIT(argument_node = argument_node->neighbor; i++;);
            Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, argument_node);
            switch (expr_result.type)
            {
            case Analysis_Result_Type::DEPENDENCY: {
                error_exit = true;
                return expr_result;
            }
            case Analysis_Result_Type::ERROR_OCCURED: {
                break;
            }
            case Analysis_Result_Type::SUCCESS:
            {
                ModTree_Expression* success = expr_result.options.expression;
                ModTree_Expression* cast = semantic_analyser_cast_implicit_if_possible(analyser, success, signature->options.function.parameter_types[i]);
                if (cast != 0) {
                    success = cast;
                }
                else {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::INVALID_TYPE_ARGUMENT;
                    error.function_type = signature;
                    error.given_type = success->result_type;
                    error.expected_type = signature->options.function.parameter_types[i];
                    error.invalid_argument_count.expected = signature->options.function.parameter_types.size;
                    error.invalid_argument_count.given = arguments_node->child_count;
                    error.error_node = argument_node;
                    semantic_analyser_log_error(analyser, error);
                }
                dynamic_array_push_back(&expression.options.function_call.arguments, success);
                break;
            }
            default: {
                panic("What");
            }
            }
        }
        return expression_analysis_result_make_success(expression);
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ:
    {
        Symbol* symbol = 0;
        Identifier_Analysis_Result variable_identifier =
            semantic_analyser_analyse_identifier_node(analyser, symbol_table, expression_node->child_start, false, Usage_Type::VARIABLE_READ);
        switch (variable_identifier.type) {
        case Analysis_Result_Type::SUCCESS:
            symbol = variable_identifier.options.symbol;
            break;
        case Analysis_Result_Type::DEPENDENCY:
            return expression_analysis_result_make_dependency(variable_identifier.options.dependency);
        case Analysis_Result_Type::ERROR_OCCURED:
            return expression_analysis_result_make_error();
        default: panic("HEY");
        }

        ModTree_Expression expression;
        expression.value = expression_value_make_unknown();
        if (symbol->type == Symbol_Type::VARIABLE)
        {
            expression.expression_type = ModTree_Expression_Type::VARIABLE_READ;
            expression.options.variable_read = symbol->options.variable;
            expression.result_type = symbol->options.variable->data_type;
            return expression_analysis_result_make_success(expression);
        }
        else if (symbol->type == Symbol_Type::FUNCTION)
        {
            // This works in tandum with Address-Of and Function Calls
            expression.expression_type = ModTree_Expression_Type::VARIABLE_READ;
            expression.options.function_pointer_read = symbol->options.function;
            expression.result_type = symbol->options.function->signature; // Returns actual function type, not function pointer
            return expression_analysis_result_make_success(expression);
        }
        else if (symbol->type == Symbol_Type::TYPE && symbol->options.type->type == Signature_Type::ENUM) 
        {
            // This works in tandum with Member access to allow enum accesses
            expression.expression_type = ModTree_Expression_Type::VARIABLE_READ;
            expression.result_type = symbol->options.type;
            expression.options.variable_read = 0;
            return expression_analysis_result_make_success(expression);
        }
        else {
            Semantic_Error error;
            error.type = Semantic_Error_Type::SYMBOL_EXPECTED_VARIABLE_OR_FUNCTION_ON_VARIABLE_READ;
            error.symbol = symbol;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
            error_exit = true;
            return expression_analysis_result_make_error();
        }
        panic("HEY");
        break;
    }
    case AST_Node_Type::EXPRESSION_CAST:
    {
        Type_Analysis_Result cast_destination_result = semantic_analyser_analyse_type(
            analyser, symbol_table, expression_node->child_start, Usage_Type::CAST_DESTINATION_TYPE
        );
        if (cast_destination_result.type != Analysis_Result_Type::SUCCESS)
        {
            error_exit = true;
            if (cast_destination_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                return expression_analysis_result_make_error();
            }
            if (cast_destination_result.type == Analysis_Result_Type::DEPENDENCY) {
                return expression_analysis_result_make_dependency(cast_destination_result.options.dependency);
            }
            panic("Should not happen");
        }
        Type_Signature* destination_type = cast_destination_result.options.result_type;

        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_end);
        if (expr_result.type != Analysis_Result_Type::SUCCESS) {
            error_exit = true;
            return expr_result;
        }

        ModTree_Expression result;
        result.expression_type = ModTree_Expression_Type::CAST;
        result.result_type = destination_type;
        result.value = expression_value_make_unknown();
        result.options.cast.cast_argument = expr_result.options.expression;
        result.options.cast.type = ModTree_Cast_Type::INTEGERS; // Placeholder

        Optional<ModTree_Cast_Type> cast_valid = semantic_analyser_check_if_cast_possible(
            analyser, expr_result.options.expression->result_type, destination_type, false
        );
        if (cast_valid.available)
        {
            result.options.cast.type = cast_valid.value;
            return expression_analysis_result_make_success(result);
        }
        else
        {
            Semantic_Error error;
            error.type = Semantic_Error_Type::EXPRESSION_INVALID_CAST;
            error.given_type = expr_result.options.expression->result_type;
            error.expected_type = destination_type;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
            return expression_analysis_result_make_success(result);
        }

        break;
    }
    case AST_Node_Type::EXPRESSION_LITERAL:
    {
        Token* token = expression_node->literal_token;
        Type_System* type_system = &analyser->compiler->type_system;
        void* value_ptr;
        Type_Signature* literal_type;
        void* null_pointer = 0;
        Upp_String string_buffer;

        if (token->type == Token_Type::BOOLEAN_LITERAL) {
            literal_type = type_system->bool_type;
            value_ptr = &token->attribute.bool_value;
        }
        else if (token->type == Token_Type::INTEGER_LITERAL) {
            literal_type = type_system->i32_type;
            value_ptr = &token->attribute.integer_value;
        }
        else if (token->type == Token_Type::FLOAT_LITERAL) {
            literal_type = type_system->f32_type;
            value_ptr = &token->attribute.integer_value;
        }
        else if (token->type == Token_Type::NULLPTR) {
            literal_type = type_system->void_ptr_type;
            value_ptr = &null_pointer;
        }
        else if (token->type == Token_Type::STRING_LITERAL)
        {
            String* string = token->attribute.id;
            string_buffer.character_buffer_data = string->characters;
            string_buffer.character_buffer_size = string->capacity;
            string_buffer.size = string->size;

            literal_type = type_system->string_type;
            value_ptr = &string_buffer;
        }
        else {
            panic("Should not happen!");
        }

        Expression_Analysis_Result result;
        result.type = Analysis_Result_Type::SUCCESS;
        result.options.expression = modtree_expression_create_constant(
            analyser, literal_type, array_create_static<byte>((byte*)value_ptr, literal_type->size)
        );
        return result;
    }
    case AST_Node_Type::EXPRESSION_NEW:
    case AST_Node_Type::EXPRESSION_NEW_ARRAY:
    {
        bool is_array = expression_node->type == AST_Node_Type::EXPRESSION_NEW_ARRAY;
        Type_Analysis_Result new_type_result = semantic_analyser_analyse_type(
            analyser, symbol_table, is_array ? expression_node->child_start->neighbor : expression_node->child_start, Usage_Type::ALLOCATION_TYPE
        );
        if (new_type_result.type != Analysis_Result_Type::SUCCESS)
        {
            if (new_type_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                return expression_analysis_result_make_error();
            }
            if (new_type_result.type == Analysis_Result_Type::DEPENDENCY) {
                return expression_analysis_result_make_dependency(new_type_result.options.dependency);
            }
            panic("Should not happen");
        }

        Type_Signature* new_type = new_type_result.options.result_type;
        if (new_type == analyser->compiler->type_system.void_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
            error.error_node = is_array ? expression_node->child_start->neighbor : expression_node->child_start;
            semantic_analyser_log_error(analyser, error);
            return expression_analysis_result_make_error();
        }
        if (new_type->size == 0 && new_type->alignment == 0) {
            return expression_analysis_result_make_dependency(workload_dependency_make_type_size_unknown(new_type, expression_node));
        }

        ModTree_Expression result;
        result.expression_type = ModTree_Expression_Type::NEW_ALLOCATION;
        result.value = expression_value_make_unknown();
        result.options.new_allocation.allocation_size = new_type->size;
        result.result_type = type_system_make_pointer(&analyser->compiler->type_system, new_type);
        result.options.new_allocation.element_count = optional_make_failure<ModTree_Expression*>();

        if (is_array)
        {
            result.result_type = type_system_make_slice(&analyser->compiler->type_system, new_type);
            Expression_Analysis_Result element_count_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start);
            switch (element_count_result.type)
            {
            case Analysis_Result_Type::DEPENDENCY: {
                return element_count_result;
            }
            case Analysis_Result_Type::SUCCESS: {
                if (element_count_result.options.expression->result_type != analyser->compiler->type_system.i32_type)
                {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE;
                    error.given_type = element_count_result.options.expression->result_type;
                    error.expected_type = analyser->compiler->type_system.i32_type; // TODO: Try implicit casting
                    error.error_node = expression_node->child_start;
                    semantic_analyser_log_error(analyser, error);
                }
                result.options.new_allocation.element_count = optional_make_success(element_count_result.options.expression);
                break;
            }
            case Analysis_Result_Type::ERROR_OCCURED:
                break;
            }
        }

        return expression_analysis_result_make_success(result);
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
    {
        Expression_Analysis_Result array_access_expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start);
        if (array_access_expr_result.type != Analysis_Result_Type::SUCCESS) {
            error_exit = true;
            return array_access_expr_result;
        }
        SCOPE_EXIT(if (error_exit) modtree_expression_destroy(array_access_expr_result.options.expression););

        Type_Signature* array_type = array_access_expr_result.options.expression->result_type;
        if (array_type->type != Signature_Type::ARRAY && array_type->type != Signature_Type::SLICE) {
            Semantic_Error error;
            error.given_type = array_type;
            error.type = Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS;
            error.error_node = expression_node->child_start;
            semantic_analyser_log_error(analyser, error);
            error_exit = true;
            return expression_analysis_result_make_error();
        }

        ModTree_Expression result;
        result.expression_type = ModTree_Expression_Type::ARRAY_ACCESS;
        result.value = expression_value_make_unknown();
        result.result_type = array_type->options.array.element_type;
        result.options.array_access.array_expression = array_access_expr_result.options.expression;

        Expression_Analysis_Result index_expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start->neighbor);
        switch (index_expr_result.type)
        {
        case Analysis_Result_Type::SUCCESS:
            if (index_expr_result.options.expression->result_type != analyser->compiler->type_system.i32_type) { // Todo: Try cast
                Semantic_Error error;
                error.given_type = index_expr_result.options.expression->result_type;
                error.expected_type = analyser->compiler->type_system.i32_type;
                error.type = Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX;
                error.error_node = expression_node->child_start->neighbor;
                semantic_analyser_log_error(analyser, error);
            }
            result.options.array_access.index_expression = index_expr_result.options.expression;
            break;
        case Analysis_Result_Type::ERROR_OCCURED:
            result.options.array_access.index_expression = modtree_expression_create_constant_i32(analyser, 1);
            break;
        case Analysis_Result_Type::DEPENDENCY:
            error_exit = true;
            return index_expr_result;
        }

        return expression_analysis_result_make_success(result);
    }
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
    {
        Expression_Analysis_Result access_expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start);
        if (access_expr_result.type != Analysis_Result_Type::SUCCESS) {
            error_exit = true;
            return access_expr_result;
        }
        ModTree_Expression* access_expr = access_expr_result.options.expression;
        SCOPE_EXIT(if (error_exit) modtree_expression_destroy(access_expr););

        // Check if we are accessing enum value
        if (access_expr->result_type->type == Signature_Type::ENUM &&
            access_expr->expression_type == ModTree_Expression_Type::VARIABLE_READ &&
            access_expr->options.variable_read == 0)  
        {
            Type_Signature* enum_type = access_expr->result_type;
            if (enum_type->size == 0 && enum_type->alignment == 0) {
                return expression_analysis_result_make_dependency(workload_dependency_make_type_size_unknown(enum_type, expression_node));
            }
            modtree_expression_destroy(access_expr);

            Enum_Member* found = 0;
            for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
                Enum_Member* member = &enum_type->options.enum_type.members[i];
                if (member->id == expression_node->id) {
                    found = member;
                    break;
                }
            }

            int value = 0;
            if (found == 0) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER;
                error.error_node = expression_node->child_start;
                semantic_analyser_log_error(analyser, error);
            }
            else {
                value = found->value;
            }

            ModTree_Expression* result = modtree_expression_create_constant_i32(analyser, value);
            result->result_type = enum_type;
            Expression_Analysis_Result ret_val;
            ret_val.type = Analysis_Result_Type::SUCCESS;
            ret_val.options.expression = result;
            return ret_val;
        }

        // One layer of dereferencing on member access 
        Type_Signature* struct_signature = access_expr_result.options.expression->result_type;
        if (struct_signature->type == Signature_Type::POINTER)
        {
            ModTree_Expression* dereference_expr = new ModTree_Expression;
            dereference_expr->expression_type = ModTree_Expression_Type::UNARY_OPERATION;
            dereference_expr->value = expression_value_make_unknown();
            dereference_expr->result_type = struct_signature->options.pointer_child;
            dereference_expr->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::DEREFERENCE;
            dereference_expr->options.unary_operation.operand = access_expr;
            struct_signature = struct_signature->options.pointer_child;
            access_expr = dereference_expr;
        }

        if (struct_signature->type == Signature_Type::STRUCT)
        {
            Struct_Member* found = 0;
            for (int i = 0; i < struct_signature->options.structure.members.size; i++) {
                Struct_Member* member = &struct_signature->options.structure.members[i];
                if (member->id == expression_node->id) {
                    found = member;
                }
            }
            if (found == 0)
            {
                if (struct_signature->size == 0 && struct_signature->alignment == 0) {
                    error_exit = true;
                    return expression_analysis_result_make_dependency(workload_dependency_make_type_size_unknown(struct_signature, expression_node));
                }
                else {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND;
                    error.given_type = struct_signature;
                    error.error_node = expression_node;
                    semantic_analyser_log_error(analyser, error);
                    error_exit = true;
                    return expression_analysis_result_make_error();
                }
            }

            ModTree_Expression result;
            result.expression_type = ModTree_Expression_Type::MEMBER_ACCESS;
            result.value = expression_value_make_unknown();
            result.result_type = found->type;
            result.options.member_access.structure_expression = access_expr;
            result.options.member_access.member = *found;
            return expression_analysis_result_make_success(result);
        }
        else if (struct_signature->type == Signature_Type::ARRAY || struct_signature->type == Signature_Type::SLICE)
        {
            if (expression_node->id != analyser->id_size && expression_node->id != analyser->id_data) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND;
                error.given_type = struct_signature;
                error.error_node = expression_node;
                semantic_analyser_log_error(analyser, error);
                error_exit = true;
                return expression_analysis_result_make_error();
            }

            if (struct_signature->type == Signature_Type::ARRAY)
            {
                if (expression_node->id == analyser->id_size)
                {
                    Expression_Analysis_Result result;
                    result.type = Analysis_Result_Type::SUCCESS;
                    result.options.expression = modtree_expression_create_constant_i32(analyser, struct_signature->options.array.element_count);
                    return result;
                }
                else // Token index data
                {
                    ModTree_Expression result;
                    result.result_type = type_system_make_pointer(&analyser->compiler->type_system, struct_signature->options.array.element_type);
                    result.value = expression_value_make_unknown();
                    result.expression_type = ModTree_Expression_Type::UNARY_OPERATION;
                    result.options.unary_operation.operand = access_expr;
                    result.options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
                    return expression_analysis_result_make_success(result);
                }
            }
            else // Array_Unsized
            {
                ModTree_Expression result;
                result.expression_type = ModTree_Expression_Type::MEMBER_ACCESS;
                result.value = expression_value_make_unknown();
                result.options.member_access.structure_expression = access_expr;
                if (expression_node->id == analyser->id_size) {
                    result.options.member_access.member = struct_signature->options.slice.size_member;
                }
                else
                {
                    result.options.member_access.member = struct_signature->options.slice.data_member;
                }
                result.result_type = result.options.member_access.member.type;
                return expression_analysis_result_make_success(result);
            }
        }
        else
        {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS;
            error.given_type = struct_signature;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
            error_exit = true;
            return expression_analysis_result_make_error();
        }
        panic("Should not happen");
        return expression_analysis_result_make_error();
    }
    case AST_Node_Type::EXPRESSION_BAKE:
    {
        assert(analyser->current_workload->type == Analysis_Workload_Type::CODE, "");
        Analysis_Workload_Code* work = &analyser->current_workload->options.code_block;
        // Check if we have already created the function
        Bake_Location location;
        location.block = work->active_block->block;
        location.node = expression_node;
        {
            ModTree_Function** function = hashtable_find_element(&analyser->bake_locations, location);

            Expression_Value result;
            if (function != 0)
            {
                Partial_Compile_Result comp_result = partial_compilation_compile_function_for_bake(analyser, *function, expression_node);
                switch (comp_result.type)
                {
                case Analysis_Result_Type::SUCCESS:
                {
                    IR_Function* ir_func = *hashtable_find_element(&analyser->compiler->ir_generator.function_mapping, *function);
                    int func_start_instr_index = *hashtable_find_element(&analyser->compiler->bytecode_generator.function_locations, ir_func);
                    analyser->compiler->bytecode_interpreter.instruction_limit_enabled = true;
                    analyser->compiler->bytecode_interpreter.instruction_limit = 5000;
                    bytecode_interpreter_run_function(&analyser->compiler->bytecode_interpreter, func_start_instr_index);
                    if (analyser->compiler->bytecode_interpreter.exit_code != Exit_Code::SUCCESS)
                    {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED;
                        error.exit_code = analyser->compiler->bytecode_interpreter.exit_code;
                        error.error_node = expression_node;
                        semantic_analyser_log_error(analyser, error);

                        Expression_Analysis_Result result;
                        result.type = Analysis_Result_Type::ERROR_OCCURED;
                        return result;
                    }

                    void* value_ptr = analyser->compiler->bytecode_interpreter.return_register;
                    ModTree_Expression* expression = new ModTree_Expression;
                    expression->result_type = (*function)->signature->options.function.return_type;
                    expression->expression_type = ModTree_Expression_Type::CONSTANT_READ;
                    expression->options.literal_read.constant_index = constant_pool_add_constant(
                        &analyser->compiler->constant_pool, expression->result_type, array_create_static<byte>((byte*)value_ptr, expression->result_type->size)
                    );
                    expression->value = expression_value_make_type(analyser, expression->result_type);
                    memory_copy(expression->value.data, value_ptr, (u64)expression->result_type->size);

                    Expression_Analysis_Result result;
                    result.type = Analysis_Result_Type::SUCCESS;
                    result.options.expression = expression;
                    return result;
                }
                case Analysis_Result_Type::ERROR_OCCURED:
                    return expression_analysis_result_make_error();
                case Analysis_Result_Type::DEPENDENCY:
                    return expression_analysis_result_make_dependency(comp_result.dependency);
                default: panic("");
                }
                panic("Todo something here");

                assert(result.signature == type_system->i32_type, "");
                assert(result.data != 0, "");
                Expression_Analysis_Result ret_val;
                ret_val.type = Analysis_Result_Type::SUCCESS;
                ret_val.options.expression = modtree_expression_create_constant_i32(analyser, *(i32*)result.data);
                return ret_val;
            }
        }

        Type_Analysis_Result type_result = semantic_analyser_analyse_type(analyser, symbol_table, expression_node->child_start, Usage_Type::BAKE_TYPE);
        switch (type_result.type)
        {
        case Analysis_Result_Type::SUCCESS:
            if (type_result.options.result_type->type != Signature_Type::PRIMITIVE) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_BAKE_MUST_BE_PRIMITIVE;
                error.error_node = expression_node;
                error.given_type = type_result.options.result_type;
                semantic_analyser_log_error(analyser, error);
                return expression_analysis_result_make_error();
            }
            break;
        case Analysis_Result_Type::ERROR_OCCURED:
            return expression_analysis_result_make_error();
        case Analysis_Result_Type::DEPENDENCY:
            return expression_analysis_result_make_dependency(type_result.options.dependency);
        default: panic("");
        }

        // Create function and analyse it
        ModTree_Function* function = modtree_function_make_empty(
            analyser, work->function->parent_module, work->function->parent_module->symbol_table,
            type_system_make_function(type_system, dynamic_array_create_empty<Type_Signature*>(1), type_result.options.result_type),
            0, expression_node
        );
        dynamic_array_push_back(&analyser->active_workloads, analysis_workload_make_code_block(function->options.function.body, expression_node->child_end));
        hashtable_insert_element(&analyser->bake_locations, location, function);
        return expression_analysis_result_make_dependency(workload_dependency_make_code_block_finished(function->options.function.body, expression_node));
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
    {
        Expression_Analysis_Result operand_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start);
        if (operand_result.type != Analysis_Result_Type::SUCCESS)
        {
            if (operand_result.type == Analysis_Result_Type::ERROR_OCCURED)
            {
                Expression_Analysis_Result result;
                result.type = Analysis_Result_Type::SUCCESS;
                bool boolean = false;
                result.options.expression = modtree_expression_create_constant(
                    analyser, analyser->compiler->type_system.bool_type,
                    array_create_static((byte*)&boolean, 1)
                );
                return result;
            }
            if (operand_result.type == Analysis_Result_Type::DEPENDENCY) {
                return operand_result;
            }
            panic("Should not happen");
        }

        ModTree_Expression* operand = operand_result.options.expression;
        if (operand->result_type != type_system->bool_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR;
            error.given_type = operand->result_type;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
        }

        ModTree_Expression result;
        result.expression_type = ModTree_Expression_Type::UNARY_OPERATION;
        result.result_type = type_system->bool_type;
        result.value = expression_value_make_unknown();
        result.options.unary_operation.operation_type = ModTree_Unary_Operation_Type::LOGICAL_NOT;
        result.options.unary_operation.operand = operand;
        return expression_analysis_result_make_success(result);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
    {
        Expression_Analysis_Result operand_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start);
        if (operand_result.type != Analysis_Result_Type::SUCCESS) {
            return operand_result;
        }

        ModTree_Expression* operand = operand_result.options.expression;
        Type_Signature* result_type = operand->result_type;
        bool valid = false;
        if (operand->result_type->type == Signature_Type::PRIMITIVE) {
            valid = operand->result_type->options.primitive.is_signed && operand->result_type->options.primitive.type != Primitive_Type::BOOLEAN;
        }
        if (!valid) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR;
            error.given_type = operand->result_type;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
            result_type = type_system->i8_type;
        }

        ModTree_Expression result;
        result.expression_type = ModTree_Expression_Type::UNARY_OPERATION;
        result.result_type = result_type;
        result.value = expression_value_make_unknown();
        result.options.unary_operation.operation_type = ModTree_Unary_Operation_Type::NEGATE;
        result.options.unary_operation.operand = operand;
        return expression_analysis_result_make_success(result);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
    {
        Expression_Analysis_Result operand_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start);
        if (operand_result.type != Analysis_Result_Type::SUCCESS) {
            return operand_result;
        }

        ModTree_Expression* operand = operand_result.options.expression;
        if (operand->result_type == type_system->void_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
            error.given_type = operand->result_type;
            error.expected_type = type_system->bool_type;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
            modtree_expression_destroy(operand);
            return expression_analysis_result_make_error();
        }
        // Function pointer read, check Variable read for details
        if (operand->result_type->type == Signature_Type::FUNCTION)
        {
            if (operand->options.function_pointer_read->function_type == ModTree_Function_Type::HARDCODED_FUNCTION)
            {
                Semantic_Error error;
                error.type = Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION;
                error.error_node = expression_node;
                semantic_analyser_log_error(analyser, error);
            }
            operand->expression_type = ModTree_Expression_Type::FUNCTION_POINTER_READ;
            operand->result_type = type_system_make_pointer(&analyser->compiler->type_system, operand->result_type);
            Expression_Analysis_Result result;
            result.options.expression = operand;
            result.type = Analysis_Result_Type::SUCCESS;
            return result;
        }
        if (modtree_expression_result_is_temporary(operand)) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
        }

        ModTree_Expression result;
        result.value = expression_value_make_unknown();
        result.expression_type = ModTree_Expression_Type::UNARY_OPERATION;
        result.options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
        result.options.unary_operation.operand = operand;
        result.result_type = type_system_make_pointer(type_system, operand->result_type);
        return expression_analysis_result_make_success(result);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
    {
        Expression_Analysis_Result operand_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start);
        if (operand_result.type != Analysis_Result_Type::SUCCESS) {
            return operand_result;
        }

        ModTree_Expression* operand = operand_result.options.expression;
        if (operand->result_type->type != Signature_Type::POINTER || operand->result_type == type_system->void_ptr_type)
        {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR;
            error.given_type = operand->result_type;
            error.error_node = expression_node;
            semantic_analyser_log_error(analyser, error);
            modtree_expression_destroy(operand);
            return expression_analysis_result_make_error();
        }

        ModTree_Expression result;
        result.value = expression_value_make_unknown();
        result.expression_type = ModTree_Expression_Type::UNARY_OPERATION;
        result.options.unary_operation.operation_type = ModTree_Unary_Operation_Type::DEREFERENCE;
        result.options.unary_operation.operand = operand;
        result.result_type = operand->result_type->options.pointer_child;
        return expression_analysis_result_make_success(result);
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::ADDITION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::SUBTRACTION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::DIVISION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::MULTIPLICATION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::GREATER;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::GREATER_OR_EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::LESS;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::LESS_OR_EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::MODULO;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::AND;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::OR;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
        is_binary_op = true;
        binary_op_type = ModTree_Binary_Operation_Type::NOT_EQUAL;
        break;
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    if (!is_binary_op) {
        panic("Should not happen!");
    }

    // Evaluate operands
    ModTree_Expression* left_expr;
    ModTree_Expression* right_expr;
    {
        Expression_Analysis_Result left_expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start);
        if (left_expr_result.type != Analysis_Result_Type::SUCCESS) {
            error_exit = true;
            return left_expr_result;
        }
        Expression_Analysis_Result right_expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node->child_start->neighbor);
        if (right_expr_result.type != Analysis_Result_Type::SUCCESS) {
            error_exit = true;
            modtree_expression_destroy(left_expr_result.options.expression);
            return right_expr_result;
        }
        left_expr = left_expr_result.options.expression;
        right_expr = right_expr_result.options.expression;
    }
    SCOPE_EXIT(
        if (error_exit) {
            modtree_expression_destroy(left_expr);
            modtree_expression_destroy(right_expr);
        }
    );

    // Determine what operands are valid
    bool int_valid = false;
    bool float_valid = false;
    bool bool_valid = false;
    bool ptr_valid = false;
    bool result_type_is_bool = false;
    bool enum_valid = false;
    switch (binary_op_type)
    {
    case ModTree_Binary_Operation_Type::ADDITION:
    case ModTree_Binary_Operation_Type::SUBTRACTION:
    case ModTree_Binary_Operation_Type::MULTIPLICATION:
    case ModTree_Binary_Operation_Type::DIVISION:
        float_valid = true;
        int_valid = true;
        break;
    case ModTree_Binary_Operation_Type::MODULO:
        int_valid = true;
        break;
    case ModTree_Binary_Operation_Type::GREATER:
    case ModTree_Binary_Operation_Type::GREATER_OR_EQUAL:
    case ModTree_Binary_Operation_Type::LESS:
    case ModTree_Binary_Operation_Type::LESS_OR_EQUAL:
        float_valid = true;
        int_valid = true;
        result_type_is_bool = true;
        enum_valid = true;
        break;
    case ModTree_Binary_Operation_Type::EQUAL:
    case ModTree_Binary_Operation_Type::NOT_EQUAL:
        float_valid = true;
        int_valid = true;
        bool_valid = true;
        ptr_valid = true;
        result_type_is_bool = true;
        enum_valid = true;
        break;
    case ModTree_Binary_Operation_Type::AND:
    case ModTree_Binary_Operation_Type::OR:
        bool_valid = true;
        result_type_is_bool = true;
        break;
    }

    // Try implicit casting if types dont match
    Type_Signature* left_type = left_expr->result_type;
    Type_Signature* right_type = right_expr->result_type;
    Type_Signature* operand_type = left_type;
    bool types_are_valid = true;
    Semantic_Error_Type error_type = Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH;
    if (left_type != right_type)
    {
        if (semantic_analyser_check_if_cast_possible(analyser, left_type, right_type, true).available) {
            left_expr = semantic_analyser_cast_implicit_if_possible(analyser, left_expr, right_type);
            operand_type = right_type;
        }
        else if (semantic_analyser_check_if_cast_possible(analyser, right_type, left_type, true).available) {
            right_expr = semantic_analyser_cast_implicit_if_possible(analyser, right_expr, left_type);
            operand_type = left_type;
        }
        else {
            types_are_valid = false;
        }
    }

    // Check if given type is valid
    error_type = Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR;
    if (operand_type->type == Signature_Type::POINTER) {
        types_are_valid = ptr_valid;
    }
    else if (operand_type->type == Signature_Type::PRIMITIVE)
    {
        if (operand_type->options.primitive.type == Primitive_Type::INTEGER && !int_valid) {
            types_are_valid = false;
        }
        if (operand_type->options.primitive.type == Primitive_Type::FLOAT && !float_valid) {
            types_are_valid = false;
        }
        if (operand_type->options.primitive.type == Primitive_Type::BOOLEAN && !bool_valid) {
            types_are_valid = false;
        }
    }
    else if (operand_type->type == Signature_Type::ENUM) {
        types_are_valid = enum_valid;
    }
    else {
        types_are_valid = false;
    }

    ModTree_Expression result;
    result.value = expression_value_make_unknown();
    result.result_type = result_type_is_bool ? type_system->bool_type : operand_type;
    result.expression_type = ModTree_Expression_Type::BINARY_OPERATION;
    result.options.binary_operation.operation_type = binary_op_type;
    result.options.binary_operation.left_operand = left_expr;
    result.options.binary_operation.right_operand = right_expr;

    if (!types_are_valid)
    {
        Semantic_Error error;
        error.type = error_type;
        error.binary_op_left_type = left_type;
        error.binary_op_right_type = right_type;
        error.error_node = expression_node;
        semantic_analyser_log_error(analyser, error);
        if (!result_type_is_bool) { // Otherwise we know that the result is a bool
            error_exit = true;
            return expression_analysis_result_make_error();
        }
    }
    return expression_analysis_result_make_success(result);
}

Expression_Analysis_Result semantic_analyser_analyse_expression(Semantic_Analyser* analyser, Symbol_Table* symbol_table, AST_Node* expression_node)
{
    int error_before_count = analyser->errors.size;
    Expression_Analysis_Result result = semantic_analyser_analyse_expression_without_value(analyser, symbol_table, expression_node);
    if (result.type != Analysis_Result_Type::SUCCESS) return result;
    if (error_before_count != analyser->errors.size) return result;

    ModTree_Expression* expr = result.options.expression;
    // Check if constant values was already generated (Currently only the case for Bake-Expressions)
    if (expr->value.signature != 0) {
        return result;
    }
    else {
        assert(expr->value.data == 0, "");
    }
    switch (expr->expression_type)
    {
    case ModTree_Expression_Type::BINARY_OPERATION:
    {
        Expression_Value left_val = expr->options.binary_operation.left_operand->value;
        Expression_Value right_val = expr->options.binary_operation.right_operand->value;
        if (left_val.data == 0 || right_val.data == 0 || left_val.signature != right_val.signature) {
            break;
        }

        Instruction_Type instr_type;
        switch (expr->options.binary_operation.operation_type)
        {
        case ModTree_Binary_Operation_Type::ADDITION: instr_type = Instruction_Type::BINARY_OP_ADDITION; break;
        case ModTree_Binary_Operation_Type::SUBTRACTION: instr_type = Instruction_Type::BINARY_OP_SUBTRACTION; break;
        case ModTree_Binary_Operation_Type::DIVISION: instr_type = Instruction_Type::BINARY_OP_DIVISION; break;
        case ModTree_Binary_Operation_Type::MULTIPLICATION: instr_type = Instruction_Type::BINARY_OP_MULTIPLICATION; break;
        case ModTree_Binary_Operation_Type::MODULO: instr_type = Instruction_Type::BINARY_OP_MODULO; break;
        case ModTree_Binary_Operation_Type::AND: instr_type = Instruction_Type::BINARY_OP_AND; break;
        case ModTree_Binary_Operation_Type::OR: instr_type = Instruction_Type::BINARY_OP_OR; break;
        case ModTree_Binary_Operation_Type::EQUAL: instr_type = Instruction_Type::BINARY_OP_EQUAL; break;
        case ModTree_Binary_Operation_Type::NOT_EQUAL: instr_type = Instruction_Type::BINARY_OP_NOT_EQUAL; break;
        case ModTree_Binary_Operation_Type::LESS: instr_type = Instruction_Type::BINARY_OP_LESS_THAN; break;
        case ModTree_Binary_Operation_Type::LESS_OR_EQUAL: instr_type = Instruction_Type::BINARY_OP_LESS_EQUAL; break;
        case ModTree_Binary_Operation_Type::GREATER: instr_type = Instruction_Type::BINARY_OP_GREATER_THAN; break;
        case ModTree_Binary_Operation_Type::GREATER_OR_EQUAL: instr_type = Instruction_Type::BINARY_OP_GREATER_EQUAL; break;
        default: panic("");
        }
        expr->value = expression_value_make_type(analyser, expr->result_type);
        if (expr->value.data == 0) break;
        if (!bytecode_execute_binary_instr(instr_type, type_signature_to_bytecode_type(left_val.signature), expr->value.data, left_val.data, right_val.data)) {
            expr->value = expression_value_make_unknown();
        }
        break;
    }
    case ModTree_Expression_Type::UNARY_OPERATION:
    {
        Expression_Value value = expr->options.unary_operation.operand->value;
        if (value.data == 0) break;
        expr->value = expression_value_make_type(analyser, expr->result_type);
        if (expr->value.data == 0) break;
        switch (expr->options.unary_operation.operation_type)
        {
        case ModTree_Unary_Operation_Type::LOGICAL_NOT:
        case ModTree_Unary_Operation_Type::NEGATE: {
            Instruction_Type instr_type = expr->options.unary_operation.operation_type == ModTree_Unary_Operation_Type::NEGATE ?
                Instruction_Type::UNARY_OP_NEGATE : Instruction_Type::UNARY_OP_NOT;
            bytecode_execute_unary_instr(instr_type, type_signature_to_bytecode_type(value.signature), expr->value.data, value.data);
            break;
        }
        case ModTree_Unary_Operation_Type::ADDRESS_OF:
            if (expr->options.unary_operation.operand->expression_type == ModTree_Expression_Type::VARIABLE_READ) {
                constant_analysis_take_address_of(expr->options.unary_operation.operand->options.variable_read);
            }
            break;
        case ModTree_Unary_Operation_Type::DEREFERENCE:
            break;
        }
    }
    break;
    case ModTree_Expression_Type::CONSTANT_READ: {
        Constant_Pool* pool = &analyser->compiler->constant_pool;
        expr->value.signature = expr->result_type;
        expr->value.data = stack_allocator_allocate_size(&analyser->allocator_values, expr->result_type->size, expr->result_type->alignment);
        memory_copy(expr->value.data, &pool->buffer[pool->constants[expr->options.literal_read.constant_index].offset], expr->result_type->size);
        break;
    }
    case ModTree_Expression_Type::FUNCTION_POINTER_READ: {
        expr->value.signature = expr->result_type;
        expr->value.data = expr->options.function_pointer_read;
        break;
    }
    case ModTree_Expression_Type::VARIABLE_READ: {
        if (!(expr->result_type->type == Signature_Type::FUNCTION) && (analyser->current_workload->type == Analysis_Workload_Type::CODE) &&
            !(expr->result_type->type == Signature_Type::ENUM && expr->expression_type == ModTree_Expression_Type::VARIABLE_READ && expr->options.variable_read == 0)) {
            expr->value = constant_analysis_get_variable_value(
                expr->options.variable_read, analyser->current_workload->options.code_block.active_block->block
            );
        }
        break;
    }
    case ModTree_Expression_Type::CAST:
    {
        Expression_Value value = expr->options.cast.cast_argument->value;
        if (value.data == 0) break;
        expr->value = expression_value_make_type(analyser, expr->result_type);
        if (expr->value.data == 0) break;
        Instruction_Type instr_type = (Instruction_Type)-1;
        switch (expr->options.cast.type)
        {
        case ModTree_Cast_Type::FLOATS: instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::FLOAT_TO_INT: instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
        case ModTree_Cast_Type::INT_TO_FLOAT: instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
        case ModTree_Cast_Type::INTEGERS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::POINTERS:
        case ModTree_Cast_Type::POINTER_TO_U64:
        case ModTree_Cast_Type::U64_TO_POINTER:
        case ModTree_Cast_Type::ARRAY_SIZED_TO_UNSIZED:
            break;
        default: panic("");
        }
        if ((int)instr_type == -1) break;

        bytecode_execute_cast_instr(
            instr_type, expr->value.data, expr->options.cast.cast_argument->value.data,
            type_signature_to_bytecode_type(expr->value.signature),
            type_signature_to_bytecode_type(expr->options.cast.cast_argument->result_type)
        );
        break;
    }
    break;
    case ModTree_Expression_Type::ARRAY_ACCESS:
        if (expr->options.array_access.array_expression->expression_type == ModTree_Expression_Type::VARIABLE_READ) {
            constant_analysis_take_address_of(expr->options.array_access.array_expression->options.variable_read);
        };
        break;
    case ModTree_Expression_Type::MEMBER_ACCESS:
        if (expr->options.member_access.structure_expression->expression_type == ModTree_Expression_Type::VARIABLE_READ) {
            constant_analysis_take_address_of(expr->options.member_access.structure_expression->options.variable_read);
        };
        break;
    case ModTree_Expression_Type::FUNCTION_CALL:
    case ModTree_Expression_Type::NEW_ALLOCATION:
        break;
    default: panic("");
    }

    return result;
}

Variable_Creation_Analysis_Result semantic_analyser_analyse_variable_creation_node(
    Semantic_Analyser* analyser, Symbol_Table* symbol_table, AST_Node* var_node, ModTree_Variable_Origin origin)
{
    bool needs_expression_evaluation;
    bool type_is_given;
    AST_Node* type_node;
    AST_Node* expression_node;
    switch (var_node->type)
    {
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
    {
        needs_expression_evaluation = false;
        type_is_given = true;
        type_node = var_node->child_start;
        assert(origin.type == ModTree_Variable_Origin_Type::LOCAL || origin.type == ModTree_Variable_Origin_Type::GLOBAL, "HEY");
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
    {
        needs_expression_evaluation = true;
        expression_node = var_node->child_start->neighbor;
        type_is_given = true;
        type_node = var_node->child_start;
        assert(origin.type == ModTree_Variable_Origin_Type::LOCAL || origin.type == ModTree_Variable_Origin_Type::GLOBAL, "HEY");
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
    {
        needs_expression_evaluation = true;
        expression_node = var_node->child_start;
        type_is_given = false;
        assert(origin.type == ModTree_Variable_Origin_Type::LOCAL || origin.type == ModTree_Variable_Origin_Type::GLOBAL, "HEY");
        break;
    }
    case AST_Node_Type::NAMED_PARAMETER:
    {
        needs_expression_evaluation = false;
        type_is_given = true;
        type_node = var_node->child_start;
        assert(origin.type == ModTree_Variable_Origin_Type::PARAMETER, "HEY");
        break;
    }
    default:
        panic("Should not happen!");
    }

    Type_Signature* definition_type = 0;
    if (type_is_given)
    {
        Type_Analysis_Result definition_result = semantic_analyser_analyse_type(analyser, symbol_table, type_node, Usage_Type::VARIABLE_TYPE);
        switch (definition_result.type)
        {
        case Analysis_Result_Type::SUCCESS:
            definition_type = definition_result.options.result_type;
            if (definition_type == analyser->compiler->type_system.void_type) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
                error.error_node = type_node;
                semantic_analyser_log_error(analyser, error);
                definition_type = analyser->compiler->type_system.error_type;
            }
            break;
        case Analysis_Result_Type::ERROR_OCCURED:
            definition_type = analyser->compiler->type_system.error_type;
            break;
        case Analysis_Result_Type::DEPENDENCY: {
            Variable_Creation_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = definition_result.options.dependency;
            return result;
        }
        }
    }

    ModTree_Expression* init_expression = 0;
    Type_Signature* infered_type = 0;
    if (needs_expression_evaluation)
    {
        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, expression_node);
        switch (expr_result.type)
        {
        case Analysis_Result_Type::SUCCESS:
            init_expression = expr_result.options.expression;
            infered_type = init_expression->result_type;
            if (infered_type == analyser->compiler->type_system.void_type) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
                error.error_node = expression_node;
                semantic_analyser_log_error(analyser, error);
                infered_type = analyser->compiler->type_system.error_type;
            }
            break;
        case Analysis_Result_Type::ERROR_OCCURED:
            infered_type = analyser->compiler->type_system.error_type;
            break;
        case Analysis_Result_Type::DEPENDENCY: {
            Variable_Creation_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = expr_result.options.dependency;
            return result;
        }
        }
    }

    // Create variable 
    ModTree_Variable* variable = new ModTree_Variable(modtree_variable_make(origin, 0, 0));
    variable->symbol = symbol_table_define_symbol(
        analyser, symbol_table, var_node->id, var_node, Symbol_Type::VARIABLE,
        symbol_options_make_variable(variable), symbol_template_data_make_no_template()
    );

    switch (origin.type)
    {
    case ModTree_Variable_Origin_Type::GLOBAL:
        dynamic_array_push_back(&origin.options.parent_module->globals, variable);
        break;
    case ModTree_Variable_Origin_Type::LOCAL:
        dynamic_array_push_back(&origin.options.local_block->variables, variable);
        break;
    case ModTree_Variable_Origin_Type::PARAMETER:
        assert(origin.options.parameter.function->function_type == ModTree_Function_Type::FUNCTION, "HEY");
        assert(origin.options.parameter.function->options.function.parameters.size == origin.options.parameter.index, "Index not right");
        dynamic_array_push_back(&origin.options.parameter.function->options.function.parameters, variable);
        break;
    }

    if (type_is_given)
    {
        variable->data_type = definition_type;
        if (needs_expression_evaluation && definition_type != infered_type && infered_type != analyser->compiler->type_system.error_type)
        {
            ModTree_Expression* casted_expr = semantic_analyser_cast_implicit_if_possible(analyser, init_expression, definition_type);
            if (casted_expr == 0) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT;
                error.given_type = infered_type;
                error.expected_type = definition_type;
                error.error_node = var_node;
                semantic_analyser_log_error(analyser, error);
            }
            else {
                init_expression = casted_expr;
            }
        }
    }
    else {
        variable->data_type = infered_type;
    }

    // Make initialization code
    if (needs_expression_evaluation && infered_type->type != Signature_Type::ERROR_TYPE)
    {
        ModTree_Expression* dst = new ModTree_Expression;
        dst->value = expression_value_make_unknown();
        dst->result_type = variable->data_type;
        dst->expression_type = ModTree_Expression_Type::VARIABLE_READ;
        dst->options.variable_read = variable;

        ModTree_Statement* assignment = new ModTree_Statement;
        assignment->type = ModTree_Statement_Type::ASSIGNMENT;
        assignment->options.assignment.destination = dst;
        assignment->options.assignment.source = init_expression;

        switch (origin.type)
        {
        case ModTree_Variable_Origin_Type::GLOBAL:
            dynamic_array_push_back(&analyser->global_init_function->options.function.body->statements, assignment);
            break;
        case ModTree_Variable_Origin_Type::LOCAL:
            dynamic_array_push_back(&origin.options.local_block->statements, assignment);
            constant_analysis_assign_variable_value(variable, init_expression, origin.options.local_block);
            break;
        default: panic("No parameters allowed here");
        }
    }

    Variable_Creation_Analysis_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    result.options.variable = variable;
    return result;
}

void semantic_analyser_analyse_module(Semantic_Analyser* analyser, ModTree_Module* parent_module, Symbol_Table* parent_table, AST_Node* module_node)
{
    if (module_node->type != AST_Node_Type::ROOT && module_node->type != AST_Node_Type::MODULE && module_node->type != AST_Node_Type::MODULE_TEMPLATED) {
        panic("Should not happen");
    }

    // Create module if necessary
    ModTree_Module* module;
    Symbol_Template_Data template_data;
    if (module_node->type == AST_Node_Type::ROOT) {
        module = analyser->program->root_module;
        template_data.is_templated = false;
    }
    else
    {
        module = modtree_module_create(analyser, symbol_table_create(analyser, parent_table, module_node), parent_module, 0);
        // Check if templated
        template_data.is_templated = false;
        if (module_node->type == AST_Node_Type::MODULE_TEMPLATED)
        {
            AST_Node* template_parameter_node = module_node->child_start;
            template_data.is_templated = true;
            template_data.parameter_names = dynamic_array_create_empty<String*>(template_parameter_node->child_count);
            template_data.instances = dynamic_array_create_empty<Symbol_Template_Instance*>(1);

            AST_Node* param_node = template_parameter_node->child_start;
            while (param_node != 0)
            {
                SCOPE_EXIT(param_node = param_node->neighbor);
                dynamic_array_push_back(&template_data.parameter_names, param_node->id);
                symbol_table_define_symbol(
                    analyser, module->symbol_table, param_node->id, param_node, Symbol_Type::TYPE,
                    symbol_options_make_type(type_system_make_template(&analyser->compiler->type_system, param_node->id)),
                    symbol_template_data_make_no_template()
                );
            }
        }

        module->symbol = symbol_table_define_symbol(
            analyser, parent_table, module_node->id, module_node, Symbol_Type::MODULE,
            symbol_options_make_module(module), template_data
        );
    }

    // Analyse Definitions
    AST_Node* definitions_node = module_node->child_start;
    if (definitions_node->type != AST_Node_Type::DEFINITIONS) {
        definitions_node = definitions_node->neighbor;
        assert(definitions_node->type == AST_Node_Type::DEFINITIONS, "");
    }

    AST_Node* top_level_node = definitions_node->child_start;
    while (top_level_node != 0)
    {
        SCOPE_EXIT(top_level_node = top_level_node->neighbor);
        switch (top_level_node->type)
        {
        case AST_Node_Type::MODULE:
        case AST_Node_Type::MODULE_TEMPLATED:
        {
            if (template_data.is_templated) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES;
                error.error_node = top_level_node;
                semantic_analyser_log_error(analyser, error);
            }
            semantic_analyser_analyse_module(analyser, module, module->symbol_table, top_level_node);
            break;
        }
        case AST_Node_Type::EXTERN_FUNCTION_DECLARATION:
        {
            if (template_data.is_templated) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES;
                error.error_node = top_level_node;
                semantic_analyser_log_error(analyser, error);
                break;
            }
            // Create Workload
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION;
            workload.options.extern_function.node = top_level_node;
            workload.options.extern_function.parent_module = module;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case AST_Node_Type::EXTERN_HEADER_IMPORT:
        {
            if (template_data.is_templated) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES;
                error.error_node = top_level_node;
                semantic_analyser_log_error(analyser, error);
                break;
            }
            // Create Workload
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::EXTERN_HEADER_IMPORT;
            workload.options.extern_header.parent_module = module;
            workload.options.extern_header.node = top_level_node;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case AST_Node_Type::LOAD_FILE:
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
        case AST_Node_Type::EXTERN_LIB_IMPORT: {
            if (template_data.is_templated) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES;
                error.error_node = top_level_node;
                semantic_analyser_log_error(analyser, error);
                break;
            }
            dynamic_array_push_back(&analyser->compiler->extern_sources.lib_files, top_level_node->id);
            break;
        }
        case AST_Node_Type::FUNCTION:
        {
            ModTree_Function* function = modtree_function_make_empty(analyser, module, module->symbol_table, 0, 0, top_level_node);
            function->symbol = symbol_table_define_symbol(
                analyser, module->symbol_table, top_level_node->id, top_level_node, Symbol_Type::FUNCTION,
                symbol_options_make_function(function), symbol_template_data_make_inherited(&template_data)
            );

            // Create header workload
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::FUNCTION_HEADER;
            workload.options.function_header.function_node = top_level_node;
            workload.options.function_header.function = function;
            workload.options.function_header.symbol_table = function->options.function.body->symbol_table;
            workload.options.function_header.next_parameter_node = top_level_node->child_start->child_start->child_start,
                dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case AST_Node_Type::ENUM:
        {
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::ENUM_BODY;
            workload.options.enum_body.current_member = top_level_node->child_start;
            workload.options.enum_body.enum_type = type_system_make_enum_empty(&analyser->compiler->type_system, top_level_node->id);
            workload.options.enum_body.next_integer_value = 1;
            workload.options.enum_body.symbol_table = module->symbol_table;
            dynamic_array_push_back(&analyser->active_workloads, workload);

            symbol_table_define_symbol(
                analyser, module->symbol_table, top_level_node->id, top_level_node,
                Symbol_Type::TYPE, symbol_options_make_type(workload.options.enum_body.enum_type),
                symbol_template_data_make_inherited(&template_data)
            );
            break;
        }
        case AST_Node_Type::UNION:
        case AST_Node_Type::STRUCT:
        {
            AST_Node* struct_node = top_level_node;

            Type_Signature* signature = type_system_make_struct_empty(&analyser->compiler->type_system, struct_node, top_level_node->type == AST_Node_Type::UNION);
            symbol_table_define_symbol(
                analyser, module->symbol_table, struct_node->id, struct_node, Symbol_Type::TYPE,
                symbol_options_make_type(signature), symbol_template_data_make_inherited(&template_data)
            );

            // This check is done afterwards so that other errors can also be found
            if (struct_node->child_count == 0) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::OTHERS_STRUCT_MUST_CONTAIN_MEMBER;
                error.error_node = top_level_node;
                semantic_analyser_log_error(analyser, error);
                break;
            }

            // Prepare struct body workload
            {
                Analysis_Workload body_workload;
                body_workload.type = Analysis_Workload_Type::STRUCT_BODY;
                body_workload.options.struct_body.symbol_table = module->symbol_table;
                body_workload.options.struct_body.struct_signature = signature;
                body_workload.options.struct_body.current_member_node = struct_node->child_start;
                body_workload.options.struct_body.offset = 0;
                body_workload.options.struct_body.alignment = 0;
                dynamic_array_push_back(&analyser->active_workloads, body_workload);
            }
            break;
        }
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
        {
            if (template_data.is_templated) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_TEMPLATED_GLOBALS;
                error.error_node = top_level_node;
                semantic_analyser_log_error(analyser, error);
                continue;
            }
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::GLOBAL;
            workload.options.global.parent_module = module;
            workload.options.global.node = top_level_node;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        default: panic("HEy");
        }
    }
}

enum class Defer_Resolve_Depth
{
    WHOLE_FUNCTION,
    LOCAL_BLOCK,
    LOOP_EXIT
};

Type_Signature* import_c_type(Semantic_Analyser* analyser, C_Import_Type* type, Hashtable<C_Import_Type*, Type_Signature*>* type_conversions);

void workload_code_block_work_through_defers(Semantic_Analyser* analyser, Analysis_Workload_Code* workload, Defer_Resolve_Depth resolve_depth)
{
    if (workload->active_block->inside_defer) return;

    int defer_start_index = 0;
    switch (resolve_depth)
    {
    case Defer_Resolve_Depth::WHOLE_FUNCTION:
        defer_start_index = 0;
        break;
    case Defer_Resolve_Depth::LOCAL_BLOCK: {
        defer_start_index = workload->active_block->defer_count_block_start;
        break;
    }
    case Defer_Resolve_Depth::LOOP_EXIT: {
        defer_start_index = workload->active_block->defer_count_surrounding_loop;
        break;
    }
    default: panic("what");
    }

    ModTree_Block* parent = workload->active_block->block;
    for (int i = workload->defer_nodes.size - 1; i >= defer_start_index; i--)
    {
        AST_Node* block_node = workload->defer_nodes[i];
        ModTree_Statement* block_stmt = new ModTree_Statement;
        block_stmt->type = ModTree_Statement_Type::BLOCK;
        block_stmt->options.block = modtree_block_create_block_child(symbol_table_create(analyser, parent->symbol_table, 0), parent, Block_Type::DEFER_BLOCK);
        dynamic_array_push_back(&parent->statements, block_stmt);
        analysis_workload_code_block_add_block(workload, block_stmt->options.block, block_node);
    }

    dynamic_array_rollback_to_size(&workload->defer_nodes, workload->active_block->defer_count_block_start);
}

void analysis_workload_destroy(Analysis_Workload* workload)
{
    switch (workload->type)
    {
    case Analysis_Workload_Type::STRUCT_BODY:
    case Analysis_Workload_Type::GLOBAL:
    case Analysis_Workload_Type::ARRAY_SIZE:
    case Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION:
    case Analysis_Workload_Type::EXTERN_HEADER_IMPORT:
    case Analysis_Workload_Type::FUNCTION_HEADER:
    case Analysis_Workload_Type::MODULE_ANALYSIS:
    case Analysis_Workload_Type::ENUM_BODY:
        break;
    case Analysis_Workload_Type::CODE:
        dynamic_array_destroy(&workload->options.code_block.defer_nodes);
        list_destroy(&workload->options.code_block.block_queue);
        hashtable_destroy(&workload->options.code_block.active_switches);
        break;
    default: panic("Hey");
    }
}

void analysis_workload_append_to_string(Analysis_Workload* workload, String* string, Semantic_Analyser* analyser);
void workload_dependency_append_to_string(Workload_Dependency* dependency, String* string, Semantic_Analyser* analyser);

void semantic_analyser_reset(Semantic_Analyser* analyser, Compiler* compiler)
{
    // Reset analyser data
    Symbol_Table* base_table;
    {
        analyser->compiler = compiler;
        for (int i = 0; i < analyser->symbol_tables.size; i++) {
            symbol_table_destroy(analyser->symbol_tables[i]);
        }
        dynamic_array_reset(&analyser->symbol_tables);
        stack_allocator_reset(&analyser->allocator_values);
        dynamic_array_reset(&analyser->errors);
        dynamic_array_reset(&analyser->active_workloads);
        dynamic_array_reset(&analyser->waiting_workload);
        hashtable_reset(&analyser->finished_code_blocks);
        hashtable_reset(&analyser->ast_to_symbol_table);
        hashtable_reset(&analyser->bake_locations);
        hashset_reset(&analyser->loaded_filenames);
        hashset_reset(&analyser->visited_functions);

        base_table = symbol_table_create(analyser, nullptr, 0);
        analyser->base_table = base_table;

        if (analyser->program != 0) {
            modtree_program_destroy(analyser->program);
        }
        analyser->program = modtree_program_create(analyser, base_table);
        analyser->global_init_function = 0;
        analyser->current_workload = 0;
    }

    // Add symbols for basic datatypes
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

        semantic_analyser_define_type_symbol(analyser, base_table, id_int, analyser->compiler->type_system.i32_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_bool, analyser->compiler->type_system.bool_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_float, analyser->compiler->type_system.f32_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_f32, analyser->compiler->type_system.f32_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_f64, analyser->compiler->type_system.f64_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_u8, analyser->compiler->type_system.u8_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_byte, analyser->compiler->type_system.u8_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_u16, analyser->compiler->type_system.u16_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_u32, analyser->compiler->type_system.u32_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_u64, analyser->compiler->type_system.u64_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_i8, analyser->compiler->type_system.i8_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_i16, analyser->compiler->type_system.i16_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_i32, analyser->compiler->type_system.i32_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_i64, analyser->compiler->type_system.i64_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_void, analyser->compiler->type_system.void_type, 0);
        semantic_analyser_define_type_symbol(analyser, base_table, id_string, analyser->compiler->type_system.string_type, 0);

        analyser->id_size = identifier_pool_add(&compiler->identifier_pool, string_create_static("size"));
        analyser->id_data = identifier_pool_add(&compiler->identifier_pool, string_create_static("data"));
        analyser->id_main = identifier_pool_add(&compiler->identifier_pool, string_create_static("main"));
    }

    // Initialize hardcoded_functions
    for (int i = 0; i < (int)Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT; i++)
    {
        Hardcoded_Function_Type type = (Hardcoded_Function_Type)i;
        Type_System* type_system = &analyser->compiler->type_system;

        // Create Function
        ModTree_Function* hardcoded_function = new ModTree_Function;
        hardcoded_function->function_type = ModTree_Function_Type::HARDCODED_FUNCTION;
        hardcoded_function->parent_module = analyser->program->root_module;
        hardcoded_function->options.hardcoded_type = type;
        hardcoded_function->symbol = 0;
        dynamic_array_push_back(&analyser->program->root_module->functions, hardcoded_function);

        // Find signature
        Dynamic_Array<Type_Signature*> parameters = dynamic_array_create_empty<Type_Signature*>(2);
        Type_Signature* return_type = type_system->void_type;
        String* name_handle = 0;
        switch (type)
        {
        case Hardcoded_Function_Type::PRINT_I32: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_i32"));
            dynamic_array_push_back(&parameters, type_system->i32_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_F32: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_f32"));
            dynamic_array_push_back(&parameters, type_system->f32_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_BOOL: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_bool"));
            dynamic_array_push_back(&parameters, type_system->bool_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_STRING: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_string"));
            dynamic_array_push_back(&parameters, type_system->string_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_LINE: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("print_line"));
            break;
        }
        case Hardcoded_Function_Type::READ_I32: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("read_i32"));
            return_type = type_system->i32_type;
            break;
        }
        case Hardcoded_Function_Type::READ_F32: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("read_f32"));
            return_type = type_system->f32_type;
            break;
        }
        case Hardcoded_Function_Type::READ_BOOL: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("read_bool"));
            return_type = type_system->bool_type;
            break;
        }
        case Hardcoded_Function_Type::RANDOM_I32: {
            name_handle = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("random_i32"));
            return_type = type_system->i32_type;
            break;
        }
        case Hardcoded_Function_Type::MALLOC_SIZE_I32:
            analyser->malloc_function = hardcoded_function;
            dynamic_array_push_back(&parameters, type_system->i32_type);
            return_type = type_system->void_ptr_type;
            break;
        case Hardcoded_Function_Type::FREE_POINTER:
            analyser->free_function = hardcoded_function;
            dynamic_array_push_back(&parameters, type_system->void_ptr_type);
            return_type = type_system->void_type;
            break;
        default:
            panic("What");
        }
        hardcoded_function->signature = type_system_make_function(type_system, parameters, return_type);

        // Define symbol
        if (name_handle != 0) {
            hardcoded_function->symbol = symbol_table_define_symbol(
                analyser, base_table, name_handle, 0, Symbol_Type::FUNCTION,
                symbol_options_make_function(hardcoded_function), symbol_template_data_make_no_template()
            );
        }
    }

    // Add assert function
    {
        Dynamic_Array<Type_Signature*> params = dynamic_array_create_empty<Type_Signature*>(1);
        dynamic_array_push_back(&params, analyser->compiler->type_system.bool_type);
        ModTree_Function* assert_fn = modtree_function_make_empty(
            analyser, analyser->program->root_module, analyser->base_table,
            type_system_make_function(&analyser->compiler->type_system, params, analyser->compiler->type_system.void_type),
            0, 0
        );
        assert_fn->symbol = symbol_table_define_symbol(
            analyser, base_table, identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("assert")),
            0, Symbol_Type::FUNCTION, symbol_options_make_function(assert_fn), symbol_template_data_make_no_template()
        );

        ModTree_Variable_Origin origin;
        origin.type = ModTree_Variable_Origin_Type::PARAMETER;
        origin.options.parameter.function = assert_fn;
        origin.options.parameter.index = 0;
        ModTree_Variable* cond_var = new ModTree_Variable(modtree_variable_make(origin, 0, analyser->compiler->type_system.bool_type));
        dynamic_array_push_back(&assert_fn->options.function.parameters, cond_var);

        // Make function body
        {
            ModTree_Expression* param_read_expr = new ModTree_Expression;
            param_read_expr->result_type = analyser->compiler->type_system.bool_type;
            param_read_expr->value = expression_value_make_unknown();
            param_read_expr->expression_type = ModTree_Expression_Type::VARIABLE_READ;
            param_read_expr->options.variable_read = cond_var;

            ModTree_Expression* cond_expr = new ModTree_Expression;
            cond_expr->result_type = analyser->compiler->type_system.bool_type;
            cond_expr->value = expression_value_make_unknown();
            cond_expr->expression_type = ModTree_Expression_Type::UNARY_OPERATION;
            cond_expr->options.unary_operation.operand = param_read_expr;
            cond_expr->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::LOGICAL_NOT;

            ModTree_Statement* if_stat = new ModTree_Statement;
            if_stat->type = ModTree_Statement_Type::IF;
            if_stat->options.if_statement.condition = cond_expr;
            if_stat->options.if_statement.if_block = modtree_block_create_block_child(
                assert_fn->options.function.body->symbol_table,
                assert_fn->options.function.body, Block_Type::IF_TRUE_BLOCK
            );
            if_stat->options.if_statement.else_block = modtree_block_create_block_child(
                assert_fn->options.function.body->symbol_table,
                assert_fn->options.function.body, Block_Type::IF_ELSE_BLOCK
            );
            dynamic_array_push_back(&assert_fn->options.function.body->statements, if_stat);

            ModTree_Statement* exit_stat = new ModTree_Statement;
            exit_stat->type = ModTree_Statement_Type::EXIT;
            exit_stat->options.exit_code = Exit_Code::ASSERTION_FAILED;
            dynamic_array_push_back(&if_stat->options.if_statement.if_block->statements, exit_stat);

            ModTree_Statement* return_stat = new ModTree_Statement;
            return_stat->type = ModTree_Statement_Type::RETURN;
            return_stat->options.return_value.available = false;
            dynamic_array_push_back(&assert_fn->options.function.body->statements, return_stat);
        }
        analyser->assert_function = assert_fn;
    }

    // Add global init function
    analyser->global_init_function = modtree_function_make_empty(
        analyser, analyser->program->root_module, analyser->base_table,
        type_system_make_function(
            &analyser->compiler->type_system,
            dynamic_array_create_empty<Type_Signature*>(1),
            analyser->compiler->type_system.void_type
        ),
        0, 0
    );
}

void semantic_analyser_execute_workloads(Semantic_Analyser* analyser)
{
    if (PRINT_DEPENDENCIES) {
        logg("SEMANTIC_ANALYSER_DEPENDECIES:\n-----------------------------\n");
    }
    SCOPE_EXIT(if (PRINT_DEPENDENCIES) logg("------------------------------------\n"););

    // Execute all Workloads
    while (analyser->active_workloads.size != 0)
    {
        // Get next workload
        Analysis_Workload workload = analyser->active_workloads[analyser->active_workloads.size - 1];
        dynamic_array_swap_remove(&analyser->active_workloads, analyser->active_workloads.size - 1);
        analyser->current_workload = &workload;

        if (PRINT_DEPENDENCIES)
        {
            String output = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&output));
            string_append_formated(&output, "WORKING ON: ");
            analysis_workload_append_to_string(&workload, &output, analyser);
            string_append_formated(&output, "\n");
            logg(output.characters);
        }

        // Execute Workload
        bool found_workload_dependency = false;
        Workload_Dependency found_dependency;
        switch (workload.type)
        {
        case Analysis_Workload_Type::MODULE_ANALYSIS:
        {
            Analysis_Workload_Module_Analysis* work = &workload.options.module_analysis;
            semantic_analyser_analyse_module(analyser, analyser->program->root_module, analyser->base_table, work->root_node);
            break;
        }
        case Analysis_Workload_Type::ARRAY_SIZE:
        {
            Type_Signature* array_sig = workload.options.array_size.array_signature;
            if (array_sig->options.array.element_type->size == 0 || array_sig->options.array.element_type->alignment == 0) {
                panic("Hey, at this point this should be resolved!");
            }
            array_sig->alignment = array_sig->options.array.element_type->alignment;
            array_sig->size = math_round_next_multiple(array_sig->options.array.element_type->size,
                array_sig->options.array.element_type->alignment) * array_sig->options.array.element_count;
            break;
        }
        case Analysis_Workload_Type::FUNCTION_HEADER:
        {
            assert(workload.options.function_header.function->signature == 0, "Function already analysed!");
            ModTree_Function* function = workload.options.function_header.function;
            AST_Node* function_node = workload.options.function_header.function_node;
            AST_Node* signature_node = function_node->child_start;
            AST_Node* parameter_block = signature_node->child_start;
            AST_Node* body_node = function_node->child_end;

            // Analyse parameters
            {
                AST_Node* parameter_node = parameter_block->child_start;
                int i = 0;
                while (parameter_node != 0)
                {
                    SCOPE_EXIT(parameter_node = parameter_node->neighbor; i++;);
                    ModTree_Variable_Origin origin;
                    origin.type = ModTree_Variable_Origin_Type::PARAMETER;
                    origin.options.parameter.function = function;
                    origin.options.parameter.index = i;
                    Variable_Creation_Analysis_Result parameter_result = semantic_analyser_analyse_variable_creation_node(
                        analyser, workload.options.function_header.symbol_table, parameter_node, origin
                    );
                    if (parameter_result.type == Analysis_Result_Type::DEPENDENCY) {
                        found_dependency = parameter_result.options.dependency;
                        found_workload_dependency = true;
                        break;
                    }
                    workload.options.function_header.next_parameter_node = parameter_node->neighbor;
                }
                if (found_workload_dependency) break;
            }

            // Analyse return type
            Type_Signature* return_type = analyser->compiler->type_system.void_type;
            if (signature_node->child_count == 2)
            {
                Type_Analysis_Result return_type_result = semantic_analyser_analyse_type(
                    analyser, workload.options.function_header.symbol_table, signature_node->child_start->neighbor, Usage_Type::FUNCTION_RETURN_TYPE
                );
                if (return_type_result.type == Analysis_Result_Type::SUCCESS) {
                    return_type = return_type_result.options.result_type;
                }
                else if (return_type_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                    return_type = analyser->compiler->type_system.error_type;
                }
                else {
                    found_workload_dependency = true;
                    found_dependency = return_type_result.options.dependency;
                    break;
                }
            }

            Dynamic_Array<Type_Signature*> parameters = dynamic_array_create_empty<Type_Signature*>(parameter_block->child_count);
            for (int i = 0; i < function->options.function.parameters.size; i++) {
                dynamic_array_push_back(&parameters, function->options.function.parameters[i]->data_type);
            }
            function->signature = type_system_make_function(&analyser->compiler->type_system, parameters, return_type);

            // Create workload
            dynamic_array_push_back(&analyser->active_workloads, analysis_workload_make_code_block(function->options.function.body, body_node));

            // Check for main function
            if (function_node->id == analyser->id_main)
            {
                if (function->symbol != 0 && function->symbol->template_data.is_templated) {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::OTHERS_MAIN_CANNOT_BE_TEMPLATED;
                    error.error_node = workload.options.function_header.function_node;
                    semantic_analyser_log_error(analyser, error);
                }
                analyser->program->entry_function = function;
                if (function->signature->options.function.return_type != analyser->compiler->type_system.void_type ||
                    function->signature->options.function.parameter_types.size != 0) {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::OTHERS_MAIN_UNEXPECTED_SIGNATURE;
                    error.error_node = workload.options.function_header.function_node;
                    error.given_type = function->signature;
                    error.expected_type = analyser->global_init_function->signature;
                    semantic_analyser_log_error(analyser, error);
                }
                // Add call to init function
                ModTree_Expression* call_expr = new ModTree_Expression;
                call_expr->expression_type = ModTree_Expression_Type::FUNCTION_CALL;
                call_expr->result_type = analyser->compiler->type_system.void_type;
                call_expr->options.function_call.is_pointer_call = false;
                call_expr->options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(1);
                call_expr->options.function_call.function = analyser->global_init_function;
                call_expr->value = expression_value_make_unknown();
                ModTree_Statement* call_stmt = new ModTree_Statement;
                call_stmt->type = ModTree_Statement_Type::EXPRESSION;
                call_stmt->options.expression = call_expr;
                dynamic_array_push_back(&function->options.function.body->statements, call_stmt);
            }
            break;
        }
        case Analysis_Workload_Type::GLOBAL: {
            ModTree_Variable_Origin origin;
            origin.type = ModTree_Variable_Origin_Type::GLOBAL;
            origin.options.parent_module = workload.options.global.parent_module;
            Variable_Creation_Analysis_Result result = semantic_analyser_analyse_variable_creation_node(
                analyser, workload.options.global.parent_module->symbol_table, workload.options.global.node, origin
            );
            if (result.type == Analysis_Result_Type::DEPENDENCY) {
                found_workload_dependency = true;
                found_dependency = result.options.dependency;
            }
            break;
        }
        case Analysis_Workload_Type::EXTERN_HEADER_IMPORT:
        {
            AST_Node* extern_node = workload.options.extern_header.node;
            String* header_name_id = extern_node->id;
            Optional<C_Import_Package> package = c_importer_import_header(&analyser->compiler->c_importer, *header_name_id, &analyser->compiler->identifier_pool);
            if (package.available)
            {
                logg("Importing header successfull: %s\n", header_name_id->characters);
                dynamic_array_push_back(&analyser->compiler->extern_sources.headers_to_include, header_name_id);
                Hashtable<C_Import_Type*, Type_Signature*> type_conversion_table = hashtable_create_pointer_empty<C_Import_Type*, Type_Signature*>(256);
                SCOPE_EXIT(hashtable_destroy(&type_conversion_table));

                AST_Node* import_id_node = extern_node->child_start;
                while (import_id_node != 0)
                {
                    SCOPE_EXIT(import_id_node = import_id_node->neighbor);
                    String* import_id = import_id_node->id;
                    C_Import_Symbol* import_symbol = hashtable_find_element(&package.value.symbol_table.symbols, import_id);
                    if (import_symbol == 0) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL;
                        error.id = import_id;
                        error.error_node = import_id_node;
                        semantic_analyser_log_error(analyser, error);
                        continue;
                    }

                    switch (import_symbol->type)
                    {
                    case C_Import_Symbol_Type::TYPE:
                    {
                        Type_Signature* type = import_c_type(analyser, import_symbol->data_type, &type_conversion_table);
                        if (type->type == Signature_Type::STRUCT) {
                            hashtable_insert_element(&analyser->compiler->extern_sources.extern_type_signatures, type, import_id);
                        }
                        symbol_table_define_symbol(
                            analyser, workload.options.extern_header.parent_module->symbol_table, import_id,
                            import_id_node, Symbol_Type::TYPE, symbol_options_make_type(type), symbol_template_data_make_no_template()
                        );
                        break;
                    }
                    case C_Import_Symbol_Type::FUNCTION:
                    {
                        ModTree_Function* extern_fn = new ModTree_Function;
                        extern_fn->parent_module = workload.options.extern_header.parent_module;
                        extern_fn->function_type = ModTree_Function_Type::EXTERN_FUNCTION;
                        extern_fn->options.extern_function.id = import_id;
                        extern_fn->signature = import_c_type(analyser, import_symbol->data_type, &type_conversion_table);
                        extern_fn->options.extern_function.function_signature = extern_fn->signature;
                        assert(extern_fn->signature->type == Signature_Type::FUNCTION, "HEY");

                        dynamic_array_push_back(&workload.options.extern_header.parent_module->functions, extern_fn);
                        extern_fn->symbol = symbol_table_define_symbol(
                            analyser, workload.options.extern_header.parent_module->symbol_table, import_id,
                            import_id_node, Symbol_Type::FUNCTION, symbol_options_make_function(extern_fn), symbol_template_data_make_no_template()
                        );
                        break;
                    }
                    case C_Import_Symbol_Type::GLOBAL_VARIABLE: {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT;
                        error.error_node = import_id_node;
                        semantic_analyser_log_error(analyser, error);
                        break;
                    }
                    default: panic("hey");
                    }
                }

                // Import all used type names
                auto iter = hashtable_iterator_create(&package.value.symbol_table.symbols);
                while (hashtable_iterator_has_next(&iter))
                {
                    String* id = *iter.key;
                    if (symbol_table_find_symbol(workload.options.extern_header.parent_module->symbol_table, id, true, symbol_reference_make_ignore(), analyser) != 0) {
                        hashtable_iterator_next(&iter);
                        continue;
                    }
                    C_Import_Symbol* import_sym = iter.value;
                    if (import_sym->type == C_Import_Symbol_Type::TYPE)
                    {
                        Type_Signature** signature = hashtable_find_element(&type_conversion_table, import_sym->data_type);
                        if (signature != 0)
                        {
                            Type_Signature* type = *signature;
                            if (type->type == Signature_Type::STRUCT) {
                                hashtable_insert_element(&analyser->compiler->extern_sources.extern_type_signatures, type, id);
                            }
                            symbol_table_define_symbol(
                                analyser, workload.options.extern_header.parent_module->symbol_table, id,
                                workload.options.extern_header.node, Symbol_Type::TYPE, symbol_options_make_type(type), symbol_template_data_make_no_template()
                            );
                        }
                    }
                    hashtable_iterator_next(&iter);
                }
            }
            else {
                Semantic_Error error;
                error.type = Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED;
                error.error_node = workload.options.extern_header.node;
                semantic_analyser_log_error(analyser, error);
            }
            break;
        }
        case Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION:
        {
            Analysis_Workload_Extern_Function* work = &workload.options.extern_function;
            Symbol_Table* symbol_table = work->parent_module->symbol_table;
            AST_Node* extern_node = work->node;
            Type_Analysis_Result result = semantic_analyser_analyse_type(
                analyser, work->parent_module->symbol_table, extern_node->child_start, Usage_Type::EXTERN_FUNCTION_TYPE
            );
            switch (result.type)
            {
            case Analysis_Result_Type::SUCCESS:
            {
                if (!(result.options.result_type->type == Signature_Type::POINTER &&
                    result.options.result_type->options.pointer_child->type == Signature_Type::FUNCTION)) {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER;
                    error.error_node = extern_node->child_start;
                    semantic_analyser_log_error(analyser, error);
                    break;
                }

                ModTree_Function* extern_fn = new ModTree_Function;
                extern_fn->parent_module = work->parent_module;
                extern_fn->function_type = ModTree_Function_Type::EXTERN_FUNCTION;
                extern_fn->options.extern_function.id = extern_node->id;
                extern_fn->signature = result.options.result_type->options.pointer_child;
                extern_fn->options.extern_function.function_signature = extern_fn->signature;
                assert(extern_fn->signature->type == Signature_Type::FUNCTION, "HEY");
                dynamic_array_push_back(&extern_fn->parent_module->functions, extern_fn);
                dynamic_array_push_back(&analyser->compiler->extern_sources.extern_functions, extern_fn->options.extern_function);

                extern_fn->symbol = symbol_table_define_symbol(
                    analyser, work->parent_module->symbol_table, extern_fn->options.extern_function.id,
                    work->node, Symbol_Type::FUNCTION, symbol_options_make_function(extern_fn), symbol_template_data_make_no_template()
                );
                break;
            }
            case Analysis_Result_Type::DEPENDENCY:
                found_workload_dependency = true;
                found_dependency = result.options.dependency;
                break;
            case Analysis_Result_Type::ERROR_OCCURED:
                break;
            }
            break;
        }
        case Analysis_Workload_Type::CODE:
        {
            Analysis_Workload_Code* code_workload = &workload.options.code_block;
            while (!found_workload_dependency && code_workload->block_queue.count != 0)
            {
                code_workload->active_block = &code_workload->block_queue.tail->value;
                AST_Node* statement_node = code_workload->active_block->current_statement_node;

                // Check if the last statement may have cause some control flow change
                if (code_workload->active_block->last_analysed_statement != 0)
                {
                    ModTree_Statement* last_statement = code_workload->active_block->last_analysed_statement;
                    if (last_statement->type == ModTree_Statement_Type::BLOCK)
                    {
                        Block_Control_Flow* last_block_flow = hashtable_find_element(&analyser->finished_code_blocks, last_statement->options.block);
                        assert(last_block_flow != 0, "");
                        code_workload->active_block->block_flow = *last_block_flow;
                    }
                    else if (last_statement->type == ModTree_Statement_Type::IF)
                    {
                        if (last_statement->options.if_statement.else_block->statements.size != 0)
                        {
                            Block_Control_Flow* true_branch_opt =
                                hashtable_find_element(&analyser->finished_code_blocks, last_statement->options.if_statement.if_block);
                            Block_Control_Flow* false_branch_opt =
                                hashtable_find_element(&analyser->finished_code_blocks, last_statement->options.if_statement.else_block);
                            assert(true_branch_opt != 0 && false_branch_opt != 0, "");
                            if (*true_branch_opt == *false_branch_opt) {
                                code_workload->active_block->block_flow = *false_branch_opt;
                            }
                        }
                    }
                    else if (last_statement->type == ModTree_Statement_Type::WHILE)
                    {
                        Block_Control_Flow* body_result = hashtable_find_element(&analyser->finished_code_blocks, last_statement->options.while_statement.while_block);
                        assert(body_result != 0, "Should not happen");
                        if (*body_result == Block_Control_Flow::RETURN) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS;
                            error.error_node = code_workload->active_block->last_analysed_node;
                            semantic_analyser_log_error(analyser, error);
                        }
                        else if (*body_result == Block_Control_Flow::CONTINUE) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS;
                            error.error_node = code_workload->active_block->last_analysed_node;
                            semantic_analyser_log_error(analyser, error);
                        }
                        else if (*body_result == Block_Control_Flow::BREAK) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE;
                            error.error_node = code_workload->active_block->last_analysed_node;
                            semantic_analyser_log_error(analyser, error);
                        }
                    }
                    code_workload->active_block->last_analysed_node = 0;
                    code_workload->active_block->last_analysed_statement = 0;
                }


                // Check if end of block was reached
                if (statement_node == 0)
                {
                    // Check if return value exists
                    if (code_workload->active_block->block->type == Block_Type::FUNCTION_BODY &&
                        code_workload->active_block->block_flow == Block_Control_Flow::NO_RETURN)
                    {
                        if (code_workload->function->signature->options.function.return_type == analyser->compiler->type_system.void_type)
                        {
                            // Append return instruction 
                            workload_code_block_work_through_defers(analyser, code_workload, Defer_Resolve_Depth::WHOLE_FUNCTION);
                            ModTree_Statement* return_statement = new ModTree_Statement;
                            if (code_workload->function == analyser->program->entry_function) {
                                return_statement->type = ModTree_Statement_Type::EXIT;
                                return_statement->options.exit_code = Exit_Code::SUCCESS;
                            }
                            else {
                                return_statement->type = ModTree_Statement_Type::RETURN;
                                return_statement->options.return_value.available = false;
                            }
                            dynamic_array_push_back(&code_workload->active_block->block->statements, return_statement);
                        }
                        else {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT;
                            error.error_node = code_workload->active_block->block_node;
                            semantic_analyser_log_error(analyser, error);
                        }
                    }
                    else {
                        //if (!modtree_block_origin_inside_defer(&code_workload->progress.block->origin)) {
                        workload_code_block_work_through_defers(analyser, code_workload, Defer_Resolve_Depth::LOCAL_BLOCK);
                        //}
                    }

                    // Register block as finished
                    hashtable_insert_element(&analyser->finished_code_blocks, code_workload->active_block->block, code_workload->active_block->block_flow);
                    // Remove workload
                    list_remove_node_item(&code_workload->block_queue, code_workload->active_block);

                    continue;
                }

                bool auto_switch_node = true;
                SCOPE_EXIT(
                    if (!found_workload_dependency)
                    {
                        if (code_workload->active_block->block->statements.size != 0) {
                            code_workload->active_block->last_analysed_statement =
                                code_workload->active_block->block->statements[code_workload->active_block->block->statements.size - 1];
                            code_workload->active_block->last_analysed_node = code_workload->active_block->current_statement_node;
                        }
                        if (auto_switch_node) {
                            code_workload->active_block->current_statement_node = code_workload->active_block->current_statement_node->neighbor;
                        }
                    }
                );

                if (code_workload->active_block->block_flow != Block_Control_Flow::NO_RETURN) {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE;
                    error.error_node = statement_node;
                    semantic_analyser_log_error(analyser, error);
                }

                Symbol_Table* symbol_table = code_workload->active_block->block->symbol_table;
                ModTree_Block* block = code_workload->active_block->block;
                switch (statement_node->type)
                {
                case AST_Node_Type::STATEMENT_RETURN:
                {
                    ModTree_Statement return_statement;

                    // Determine return type
                    if (code_workload->function == analyser->program->entry_function)
                    {
                        return_statement.type = ModTree_Statement_Type::EXIT;
                        return_statement.options.exit_code = Exit_Code::SUCCESS;
                        if (statement_node->child_count == 1) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::OTHERS_RETURN_EXPECTED_NO_VALUE;
                            error.error_node = statement_node->child_start;
                            semantic_analyser_log_error(analyser, error);
                        }
                    }
                    else
                    {
                        Type_Signature* return_type = 0;
                        return_statement.type = ModTree_Statement_Type::RETURN;
                        if (statement_node->child_count == 0) {
                            return_type = analyser->compiler->type_system.void_type;
                            return_statement.options.return_value.available = false;
                        }
                        else
                        {
                            return_statement.options.return_value.available = true;
                            Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, statement_node->child_start);
                            switch (expr_result.type)
                            {
                            case Analysis_Result_Type::SUCCESS: {
                                return_type = expr_result.options.expression->result_type;
                                return_statement.options.return_value.value = expr_result.options.expression;
                                break;
                            }
                            case Analysis_Result_Type::DEPENDENCY:
                                found_dependency = expr_result.options.dependency;
                                found_workload_dependency = true;
                                break;
                            case Analysis_Result_Type::ERROR_OCCURED:
                                continue; // Just ignore return block_node if an error occured
                            }
                        }
                        if (found_workload_dependency) break;
                        if (return_type != code_workload->function->signature->options.function.return_type) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_RETURN;
                            error.given_type = return_type;
                            error.expected_type = code_workload->function->signature->options.function.return_type;
                            error.error_node = statement_node;
                            semantic_analyser_log_error(analyser, error);
                        }
                    }

                    // Check if inside defer
                    if (code_workload->active_block->inside_defer)
                    {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED;
                        error.error_node = statement_node;
                        semantic_analyser_log_error(analyser, error);
                        break;
                    }

                    code_workload->active_block->block_flow = Block_Control_Flow::RETURN;
                    // Defers with return is tricky, since the expression needs to be evaluated before the defers are run
                    if (code_workload->defer_nodes.size != 0 && statement_node->child_count != 0 && return_statement.type != ModTree_Statement_Type::EXIT)
                    {
                        ModTree_Variable_Origin origin;
                        origin.type = ModTree_Variable_Origin_Type::LOCAL;
                        origin.options.local_block = block;
                        ModTree_Variable* variable = new ModTree_Variable(
                            modtree_variable_make(origin, 0, return_statement.options.return_value.value->result_type)
                        );
                        variable->symbol = 0;
                        dynamic_array_push_back(&block->variables, variable);

                        ModTree_Expression* read_expr = new ModTree_Expression;
                        read_expr->value = expression_value_make_unknown();
                        read_expr->result_type = variable->data_type;
                        read_expr->expression_type = ModTree_Expression_Type::VARIABLE_READ;
                        read_expr->options.variable_read = variable;

                        ModTree_Statement* assign_stmt = new ModTree_Statement;
                        assign_stmt->type = ModTree_Statement_Type::ASSIGNMENT;
                        assign_stmt->options.assignment.destination = read_expr;
                        assign_stmt->options.assignment.source = return_statement.options.return_value.value;
                        dynamic_array_push_back(&block->statements, assign_stmt);

                        // Create second read expression
                        read_expr = new ModTree_Expression;
                        read_expr->value = expression_value_make_unknown();
                        read_expr->result_type = variable->data_type;
                        read_expr->expression_type = ModTree_Expression_Type::VARIABLE_READ;
                        read_expr->options.variable_read = variable;
                        return_statement.options.return_value.value = read_expr;
                    }
                    workload_code_block_work_through_defers(analyser, code_workload, Defer_Resolve_Depth::WHOLE_FUNCTION);
                    dynamic_array_push_back(&block->statements, new ModTree_Statement(return_statement));
                    break;
                }
                case AST_Node_Type::STATEMENT_BREAK:
                {
                    if (!code_workload->active_block->inside_loop) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_BREAK_NOT_INSIDE_LOOP;
                        error.error_node = statement_node;
                        semantic_analyser_log_error(analyser, error);
                    }
                    workload_code_block_work_through_defers(analyser, code_workload, Defer_Resolve_Depth::LOOP_EXIT);

                    ModTree_Statement* break_stmt = new ModTree_Statement;
                    break_stmt->type = ModTree_Statement_Type::BREAK;
                    dynamic_array_push_back(&block->statements, break_stmt);
                    code_workload->active_block->block_flow = Block_Control_Flow::BREAK;
                    break;
                }
                case AST_Node_Type::STATEMENT_CONTINUE:
                {
                    if (!code_workload->active_block->inside_loop) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_CONTINUE_NOT_INSIDE_LOOP;
                        error.error_node = statement_node;
                        semantic_analyser_log_error(analyser, error);
                    }
                    workload_code_block_work_through_defers(analyser, code_workload, Defer_Resolve_Depth::LOOP_EXIT);

                    ModTree_Statement* break_stmt = new ModTree_Statement;
                    break_stmt->type = ModTree_Statement_Type::CONTINUE;
                    dynamic_array_push_back(&block->statements, break_stmt);
                    code_workload->active_block->block_flow = Block_Control_Flow::CONTINUE;
                    break;
                }
                case AST_Node_Type::STATEMENT_DEFER:
                {
                    if (code_workload->active_block->inside_defer) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS;
                        error.error_node = statement_node;
                        semantic_analyser_log_error(analyser, error);
                    }
                    else {
                        dynamic_array_push_back(&code_workload->defer_nodes, statement_node->child_start);
                    }
                    break;
                }
                case AST_Node_Type::STATEMENT_EXPRESSION:
                {
                    AST_Node* expression_node = statement_node->child_start;
                    if (expression_node->type != AST_Node_Type::EXPRESSION_FUNCTION_CALL) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL;
                        error.error_node = statement_node;
                        semantic_analyser_log_error(analyser, error);
                        break;
                    }
                    Expression_Analysis_Result result = semantic_analyser_analyse_expression(
                        analyser, block->symbol_table, statement_node->child_start
                    );
                    switch (result.type)
                    {
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        break;
                    case Analysis_Result_Type::SUCCESS: {
                        ModTree_Statement* expr_stmt = new ModTree_Statement;
                        expr_stmt->type = ModTree_Statement_Type::EXPRESSION;
                        expr_stmt->options.expression = result.options.expression;
                        dynamic_array_push_back(&block->statements, expr_stmt);
                        break;
                    }
                    default: panic("HEY");
                    }
                    break;
                }
                case AST_Node_Type::STATEMENT_BLOCK:
                {
                    ModTree_Statement* block_stmt = new ModTree_Statement;
                    block_stmt->type = ModTree_Statement_Type::BLOCK;
                    block_stmt->options.block = modtree_block_create_block_child(
                        symbol_table_create(analyser, symbol_table, statement_node), // TODO: Workloads needs template info, so we dont overwrite mapping
                        block, Block_Type::ANONYMOUS_BLOCK
                    );
                    dynamic_array_push_back(&block->statements, block_stmt);
                    analysis_workload_code_block_add_block(code_workload, block_stmt->options.block, statement_node);
                    break;
                }
                case AST_Node_Type::STATEMENT_IF:
                case AST_Node_Type::STATEMENT_IF_ELSE:
                {
                    ModTree_Expression* condition = 0;
                    Expression_Analysis_Result expression_result = semantic_analyser_analyse_expression(analyser, symbol_table, statement_node->child_start);
                    switch (expression_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        if (expression_result.options.expression->result_type != analyser->compiler->type_system.bool_type) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_IF_CONDITION;
                            error.error_node = statement_node->child_start;
                            error.given_type = expression_result.options.expression->result_type;
                            semantic_analyser_log_error(analyser, error);
                        }
                        condition = expression_result.options.expression;
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = expression_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        condition = modtree_expression_create_constant_i32(analyser, 0);
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency) {
                        break;
                    }

                    ModTree_Statement* if_statement = new ModTree_Statement;
                    if_statement->type = ModTree_Statement_Type::IF;
                    if_statement->options.if_statement.condition = condition;
                    dynamic_array_push_back(&block->statements, if_statement);

                    AST_Node* if_block = statement_node->child_start->neighbor;
                    AST_Node* else_block = if_block->neighbor;
                    if_statement->options.if_statement.if_block = modtree_block_create_block_child(
                        symbol_table_create(analyser, symbol_table, if_block), // TODO: Workloads needs template info, so we dont overwrite mapping
                        block, Block_Type::IF_TRUE_BLOCK
                    );
                    if_statement->options.if_statement.else_block = modtree_block_create_block_child(
                        else_block == 0 ? symbol_table : symbol_table_create(analyser, symbol_table, else_block), // TODO: Workloads needs template info, so we dont overwrite mapping
                        block, Block_Type::IF_ELSE_BLOCK
                    );

                    analysis_workload_code_block_add_block(code_workload, if_statement->options.if_statement.if_block, if_block);
                    if (else_block != 0) {
                        analysis_workload_code_block_add_block(code_workload, if_statement->options.if_statement.else_block, else_block);
                    }
                    break;
                }
                case AST_Node_Type::SWITCH_CASE:
                case AST_Node_Type::SWITCH_DEFAULT_CASE:
                {
                    ModTree_Statement* switch_statement = *hashtable_find_element(&code_workload->active_switches, statement_node->parent);
                    Type_Signature* switch_type = switch_statement->options.switch_statement.condition->result_type;

                    bool is_default_case = statement_node->type == AST_Node_Type::SWITCH_DEFAULT_CASE;
                    ModTree_Expression* case_expr = 0;
                    int case_value = 0;
                    if (!is_default_case)
                    {
                        Expression_Analysis_Result result = semantic_analyser_analyse_expression(analyser, symbol_table, statement_node->child_start);
                        switch (result.type)
                        {
                            case Analysis_Result_Type::SUCCESS:
                                case_expr = result.options.expression;
                                if (switch_type->type == Signature_Type::ENUM && case_expr->result_type != switch_type)
                                {
                                    Semantic_Error error;
                                    error.type = Semantic_Error_Type::SWITCH_CASE_TYPE_INVALID;
                                    error.given_type = case_expr->result_type;
                                    error.expected_type = switch_type;
                                    error.error_node = statement_node;
                                    semantic_analyser_log_error(analyser, error);
                                }
                                else 
                                {
                                    if (case_expr->value.data == 0) {
                                        Semantic_Error error;
                                        error.type = Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN;
                                        error.error_node = statement_node->child_start;
                                        semantic_analyser_log_error(analyser, error);
                                    }
                                    else {
                                        case_value = *(int*)case_expr->value.data;
                                    }
                                }
                                break;
                            case Analysis_Result_Type::DEPENDENCY:
                                found_workload_dependency = true;
                                found_dependency = result.options.dependency;
                                break;
                            case Analysis_Result_Type::ERROR_OCCURED:
                                case_expr = modtree_expression_create_constant_i32(analyser, 0);
                                case_value = 0;
                                break;
                        }
                        if (found_workload_dependency) break;

                        // Check if case is unique
                        for (int i = 0; i < switch_statement->options.switch_statement.cases.size; i++)
                        {
                            int value = switch_statement->options.switch_statement.cases[i].value;
                            if (value == case_value) {
                                Semantic_Error error;
                                error.type = Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE;
                                error.error_node = statement_node;
                                semantic_analyser_log_error(analyser, error);
                                break;
                            }
                        }
                    }
                    else if (switch_statement->options.switch_statement.default_block != 0) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED;
                        error.error_node = statement_node;
                        semantic_analyser_log_error(analyser, error);
                        is_default_case = false;
                        case_expr = modtree_expression_create_constant_i32(analyser, 0);
                        case_value = 0;
                    }

                    ModTree_Block* case_body = modtree_block_create_block_child(
                        symbol_table_create(analyser, symbol_table, 0), block, is_default_case ? Block_Type::SWITCH_DEFAULT_CASE : Block_Type::SWITCH_CASE
                    );

                    if (is_default_case) {
                        switch_statement->options.switch_statement.default_block = case_body;
                    }
                    else {
                        ModTree_Switch_Case new_case;
                        new_case.body = case_body;
                        new_case.value = case_value;
                        new_case.expression = case_expr;
                        dynamic_array_push_back(&switch_statement->options.switch_statement.cases, new_case);
                    }

                    auto_switch_node = false;
                    if (statement_node->neighbor != 0) {
                        code_workload->active_block->current_statement_node = statement_node->neighbor;
                    }
                    else
                    {
                        // Finish up switch
                        code_workload->active_block->current_statement_node = statement_node->parent->neighbor;
                        hashtable_remove_element(&code_workload->active_switches, statement_node->parent);

                        // Check if all enum cases are handled
                        if (switch_statement->options.switch_statement.default_block == 0 &&
                            switch_statement->options.switch_statement.condition->result_type->type == Signature_Type::ENUM) 
                        {
                            if (switch_statement->options.switch_statement.cases.size < switch_type->options.enum_type.members.size) {
                                Semantic_Error error;
                                error.type = Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES;
                                error.error_node = statement_node->parent;
                                semantic_analyser_log_error(analyser, error);
                            }
                        }
                    }

                    analysis_workload_code_block_add_block(code_workload, case_body, is_default_case ? statement_node->child_start : statement_node->child_end);
                    break;
                }
                case AST_Node_Type::STATEMENT_SWITCH:
                {
                    Expression_Analysis_Result result = semantic_analyser_analyse_expression(analyser, symbol_table, statement_node->child_start);
                    ModTree_Expression* expression = 0;
                    switch (result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                    {
                        expression = result.options.expression;
                        if (expression->result_type->type != Signature_Type::ENUM)
                        {
                            Semantic_Error error;
                            error.error_node = statement_node->child_start;
                            error.given_type = expression->result_type;
                            error.type = Semantic_Error_Type::SWITCH_REQUIRES_ENUM;
                            semantic_analyser_log_error(analyser, error);
                        }
                        break;
                    }
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        expression = modtree_expression_create_constant_i32(analyser, 0);
                        break;
                    }
                    if (found_workload_dependency) {
                        break;
                    }

                    ModTree_Statement* switch_statement = new ModTree_Statement;
                    switch_statement->type = ModTree_Statement_Type::SWITCH;
                    switch_statement->options.switch_statement.cases = dynamic_array_create_empty<ModTree_Switch_Case>(statement_node->child_count);
                    switch_statement->options.switch_statement.condition = expression;
                    switch_statement->options.switch_statement.default_block = 0;
                    dynamic_array_push_back(&block->statements, switch_statement);
                    hashtable_insert_element(&code_workload->active_switches, statement_node, switch_statement);

                    if (statement_node->child_count == 1) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::SWITCH_MUST_NOT_BE_EMPTY;
                        error.error_node = statement_node;
                        semantic_analyser_log_error(analyser, error);
                    }
                    else {
                        auto_switch_node = false;
                        code_workload->active_block->current_statement_node = statement_node->child_start->neighbor;
                    }
                    break;
                }
                case AST_Node_Type::STATEMENT_WHILE:
                {
                    ModTree_Expression* condition_expr = 0;
                    Expression_Analysis_Result expression_result = semantic_analyser_analyse_expression(
                        analyser, symbol_table, statement_node->child_start
                    );
                    switch (expression_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        if (expression_result.options.expression->result_type != analyser->compiler->type_system.bool_type) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION;
                            error.error_node = statement_node->child_start;
                            error.given_type = expression_result.options.expression->result_type;
                            semantic_analyser_log_error(analyser, error);
                        }
                        condition_expr = expression_result.options.expression;
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = expression_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        condition_expr = modtree_expression_create_constant_i32(analyser, 0);
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency) {
                        break;
                    }

                    ModTree_Statement* while_statement = new ModTree_Statement;
                    while_statement->type = ModTree_Statement_Type::WHILE;
                    while_statement->options.while_statement.condition = condition_expr;
                    dynamic_array_push_back(&block->statements, while_statement);

                    AST_Node* body_node = statement_node->child_end;
                    while_statement->options.while_statement.while_block = modtree_block_create_block_child(
                        symbol_table_create(analyser, symbol_table, body_node),
                        block, Block_Type::WHILE_BODY
                    );

                    analysis_workload_code_block_add_block(code_workload, while_statement->options.while_statement.while_block, body_node);
                    break;
                }
                case AST_Node_Type::STATEMENT_DELETE:
                {
                    Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(analyser, symbol_table, statement_node->child_start);
                    bool error_occured = false;
                    Type_Signature* delete_type;
                    switch (expr_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                    {
                        delete_type = expr_result.options.expression->result_type;
                        if (delete_type->type != Signature_Type::POINTER && delete_type->type != Signature_Type::SLICE) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_DELETE;
                            error.error_node = statement_node;
                            error.given_type = delete_type;
                            semantic_analyser_log_error(analyser, error);
                            error_occured = true;
                        }
                        break;
                    }
                    break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = expr_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        error_occured = true;
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency || error_occured) {
                        break;
                    }


                    ModTree_Expression* call_expr = new ModTree_Expression;
                    call_expr->expression_type = ModTree_Expression_Type::FUNCTION_CALL;
                    call_expr->value = expression_value_make_unknown();
                    call_expr->result_type = analyser->compiler->type_system.void_type;
                    call_expr->options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(1);
                    call_expr->options.function_call.is_pointer_call = false;
                    call_expr->options.function_call.function = analyser->free_function;

                    ModTree_Statement* expr_statement = new ModTree_Statement;
                    expr_statement->type = ModTree_Statement_Type::EXPRESSION;
                    expr_statement->options.expression = call_expr;
                    dynamic_array_push_back(&block->statements, expr_statement);

                    // Add argument
                    if (delete_type->type == Signature_Type::SLICE)
                    {
                        ModTree_Expression* data_member_access = new ModTree_Expression;
                        data_member_access->expression_type = ModTree_Expression_Type::MEMBER_ACCESS;
                        data_member_access->value = expression_value_make_unknown();
                        data_member_access->result_type = delete_type->options.slice.data_member.type;
                        data_member_access->options.member_access.structure_expression = expr_result.options.expression;
                        data_member_access->options.member_access.member = delete_type->options.slice.data_member;
                        dynamic_array_push_back(&call_expr->options.function_call.arguments, data_member_access);
                    }
                    else {
                        dynamic_array_push_back(&call_expr->options.function_call.arguments, expr_result.options.expression);
                    }
                    break;
                }
                case AST_Node_Type::STATEMENT_ASSIGNMENT:
                {
                    bool error_occured = false;
                    bool ignorable_error_occured = false;
                    int error_count = analyser->errors.size;
                    Expression_Analysis_Result left_result = semantic_analyser_analyse_expression(analyser, symbol_table, statement_node->child_start);
                    if (analyser->errors.size != error_count) ignorable_error_occured = true;
                    Type_Signature* left_type;
                    switch (left_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        left_type = left_result.options.expression->result_type;
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = left_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        error_occured = true;
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency || error_occured) {
                        break;
                    }

                    error_count = analyser->errors.size;
                    Expression_Analysis_Result right_result = semantic_analyser_analyse_expression(
                        analyser, symbol_table, statement_node->child_start->neighbor
                    );
                    if (analyser->errors.size != error_count) ignorable_error_occured = true;
                    Type_Signature* right_type = 0;
                    switch (right_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        right_type = right_result.options.expression->result_type;
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = right_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        error_occured = true;
                        break;
                    default: panic("what");
                    }
                    if (error_occured || found_workload_dependency) {
                        modtree_expression_destroy(left_result.options.expression);
                        break;
                    }

                    if (right_type == analyser->compiler->type_system.void_type) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
                        error.error_node = statement_node->child_start;
                        semantic_analyser_log_error(analyser, error);
                        ignorable_error_occured = true;
                    }
                    if (modtree_expression_result_is_temporary(left_result.options.expression)) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS;
                        error.error_node = statement_node;
                        semantic_analyser_log_error(analyser, error);
                        ignorable_error_occured = true;
                    }
                    if (left_type != right_type)
                    {
                        ModTree_Expression* cast_expr = semantic_analyser_cast_implicit_if_possible(analyser, right_result.options.expression, left_type);
                        if (cast_expr == 0) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT;
                            error.error_node = statement_node;
                            error.given_type = right_type;
                            error.expected_type = left_type;
                            semantic_analyser_log_error(analyser, error);
                            ignorable_error_occured = true;
                        }
                        else {
                            right_result.options.expression = cast_expr;
                        }
                    }
                    if (!ignorable_error_occured) {
                        constant_analysis_assign_value(left_result.options.expression, right_result.options.expression, block);
                    }
                    ModTree_Statement* assign_statement = new ModTree_Statement;
                    assign_statement->type = ModTree_Statement_Type::ASSIGNMENT;
                    assign_statement->options.assignment.destination = left_result.options.expression;
                    assign_statement->options.assignment.source = right_result.options.expression;
                    dynamic_array_push_back(&block->statements, assign_statement);
                    break;
                }
                case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
                case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
                case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: {
                    ModTree_Variable_Origin origin;
                    origin.type = ModTree_Variable_Origin_Type::LOCAL;
                    origin.options.local_block = block;
                    Variable_Creation_Analysis_Result result = semantic_analyser_analyse_variable_creation_node(
                        analyser, symbol_table, statement_node, origin
                    );
                    if (result.type == Analysis_Result_Type::DEPENDENCY) {
                        found_workload_dependency = true;
                        found_dependency = result.options.dependency;
                    }
                    break;
                }
                default: {
                    panic("Should be covered!\n");
                    break;
                }
                }
            }

            if (found_workload_dependency) {
                break; // Will be added to waiting queue outside this thing
            }

            break;
        }
        case Analysis_Workload_Type::ENUM_BODY:
        {
            Analysis_Workload_Enum* work = &workload.options.enum_body;
            Symbol_Table* symbol_table = work->symbol_table;
            assert(work->enum_type->size == 0 && work->enum_type->alignment == 0, "Already analysed");

            while (work->current_member != 0)
            {
                int member_value = 0;
                if (work->current_member->child_count != 0)
                {
                    Expression_Analysis_Result result = semantic_analyser_analyse_expression(analyser, symbol_table, work->current_member->child_start);
                    bool error_occured = false;
                    switch (result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        if (result.options.expression->result_type != analyser->compiler->type_system.i32_type)
                        {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_ENUM_VALUE;
                            error.error_node = work->current_member;
                            error.given_type = result.options.expression->result_type;
                            error.expected_type = analyser->compiler->type_system.i32_type;
                            semantic_analyser_log_error(analyser, error);
                            error_occured = true;
                        }
                        else if (result.options.expression->value.data == 0) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN;
                            error.error_node = work->current_member->child_start;
                            semantic_analyser_log_error(analyser, error);
                            error_occured = true;
                        }
                        else {
                            member_value = *(i32*)result.options.expression->value.data;
                        }
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        error_occured = true;
                        break;
                    }
                    if (found_workload_dependency) break;

                    if (error_occured) {
                        member_value = work->next_integer_value;
                        work->next_integer_value++;
                        break;
                    }
                }
                else {
                    member_value = work->next_integer_value;
                    work->next_integer_value++;
                }

                Enum_Member member;
                member.id = work->current_member->id;
                member.value = member_value;

                for (int i = 0; i < work->enum_type->options.enum_type.members.size; i++)
                {
                    if (work->enum_type->options.enum_type.members[i].id == member.id) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE;
                        error.error_node = work->current_member;
                        semantic_analyser_log_error(analyser, error);
                    }
                    if (work->enum_type->options.enum_type.members[i].value == member.value) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE;
                        error.error_node = work->current_member;
                        semantic_analyser_log_error(analyser, error);
                    }
                }

                dynamic_array_push_back(&work->enum_type->options.enum_type.members, member);
                work->current_member = work->current_member->neighbor;
            }
            if (found_workload_dependency) break;

            work->enum_type->size = 4;
            work->enum_type->alignment = 4;
            break;
        }
        case Analysis_Workload_Type::STRUCT_BODY:
        {
            Analysis_Workload_Struct_Body* work = &workload.options.struct_body;
            Symbol_Table* symbol_table = work->symbol_table;
            Type_Signature* struct_signature = work->struct_signature;
            if (struct_signature->size != 0 || struct_signature->alignment != 0) {
                panic("Already analysed!");
            }

            AST_Node* member_node = work->current_member_node;
            while (member_node != 0)
            {
                work->current_member_node = member_node;
                SCOPE_EXIT(member_node = member_node->neighbor);
                Type_Analysis_Result member_result = semantic_analyser_analyse_type(analyser, symbol_table, member_node->child_start, Usage_Type::MEMBER_TYPE);
                Type_Signature* member_type = 0;
                switch (member_result.type)
                {
                case Analysis_Result_Type::SUCCESS:
                    member_type = member_result.options.result_type;
                    if (member_type->alignment == 0 && member_type->size == 0) {
                        found_workload_dependency = true;
                        found_dependency = workload_dependency_make_type_size_unknown(member_type, member_node);
                    }
                    break;
                case Analysis_Result_Type::DEPENDENCY:
                    found_workload_dependency = true;
                    found_dependency = member_result.options.dependency;
                    break;
                case Analysis_Result_Type::ERROR_OCCURED:
                    member_type = analyser->compiler->type_system.error_type;
                    break;
                default: panic("HEY");
                }
                if (found_workload_dependency) {
                    break;
                }
                work->alignment = math_maximum(work->alignment, member_type->alignment);
                if (!struct_signature->options.structure.is_union) {
                    work->offset = math_round_next_multiple(work->offset, member_type->alignment);
                }

                for (int j = 0; j < struct_signature->options.structure.members.size; j++) {
                    if (struct_signature->options.structure.members[j].id == member_node->id) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED;
                        error.id = member_node->id;
                        error.error_node = member_node;
                        semantic_analyser_log_error(analyser, error);
                    }
                }
                Struct_Member member;
                member.id = member_node->id;
                member.offset = workload.options.struct_body.offset;
                if (struct_signature->options.structure.is_union) {
                    member.offset = 0;
                }
                member.type = member_type;
                dynamic_array_push_back(&struct_signature->options.structure.members, member);

                if (struct_signature->options.structure.is_union) {
                    work->offset = math_maximum(work->offset, member_type->size);
                }
                else {
                    work->offset += member.type->size;
                }
            }

            if (found_workload_dependency) {
                break;
            }

            struct_signature->size = math_round_next_multiple(workload.options.struct_body.offset, workload.options.struct_body.alignment);
            struct_signature->alignment = workload.options.struct_body.alignment;
            if (struct_signature->options.structure.is_union) {
                struct_signature->size = work->offset;
            }

            break;
        }
        default: panic("Hey");
        }

        // Check if dependencies were found
        if (found_workload_dependency)
        {
            Waiting_Workload waiting;
            waiting.workload = workload;
            waiting.dependency = found_dependency;
            dynamic_array_push_back(&analyser->waiting_workload, waiting);

            if (PRINT_DEPENDENCIES)
            {
                String output = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&output));
                string_append_formated(&output, "DEPENDENCY: ");
                workload_dependency_append_to_string(&waiting.dependency, &output, analyser);
                string_append_formated(&output, "   |||   Workload: ");
                analysis_workload_append_to_string(&workload, &output, analyser);
                string_append_formated(&output, "\n");
                logg(output.characters);
            }
        }
        else
        {
            // Workload finished
            if (PRINT_DEPENDENCIES)
            {
                String output = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&output));
                string_append_formated(&output, "FINISHED: ");
                analysis_workload_append_to_string(&workload, &output, analyser);
                string_append_formated(&output, "\n");
                logg(output.characters);
            }

            // Destroy Workload
            analysis_workload_destroy(&workload);
        }

        // Check if waiting workloads could be activated
        if (analyser->active_workloads.size == 0)
        {
            // TODO: We would probably want a message system at some point, which resolves these things automatically
            for (int i = 0; i < analyser->waiting_workload.size; i++)
            {
                Waiting_Workload* waiting = &analyser->waiting_workload[i];
                bool dependency_resolved = false;
                bool error_occured = false;
                // Check if dependency is resolved
                switch (waiting->dependency.type)
                {
                case Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED: {
                    Block_Control_Flow* result = hashtable_find_element(&analyser->finished_code_blocks, waiting->dependency.options.code_block);
                    dependency_resolved = result != 0;
                    break;
                }
                case Workload_Dependency_Type::IDENTIFER_NOT_FOUND:
                {
                    Identifier_Analysis_Result result = semantic_analyser_analyse_identifier_node(
                        analyser,
                        waiting->dependency.options.identifier_not_found.symbol_table,
                        waiting->dependency.node, waiting->dependency.options.identifier_not_found.current_scope_only,
                        Usage_Type::IGNORE_REFERENCE
                    );
                    switch (result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        dependency_resolved = true;
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        waiting->dependency = result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        dependency_resolved = true;
                        error_occured = true;
                        break;
                    }
                    break;
                }
                case Workload_Dependency_Type::TYPE_SIZE_UNKNOWN: {
                    if (waiting->dependency.options.type_signature->alignment != 0 &&
                        waiting->dependency.options.type_signature->size != 0) {
                        dependency_resolved = true;
                    }
                    break;
                }
                case Workload_Dependency_Type::FUNCTION_HEADER_NOT_ANALYSED: {
                    dependency_resolved = waiting->dependency.options.function_header_not_analysed->signature != 0;
                    break;
                }
                default: panic("Hey"); break;
                }

                if (dependency_resolved)
                {
                    if (PRINT_DEPENDENCIES && !error_occured)
                    {
                        String output = string_create_empty(256);
                        SCOPE_EXIT(string_destroy(&output));
                        string_append_formated(&output, "RESOLVED: ");
                        workload_dependency_append_to_string(&waiting->dependency, &output, analyser);
                        string_append_formated(&output, "   |||   Workload: ");
                        analysis_workload_append_to_string(&waiting->workload, &output, analyser);
                        string_append_formated(&output, "\n");
                        logg(output.characters);
                    }

                    if (!error_occured)
                    {
                        dynamic_array_push_back(&analyser->active_workloads, waiting->workload);
                        //workload_dependency_destroy(&waiting->dependency);
                    }
                    dynamic_array_swap_remove(&analyser->waiting_workload, i);
                    i = i - 1;
                }
            }
        }
    }

    // Add return for global init function
    {
    ModTree_Statement* return_stmt = new ModTree_Statement;
    return_stmt->type = ModTree_Statement_Type::RETURN;
    return_stmt->options.return_value.available = false;
    dynamic_array_push_back(&analyser->global_init_function->options.function.body->statements, return_stmt);
    }

    if (analyser->program->entry_function == 0) {
        Semantic_Error error;
        error.type = Semantic_Error_Type::OTHERS_MAIN_NOT_DEFINED;
        error.error_node = 0;
        semantic_analyser_log_error(analyser, error);
    }

    // Log unresolved dependency errors
    if (analyser->errors.size == 0 && analyser->waiting_workload.size != 0)
    {
        for (int i = 0; i < analyser->waiting_workload.size; i++)
        {
            Workload_Dependency* dependency = &analyser->waiting_workload[i].dependency;
            Semantic_Error error;
            error.error_node = dependency->node;
            switch (dependency->type)
            {
            case Workload_Dependency_Type::FUNCTION_HEADER_NOT_ANALYSED: {
                error.type = Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_FUNCTION_HEADER;
                break;
            }
            case Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED: {
                error.type = Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK;
                break;
            }
            case Workload_Dependency_Type::IDENTIFER_NOT_FOUND:
                error.type = Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL;
                break;
            case Workload_Dependency_Type::TYPE_SIZE_UNKNOWN:
                error.type = Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE;
                break;
            default: panic("Hey"); break;
            }
            semantic_analyser_log_error(analyser, error);
            analysis_workload_destroy(&analyser->waiting_workload[i].workload);
        }
        dynamic_array_reset(&analyser->waiting_workload);
        dynamic_array_reset(&analyser->active_workloads);
    }
}

u64 hash_bake_location(Bake_Location* location)
{
    return hash_memory(array_create_static<byte>((byte*)location, sizeof(location)));
}

bool equals_bake_location(Bake_Location* loc1, Bake_Location* loc2)
{
    return loc1->block == loc2->block && loc1->node == loc2->node;
}

Semantic_Analyser semantic_analyser_create()
{
    Semantic_Analyser result;
    result.symbol_tables = dynamic_array_create_empty<Symbol_Table*>(64);
    result.active_workloads = dynamic_array_create_empty<Analysis_Workload>(64);
    result.waiting_workload = dynamic_array_create_empty<Waiting_Workload>(64);
    result.errors = dynamic_array_create_empty<Semantic_Error>(64);
    result.allocator_values = stack_allocator_create_empty(2048);
    result.finished_code_blocks = hashtable_create_pointer_empty<ModTree_Block*, Block_Control_Flow>(64);
    result.ast_to_symbol_table = hashtable_create_pointer_empty<AST_Node*, Symbol_Table*>(256);
    result.loaded_filenames = hashset_create_pointer_empty<String*>(32);
    result.visited_functions = hashset_create_pointer_empty<ModTree_Function*>(32);
    result.bake_locations = hashtable_create_empty<Bake_Location, ModTree_Function*>(32, hash_bake_location, equals_bake_location);
    result.program = 0;
    result.current_workload = 0;

    return result;
}

void semantic_analyser_destroy(Semantic_Analyser* analyser)
{
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        symbol_table_destroy(analyser->symbol_tables[i]);
    }
    dynamic_array_destroy(&analyser->symbol_tables);
    stack_allocator_destroy(&analyser->allocator_values);
    dynamic_array_destroy(&analyser->errors);
    dynamic_array_destroy(&analyser->active_workloads);
    dynamic_array_destroy(&analyser->waiting_workload);
    hashtable_destroy(&analyser->ast_to_symbol_table);
    hashtable_destroy(&analyser->finished_code_blocks);
    hashtable_destroy(&analyser->bake_locations);
    hashset_destroy(&analyser->loaded_filenames);
    hashset_destroy(&analyser->visited_functions);

    if (analyser->program != 0) {
        modtree_program_destroy(analyser->program);
    }
}

void analysis_workload_append_to_string(Analysis_Workload* workload, String* string, Semantic_Analyser* analyser)
{
    switch (workload->type)
    {
    case Analysis_Workload_Type::CODE: {
        string_append_formated(string, "Code_Block");
        break;
    }
    case Analysis_Workload_Type::FUNCTION_HEADER: {
        string_append_formated(string, "Function Header, name: %s", workload->options.function_header.function_node->id->characters
        );
        break;
    }
    case Analysis_Workload_Type::MODULE_ANALYSIS: {
        string_append_formated(string, "Module_Analysis");
        break;
    }
    case Analysis_Workload_Type::GLOBAL: {
        string_append_formated(string, "Global Variable, name: %s", workload->options.global.node->id->characters);
        break;
    }
    case Analysis_Workload_Type::ARRAY_SIZE: {
        string_append_formated(string, "Sized Array");
        break;
    }
    case Analysis_Workload_Type::EXTERN_HEADER_IMPORT: {
        string_append_formated(string, "Extern header import, name: %s", workload->options.extern_header.node->id->characters);
        break;
    }
    case Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION: {
        string_append_formated(string, "Extern function declaration, name: %s", workload->options.extern_function.node->id->characters);
        break;
    }
    case Analysis_Workload_Type::STRUCT_BODY: {
        string_append_formated(string, "Struct Body, name: %s", workload->options.struct_body.current_member_node->parent->id->characters);
        break;
    }
    case Analysis_Workload_Type::ENUM_BODY: {
        string_append_formated(string, "Struct Body, name: %s", workload->options.enum_body.current_member->parent->id->characters);
        break;
    }
    default: panic("Hey");
    }
}

void semantic_error_get_error_location(Semantic_Analyser* analyser, Semantic_Error error, Dynamic_Array<Token_Range>* locations)
{
    if (error.error_node == 0) return;
    {
        Code_Source* source = compiler_ast_node_to_code_source(analyser->compiler, error.error_node);
        switch (source->origin.type) {
        case Code_Origin_Type::GENERATED:
            return;
        case Code_Origin_Type::LOADED_FILE:
            dynamic_array_push_back(locations, source->origin.load_node->token_range);
            return;
        case Code_Origin_Type::MAIN_PROJECT:
            break;
        default: panic("HEY");
        }
    }

    switch (error.type)
    {
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT:
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE: {
        AST_Node* identifier_node = error.error_node;
        assert(identifier_node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED || identifier_node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED, "What");
        Token_Range unnamed_block_range = identifier_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(unnamed_block_range.start_index, unnamed_block_range.start_index + 1));
        dynamic_array_push_back(locations, token_range_make(unnamed_block_range.end_index - 1, unnamed_block_range.end_index));
        break;
    }
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SWITCH_REQUIRES_ENUM: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SWITCH_MUST_NOT_BE_EMPTY:
    case Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES: {
        dynamic_array_push_back(locations,
            token_range_make(error.error_node->child_start->token_range.end_index, error.error_node->child_start->token_range.end_index + 1)
        );
        dynamic_array_push_back(locations,
            token_range_make(error.error_node->token_range.end_index - 1, error.error_node->token_range.end_index)
        );
        break;
    }
    case Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SWITCH_CASE_TYPE_INVALID: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE: {
        dynamic_array_push_back(locations, error.error_node->child_start->token_range);
        break;
    }
    case Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED: {
        Token_Range extern_header_node = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(extern_header_node.start_index + 1, extern_header_node.start_index + 2));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_VOID_USAGE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARGUMENT: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        dynamic_array_push_back(locations, token_range_make(
            error.error_node->child_start->token_range.end_index, error.error_node->child_start->token_range.end_index + 1
        ));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS: {
        AST_Node* member_access_node = error.error_node;
        assert(member_access_node->type == AST_Node_Type::EXPRESSION_MEMBER_ACCESS, "What");
        Token_Range range = member_access_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index + 1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_IF_CONDITION: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR: {
        AST_Node* binary_op_node = error.error_node;
        assert(ast_node_type_is_binary_expression(binary_op_node->type), "HEY");
        Token_Range range = binary_op_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index + 1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT: {
        AST_Node* assign_node = error.error_node;
        assert(assign_node->type == AST_Node_Type::STATEMENT_ASSIGNMENT ||
            assign_node->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN ||
            assign_node->type == AST_Node_Type::NAMED_PARAMETER, "hey");
        Token_Range range = assign_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index + 1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_RETURN: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_DELETE: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SYMBOL_EXPECTED_VARIABLE_OR_FUNCTION_ON_VARIABLE_READ: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_BAKE_MUST_BE_PRIMITIVE:
    {
        Token_Range range = error.error_node->child_start->token_range;
        range.start_index -= 1;
        range.end_index += 1;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 2));
        break;
    }
    case Semantic_Error_Type::BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 2));
        break;
    }
    case Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH: {
        AST_Node* expression_node = error.error_node;
        assert(expression_node->type == AST_Node_Type::EXPRESSION_FUNCTION_CALL, "What");
        Token_Range arguments_range = expression_node->child_start->neighbor->token_range;
        dynamic_array_push_back(locations, token_range_make(arguments_range.start_index, arguments_range.start_index + 1));
        dynamic_array_push_back(locations, token_range_make(arguments_range.end_index - 1, arguments_range.end_index));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_INVALID_CAST: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND: {
        AST_Node* member_access_node = error.error_node;
        assert(member_access_node->type == AST_Node_Type::EXPRESSION_MEMBER_ACCESS, "What");
        Token_Range range = member_access_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index + 1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH: {
        AST_Node* binary_op_node = error.error_node;
        assert(ast_node_type_is_binary_expression(binary_op_node->type), "HEY");
        Token_Range range = binary_op_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index + 1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::OTHERS_STRUCT_MUST_CONTAIN_MEMBER: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE:
    case Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS:
    case Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_BREAK_NOT_INSIDE_LOOP: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_CONTINUE_NOT_INSIDE_LOOP: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.end_index - 1, range.end_index));
        break;
    }
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_FUNCTION_HEADER: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::OTHERS_MAIN_CANNOT_BE_TEMPLATED: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_MAIN_NOT_DEFINED: {
        break;
    }
    case Semantic_Error_Type::OTHERS_MAIN_UNEXPECTED_SIGNATURE: {
        break;
    }
    case Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION:
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    case Semantic_Error_Type::OTHERS_NO_CALLING_TO_MAIN: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_TEMPLATED_GLOBALS: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::OTHERS_ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::OTHERS_ARRAY_SIZE_MUST_BE_GREATER_ZERO: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE: {
        if (error.error_node != 0) {
            Token_Range range = error.error_node->token_range;
            dynamic_array_push_back(locations, range);
        }
        break;
    }
    case Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE: {
        Token_Range range = error.error_node->token_range;
        range.end_index = range.start_index + 1;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN: {
        Token_Range range = error.error_node->token_range;
        range.end_index = range.start_index + 1;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER: {
        Token_Range range = error.error_node->token_range;
        range.end_index = range.start_index + 1;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE: {
        Token_Range range = error.error_node->token_range;
        range.end_index = range.start_index + 1;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    default: panic("hey");
    }
}

void semantic_error_append_to_string(Semantic_Analyser* analyser, Semantic_Error e, String* string)
{
    bool print_symbol = false;
    bool print_given_type = false;
    bool print_expected_type = false;
    bool print_function_type = false;
    bool print_binary_type = false;
    bool print_required_argument_count = false;
    bool print_name_id = false;
    bool print_struct_members = false;
    bool print_identifier_node = false;
    bool print_member_access_name_id = false;

    int rollback_index = analyser->errors.size;
    SCOPE_EXIT(dynamic_array_rollback_to_size(&analyser->errors, rollback_index));

    switch (e.type)
    {
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT:
        string_append_formated(string, "Invalid Template Argument count");
        print_required_argument_count = true;
        print_symbol = true;
        break;
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE:
        string_append_formated(string, "Template arguments invalid, symbol is not templated");
        print_symbol = true;
        break;
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED:
        string_append_formated(string, "Symbol is templated, requires template arguments");
        print_symbol = true;
        break;
    case Semantic_Error_Type::SWITCH_REQUIRES_ENUM:
        string_append_formated(string, "Switch requires enum");
        print_given_type = true;
        break;
    case Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN:
        string_append_formated(string, "Switch case must be compile time known");
        break;
    case Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES:
        string_append_formated(string, "Switch does not handle all cases");
        break;
    case Semantic_Error_Type::SWITCH_MUST_NOT_BE_EMPTY:
        string_append_formated(string, "Switch must not be empty");
        break;
    case Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED:
        string_append_formated(string, "Switch only one default case allowed");
        break;
    case Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE:
        string_append_formated(string, "Switch case must be unique");
        break;
    case Semantic_Error_Type::SWITCH_CASE_TYPE_INVALID:
        string_append_formated(string, "Switch case type must be enum value");
        print_given_type = true;
        print_expected_type = true;
        break;
    case Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL:
        string_append_formated(string, "Extern header does not contain this symbol");
        print_name_id = true;
        break;
    case Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED:
        string_append_formated(string, "Parsing extern header failed");
        break;
    case Semantic_Error_Type::INVALID_TYPE_VOID_USAGE:
        string_append_formated(string, "Invalid use of void type");
        break;
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL:
        string_append_formated(string, "Expected function pointer type on function call");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER:
        string_append_formated(string, "Expected function type on function import");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARGUMENT:
        string_append_formated(string, "Argument type does not match function parameter type");
        print_given_type = true;
        print_expected_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS:
        string_append_formated(string, "Array access only works on array types");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX:
        string_append_formated(string, "Array access index must be of type i32");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE:
        string_append_formated(string, "Array allocation size must be of type i32");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE:
        string_append_formated(string, "Array size must be of type i32");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS:
        string_append_formated(string, "Member access only valid on struct/array or pointer to struct/array types");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_IF_CONDITION:
        string_append_formated(string, "If condition must be boolean");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION:
        string_append_formated(string, "While condition must be boolean");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR:
        string_append_formated(string, "Unary operator type invalid");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR:
        string_append_formated(string, "Binary operator types invalid");
        print_binary_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT:
        string_append_formated(string, "Invalid assignment type");
        print_given_type = true;
        print_expected_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_RETURN:
        string_append_formated(string, "Invalid return type");
        print_given_type = true;
        print_expected_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_DELETE:
        string_append_formated(string, "Only pointer or unsized array types can be deleted");
        print_given_type = true;
        break;
    case Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER:
        string_append_formated(string, "Expected Type symbol");
        print_symbol = true;
        break;
    case Semantic_Error_Type::SYMBOL_EXPECTED_VARIABLE_OR_FUNCTION_ON_VARIABLE_READ:
        string_append_formated(string, "Expected Variable or Function symbol for Variable read");
        print_symbol = true;
        break;
    case Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH:
        string_append_formated(string, "Expected module in indentifier path");
        print_symbol = true;
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL:
        string_append_formated(string, "Could not resolve symbol");
        print_identifier_node = true;
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED:
        string_append_formated(string, "Symbol already defined");
        print_symbol = true;
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED:
        string_append_formated(string, "Module already defined");
        print_name_id = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_BAKE_MUST_BE_PRIMITIVE:
        string_append_formated(string, "Bake type must be primitive currently");
        print_given_type = true;
        break;
    case Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED:
        string_append_formated(string, "Bake error, exit code: ");
        exit_code_append_to_string(string, e.exit_code);
        break;
    case Semantic_Error_Type::BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS:
        string_append_formated(string, "Bake function must not reference globals!");
        break;
    case Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH:
        string_append_formated(string, "Parameter count does not match argument count, expected: %d, given: %d",
            e.invalid_argument_count.expected, e.invalid_argument_count.given
        );
        print_required_argument_count = true;
        print_function_type = true;
        break;
    case Semantic_Error_Type::EXPRESSION_INVALID_CAST:
        string_append_formated(string, "Invalid cast");
        print_expected_type = true;
        print_given_type = true;
        break;
    case Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND:
        string_append_formated(string, "Struct/Array does not contain member");
        print_given_type = true;
        print_member_access_name_id = true;
        break;
    case Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT:
        string_append_formated(string, "Cannot take address, expression result is only temporary");
        break;
    case Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH:
        string_append_formated(string, "Binary op types do not match and cannot be implicitly casted");
        print_binary_type = true;
        break;
    case Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL:
        string_append_formated(string, "Expression does not do anything, must be function call");
        break;
    case Semantic_Error_Type::OTHERS_STRUCT_MUST_CONTAIN_MEMBER:
        string_append_formated(string, "Struct must contain at least one member");
        break;
    case Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED:
        string_append_formated(string, "Struct member is already defined");
        print_name_id = true;
        break;
    case Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE:
        string_append_formated(string, "While loop always exits, never runs more than once");
        break;
    case Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS:
        string_append_formated(string, "While loop always returns, never runs more than once");
        break;
    case Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS:
        string_append_formated(string, "While loop always continues, never stops");
        break;
    case Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE:
        string_append_formated(string, "Unreachable statement");
        break;
    case Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED:
        string_append_formated(string, "No returns allowed inside of defer");
        break;
    case Semantic_Error_Type::OTHERS_BREAK_NOT_INSIDE_LOOP:
        string_append_formated(string, "Break not inside a loop");
        break;
    case Semantic_Error_Type::OTHERS_CONTINUE_NOT_INSIDE_LOOP:
        string_append_formated(string, "Continue not inside a loop");
        break;
    case Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT:
        string_append_formated(string, "Function is missing a return statement");
        break;
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_FUNCTION_HEADER:
        string_append_formated(string, "Unfinished workload function header");
        break;
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK:
        string_append_formated(string, "Unfinished workload code block");
        break;
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE:
        string_append_formated(string, "Unfinished workload type size");
        break;
    case Semantic_Error_Type::OTHERS_MAIN_CANNOT_BE_TEMPLATED:
        string_append_formated(string, "Main function cannot be templated");
        break;
    case Semantic_Error_Type::OTHERS_MAIN_NOT_DEFINED:
        string_append_formated(string, "Main function not found");
        break;
    case Semantic_Error_Type::OTHERS_MAIN_UNEXPECTED_SIGNATURE:
        string_append_formated(string, "Main unexpected signature");
        print_given_type = true;
        print_expected_type = true;
        break;
    case Semantic_Error_Type::OTHERS_NO_CALLING_TO_MAIN:
        string_append_formated(string, "Cannot call main function again");
        break;
    case Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE:
        string_append_formated(string, "Could not load file");
        break;
    case Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION:
        string_append_formated(string, "Cannot take address of hardcoded function");
        break;
    case Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS:
        string_append_formated(string, "Left side of assignment does not have a memory address");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_TEMPLATED_GLOBALS:
        string_append_formated(string, "Templated globals not implemented yet");
        break;
    case Semantic_Error_Type::OTHERS_ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN:
        string_append_formated(string, "Array size not known at compile time");
        break;
    case Semantic_Error_Type::OTHERS_ARRAY_SIZE_MUST_BE_GREATER_ZERO:
        string_append_formated(string, "Array size must be greater zero!");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES:
        string_append_formated(string, "Nested template modules not implemented yet");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES:
        string_append_formated(string, "Extern imports inside templates not allowed");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT:
        string_append_formated(string, "Extern global variable import not implemented yet");
        break;
    case Semantic_Error_Type::MISSING_FEATURE: {
        string_append_formated(string, "Missing feature");
        break;
    }
    case Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE: {
        string_append_formated(string, "Enum member name must be unique");
        break;
    }
    case Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN: {
        string_append_formated(string, "enum value must be compile time known");
        break;
    }
    case Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE: {
        string_append_formated(string, "Enum value must be unique");
        break;
    }
    case Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER: {
        string_append_formated(string, "Enum member does not exist");
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS:
        string_append_formated(string, "Nested defers not implemented yet");
        break;
    default: panic("ERROR");
    }

    if (print_symbol) {
        string_append_formated(string, "\n  Symbol: ");
        symbol_append_to_string(e.symbol, string);
    }
    if (print_given_type) {
        string_append_formated(string, "\n  Given Type:    ");
        type_signature_append_to_string(string, e.given_type);
    }
    if (print_expected_type) {
        string_append_formated(string, "\n  Expected Type: ");
        type_signature_append_to_string(string, e.expected_type);
    }
    if (print_function_type) {
        string_append_formated(string, "\n  Function Type: ");
        type_signature_append_to_string(string, e.function_type);
    }
    if (print_binary_type) {
        string_append_formated(string, "\n  Left Operand type:  ");
        type_signature_append_to_string(string, e.binary_op_left_type);
        string_append_formated(string, "\n  Right Operand type: ");
        type_signature_append_to_string(string, e.binary_op_right_type);
    }
    if (print_required_argument_count) {
        string_append_formated(string, "\n  Given argument count: %d, required: %d", e.invalid_argument_count.given, e.invalid_argument_count.expected);
    }
    if (print_name_id) {
        string_append_formated(string, "\n  Name: %s", e.id->characters);
    }
    if (print_member_access_name_id) {
        AST_Node* node = e.error_node;
        assert(node->type == AST_Node_Type::EXPRESSION_MEMBER_ACCESS, "BAllern");
        string_append_formated(string, "\n  Accessed member name: %s", node->id->characters);
    }
    if (print_struct_members)
    {
        string_append_formated(string, "\n  Available struct members: %s");
        assert(e.given_type->type == Signature_Type::STRUCT, "HEY");
        for (int i = 0; i < e.given_type->options.structure.members.size; i++) {
            Struct_Member* member = &e.given_type->options.structure.members[i];
            string_append_formated(string, "\n\t\t%s", member->id->characters);
        }
    }
}

void identifer_or_path_append_to_string(AST_Node* node, Semantic_Analyser* analyser, String* string)
{
    assert(node->type == AST_Node_Type::IDENTIFIER_NAME ||
        node->type == AST_Node_Type::IDENTIFIER_PATH ||
        node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED ||
        node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED, "hEY");
    while (node->type != AST_Node_Type::IDENTIFIER_NAME && node->type != AST_Node_Type::IDENTIFIER_NAME_TEMPLATED)
    {
        string_append_formated(string, "%s::", node->id->characters);
        if (node->type == AST_Node_Type::IDENTIFIER_PATH) {
            node = node->child_start;
        }
        else {
            node = node->child_start->neighbor;
        }
    }
    if (node->type == AST_Node_Type::IDENTIFIER_NAME) {
        string_append_formated(string, "%s", node->id->characters);
    }
}

void workload_dependency_append_to_string(Workload_Dependency* dependency, String* string, Semantic_Analyser* analyser)
{
    switch (dependency->type)
    {
    case Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED:
        string_append_formated(string, "Code not finished");
        break;
    case Workload_Dependency_Type::IDENTIFER_NOT_FOUND:
        string_append_formated(string, "Identifier not found \"");
        identifer_or_path_append_to_string(dependency->node, analyser, string);
        string_append_formated(string, "\"");
        break;
    case Workload_Dependency_Type::TYPE_SIZE_UNKNOWN:
        string_append_formated(string, "Type size unknown ");
        type_signature_append_to_string(string, dependency->options.type_signature);
        break;
    case Workload_Dependency_Type::FUNCTION_HEADER_NOT_ANALYSED: {
        string_append_formated(string, "Function Header not analysed, ");
        Symbol* s = dependency->options.function_header_not_analysed->symbol;
        assert(s != 0, "Should not happen i think");
        string_append_formated(string, s->id->characters);
        break;
    }
    default: panic("hey");
    }
}

Type_Signature* import_c_type(Semantic_Analyser* analyser, C_Import_Type* type, Hashtable<C_Import_Type*, Type_Signature*>* type_conversions)
{
    {
        Type_Signature** converted = hashtable_find_element(type_conversions, type);
        if (converted != 0) {
            return *converted;
        }
    }
    Type_Signature signature;
    signature.size = type->byte_size;
    signature.alignment = type->alignment;
    Type_Signature* result_type = 0;
    switch (type->type)
    {
    case C_Import_Type_Type::ARRAY: {
        signature.type = Signature_Type::ARRAY;
        signature.options.array.element_count = type->array.array_size;
        signature.options.array.element_type = import_c_type(analyser, type->array.element_type, type_conversions);
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    case C_Import_Type_Type::POINTER: {
        signature.type = Signature_Type::POINTER;
        signature.options.pointer_child = import_c_type(analyser, type->array.element_type, type_conversions);
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    case C_Import_Type_Type::PRIMITIVE: {
        switch (type->primitive)
        {
        case C_Import_Primitive::VOID_TYPE:
            result_type = analyser->compiler->type_system.void_type;
            break;
        case C_Import_Primitive::BOOL:
            result_type = analyser->compiler->type_system.bool_type;
            break;
        case C_Import_Primitive::CHAR: {
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u8_type;
            }
            else {
                result_type = analyser->compiler->type_system.i8_type;
            }
            break;
        }
        case C_Import_Primitive::DOUBLE:
            result_type = analyser->compiler->type_system.f64_type;
            break;
        case C_Import_Primitive::FLOAT:
            result_type = analyser->compiler->type_system.f32_type;
            break;
        case C_Import_Primitive::INT:
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u32_type;
            }
            else {
                result_type = analyser->compiler->type_system.i32_type;
            }
            break;
        case C_Import_Primitive::LONG:
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u32_type;
            }
            else {
                result_type = analyser->compiler->type_system.i32_type;
            }
            break;
        case C_Import_Primitive::LONG_DOUBLE:
            result_type = analyser->compiler->type_system.f64_type;
            break;
        case C_Import_Primitive::LONG_LONG:
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u64_type;
            }
            else {
                result_type = analyser->compiler->type_system.i64_type;
            }
            break;
        case C_Import_Primitive::SHORT:
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u16_type;
            }
            else {
                result_type = analyser->compiler->type_system.i16_type;
            }
            break;
        default: panic("WHAT");
        }
        break;
    }
    case C_Import_Type_Type::ENUM: 
    {
        String* enum_id;
        if (type->enumeration.is_anonymous) {
            enum_id = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("__c_anon_enum"));
        }
        else {
            enum_id = type->enumeration.id;
        }
        result_type = type_system_make_enum_empty(&analyser->compiler->type_system, enum_id);
        result_type->size = type->byte_size;
        result_type->alignment = type->alignment;
        for (int i = 0; i < type->enumeration.members.size; i++) {
            Enum_Member new_member;
            new_member.id = type->enumeration.members[i].id;
            new_member.value = type->enumeration.members[i].value;
            dynamic_array_push_back(&result_type->options.enum_type.members, new_member);
        }
        break;
    }
    case C_Import_Type_Type::ERROR_TYPE: {
        signature.type = Signature_Type::ARRAY;
        signature.options.array.element_type = analyser->compiler->type_system.u8_type;
        signature.options.array.element_count = type->byte_size;
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    case C_Import_Type_Type::STRUCTURE:
    {
        signature.type = Signature_Type::STRUCT;
        if (type->structure.is_anonymous) {
            signature.options.structure.id = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("__c_anon"));
        }
        else {
            signature.options.structure.id = type->structure.id;
        }
        signature.options.structure.is_union = type->structure.is_union;
        signature.options.structure.members = dynamic_array_create_empty<Struct_Member>(type->structure.members.size);
        if (!type->structure.contains_bitfield)
        {
            for (int i = 0; i < type->structure.members.size; i++) {
                C_Import_Structure_Member* mem = &type->structure.members[i];
                Struct_Member member;
                member.id = mem->id;
                member.offset = mem->offset;
                member.type = import_c_type(analyser, mem->type, type_conversions);
                dynamic_array_push_back(&signature.options.structure.members, member);
            }
        }
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    case C_Import_Type_Type::FUNCTION_SIGNATURE:
    {
        signature.type = Signature_Type::FUNCTION;
        signature.options.function.return_type = import_c_type(analyser, type->function_signature.return_type, type_conversions);
        signature.options.function.parameter_types = dynamic_array_create_empty<Type_Signature*>(type->function_signature.parameters.size);
        for (int i = 0; i < type->function_signature.parameters.size; i++) {
            dynamic_array_push_back(&signature.options.function.parameter_types, import_c_type(analyser, type->function_signature.parameters[i].type, type_conversions));
        }
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    default: panic("WHAT");
    }

    assert(result_type != 0, "HEY");
    hashtable_insert_element(type_conversions, type, result_type);
    return result_type;
}

