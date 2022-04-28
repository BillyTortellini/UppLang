#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../datastructures/dependency_graph.hpp"
#include "../../utility/file_io.hpp"

#include "compiler.hpp"
#include "type_system.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "rc_analyser.hpp"
#include "c_importer.hpp"
#include "compiler_misc.hpp"
#include "ast_parser.hpp"
#include "ir_code.hpp"

bool PRINT_DEPENDENCIES = false;

struct Expression_Result;
struct Expression_Context;

// PROTOTYPES
Type_Signature* import_c_type(Semantic_Analyser* analyser, C_Import_Type* type, Hashtable<C_Import_Type*, Type_Signature*>* type_conversions);

Expression_Result semantic_analyser_analyse_expression_any(Semantic_Analyser* analyser, RC_Expression* rc_expression, Expression_Context context);
ModTree_Expression* semantic_analyser_analyse_expression_value(Semantic_Analyser* analyser, RC_Expression* rc_expression, Expression_Context context);
Type_Signature* semantic_analyser_analyse_expression_type(Semantic_Analyser* analyser, RC_Expression* rc_expression);
void analysis_workload_add_dependency_internal(Workload_Executer* graph, Analysis_Workload* workload, Analysis_Workload* dependency, RC_Symbol_Read* symbol_read);
void analysis_workload_destroy(Analysis_Workload* workload);
void modtree_block_destroy(ModTree_Block* block);
void modtree_function_destroy(ModTree_Function* function);
void modtree_statement_destroy(ModTree_Statement* statement);
ModTree_Expression* semantic_analyser_cast_implicit_if_possible(Semantic_Analyser* analyser, ModTree_Expression* expression, Type_Signature* destination_type);
bool analysis_workload_execute(Analysis_Workload* workload, Semantic_Analyser* analyser);
void analysis_workload_add_struct_dependency(
    Workload_Executer* graph, Analysis_Workload* my_workload, Struct_Progress* other_progress, RC_Dependency_Type type, RC_Symbol_Read* symbol_read);
void semantic_analyser_fill_block(Semantic_Analyser* analyser, ModTree_Block* block, RC_Block* rc_block);
void analysis_workload_append_to_string(Analysis_Workload* workload, String* string);
Analysis_Workload* workload_executer_add_workload_empty(Workload_Executer* graph, Analysis_Workload_Type type, RC_Analysis_Item* item, bool add_item_dependencies);
void analysis_workload_check_if_runnable(Workload_Executer* graph, Analysis_Workload* workload);

/*
ERROR Helpers
*/
void semantic_analyser_set_error_flag(Semantic_Analyser* analyser, bool error_due_to_unknown) 
{
    analyser->error_flag_count += 1;
    if (analyser->current_function != 0) 
    {
        if (analyser->current_function->type == ModTree_Function_Type::POLYMORPHIC_BASE) {
            if (!error_due_to_unknown) {
                analyser->current_function->contains_errors = true;
            }
        }
        else {
            analyser->current_function->contains_errors = true;
        }
        analyser->current_function->is_runnable = false;
    }
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, Semantic_Error_Type type, AST_Node* node) {

    Semantic_Error error;
    error.type = type;
    error.error_node = node;
    error.information = dynamic_array_create_empty<Error_Information>(2);
    dynamic_array_push_back(&analyser->errors, error);
    semantic_analyser_set_error_flag(analyser, false);
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, Semantic_Error_Type type, RC_Expression* expression) {
    AST_Node** node = hashtable_find_element(&analyser->compiler->rc_analyser->mapping_expressions_to_ast, expression);
    assert(node != 0, "");
    semantic_analyser_log_error(analyser, type, *node);
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, Semantic_Error_Type type, RC_Statement* statement) {
    AST_Node** node = hashtable_find_element(&analyser->compiler->rc_analyser->mapping_statements_to_ast, statement);
    assert(node != 0, "");
    semantic_analyser_log_error(analyser, type, *node);
}

void semantic_analyser_add_error_info(Semantic_Analyser* analyser, Error_Information info) {
    assert(analyser->errors.size != 0, "");
    Semantic_Error* last_error = &analyser->errors[analyser->errors.size - 1];
    dynamic_array_push_back(&last_error->information, info);
}

Error_Information error_information_make_empty(Error_Information_Type type) {
    Error_Information info;
    info.type = type;
    return info;
}

Error_Information error_information_make_argument_count(int given_argument_count, int expected_argument_count) {
    Error_Information info = error_information_make_empty(Error_Information_Type::ARGUMENT_COUNT);
    info.options.invalid_argument_count.expected = expected_argument_count;
    info.options.invalid_argument_count.given = given_argument_count;
    return info;
}

Error_Information error_information_make_invalid_member(Type_Signature* struct_signature, String* id) {
    Error_Information info = error_information_make_empty(Error_Information_Type::INVALID_MEMBER);
    info.options.invalid_member.member_id = id;
    info.options.invalid_member.struct_signature = struct_signature;
    return info;
}

Error_Information error_information_make_id(String* id) {
    assert(id != 0, "");
    Error_Information info = error_information_make_empty(Error_Information_Type::ID);
    info.options.id = id;
    return info;
}

Error_Information error_information_make_symbol(Symbol* symbol) {
    Error_Information info = error_information_make_empty(Error_Information_Type::SYMBOL);
    info.options.symbol = symbol;
    return info;
}

Error_Information error_information_make_exit_code(Exit_Code code) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXIT_CODE);
    info.options.exit_code = code;
    return info;
}

Error_Information error_information_make_given_type(Type_Signature* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::GIVEN_TYPE);
    info.options.type = type;
    return info;
}

Error_Information error_information_make_expected_type(Type_Signature* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPECTED_TYPE);
    info.options.type = type;
    return info;
}

Error_Information error_information_make_function_type(Type_Signature* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::FUNCTION_TYPE);
    info.options.type = type;
    return info;
}

Error_Information error_information_make_binary_op_type(Type_Signature* left_type, Type_Signature* right_type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::BINARY_OP_TYPES);
    info.options.binary_op_types.left_type = left_type;
    info.options.binary_op_types.right_type = right_type;
    return info;
}

Error_Information error_information_make_expression_result_type(Expression_Result_Type result_type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPRESSION_RESULT_TYPE);
    info.options.expression_type = result_type;
    return info;
}

Error_Information error_information_make_constant_status(Constant_Status status) {
    Error_Information info = error_information_make_empty(Error_Information_Type::CONSTANT_STATUS);
    info.options.constant_status = status;
    return info;
}

Error_Information error_information_make_cycle_workload(Analysis_Workload* workload) {
    Error_Information info = error_information_make_empty(Error_Information_Type::CYCLE_WORKLOAD);
    info.options.cycle_workload = workload;
    return info;
}



/*
MOD_TREE
*/

ModTree_Statement* modtree_block_add_statement_empty(ModTree_Block* block, ModTree_Statement_Type statement_type)
{
    ModTree_Statement* statement = new ModTree_Statement;
    statement->type = statement_type;
    dynamic_array_push_back(&block->statements, statement);
    return statement;
}

ModTree_Expression* modtree_expression_create_empty(ModTree_Expression_Type type, Type_Signature* result_type)
{
    ModTree_Expression* expr = new ModTree_Expression;
    expr->result_type = result_type;
    expr->expression_type = type;
    return expr;
}

ModTree_Expression* modtree_expression_create_variable_read(ModTree_Variable* variable)
{
    ModTree_Expression* expr = modtree_expression_create_empty(ModTree_Expression_Type::VARIABLE_READ, variable->data_type);
    expr->options.variable_read = variable;
    return expr;
}

ModTree_Block* modtree_block_create_empty(RC_Block* rc_block)
{
    ModTree_Block* block = new ModTree_Block;
    block->statements = dynamic_array_create_empty<ModTree_Statement*>(1);
    block->variables = dynamic_array_create_empty<ModTree_Variable*>(1);
    block->control_flow_locked = false;
    block->flow = Control_Flow::SEQUENTIAL;
    block->rc_block = rc_block;
    block->defer_start_index = -1;
    return block;
}

void modtree_block_destroy(ModTree_Block* block)
{
    for (int i = 0; i < block->statements.size; i++) {
        modtree_statement_destroy(block->statements[i]);
    }
    dynamic_array_destroy(&block->statements);
    for (int i = 0; i < block->variables.size; i++) {
        delete block->variables[i];
    }
    dynamic_array_destroy(&block->variables);
    delete block;
}

ModTree_Variable* modtree_block_add_variable(ModTree_Block* block, Type_Signature* type, Symbol* symbol)
{
    ModTree_Variable* var = new ModTree_Variable;
    assert(type != 0, "");
    var->data_type = type;
    var->symbol = symbol;
    dynamic_array_push_back(&block->variables, var);
    return var;
}

ModTree_Function* modtree_function_create_empty(ModTree_Program* program, Type_Signature* signature, Symbol* symbol, RC_Block* body_block)
{
    ModTree_Function* function = new ModTree_Function;
    function->type = ModTree_Function_Type::NORMAL;
    function->parameters = dynamic_array_create_empty<ModTree_Parameter>(1);
    function->return_type = 0;
    function->symbol = symbol;
    function->signature = signature;
    function->called_from = dynamic_array_create_empty<ModTree_Function*>(1);
    function->calls = dynamic_array_create_empty<ModTree_Function*>(1);
    function->body = modtree_block_create_empty(body_block);
    function->contains_errors = false;
    function->is_runnable = true;
    dynamic_array_push_back(&program->functions, function);
    return function;
}

ModTree_Function* modtree_function_create_poly_instance(Semantic_Analyser* analyser, ModTree_Function* base_function, Dynamic_Array<Upp_Constant> arguments)
{
    ModTree_Function* instance = modtree_function_create_empty(analyser->program, base_function->signature, base_function->symbol, base_function->body->rc_block);
    instance->type = ModTree_Function_Type::POLYMOPRHIC_INSTANCE;
    instance->return_type = base_function->return_type;
    for (int i = 0; i < base_function->parameters.size; i++) {
        if (base_function->parameters[i].is_comptime) continue;
        dynamic_array_push_back(&instance->parameters, base_function->parameters[i]);
    }
    instance->options.instance.instance_base_function = base_function;
    instance->options.instance.poly_arguments = arguments;
    assert(arguments.size == base_function->options.base.poly_argument_count, "");

    // Create Workloads
    Function_Progress* base_progress = hashtable_find_element(&analyser->workload_executer.progress_functions, base_function);
    assert(base_progress != 0, "");
    Analysis_Workload* body_workload = workload_executer_add_workload_empty(
        &analyser->workload_executer, Analysis_Workload_Type::FUNCTION_BODY, base_progress->body_workload->analysis_item, false
    );
    analysis_workload_add_dependency_internal(&analyser->workload_executer, body_workload, base_progress->body_workload, 0);
    body_workload->options.function_body.function = instance;
    analysis_workload_check_if_runnable(&analyser->workload_executer, body_workload); // Required so that progress made is set

    Analysis_Workload* compile_workload = workload_executer_add_workload_empty(&analyser->workload_executer, Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, 0, false);
    compile_workload->options.cluster_compile.functions = dynamic_array_create_empty<ModTree_Function*>(1);
    dynamic_array_push_back(&compile_workload->options.cluster_compile.functions, instance);
    analysis_workload_add_dependency_internal(&analyser->workload_executer, compile_workload, body_workload, 0);

    Function_Progress initial_state;
    initial_state.state = Function_State::HEADER_ANALYSED;
    initial_state.header_workload = 0;
    initial_state.body_workload = body_workload;
    initial_state.compile_workload = compile_workload;
    hashtable_insert_element(&analyser->workload_executer.progress_functions, instance, initial_state);

    return instance;
}

void modtree_function_destroy(ModTree_Function* function)
{
    for (int i = 0; i < function->parameters.size; i++) {
        if (!function->parameters[i].is_comptime) {
            delete function->parameters[i].options.variable;
        }
    }
    dynamic_array_destroy(&function->parameters);
    dynamic_array_destroy(&function->called_from);
    dynamic_array_destroy(&function->calls);
    modtree_block_destroy(function->body);
    delete function;
}

ModTree_Variable* modtree_variable_create(Type_Signature* type, Symbol* symbol)
{
    ModTree_Variable* var = new ModTree_Variable;
    assert(type != 0, "");
    var->data_type = type;
    var->symbol = symbol;
    return var;
}

ModTree_Variable* modtree_function_add_normal_parameter(ModTree_Function* function, Type_Signature* type, String* name)
{
    ModTree_Parameter param;
    param.data_type = type;
    param.is_comptime = false;
    param.name = name;
    param.options.variable = modtree_variable_create(type, 0);
    dynamic_array_push_back(&function->parameters, param);
    return param.options.variable;
}

ModTree_Variable* modtree_program_add_global(ModTree_Program* program, Type_Signature* type, Symbol* symbol)
{
    ModTree_Variable* var = new ModTree_Variable;
    assert(type != 0, "");
    var->data_type = type;
    var->symbol = symbol;
    dynamic_array_push_back(&program->globals, var);
    return var;
}


void modtree_expression_destroy(ModTree_Expression* expression)
{
    switch (expression->expression_type)
    {
    case ModTree_Expression_Type::ERROR_EXPR:
        break;
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
        if (expression->options.function_call.call_type == ModTree_Call_Type::FUNCTION_POINTER) {
            modtree_expression_destroy(expression->options.function_call.options.pointer_expression);
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
        break;
    }
    case ModTree_Expression_Type::ARRAY_INITIALIZER: {
        for (int i = 0; i < expression->options.array_initializer.size; i++) {
            modtree_expression_destroy(expression->options.array_initializer[i]);
        }
        dynamic_array_destroy(&expression->options.array_initializer);
        break;
    }
    case ModTree_Expression_Type::STRUCT_INITIALIZER: {
        for (int i = 0; i < expression->options.struct_initializer.size; i++) {
            modtree_expression_destroy(expression->options.struct_initializer[i].init_expr);
        }
        dynamic_array_destroy(&expression->options.struct_initializer);
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

ModTree_Program* modtree_program_create(Semantic_Analyser* analyser)
{
    ModTree_Program* result = new ModTree_Program();
    result->entry_function = 0;
    result->functions = dynamic_array_create_empty<ModTree_Function*>(16);
    result->globals = dynamic_array_create_empty<ModTree_Variable*>(16);
    result->extern_functions = dynamic_array_create_empty<ModTree_Extern_Function*>(16);
    result->hardcoded_functions = dynamic_array_create_empty<ModTree_Hardcoded_Function*>(16);
    return result;
}

void modtree_program_destroy(ModTree_Program* program)
{
    for (int i = 0; i < program->globals.size; i++) {
        delete program->globals[i];
    }
    dynamic_array_destroy(&program->globals);
    for (int i = 0; i < program->functions.size; i++) {
        modtree_function_destroy(program->functions[i]);
    }
    dynamic_array_destroy(&program->functions);
    for (int i = 0; i < program->hardcoded_functions.size; i++) {
        delete program->hardcoded_functions[i];
    }
    dynamic_array_destroy(&program->hardcoded_functions);
    for (int i = 0; i < program->extern_functions.size; i++) {
        delete program->extern_functions[i];
    }
    dynamic_array_destroy(&program->extern_functions);

    delete program;
}

Comptime_Result comptime_result_make_available(void* data, Type_Signature* type) {
    Comptime_Result result;
    result.type = Comptime_Result_Type::AVAILABLE;
    result.data = data;
    result.data_type = type;
    return result;
}

Comptime_Result comptime_result_make_unavailable(Type_Signature* type) {
    Comptime_Result result;
    result.type = Comptime_Result_Type::UNAVAILABLE;
    result.data_type = type;
    result.data = 0;
    return result;
}

Comptime_Result comptime_result_make_not_comptime() {
    Comptime_Result result;
    result.type = Comptime_Result_Type::NOT_COMPTIME;
    result.data = 0;
    result.data_type = 0;
    return result;
}

Comptime_Result modtree_expression_calculate_comptime_value(Semantic_Analyser* analyser, ModTree_Expression* expr)
{
    Type_System* type_system = &analyser->compiler->type_system;
    switch (expr->expression_type)
    {
    case ModTree_Expression_Type::ERROR_EXPR: {
        return comptime_result_make_unavailable(analyser->compiler->type_system.unknown_type);
    }
    case ModTree_Expression_Type::BINARY_OPERATION:
    {
        Comptime_Result left_val = modtree_expression_calculate_comptime_value(analyser, expr->options.binary_operation.left_operand);
        Comptime_Result right_val = modtree_expression_calculate_comptime_value(analyser, expr->options.binary_operation.right_operand);
        if (left_val.type != Comptime_Result_Type::AVAILABLE || right_val.type != Comptime_Result_Type::AVAILABLE) {
            if (left_val.type == Comptime_Result_Type::NOT_COMPTIME || right_val.type != Comptime_Result_Type::NOT_COMPTIME) {
                return comptime_result_make_not_comptime();
            }
            else {
                return comptime_result_make_unavailable(expr->result_type);
            }
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

        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, expr->result_type->size, expr->result_type->alignment);
        if (bytecode_execute_binary_instr(instr_type, type_signature_to_bytecode_type(left_val.data_type), result_buffer, left_val.data, right_val.data)) {
            return comptime_result_make_available(result_buffer, expr->result_type);
        }
        else {
            return comptime_result_make_not_comptime();
        }
        break;
    }
    case ModTree_Expression_Type::UNARY_OPERATION:
    {
        Comptime_Result value = modtree_expression_calculate_comptime_value(analyser, expr->options.unary_operation.operand);
        Instruction_Type instr_type;
        switch (expr->options.unary_operation.operation_type)
        {
        case ModTree_Unary_Operation_Type::LOGICAL_NOT:
        case ModTree_Unary_Operation_Type::NEGATE: {
            instr_type = expr->options.unary_operation.operation_type == ModTree_Unary_Operation_Type::NEGATE ?
                Instruction_Type::UNARY_OP_NEGATE : Instruction_Type::UNARY_OP_NOT;
            break;
        }
        case ModTree_Unary_Operation_Type::ADDRESS_OF:
            return comptime_result_make_not_comptime();
        case ModTree_Unary_Operation_Type::ADDRESS_OF:
            return comptime_result_make_not_comptime();
        case ModTree_Unary_Operation_Type::TEMPORARY_TO_STACK:
            return value;
        default: panic("");
        }

        if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(expr->result_type);
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, expr->result_type->size, expr->result_type->alignment);
        bytecode_execute_unary_instr(instr_type, type_signature_to_bytecode_type(value.data_type), result_buffer, value.data);
        comptime_result_make_available(result_buffer, expr->result_type);
        break;
    }
    break;
    case ModTree_Expression_Type::CONSTANT_READ: {
        Constant_Pool* pool = &analyser->compiler->constant_pool;
        Upp_Constant constant = expr->options.constant_read;
        return comptime_result_make_available(&pool->buffer[constant.offset], constant.type);
    }
    case ModTree_Expression_Type::FUNCTION_POINTER_READ: {
        return comptime_result_make_not_comptime(); // This will work in the future, but not currently
    }
    case ModTree_Expression_Type::VARIABLE_READ: {
        return comptime_result_make_not_comptime();
    }
    case ModTree_Expression_Type::CAST:
    {
        Comptime_Result value = modtree_expression_calculate_comptime_value(analyser, expr->options.cast.cast_argument);
        if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(expr->result_type);
        }

        Instruction_Type instr_type = (Instruction_Type)-1;
        switch (expr->options.cast.type)
        {
        case ModTree_Cast_Type::FLOATS: instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::FLOAT_TO_INT: instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
        case ModTree_Cast_Type::INT_TO_FLOAT: instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
        case ModTree_Cast_Type::INTEGERS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::ENUM_TO_INT: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::INT_TO_ENUM: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::POINTERS: return comptime_result_make_not_comptime();
        case ModTree_Cast_Type::POINTER_TO_U64: return comptime_result_make_not_comptime();
        case ModTree_Cast_Type::U64_TO_POINTER: return comptime_result_make_not_comptime();
        case ModTree_Cast_Type::ARRAY_SIZED_TO_UNSIZED: {
            Upp_Slice_Base* slice = stack_allocator_allocate<Upp_Slice_Base>(&analyser->allocator_values);
            slice->data_ptr = value.data;
            slice->size = value.data_type->options.array.element_count;
            return comptime_result_make_available(slice, expr->result_type);
        }
        case ModTree_Cast_Type::TO_ANY: {
            Upp_Any* any = stack_allocator_allocate<Upp_Any>(&analyser->allocator_values);
            any->type = expr->result_type->internal_index;
            any->data = value.data;
            return comptime_result_make_available(any, expr->result_type);
        }
        case ModTree_Cast_Type::FROM_ANY: {
            Upp_Any* given = (Upp_Any*)value.data;
            if (given->type >= analyser->compiler->type_system.types.size) {
                return comptime_result_make_not_comptime();
            }
            Type_Signature* any_type = analyser->compiler->type_system.types[given->type];
            if (any_type != value.data_type) {
                return comptime_result_make_not_comptime();
            }
            return comptime_result_make_available(given->data, any_type);
        }
        default: panic("");
        }
        if ((int)instr_type == -1) panic("");

        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, expr->result_type->size, expr->result_type->alignment);
        bytecode_execute_cast_instr(
            instr_type, result_buffer, value.data,
            type_signature_to_bytecode_type(expr->result_type),
            type_signature_to_bytecode_type(value.data_type)
        );
        return comptime_result_make_available(result_buffer, expr->result_type);
    }
    case ModTree_Expression_Type::ARRAY_ACCESS:
    {
        Type_Signature* element_type = expr->result_type;
        Comptime_Result value_array = modtree_expression_calculate_comptime_value(analyser, expr->options.array_access.array_expression);
        Comptime_Result value_index = modtree_expression_calculate_comptime_value(analyser, expr->options.array_access.index_expression);
        if (value_array.type == Comptime_Result_Type::NOT_COMPTIME || value_index.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value_array.type == Comptime_Result_Type::UNAVAILABLE || value_index.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(expr->result_type);
        }
        assert(value_index.data_type == type_system->i32_type, "Must be i32 currently");

        byte* base_ptr = 0;
        int array_size = 0;
        if (value_array.data_type->type == Signature_Type::ARRAY) {
            base_ptr = (byte*)value_array.data;
            array_size = value_array.data_type->options.array.element_count;
        }
        else if (value_array.data_type->type == Signature_Type::SLICE) {
            Upp_Slice_Base* slice = (Upp_Slice_Base*)value_array.data;
            base_ptr = (byte*)slice->data_ptr;
            array_size = slice->size;
        }
        else {
            panic("");
        }

        int index = *(int*)value_index.data;
        int element_offset = index * element_type->size;
        if (index >= array_size) {
            return comptime_result_make_not_comptime();
        }
        if (!memory_is_readable(base_ptr, element_type->size)) {
            return comptime_result_make_not_comptime();
        }
        return comptime_result_make_available(&base_ptr[element_offset], element_type);
    }
    case ModTree_Expression_Type::MEMBER_ACCESS: {
        Struct_Member* member = &expr->options.member_access.member;
        Comptime_Result value_struct = modtree_expression_calculate_comptime_value(analyser, expr->options.member_access.structure_expression);
        if (value_struct.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value_struct.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(expr->result_type);
        }
        byte* raw_data = (byte*)value_struct.data;
        return comptime_result_make_available(&raw_data[member->offset], expr->result_type);
    }
    case ModTree_Expression_Type::FUNCTION_CALL: {
        return comptime_result_make_not_comptime();
    }
    case ModTree_Expression_Type::NEW_ALLOCATION: {
        return comptime_result_make_not_comptime(); // New is always uninitialized, so it cannot have a comptime value (Future: Maybe new with values)
    }
    case ModTree_Expression_Type::ARRAY_INITIALIZER:
    {
        // NOTE: Maybe this works in the futurre, but it dependes if we can always finish the struct size after analysis
        return comptime_result_make_not_comptime();
        /*
        Type_Signature* element_type = expr->result_type->options.array.element_type;
        if (element_type->size == 0 && element_type->alignment == 0) {
            return comptime_analysis_make_error();
        }
        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, expr->result_type->size, expr->result_type->alignment);
        for (int i = 0; i < expr->options.array_initializer.size; i++)
        {
            ModTree_Expression* element_expr = expr->options.array_initializer[i];
            Comptime_Analysis value_element = modtree_expression_calculate_comptime_value(analyser, element_expr);
            if (!value_element.available) {
                return value_element;
            }

            // Copy result into the array buffer
            {
                byte* raw = (byte*)result_buffer;
                int element_offset = element_type->size * i;
                memory_copy(&raw[element_offset], value_element.data, element_type->size);
            }
        }
        return comptime_analysis_make_success(result_buffer, expr->result_type);
        */
    }
    case ModTree_Expression_Type::STRUCT_INITIALIZER:
    {
        // NOTE: Maybe this works in the future
        return comptime_result_make_not_comptime();
        /*
        Type_Signature* struct_type = expr->result_type->options.array.element_type;
        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, expr->result_type->size, expr->result_type->alignment);
        for (int i = 0; i < expr->options.struct_initializer.size; i++)
        {
            Member_Initializer* initializer = &expr->options.struct_initializer[i];
            Comptime_Analysis value_member = modtree_expression_calculate_comptime_value(analyser, initializer->init_expr);
            if (!value_member.available) {
                return value_member;
            }
            // Copy member result into actual struct memory
            {
                byte* raw = (byte*)result_buffer;
                memory_copy(&raw[initializer->member.offset], value_member.data, initializer->member.type->size);
            }
        }
        return comptime_analysis_make_success(result_buffer, expr->result_type);
        */
    }
    default: panic("");
    }
    panic("");
    return comptime_result_make_not_comptime();
}

bool modtree_expression_result_is_temporary(ModTree_Expression* expression)
{
    if (expression->result_type->size == 0 && expression->result_type->alignment != 0) return true;
    switch (expression->expression_type)
    {
    case ModTree_Expression_Type::BINARY_OPERATION: return true;
    case ModTree_Expression_Type::UNARY_OPERATION:
    {
        switch (expression->options.unary_operation.operation_type)
        {
        case ModTree_Unary_Operation_Type::ADDRESS_OF: return true; // The pointer is always temporary
        case ModTree_Unary_Operation_Type::ADDRESS_OF: return false; // There are special cases where memory loss is not detected, e.g. &(new int) 
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
    case ModTree_Expression_Type::CAST: {
        if (expression->options.cast.type == ModTree_Cast_Type::FROM_ANY) return false;
        return true;
    }
    case ModTree_Expression_Type::ARRAY_INITIALIZER: return true;
    case ModTree_Expression_Type::STRUCT_INITIALIZER: return true;
    default: panic("");
    }

    panic("");
    return false;
}

ModTree_Expression* modtree_expression_make_error(Type_Signature* result_type)
{
    ModTree_Expression* expression = new ModTree_Expression;
    expression->expression_type = ModTree_Expression_Type::ERROR_EXPR;
    expression->result_type = result_type;
    return expression;
}

ModTree_Expression* modtree_expression_create_constant(Semantic_Analyser* analyser, Type_Signature* signature, Array<byte> bytes, AST_Node* error_report_node)
{
    Constant_Result result = constant_pool_add_constant(&analyser->compiler->constant_pool, signature, bytes);
    if (result.status != Constant_Status::SUCCESS)
    {
        assert(error_report_node != 0, "Error"); // Error report node may only be null if we know that adding the constant cannot fail.
        semantic_analyser_log_error(analyser, Semantic_Error_Type::CONSTANT_POOL_ERROR, error_report_node);
        semantic_analyser_add_error_info(analyser, error_information_make_constant_status(result.status));
        return modtree_expression_make_error(signature);
    }
    ModTree_Expression* expression = new ModTree_Expression;
    expression->expression_type = ModTree_Expression_Type::CONSTANT_READ;
    expression->options.constant_read = result.constant;
    expression->result_type = signature;
    return expression;
}

ModTree_Expression* modtree_expression_create_constant_enum(Semantic_Analyser* analyser, Type_Signature* enum_type, i32 value) {
    return modtree_expression_create_constant(analyser, enum_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}

ModTree_Expression* modtree_expression_create_constant_i32(Semantic_Analyser* analyser, i32 value) {
    return modtree_expression_create_constant(analyser, analyser->compiler->type_system.i32_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}





/*
HELPERS
*/
enum class Expression_Context_Type
{
    UNKNOWN,             // Type is not known
    AUTO_DEREFERENCE,    // Type is not known, but we want pointer level 0 
    SPECIFIC_TYPE,       // Type is known, pointer level changes + implicit casting enabled
};

struct Expression_Context
{
    Expression_Context_Type type;
    Type_Signature* signature;
};

// Helpers
Expression_Context expression_context_make_unknown() {
    Expression_Context context;
    context.type = Expression_Context_Type::UNKNOWN;
    return context;
}

Expression_Context expression_context_make_auto_dereference() {
    Expression_Context context;
    context.type = Expression_Context_Type::AUTO_DEREFERENCE;
    return context;
}

Expression_Context expression_context_make_specific_type(Type_Signature* signature) {
    if (signature->type == Signature_Type::UNKNOWN_TYPE) {
        return expression_context_make_unknown();
    }
    Expression_Context context;
    context.type = Expression_Context_Type::SPECIFIC_TYPE;
    context.signature = signature;
    return context;
}


struct Expression_Result
{
    Expression_Result_Type type;
    union
    {
        ModTree_Expression* expression;
        Type_Signature* type;
        ModTree_Function* function;
        ModTree_Hardcoded_Function* hardcoded_function;
        ModTree_Extern_Function* extern_function;
        Symbol_Table* module_table;
    } options;
};

Expression_Result expression_result_make_module(Symbol_Table* module_table)
{
    Expression_Result result;
    result.type = Expression_Result_Type::MODULE;
    result.options.module_table = module_table;
    return result;
}

Expression_Result expression_result_make_error(Type_Signature* result_type)
{
    Expression_Result result;
    result.type = Expression_Result_Type::EXPRESSION;
    result.options.expression = modtree_expression_make_error(result_type);
    return result;
}

Expression_Result expression_result_make_value(ModTree_Expression* expression)
{
    Expression_Result result;
    result.type = Expression_Result_Type::EXPRESSION;
    result.options.expression = expression;
    return result;
}

Expression_Result expression_result_make_function(ModTree_Function* function)
{
    Expression_Result result;
    result.type = Expression_Result_Type::FUNCTION;
    result.options.function = function;
    return result;
}

Expression_Result expression_result_make_hardcoded(ModTree_Hardcoded_Function* hardcoded)
{
    Expression_Result result;
    result.type = Expression_Result_Type::HARDCODED_FUNCTION;
    result.options.hardcoded_function = hardcoded;
    return result;
}

Expression_Result expression_result_make_extern_function(ModTree_Extern_Function* extern_function)
{
    Expression_Result result;
    result.type = Expression_Result_Type::EXTERN_FUNCTION;
    result.options.extern_function = extern_function;
    return result;
}

Expression_Result expression_result_make_type(Type_Signature* type)
{
    Expression_Result result;
    result.type = Expression_Result_Type::TYPE;
    result.options.type = type;
    return result;
}

AST_Node* rc_to_ast(Semantic_Analyser* analyser, RC_Expression* expression)
{
    AST_Node** node = hashtable_find_element(&analyser->compiler->rc_analyser->mapping_expressions_to_ast, expression);
    assert(node != 0, "");
    return *node;
}

AST_Node* rc_to_ast(Semantic_Analyser* analyser, RC_Statement* statement)
{
    AST_Node** node = hashtable_find_element(&analyser->compiler->rc_analyser->mapping_statements_to_ast, statement);
    assert(node != 0, "");
    return *node;
}








/*
DEPENDENCY GRAPH
*/
Workload_Pair workload_pair_create(Analysis_Workload* workload, Analysis_Workload* depends_on) {
    Workload_Pair result;
    result.depends_on = depends_on;
    result.workload = workload;
    return result;
}

u64 workload_pair_hash(Workload_Pair* pair) {
    return hash_memory(array_create_static_as_bytes(pair, 1));
}

bool workload_pair_equals(Workload_Pair* p1, Workload_Pair* p2) {
    return p1->depends_on == p2->depends_on && p1->workload == p2->workload;
}

Workload_Executer workload_executer_create(Semantic_Analyser* analyser)
{
    Workload_Executer result;
    result.workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    result.analyser = analyser;
    result.runnable_workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    result.workload_dependencies = hashtable_create_empty<Workload_Pair, Dependency_Information>(8, workload_pair_hash, workload_pair_equals);
    result.item_to_workload_mapping = hashtable_create_pointer_empty<RC_Analysis_Item*, Analysis_Workload*>(8);
    result.progress_definitions = hashtable_create_pointer_empty<Symbol*, Analysis_Workload*>(8);
    result.progress_structs = hashtable_create_pointer_empty<Type_Signature*, Struct_Progress>(8);
    result.progress_functions = hashtable_create_pointer_empty<ModTree_Function*, Function_Progress>(8);
    result.progress_was_made = false;
    return result;
}

void workload_executer_destroy(Workload_Executer* graph)
{
    for (int i = 0; i < graph->workloads.size; i++) {
        analysis_workload_destroy(graph->workloads[i]);
    }
    {
        auto iter = hashtable_iterator_create(&graph->workload_dependencies);
        while (hashtable_iterator_has_next(&iter)) {
            SCOPE_EXIT(hashtable_iterator_next(&iter));
            dynamic_array_destroy(&iter.value->symbol_reads);
        }
        hashtable_destroy(&graph->workload_dependencies);
    }
    dynamic_array_destroy(&graph->workloads);
    dynamic_array_destroy(&graph->runnable_workloads);
    hashtable_destroy(&graph->item_to_workload_mapping);
    hashtable_destroy(&graph->progress_definitions);
    hashtable_destroy(&graph->progress_functions);
    hashtable_destroy(&graph->progress_structs);
}

void analysis_workload_destroy(Analysis_Workload* workload)
{
    list_destroy(&workload->dependencies);
    list_destroy(&workload->dependents);
    dynamic_array_destroy(&workload->symbol_dependencies);
    dynamic_array_destroy(&workload->reachable_clusters);
    if (workload->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE) {
        dynamic_array_destroy(&workload->options.struct_reachable.struct_types);
        dynamic_array_destroy(&workload->options.struct_reachable.unfinished_array_types);
    }
    if (workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE) {
        dynamic_array_destroy(&workload->options.cluster_compile.functions);
    }
    delete workload;
}

Symbol* semantic_analyser_resolve_symbol_read(Semantic_Analyser* analyser, RC_Symbol_Read* symbol_read)
{
    AST_Node* identifier_node = symbol_read->identifier_node;
    Symbol_Table* table = symbol_read->symbol_table;
    while (true)
    {
        assert(ast_node_type_is_identifier_node(identifier_node->type), "");
        bool is_path = identifier_node->type == AST_Node_Type::IDENTIFIER_PATH;
        Symbol* symbol = symbol_table_find_symbol(table, identifier_node->id, identifier_node != symbol_read->identifier_node, is_path ? 0 : symbol_read);
        if (is_path)
        {
            if (symbol == 0) {
                return 0; // Did not find module
            }
            if (symbol->type == Symbol_Type::UNRESOLVED) {
                return 0;
            }
            if (symbol->type == Symbol_Type::MODULE) {
                table = symbol->options.module_table;
                identifier_node = identifier_node->child_start;
                continue;
            }

            semantic_analyser_log_error(analyser, Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH, identifier_node);
            semantic_analyser_add_error_info(analyser, error_information_make_symbol(symbol));
            return analyser->compiler->rc_analyser->predefined_symbols.error_symbol;
        }
        else {
            return symbol;
        }
    }
}

void analysis_workload_add_dependency_internal(Workload_Executer* graph, Analysis_Workload* workload, Analysis_Workload* dependency, RC_Symbol_Read* symbol_read)
{
    if (dependency->is_finished) return;
    Workload_Pair pair = workload_pair_create(workload, dependency);
    Dependency_Information* infos = hashtable_find_element(&graph->workload_dependencies, pair);
    if (infos == 0) {
        Dependency_Information info;
        info.dependency_node = list_add_at_end(&workload->dependencies, dependency);
        info.dependent_node = list_add_at_end(&dependency->dependents, workload);
        info.symbol_reads = dynamic_array_create_empty<RC_Symbol_Read*>(1);
        info.only_symbol_read_dependency = false;
        if (symbol_read != 0) {
            info.only_symbol_read_dependency = true;
            dynamic_array_push_back(&info.symbol_reads, symbol_read);
        }
        bool inserted = hashtable_insert_element(&graph->workload_dependencies, pair, info);
        assert(inserted, "");
        return;
    }

    if (symbol_read != 0) {
        dynamic_array_push_back(&infos->symbol_reads, symbol_read);
    }
    else {
        infos->only_symbol_read_dependency = false;
    }
}

void workload_executer_move_dependency(Workload_Executer* graph, Analysis_Workload* move_from, Analysis_Workload* move_to, Analysis_Workload* dependency)
{
    assert(move_from != move_to, "");
    Workload_Pair original_pair = workload_pair_create(move_from, dependency);
    Workload_Pair new_pair = workload_pair_create(move_to, dependency);
    Dependency_Information info = *hashtable_find_element(&graph->workload_dependencies, original_pair);
    hashtable_remove_element(&graph->workload_dependencies, original_pair);
    list_remove_node(&move_from->dependencies, info.dependency_node);
    list_remove_node(&dependency->dependents, info.dependent_node);

    Dependency_Information* new_infos = hashtable_find_element(&graph->workload_dependencies, new_pair);
    if (new_infos == 0) {
        Dependency_Information new_info;
        new_info.dependency_node = list_add_at_end(&move_to->dependencies, dependency);
        new_info.dependent_node = list_add_at_end(&move_from->dependents, move_to);
        new_info.symbol_reads = info.symbol_reads;
        new_info.only_symbol_read_dependency = info.only_symbol_read_dependency;
        hashtable_insert_element(&graph->workload_dependencies, new_pair, new_info);
        return;
    }

    dynamic_array_append_other(&new_infos->symbol_reads, &info.symbol_reads);
    if (new_infos->only_symbol_read_dependency) {
        new_infos->only_symbol_read_dependency = info.only_symbol_read_dependency;
    }
    dynamic_array_destroy(&info.symbol_reads);
}

void workload_executer_remove_dependency(Workload_Executer* graph, Analysis_Workload* workload, Analysis_Workload* depends_on)
{
    Workload_Pair pair = workload_pair_create(workload, depends_on);
    Dependency_Information* info = hashtable_find_element(&graph->workload_dependencies, pair);
    assert(info != 0, "");
    list_remove_node(&workload->dependencies, info->dependency_node);
    list_remove_node(&depends_on->dependents, info->dependent_node);
    dynamic_array_destroy(&info->symbol_reads);
    bool worked = hashtable_remove_element(&graph->workload_dependencies, pair);
    if (workload->dependencies.count == 0 && workload->symbol_dependencies.size == 0) {
        dynamic_array_push_back(&graph->runnable_workloads, workload);
    }
}

Analysis_Workload* analysis_workload_find_associated_cluster(Analysis_Workload* workload)
{
    assert(workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE || workload->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE, "");
    if (workload->cluster == 0) {
        return workload;
    }
    workload->cluster = analysis_workload_find_associated_cluster(workload->cluster);
    return workload->cluster;
}

bool cluster_workload_check_for_cyclic_dependency(
    Analysis_Workload* workload, Analysis_Workload* start_workload,
    Hashtable<Analysis_Workload*, bool>* visited, Dynamic_Array<Analysis_Workload*>* workloads_to_merge)
{
    // Check if we already visited
    {
        bool* contains_loop = hashtable_find_element(visited, workload);
        if (contains_loop != 0) {
            return *contains_loop;
        }
    }
    hashtable_insert_element(visited, workload, false); // The boolean value changes later if we actually find a loop
    bool loop_found = false;
    for (int i = 0; i < workload->reachable_clusters.size; i++)
    {
        Analysis_Workload* reachable = analysis_workload_find_associated_cluster(workload->reachable_clusters[i]);
        if (reachable == start_workload) {
            loop_found = true;
        }
        else {
            bool transitiv_reachable = cluster_workload_check_for_cyclic_dependency(reachable, start_workload, visited, workloads_to_merge);
            if (transitiv_reachable) loop_found = true;
        }
    }
    if (loop_found) {
        bool* current_value = hashtable_find_element(visited, workload);
        *current_value = true;
        dynamic_array_push_back(workloads_to_merge, workload);
    }
    return loop_found;
}


void analysis_workload_add_cluster_dependency(Workload_Executer* graph, Analysis_Workload* add_to_workload, Analysis_Workload* dependency, RC_Symbol_Read* symbol_read)
{
    assert((add_to_workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE && dependency->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE) ||
        (add_to_workload->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE && dependency->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE), "");
    Analysis_Workload* merge_into = analysis_workload_find_associated_cluster(add_to_workload);
    Analysis_Workload* merge_from = analysis_workload_find_associated_cluster(dependency);
    if (merge_into == merge_from || merge_from->is_finished) {
        return;
    }

    Hashtable<Analysis_Workload*, bool> visited = hashtable_create_pointer_empty<Analysis_Workload*, bool>(1);
    SCOPE_EXIT(hashtable_destroy(&visited));
    Dynamic_Array<Analysis_Workload*> workloads_to_merge = dynamic_array_create_empty<Analysis_Workload*>(1);
    SCOPE_EXIT(dynamic_array_destroy(&workloads_to_merge));
    bool loop_found = cluster_workload_check_for_cyclic_dependency(merge_from, merge_into, &visited, &workloads_to_merge);
    if (!loop_found) {
        dynamic_array_push_back(&merge_into->reachable_clusters, merge_from);
        analysis_workload_add_dependency_internal(graph, merge_into, merge_from, symbol_read);
        return;
    }

    // Merge all workloads together
    for (int i = 0; i < workloads_to_merge.size; i++)
    {
        Analysis_Workload* merge_cluster = workloads_to_merge[i];
        assert(merge_cluster != merge_into, "");
        // Remove all dependent connections from the merge
        auto node = merge_cluster->dependencies.head;
        while (node != 0)
        {
            Analysis_Workload* merge_dependency = node->value;
            // Check if we need the dependency
            bool keep_dependency = true;
            {
                bool* contains_loop = hashtable_find_element(&visited, merge_dependency);
                if (contains_loop != 0) {
                    keep_dependency = !*contains_loop;
                }
            }

            if (keep_dependency) {
                workload_executer_move_dependency(graph, merge_cluster, merge_into, merge_dependency);
            }
            else {
                workload_executer_remove_dependency(graph, merge_cluster, merge_dependency);
            }
            node = node->next;
        }
        list_reset(&merge_cluster->dependencies);

        // Merge all analysis item values
        switch (merge_into->type)
        {
        case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
        {
            for (int i = 0; i < merge_cluster->options.cluster_compile.functions.size; i++) {
                dynamic_array_push_back(&
                    merge_into->options.cluster_compile.functions, merge_cluster->options.cluster_compile.functions[i]
                );
            }
            dynamic_array_reset(&merge_cluster->options.cluster_compile.functions);
            break;
        }
        case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
        {
            for (int i = 0; i < merge_cluster->options.struct_reachable.unfinished_array_types.size; i++) {
                dynamic_array_push_back(&
                    merge_into->options.struct_reachable.unfinished_array_types, merge_cluster->options.struct_reachable.unfinished_array_types[i]
                );
            }
            dynamic_array_reset(&merge_cluster->options.struct_reachable.unfinished_array_types);
            for (int i = 0; i < merge_cluster->options.struct_reachable.struct_types.size; i++) {
                dynamic_array_push_back(&
                    merge_into->options.struct_reachable.struct_types, merge_cluster->options.struct_reachable.struct_types[i]
                );
            }
            dynamic_array_reset(&merge_cluster->options.struct_reachable.struct_types);

        }
        case Analysis_Workload_Type::DEFINITION:
        case Analysis_Workload_Type::FUNCTION_BODY:
        case Analysis_Workload_Type::FUNCTION_HEADER:
        case Analysis_Workload_Type::STRUCT_ANALYSIS:
            panic("Clustering only on function clusters and reachable resolve cluster!");
        default: panic("");
        }

        // Add reachables to merged
        for (int i = 0; i < merge_cluster->reachable_clusters.size; i++) {
            dynamic_array_push_back(&merge_into->reachable_clusters, merge_cluster->reachable_clusters[i]);
        }
        dynamic_array_reset(&merge_cluster->reachable_clusters);
        analysis_workload_add_dependency_internal(graph, merge_cluster, merge_into, 0);
        merge_cluster->cluster = merge_into;
    }

    // Prune reachables
    for (int i = 0; i < merge_into->reachable_clusters.size; i++)
    {
        Analysis_Workload* reachable = analysis_workload_find_associated_cluster(merge_into->reachable_clusters[i]);
        if (reachable == merge_into) {
            // Remove self references
            dynamic_array_swap_remove(&merge_into->reachable_clusters, i);
            i = i - 1;
        }
        else
        {
            // Remove doubles
            bool found = false;
            for (int j = i + 1; j < merge_into->reachable_clusters.size; j++) {
                if (merge_into->reachable_clusters[j] == reachable) {
                    found = true;
                    break;
                }
            }
            if (found) {
                dynamic_array_swap_remove(&merge_into->reachable_clusters, i);
                i = i - 1;
            }
        }
    }
}

void analysis_workload_add_struct_dependency(
    Workload_Executer* graph, Analysis_Workload* my_workload, Struct_Progress* other_progress, RC_Dependency_Type type, RC_Symbol_Read* symbol_read)
{
    if (my_workload->type != Analysis_Workload_Type::STRUCT_ANALYSIS) {
        analysis_workload_add_dependency_internal(graph, my_workload, other_progress->reachable_resolve_workload, symbol_read);
        return;
    }
    if (other_progress->state == Struct_State::FINISHED) return;
    Struct_Progress* my_progress = hashtable_find_element(&graph->progress_structs, my_workload->options.struct_analysis_type);
    assert(my_progress != 0, "");

    switch (type)
    {
    case RC_Dependency_Type::NORMAL: {
        // Struct member references another struct, but not as a type
        // E.g. Foo :: struct { value: Bar.{...}; }
        analysis_workload_add_dependency_internal(graph, my_workload, other_progress->reachable_resolve_workload, symbol_read);
        break;
    }
    case RC_Dependency_Type::MEMBER_IN_MEMORY: {
        // Struct member references other member in memory
        // E.g. Foo :: struct { value: Bar; }
        analysis_workload_add_dependency_internal(graph, my_workload, other_progress->member_workload, symbol_read);
        analysis_workload_add_cluster_dependency(graph, my_progress->reachable_resolve_workload, other_progress->reachable_resolve_workload, symbol_read);
        break;
    }
    case RC_Dependency_Type::MEMBER_REFERENCE:
    {
        // Struct member contains some sort of reference to other member
        // E.g. Foo :: struct { value: *Bar; }
        // This means we need to unify the Reachable-Clusters
        analysis_workload_add_cluster_dependency(graph, my_progress->reachable_resolve_workload, other_progress->reachable_resolve_workload, symbol_read);
        break;
    }
    default: panic("");
    }
}



bool analysis_workload_find_cycle(
    Analysis_Workload* current_workload, int current_depth, Analysis_Workload* start_workload, int desired_cycle_size,
    Dynamic_Array<Analysis_Workload*>* loop_nodes, Hashtable<Analysis_Workload*, int>* valid_workloads)
{
    if (hashtable_find_element(valid_workloads, current_workload) == 0) {
        return false;
    }
    if (current_workload->is_finished) return false;
    if (current_depth > desired_cycle_size) return false;
    if (current_workload == start_workload) {
        assert(current_depth == desired_cycle_size, "");
        dynamic_array_push_back(loop_nodes, current_workload);
        return true;
    }

    List_Node<Analysis_Workload*>* node = current_workload->dependencies.head;
    while (node != 0)
    {
        SCOPE_EXIT(node = node->next);
        Analysis_Workload* dependency = node->value;
        if (analysis_workload_find_cycle(dependency, current_depth + 1, start_workload, desired_cycle_size, loop_nodes, valid_workloads)) {
            dynamic_array_push_back(loop_nodes, current_workload);
            return true;
        }
    }
    return false;
}

void workload_executer_resolve(Workload_Executer* graph)
{
    /*
    Resolve is a double loop:
    loop
        Check for resolved symbols
        loop check all workloads to check

        if no_changes
            Check for circular dependencies
                if they exist, resolve them (E.g. set symbols to error, set member type to error, ...)
                else, resolve one unresolved symbol (E.g. set to error) and try again
    */
    Dynamic_Array<Analysis_Workload*> unresolved_symbols_workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    SCOPE_EXIT(dynamic_array_destroy(&unresolved_symbols_workloads));
    for (int i = 0; i < graph->workloads.size; i++) {
        dynamic_array_push_back(&unresolved_symbols_workloads, graph->workloads[i]);
    }

    int round_no = 0;
    while (true)
    {
        SCOPE_EXIT(round_no += 1);
        graph->progress_was_made = false;
        // Check if symbols can be resolved (TODO: Have a symbol defined 'message' system)
        for (int i = 0; i < unresolved_symbols_workloads.size; i++)
        {
            Analysis_Workload* workload = unresolved_symbols_workloads[i];

            // Try to resolve all unresolved symbols
            for (int j = 0; j < workload->symbol_dependencies.size; j++)
            {
                RC_Symbol_Read* symbol_read = workload->symbol_dependencies[j];
                if (symbol_read->symbol == 0) {
                    symbol_read->symbol = semantic_analyser_resolve_symbol_read(graph->analyser, symbol_read);
                    if (symbol_read->symbol == 0) {
                        continue;
                    }
                    graph->progress_was_made = true;
                }

                bool symbol_read_ready = true;
                switch (symbol_read->symbol->type)
                {
                case Symbol_Type::UNRESOLVED:
                {
                    Analysis_Workload** definition_workload = hashtable_find_element(&graph->progress_definitions, symbol_read->symbol);
                    if (definition_workload != 0) {
                        symbol_read_ready = true;
                        analysis_workload_add_dependency_internal(graph, workload, *definition_workload, symbol_read);
                    }
                    else {
                        symbol_read_ready = false;
                    }
                    break;
                }
                case Symbol_Type::FUNCTION:
                {
                    Function_Progress* progress_opt = hashtable_find_element(&graph->progress_functions, symbol_read->symbol->options.function);
                    if (progress_opt != 0) {
                        analysis_workload_add_dependency_internal(graph, workload, progress_opt->header_workload, symbol_read);
                    }
                    break;
                }
                case Symbol_Type::TYPE:
                {
                    Type_Signature* type = symbol_read->symbol->options.type;
                    if (type->type == Signature_Type::STRUCT)
                    {
                        Struct_Progress* other_progress = hashtable_find_element(&graph->progress_structs, type);
                        if (other_progress == 0) {
                            assert(type->size != 0 && type->alignment != 0, "");
                            break;
                        }
                        analysis_workload_add_struct_dependency(graph, workload, other_progress, symbol_read->type, symbol_read);
                    }
                    break;
                }
                default: break;
                }

                if (symbol_read_ready) {
                    dynamic_array_swap_remove(&workload->symbol_dependencies, j);
                    j = j - 1;
                }
            }

            // Add workload to running queue if all dependencies are resolved
            if (workload->symbol_dependencies.size == 0) {
                dynamic_array_swap_remove(&unresolved_symbols_workloads, i);
                i = i - 1; // So that we properly iterate
                if (workload->dependencies.count == 0) {
                    dynamic_array_push_back(&graph->runnable_workloads, workload);
                }
                graph->progress_was_made = true;
            }
        }

        // Print workloads and dependencies
        if (true)
        {
            String tmp = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&tmp));
            string_append_formated(&tmp, "Workload Execution Round %d\n---------------------\n", round_no);
            for (int i = 0; i < graph->workloads.size; i++)
            {
                Analysis_Workload* workload = graph->workloads[i];
                if (workload->is_finished) continue;
                analysis_workload_append_to_string(workload, &tmp);
                string_append_formated(&tmp, "\n");
                List_Node<Analysis_Workload*>* dependency_node = workload->dependencies.head;
                while (dependency_node != 0) {
                    SCOPE_EXIT(dependency_node = dependency_node->next);
                    Analysis_Workload* dependency = dependency_node->value;
                    string_append_formated(&tmp, "  ");
                    analysis_workload_append_to_string(dependency, &tmp);
                    string_append_formated(&tmp, "\n");
                }
            }
            logg("%s", tmp.characters);
        }

        // Execute runnable workloads
        for (int i = 0; i < graph->runnable_workloads.size; i++)
        {
            Analysis_Workload* workload = graph->runnable_workloads[i];
            assert(workload->symbol_dependencies.size == 0, "");
            assert(workload->dependencies.count == 0, "");
            if (workload->is_finished) {
                continue;
            }
            //assert(!workload->is_finished, "");
            graph->progress_was_made = true;

            String tmp = string_create_empty(128);
            analysis_workload_append_to_string(workload, &tmp);
            logg("Executing workload: %s\n", tmp.characters);
            string_destroy(&tmp);

            analysis_workload_execute(workload, graph->analyser);
            // Note: After a workload executes, it may have added new dependencies to itself
            if (workload->dependencies.count == 0)
            {
                workload->is_finished = true;
                List_Node<Analysis_Workload*>* node = workload->dependents.head;
                while (node != 0) {
                    Analysis_Workload* dependent = node->value;
                    node = node->next; // INFO: This is required before remove_dependency, since remove will remove items from the list
                    workload_executer_remove_dependency(graph, dependent, workload);
                }
                assert(workload->dependents.count == 0, "");
                //list_reset(&workload->dependents);
            }
        }
        dynamic_array_reset(&graph->runnable_workloads);
        if (graph->progress_was_made) continue;

        // Check if all workloads finished
        {
            bool all_finished = true;
            for (int i = 0; i < graph->workloads.size; i++) {
                if (!graph->workloads[i]->is_finished) {
                    all_finished = false;
                    break;
                }
            }
            if (all_finished) {
                break;
            }
        }

        /*
            Circular Dependency Detection:
             1. Do breadth first search to find smallest possible loop/if loop exists
             2. If a loop was found, do a depth first search to reconstruct the loop (Could probably be done better)
             3. Resolve the loop (Log Error, set some of the dependencies to error)
        */
        {
            // Initialization
            Hashtable<Analysis_Workload*, int> workload_to_layer = hashtable_create_pointer_empty<Analysis_Workload*, int>(4);
            SCOPE_EXIT(hashtable_destroy(&workload_to_layer));
            Hashset<Analysis_Workload*> unvisited = hashset_create_pointer_empty<Analysis_Workload*>(4);
            SCOPE_EXIT(hashset_destroy(&unvisited));
            for (int i = 0; i < graph->workloads.size; i++) {
                Analysis_Workload* workload = graph->workloads[i];
                if (!workload->is_finished) {
                    hashset_insert_element(&unvisited, workload);
                }
            }

            bool loop_found = false;
            int loop_node_count = -1;
            Analysis_Workload* loop_node = 0;
            Analysis_Workload* loop_node_2 = 0;

            // Breadth first search
            Dynamic_Array<int> layer_start_indices = dynamic_array_create_empty<int>(4);
            SCOPE_EXIT(dynamic_array_destroy(&layer_start_indices));
            Dynamic_Array<Analysis_Workload*> layers = dynamic_array_create_empty<Analysis_Workload*>(4);
            SCOPE_EXIT(dynamic_array_destroy(&layers));

            while (!loop_found)
            {
                // Remove all nodes that are already confirmed to have no cycles (E.g nodes from last loop run)
                for (int i = 0; i < layers.size; i++) {
                    hashset_remove_element(&unvisited, layers[i]);
                }
                if (unvisited.element_count == 0) break;
                dynamic_array_reset(&layer_start_indices);
                dynamic_array_reset(&layers);
                hashtable_reset(&workload_to_layer);

                Analysis_Workload* start = *hashset_iterator_create(&unvisited).value;
                dynamic_array_push_back(&layer_start_indices, layers.size);
                dynamic_array_push_back(&layers, start);
                dynamic_array_push_back(&layer_start_indices, layers.size);

                int current_layer = 0;
                while (true && !loop_found)
                {
                    SCOPE_EXIT(current_layer++);
                    int layer_start = layer_start_indices[current_layer];
                    int layer_end = layer_start_indices[current_layer + 1];
                    if (layer_start == layer_end) break;
                    for (int i = layer_start; i < layer_end && !loop_found; i++)
                    {
                        Analysis_Workload* scan_for_loops = layers[i];
                        assert(!scan_for_loops->is_finished, "");
                        List_Node<Analysis_Workload*>* node = scan_for_loops->dependencies.head;
                        while (node != 0 && !loop_found)
                        {
                            SCOPE_EXIT(node = node->next;);
                            Analysis_Workload* dependency = node->value;
                            if (dependency == scan_for_loops) { // Self dependency
                                loop_found = true;
                                loop_node_count = 1;
                                loop_node = scan_for_loops;
                                loop_node_2 = scan_for_loops;
                                break;
                            }
                            if (!hashset_contains(&unvisited, dependency)) {
                                // Node is clear because we checked it on a previous cycle
                                continue;
                            }
                            int* found_layer = hashtable_find_element(&workload_to_layer, dependency);
                            if (found_layer == nullptr) {
                                hashtable_insert_element(&workload_to_layer, dependency, current_layer + 1);
                                dynamic_array_push_back(&layers, dependency);
                            }
                            else
                            {
                                int dependency_layer = *found_layer;
                                if (dependency_layer != current_layer + 1)
                                {
                                    loop_found = true;
                                    loop_node = scan_for_loops;
                                    loop_node_2 = dependency;
                                    if (dependency_layer == current_layer) {
                                        loop_node_count = 2;
                                    }
                                    else {
                                        loop_node_count = current_layer - dependency_layer + 1;
                                    }
                                }
                            }
                        }
                    }
                    dynamic_array_push_back(&layer_start_indices, layers.size);
                }
            }


            // Handle the loop
            if (loop_found)
            {
                // Reconstruct Loop
                Dynamic_Array<Analysis_Workload*> workload_cycle = dynamic_array_create_empty<Analysis_Workload*>(1);
                SCOPE_EXIT(dynamic_array_destroy(&workload_cycle));
                if (loop_node_count != 1) {
                    // Depth first search
                    bool found_loop_again = analysis_workload_find_cycle(loop_node_2, 1, loop_node, loop_node_count, &workload_cycle, &workload_to_layer);
                    assert(found_loop_again, "");
                    dynamic_array_reverse_order(&workload_cycle);
                }
                else {
                    dynamic_array_push_back(&workload_cycle, loop_node);
                }

                // Resolve and report error
                bool only_reads_was_found = false;
                for (int i = 0; i < workload_cycle.size; i++)
                {
                    Analysis_Workload* workload = workload_cycle[i];
                    Analysis_Workload* depends_on = i + 1 == workload_cycle.size ? workload_cycle[0] : workload_cycle[i + 1];
                    Workload_Pair pair = workload_pair_create(workload, depends_on);
                    Dependency_Information infos = *hashtable_find_element(&graph->workload_dependencies, pair);
                    if (infos.only_symbol_read_dependency) {
                        only_reads_was_found = true;
                        for (int j = 0; j < infos.symbol_reads.size; j++) {
                            infos.symbol_reads[j]->symbol = graph->analyser->compiler->rc_analyser->predefined_symbols.error_symbol;
                            semantic_analyser_log_error(graph->analyser, Semantic_Error_Type::CYCLIC_DEPENDENCY_DETECTED, infos.symbol_reads[j]->identifier_node);
                            for (int k = 0; k < workload_cycle.size; k++) {
                                Analysis_Workload* workload = workload_cycle[k];
                                semantic_analyser_add_error_info(graph->analyser, error_information_make_cycle_workload(workload));
                            }
                        }
                    }
                    workload_executer_remove_dependency(graph, workload, depends_on);
                }
                assert(only_reads_was_found, "");
                graph->progress_was_made = true;
            }
        }
        if (graph->progress_was_made) {
            continue;
        }

        // Resolve an unresolved symbol to error to continue analysis
        assert(unresolved_symbols_workloads.size != 0, "Either a loop must be found or an unresolved symbol exists, otherwise there is something wrong");
        // Search for workload with lowest unresolved symbols count, and resolve all those symbols
        Analysis_Workload* lowest = 0;
        for (int i = 0; i < unresolved_symbols_workloads.size; i++) {
            Analysis_Workload* workload = unresolved_symbols_workloads[i];
            if (workload->dependencies.count == 0) {
                if (lowest != 0) {
                    if (workload->symbol_dependencies.size < lowest->symbol_dependencies.size) {
                        lowest = workload;
                    }
                }
                else {
                    lowest = workload;
                }
            }
        }
        if (lowest == 0) {
            break;
        }

        for (int i = 0; i < lowest->symbol_dependencies.size; i++) {
            semantic_analyser_log_error(graph->analyser, Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL, lowest->symbol_dependencies[i]->identifier_node);
            lowest->symbol_dependencies[i]->symbol = graph->analyser->compiler->rc_analyser->predefined_symbols.error_symbol;
        }
        dynamic_array_reset(&lowest->symbol_dependencies);
        dynamic_array_push_back(&graph->runnable_workloads, lowest);
    }

}

void workload_executer_add_item_mapping(Workload_Executer* graph, Analysis_Workload* workload, RC_Analysis_Item* item)
{
    assert(item != 0, "");
    bool worked = hashtable_insert_element(&graph->item_to_workload_mapping, item, workload);
    assert(worked, "This may need to change with templates");
}

Analysis_Workload* workload_executer_add_workload_empty(Workload_Executer* graph, Analysis_Workload_Type type, RC_Analysis_Item* item, bool add_symbol_dependencies)
{
    graph->progress_was_made = true;
    Analysis_Workload* workload = new Analysis_Workload;
    workload->dependencies = list_create<Analysis_Workload*>();
    workload->dependents = list_create<Analysis_Workload*>();
    workload->is_finished = false;
    workload->symbol_dependencies = dynamic_array_create_empty<RC_Symbol_Read*>(1);
    workload->type = type;
    workload->cluster = 0;
    workload->reachable_clusters = dynamic_array_create_empty<Analysis_Workload*>(1);
    workload->analysis_item = item;
    if (item != 0 && add_symbol_dependencies) {
        for (int i = 0; i < item->symbol_dependencies.size; i++) {
            dynamic_array_push_back(&workload->symbol_dependencies, item->symbol_dependencies[i]);
        }
    }
    dynamic_array_push_back(&graph->workloads, workload);

    return workload;
}

Analysis_Workload* workload_executer_add_workload_from_item(Workload_Executer* graph, RC_Analysis_Item* item);
void analysis_workload_create_child_workloads(Workload_Executer* graph, Analysis_Workload* workload, RC_Analysis_Item* item)
{
    if (item == 0) return;
    for (int i = 0; i < item->item_dependencies.size; i++)
    {
        RC_Item_Dependency* item_dependency = &item->item_dependencies[i];
        Analysis_Workload* dependency = workload_executer_add_workload_from_item(graph, item_dependency->item);
        assert(dependency != 0, "");

        if (workload == 0) continue;
        if (item_dependency->type != RC_Dependency_Type::NORMAL && dependency->type == Analysis_Workload_Type::STRUCT_ANALYSIS) {
            Struct_Progress* progress = hashtable_find_element(&graph->progress_structs, dependency->options.struct_analysis_type);
            assert(progress != 0, "");
            analysis_workload_add_struct_dependency(graph, workload, progress, item_dependency->type, 0);
        }
        else {
            analysis_workload_add_dependency_internal(graph, workload, dependency, 0);
        }
    }
}

Analysis_Workload* workload_executer_add_workload_from_item(Workload_Executer* graph, RC_Analysis_Item* item)
{
    // Create workload
    Analysis_Workload* workload = 0;
    switch (item->type)
    {
    case RC_Analysis_Item_Type::ROOT: {
        analysis_workload_create_child_workloads(graph, 0, item);
        break;
    }
    case RC_Analysis_Item_Type::BAKE:
    {
        ModTree_Function* bake_function = modtree_function_create_empty(graph->analyser->program, 0, 0, item->options.bake.body);

        workload = workload_executer_add_workload_empty(graph, Analysis_Workload_Type::BAKE_EXECUTION, item, false);
        workload_executer_add_item_mapping(graph, workload, item);
        workload->options.bake_execute.bake_function = bake_function;
        workload->options.bake_execute.result = comptime_result_make_not_comptime();

        Analysis_Workload* analysis_workload = workload_executer_add_workload_empty(graph, Analysis_Workload_Type::BAKE_ANALYSIS, item, true);
        analysis_workload_create_child_workloads(graph, analysis_workload, item);
        analysis_workload_add_dependency_internal(graph, workload, analysis_workload, 0);
        analysis_workload->options.bake_analysis.bake_function = bake_function;
        analysis_workload->options.bake_analysis.execute_workload = workload;
        break;
    }
    case RC_Analysis_Item_Type::DEFINITION:
    {
        Symbol* symbol = item->options.definition.symbol;
        workload = workload_executer_add_workload_empty(graph, Analysis_Workload_Type::DEFINITION, item, true);
        analysis_workload_create_child_workloads(graph, workload, workload->analysis_item);
        hashtable_insert_element(&graph->progress_definitions, symbol, workload);
        workload_executer_add_item_mapping(graph, workload, item);
        break;
    }
    case RC_Analysis_Item_Type::FUNCTION:
    {
        ModTree_Function* function = modtree_function_create_empty(
            graph->analyser->program, 0, item->options.function.symbol, item->options.function.body_item->options.function_body
        );
        workload = workload_executer_add_workload_empty(graph, Analysis_Workload_Type::FUNCTION_HEADER, item, true);
        analysis_workload_create_child_workloads(graph, workload, workload->analysis_item);
        workload_executer_add_item_mapping(graph, workload, item);
        workload->options.function_header = function;

        Analysis_Workload* body_workload = workload_executer_add_workload_empty(
            graph, Analysis_Workload_Type::FUNCTION_BODY, item->options.function.body_item, true
        );
        workload_executer_add_item_mapping(graph, body_workload, item->options.function.body_item);
        body_workload->options.function_body.function = function;
        analysis_workload_create_child_workloads(graph, body_workload, body_workload->analysis_item);
        analysis_workload_add_dependency_internal(graph, body_workload, workload, 0);

        Analysis_Workload* compile_workload = workload_executer_add_workload_empty(graph, Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, 0, false);
        compile_workload->options.cluster_compile.functions = dynamic_array_create_empty<ModTree_Function*>(1);
        dynamic_array_push_back(&compile_workload->options.cluster_compile.functions, function);
        analysis_workload_add_dependency_internal(graph, compile_workload, body_workload, 0);

        Function_Progress initial_state;
        initial_state.state = Function_State::DEFINED;
        initial_state.body_workload = body_workload;
        initial_state.header_workload = workload;
        initial_state.compile_workload = compile_workload;
        hashtable_insert_element(&graph->progress_functions, function, initial_state);

        if (item->options.function.symbol != 0) {
            Symbol* symbol = item->options.function.symbol;
            symbol->type = Symbol_Type::FUNCTION;
            symbol->options.function = function;
        }
        break;
    }
    case RC_Analysis_Item_Type::FUNCTION_BODY:
    {
        panic("Should happen inside function");
        break;
    }
    case RC_Analysis_Item_Type::STRUCTURE:
    {
        Type_Signature* struct_type = type_system_make_struct_empty(
            &graph->analyser->compiler->type_system, item->options.structure.symbol, item->options.structure.structure_type
        );
        workload = workload_executer_add_workload_empty(graph, Analysis_Workload_Type::STRUCT_ANALYSIS, item, true);
        analysis_workload_create_child_workloads(graph, workload, workload->analysis_item);
        workload_executer_add_item_mapping(graph, workload, item);
        workload->options.struct_analysis_type = struct_type;

        Analysis_Workload* reachable_workload = workload_executer_add_workload_empty(graph, Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE, 0, false);
        reachable_workload->options.struct_reachable.struct_types = dynamic_array_create_empty<Type_Signature*>(1);
        reachable_workload->options.struct_reachable.unfinished_array_types = dynamic_array_create_empty<Type_Signature*>(1);
        dynamic_array_push_back(&reachable_workload->options.struct_reachable.struct_types, struct_type);
        analysis_workload_add_dependency_internal(graph, reachable_workload, workload, 0);

        Struct_Progress initial_state;
        initial_state.state = Struct_State::DEFINED;
        initial_state.member_workload = workload;
        initial_state.reachable_resolve_workload = reachable_workload;
        hashtable_insert_element(&graph->progress_structs, struct_type, initial_state);

        if (item->options.structure.symbol != 0) {
            Symbol* symbol = item->options.structure.symbol;
            symbol->type = Symbol_Type::TYPE;
            symbol->options.type = struct_type;
        }
        break;
    }
    default: panic("");
    }

    return workload;
}

void analysis_workload_check_if_runnable(Workload_Executer* graph, Analysis_Workload* workload)
{
    if (!workload->is_finished && workload->symbol_dependencies.size == 0 && workload->dependencies.count == 0) {
        dynamic_array_push_back(&graph->runnable_workloads, workload);
        graph->progress_was_made = true;
    }
}

void semantic_analyser_work_through_defers(Semantic_Analyser* analyser, ModTree_Block* modtree_block, int defer_start_index)
{
    assert(defer_start_index != -1, "");
    for (int i = analyser->defer_stack.size - 1; i >= defer_start_index; i--)
    {
        RC_Block* rc_block = analyser->defer_stack[i];
        ModTree_Statement* statement = modtree_block_add_statement_empty(modtree_block, ModTree_Statement_Type::BLOCK);
        statement->options.block = modtree_block_create_empty(rc_block);
        semantic_analyser_fill_block(analyser, statement->options.block, rc_block);
    }
}

bool analysis_workload_execute(Analysis_Workload* workload, Semantic_Analyser* analyser)
{
    analyser->current_workload = workload;
    analyser->current_function = 0;
    switch (workload->type)
    {
    case Analysis_Workload_Type::DEFINITION:
    {
        assert(workload->analysis_item->type == RC_Analysis_Item_Type::DEFINITION, "");
        auto definition = &workload->analysis_item->options.definition;
        Symbol* symbol = definition->symbol;

        if (!definition->is_comptime_definition) // Global variable definition
        {
            ModTree_Variable* variable;
            Type_Signature* type = 0;
            if (definition->type_expression.available) {
                type = semantic_analyser_analyse_expression_type(analyser, definition->type_expression.value);
            }
            ModTree_Expression* value = 0;
            if (definition->value_expression.available)
            {
                value = semantic_analyser_analyse_expression_value(
                    analyser, definition->value_expression.value, type != 0 ? expression_context_make_specific_type(type) : expression_context_make_unknown()
                );
                if (type == 0) {
                    type = value->result_type;
                }
            }
            variable = modtree_program_add_global(analyser->program, type, definition->symbol);
            symbol->type = Symbol_Type::VARIABLE;
            symbol->options.variable = variable;
            if (value != 0) {
                ModTree_Statement* statement = modtree_block_add_statement_empty(analyser->global_init_function->body, ModTree_Statement_Type::ASSIGNMENT);
                statement->options.assignment.destination = modtree_expression_create_variable_read(variable);
                statement->options.assignment.source = value;
            }
            ir_generator_queue_global(analyser->compiler->ir_generator, variable);
        }
        else // Constant definition
        {
            if (definition->type_expression.available) {
                Type_Signature* type = semantic_analyser_analyse_expression_type(analyser, definition->type_expression.value);
                semantic_analyser_log_error(analyser, Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED, definition->type_expression.value);
            }
            assert(definition->value_expression.available, "");
            Expression_Result result = semantic_analyser_analyse_expression_any(analyser, definition->value_expression.value, expression_context_make_unknown());
            switch (result.type)
            {
            case Expression_Result_Type::EXPRESSION:
            {
                Comptime_Result comptime = modtree_expression_calculate_comptime_value(analyser, result.options.expression);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    break;
                case Comptime_Result_Type::UNAVAILABLE: {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    return true;
                }
                case Comptime_Result_Type::NOT_COMPTIME: {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN, definition->value_expression.value);
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    return true;
                }
                default: panic("");
                }

                Constant_Result result = constant_pool_add_constant(
                    &analyser->compiler->constant_pool, comptime.data_type, array_create_static((byte*)comptime.data, comptime.data_type->size)
                );
                if (result.status != Constant_Status::SUCCESS) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::CONSTANT_POOL_ERROR, definition->value_expression.value);
                    semantic_analyser_add_error_info(analyser, error_information_make_constant_status(result.status));
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    break;
                }

                symbol->type = Symbol_Type::CONSTANT_VALUE;
                symbol->options.constant = result.constant;
                break;
            }
            case Expression_Result_Type::EXTERN_FUNCTION:
            {
                symbol->type = Symbol_Type::EXTERN_FUNCTION;
                symbol->options.extern_function = result.options.extern_function;
                break;
            }
            case Expression_Result_Type::HARDCODED_FUNCTION:
            {
                symbol->type = Symbol_Type::HARDCODED_FUNCTION;
                symbol->options.hardcoded_function = result.options.hardcoded_function;
                break;
            }
            case Expression_Result_Type::FUNCTION:
            {
                ModTree_Function* function = result.options.function;
                // I am not quite sure if this is correct/alias actually works
                if (function->symbol != 0) {
                    symbol->type = Symbol_Type::SYMBOL_ALIAS;
                    symbol->options.alias = function->symbol;
                }
                else {
                    symbol->type = Symbol_Type::FUNCTION;
                    symbol->options.function = result.options.function;
                }
                break;
            }
            case Expression_Result_Type::MODULE: {
                symbol->type = Symbol_Type::MODULE;
                symbol->options.module_table = result.options.module_table;
                break;
            }
            case Expression_Result_Type::TYPE: {
                symbol->type = Symbol_Type::TYPE;
                symbol->options.type = result.options.type;
                break;
            }
            default: panic("");
            }
        }
        break;
    }
    case Analysis_Workload_Type::FUNCTION_HEADER:
    {
        auto header = &workload->analysis_item->options.function;
        ModTree_Function* function = workload->options.function_header;
        analyser->current_function = function;

        // Analyser Header
        auto rc_sig = &header->signature_expression->options.function_signature;
        assert(rc_sig->parameters.size == header->parameter_symbols.size, "");
        int polymorphic_count = 0;

        // Determine parameter analysis order for polymorphic dependencies
        /*
        Dependency_Graph graph = dependency_graph_create();
        SCOPE_EXIT(dependency_graph_destroy(&graph));
        {
            Dynamic_Array<RC_Symbol_Read*> param_symbol_reads = dynamic_array_create_empty<RC_Symbol_Read*>(1);
            SCOPE_EXIT(dynamic_array_destroy(&param_symbol_reads));
            for (int i = 0; i < rc_sig->parameters.size; i++)
            {
                dynamic_array_reset(&param_symbol_reads);
                RC_Parameter* rc_param = &rc_sig->parameters[i];
                rc_expression_find_symbol_reads(rc_param->type_expression, &param_symbol_reads);
                for (int j = 0; j < param_symbol_reads.size; j++) {
                    RC_Symbol_Read* read = param_symbol_reads[j];
                    if (read->symbol->type == Symbol_Type::VARIABLE_UNDEFINED && read->symbol->options.variable_undefined.is_parameter) {

                    }
                }

            }
        }
        */

        // Analyse parameters
        for (int i = 0; i < rc_sig->parameters.size; i++)
        {
            RC_Parameter* rc_param = &rc_sig->parameters[i];
            Symbol* symbol = header->parameter_symbols[i];
            ModTree_Parameter modtree_param;
            modtree_param.data_type = semantic_analyser_analyse_expression_type(analyser, rc_sig->parameters[i].type_expression);
            modtree_param.is_comptime = rc_param->is_comptime;
            modtree_param.name = rc_param->param_id;
            if (rc_param->is_comptime) {
                symbol->type = Symbol_Type::POLYMORPHIC_PARAMETER;
                symbol->options.polymorphic.parameter_index = i;
                symbol->options.polymorphic.function = function;
                polymorphic_count += 1;
            }
            else {
                modtree_param.options.variable = modtree_variable_create(
                    modtree_param.data_type, header->parameter_symbols[i]
                );
                symbol->type = Symbol_Type::VARIABLE;
                symbol->options.variable = modtree_param.options.variable;
            }
            dynamic_array_push_back(&function->parameters, modtree_param);
        }
        if (rc_sig->return_type_expression.available) {
            function->return_type = semantic_analyser_analyse_expression_type(analyser, rc_sig->return_type_expression.value);
        }
        else {
            function->return_type = analyser->compiler->type_system.void_type;
        }
        if (polymorphic_count > 0) {
            function->type = ModTree_Function_Type::POLYMORPHIC_BASE;
            function->options.base.poly_argument_count = polymorphic_count;
            function->is_runnable = false;
        }

        // Create function signature
        Dynamic_Array<Type_Signature*> param_types = dynamic_array_create_empty<Type_Signature*>(math_maximum(0, rc_sig->parameters.size));
        for (int i = 0; i < function->parameters.size; i++) {
            if (!function->parameters[i].is_comptime) {
                dynamic_array_push_back(&param_types, function->parameters[i].data_type);
            }
        }
        function->signature = type_system_make_function(&analyser->compiler->type_system, param_types, function->return_type);

        // Advance function progress
        Function_Progress* progress = hashtable_find_element(&analyser->workload_executer.progress_functions, function);
        assert(progress != 0, "");
        progress->state = Function_State::HEADER_ANALYSED;
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY:
    {
        auto rc_body_block = workload->analysis_item->options.function_body;
        ModTree_Function* function = workload->options.function_body.function;
        analyser->current_function = function;
        ModTree_Block* block = function->body;

        if (function->type == ModTree_Function_Type::POLYMOPRHIC_INSTANCE) {
            ModTree_Function* base = function->options.instance.instance_base_function;
            if (base->contains_errors) {
                function->contains_errors = true;
                break;
            }
        }

        dynamic_array_reset(&analyser->block_stack);
        analyser->statement_reachable = true;
        semantic_analyser_fill_block(analyser, block, rc_body_block);
        Control_Flow flow = block->flow;
        if (flow != Control_Flow::RETURNS) {
            if (function->signature->options.function.return_type == analyser->compiler->type_system.void_type) {
                semantic_analyser_work_through_defers(analyser, function->body, 0);
                ModTree_Statement* return_statement = modtree_block_add_statement_empty(function->body, ModTree_Statement_Type::RETURN);
                return_statement->options.return_value.available = false;
            }
            else {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, (AST_Node*)0);
            }
        }
        Function_Progress* progress = hashtable_find_element(&analyser->workload_executer.progress_functions, function);
        assert(progress != 0, "");
        progress->state = Function_State::BODY_ANALYSED;
        break;
    }
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
    {
        // Check if the cluster contains errors
        bool cluster_contains_error = false;
        for (int i = 0; i < workload->options.cluster_compile.functions.size; i++) {
            ModTree_Function* function = workload->options.cluster_compile.functions[i];
            if (function->contains_errors) {
                cluster_contains_error = true;
                break;
            }
        }

        // Compile/Set error for all functions in cluster
        for (int i = 0; i < workload->options.cluster_compile.functions.size; i++)
        {
            ModTree_Function* function = workload->options.cluster_compile.functions[i];
            if (cluster_contains_error) {
                function->is_runnable = false;
            }
            else {
                ir_generator_queue_function(analyser->compiler->ir_generator, function);
            }
            Function_Progress* progress = hashtable_find_element(&analyser->workload_executer.progress_functions, function);
            assert(progress != 0, "");
            progress->state = Function_State::FINISHED;
        }
        break;
    }
    case Analysis_Workload_Type::STRUCT_ANALYSIS:
    {
        auto rc_struct = &workload->analysis_item->options.structure;
        Type_Signature* struct_signature = workload->options.struct_analysis_type;
        Struct_Progress* progress = hashtable_find_element(&analyser->workload_executer.progress_structs, struct_signature);
        assert(progress != 0, "");
        Hashset<Type_Signature*> visited_members = hashset_create_pointer_empty<Type_Signature*>(4);
        SCOPE_EXIT(hashset_destroy(&visited_members));

        for (int i = 0; i < rc_struct->members.size; i++) {
            RC_Struct_Member* rc_member = &rc_struct->members[i];
            Struct_Member member;
            member.id = rc_member->id;
            member.offset = 0;
            member.type = semantic_analyser_analyse_expression_type(analyser, rc_member->type_expression);
            assert(!(member.type->size == 0 && member.type->alignment == 0), "Must not happen with Dependency_Type system");
            dynamic_array_push_back(&struct_signature->options.structure.members, member);
        }
        progress->state = Struct_State::SIZE_KNOWN;
        type_system_finish_type(&analyser->compiler->type_system, struct_signature);
        break;
    }
    case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
    {
        for (int i = 0; i < workload->options.struct_reachable.unfinished_array_types.size; i++)
        {
            Type_Signature* array_type = workload->options.struct_reachable.unfinished_array_types[i];
            assert(array_type->type == Signature_Type::ARRAY, "");
            assert(!(array_type->options.array.element_type->size == 0 && array_type->options.array.element_type->alignment == 0), "");
            array_type->size = array_type->options.array.element_type->size * array_type->options.array.element_count;
            array_type->alignment = array_type->options.array.element_type->alignment;
            type_system_finish_type(&analyser->compiler->type_system, array_type);
        }
        for (int i = 0; i < workload->options.struct_reachable.struct_types.size; i++)
        {
            Type_Signature* struct_type = workload->options.struct_reachable.struct_types[i];
            Struct_Progress* progress = hashtable_find_element(&analyser->workload_executer.progress_structs, struct_type);
            if (progress->state == Struct_State::FINISHED) continue;
            progress->state = Struct_State::FINISHED;
        }
        break;
    }
    case Analysis_Workload_Type::BAKE_ANALYSIS:
    {
        auto rc_bake = &workload->analysis_item->options.bake;
        ModTree_Function* bake_function = workload->options.bake_analysis.bake_function;
        analyser->current_function = bake_function;

        Type_Signature* return_type = semantic_analyser_analyse_expression_type(analyser, rc_bake->type_expression);
        bake_function->signature = type_system_make_function(&analyser->compiler->type_system, dynamic_array_create_empty<Type_Signature*>(1), return_type);

        semantic_analyser_fill_block(analyser, analyser->current_function->body, rc_bake->body);
        Control_Flow flow = bake_function->body->flow;
        if (flow != Control_Flow::RETURNS) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, rc_to_ast(analyser, rc_bake->type_expression)->parent);
        }
        break;
    }
    case Analysis_Workload_Type::BAKE_EXECUTION:
    {
        auto rc_bake = &workload->analysis_item->options.bake;
        ModTree_Function* bake_function = workload->options.bake_execute.bake_function;
        Comptime_Result* result = &workload->options.bake_execute.result;

        // Check if function compilation succeeded
        if (bake_function->contains_errors) {
            *result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
            return true;
        }
        for (int i = 0; i < bake_function->calls.size; i++) {
            if (!bake_function->calls[i]->is_runnable) {
                bake_function->is_runnable = false;
                *result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
                return true;
            }
        }

        // Compile 
        ir_generator_queue_function(analyser->compiler->ir_generator, bake_function);
        ir_generator_queue_global(analyser->compiler->ir_generator, analyser->global_type_informations);
        ir_generator_generate_queued_items(analyser->compiler->ir_generator);

        // Set Global Type Informations
        {
            bytecode_interpreter_prepare_run(analyser->compiler->bytecode_interpreter);
            IR_Data_Access* global_access = hashtable_find_element(&analyser->compiler->ir_generator->variable_mapping, analyser->global_type_informations);
            assert(global_access != 0 && global_access->type == IR_Data_Access_Type::GLOBAL_DATA, "");
            Upp_Slice<Internal_Type_Information>* info_slice = (Upp_Slice<Internal_Type_Information>*)
                & analyser->compiler->bytecode_interpreter->globals.data[
                    analyser->compiler->bytecode_generator->global_data_offsets[global_access->index]
                ];
            info_slice->size = analyser->compiler->type_system.internal_type_infos.size;
            info_slice->data_ptr = analyser->compiler->type_system.internal_type_infos.data;
        }

        // Execute
        IR_Function* ir_func = *hashtable_find_element(&analyser->compiler->ir_generator->function_mapping, bake_function);
        int func_start_instr_index = *hashtable_find_element(&analyser->compiler->bytecode_generator->function_locations, ir_func);
        analyser->compiler->bytecode_interpreter->instruction_limit_enabled = true;
        analyser->compiler->bytecode_interpreter->instruction_limit = 5000;
        bytecode_interpreter_run_function(analyser->compiler->bytecode_interpreter, func_start_instr_index);
        if (analyser->compiler->bytecode_interpreter->exit_code != Exit_Code::SUCCESS) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED, rc_to_ast(analyser, rc_bake->type_expression)->parent);
            semantic_analyser_add_error_info(analyser, error_information_make_exit_code(analyser->compiler->bytecode_interpreter->exit_code));
            *result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
            return true;
        }

        void* value_ptr = analyser->compiler->bytecode_interpreter->return_register;
        *result = comptime_result_make_available(value_ptr, bake_function->signature->options.function.return_type);
        return true;
    }
    default: panic("");
    }

    return true;


    // OLD EXTERN IMPORTS
    /*
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
                    symbol_data_make_type(type), import_id_node
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
                    symbol_data_make_function(extern_fn), import_id_node
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
                        symbol_data_make_type(type), workload.options.extern_header.node
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
    Expression_Result_Type result = semantic_analyser_analyse_expression_type(
        analyser, work->parent_module->symbol_table, extern_node->child_start
    );
    switch (result.type)
    {
    case Analysis_Result_Type::SUCCESS:
    {
        if (result.options.type->type != Signature_Type::FUNCTION) {
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
        extern_fn->signature = result.options.type;
        extern_fn->options.extern_function.function_signature = extern_fn->signature;
        assert(extern_fn->signature->type == Signature_Type::FUNCTION, "HEY");
        dynamic_array_push_back(&extern_fn->parent_module->functions, extern_fn);
        dynamic_array_push_back(&analyser->compiler->extern_sources.extern_functions, extern_fn->options.extern_function);

        extern_fn->symbol = symbol_table_define_symbol(
            analyser, work->parent_module->symbol_table, extern_fn->options.extern_function.id,
            symbol_data_make_function(extern_fn), work->node
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
*/

}

void analysis_workload_append_to_string(Analysis_Workload* workload, String* string)
{
    switch (workload->type)
    {
    case Analysis_Workload_Type::DEFINITION: {
        auto definition = &workload->analysis_item->options.definition;
        if (definition->is_comptime_definition) {
            string_append_formated(string, "Comptime \"%s\"", definition->symbol->id->characters);
        }
        else {
            string_append_formated(string, "Global \"%s\"", definition->symbol->id->characters);
        }
        break;
    }
    case Analysis_Workload_Type::BAKE_ANALYSIS: {
        string_append_formated(string, "Bake-Analysis");
        break;
    }
    case Analysis_Workload_Type::BAKE_EXECUTION: {
        string_append_formated(string, "Bake-Execution");
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY: {
        Symbol* symbol = workload->options.function_body.function->symbol;
        const char* fn_id = symbol == 0 ? "Lambda" : symbol->id->characters;
        const char* appendix = "";
        if (workload->options.function_body.function->type == ModTree_Function_Type::POLYMORPHIC_BASE) {
            appendix = "(Polymorphic Base)";
        }
        else if (workload->options.function_body.function->type == ModTree_Function_Type::POLYMOPRHIC_INSTANCE) {
            appendix = "(Polymorphic Instance)";
        }
        string_append_formated(string, "Body \"%s\"%s", fn_id, appendix);
        break;
    }
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
    {
        string_append_formated(string, "Cluster-Compile [");
        Analysis_Workload* cluster = analysis_workload_find_associated_cluster(workload);
        for (int i = 0; i < cluster->options.cluster_compile.functions.size; i++) {
            Symbol* symbol = cluster->options.cluster_compile.functions[i]->symbol;
            const char* fn_id = symbol == 0 ? "Anonymous" : symbol->id->characters;
            string_append_formated(string, "%s, ", fn_id);
        }
        string_append_formated(string, "]");
        break;
    }
    case Analysis_Workload_Type::FUNCTION_HEADER: {
        Symbol* symbol = workload->options.function_header->symbol;
        const char* fn_id = symbol == 0 ? "Anonymous" : symbol->id->characters;
        string_append_formated(string, "Header \"%s\"", fn_id);
        break;
    }
    case Analysis_Workload_Type::STRUCT_ANALYSIS: {
        Symbol* symbol = workload->analysis_item->options.structure.symbol;
        const char* struct_id = symbol == 0 ? "Anonymous_Struct" : symbol->id->characters;
        string_append_formated(string, "Struct-Analysis \"%s\"", struct_id);
        break;
    }
    case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE: {
        //const char* struct_id = symbol == 0 ? "Anonymous_Struct" : symbol->id->characters;
        string_append_formated(string, "Struct-Reachable-Resolve [");
        Analysis_Workload* cluster = analysis_workload_find_associated_cluster(workload);
        for (int i = 0; i < cluster->options.struct_reachable.struct_types.size; i++) {
            Symbol* symbol = cluster->options.struct_reachable.struct_types[i]->options.structure.symbol;
            const char* fn_id = symbol == 0 ? "Anonymous" : symbol->id->characters;
            string_append_formated(string, "%s, ", fn_id);
        }
        string_append_formated(string, "]");


        break;
    }
    default: panic("");
    }
}




/*
    SEMANTIC ANALYSER
*/
// ANALYSER FUNCTIONS
Optional<ModTree_Cast_Type> semantic_analyser_check_if_cast_possible(
    Semantic_Analyser* analyser, Type_Signature* source_type, Type_Signature* destination_type, bool implicit_cast)
{
    Type_System* type_system = &analyser->compiler->type_system;
    if (source_type == type_system->unknown_type || destination_type == type_system->unknown_type) {
        semantic_analyser_set_error_flag(analyser, true);
        return optional_make_success(ModTree_Cast_Type::INTEGERS);
    }
    if (source_type == destination_type) return optional_make_failure<ModTree_Cast_Type>();
    bool cast_valid = false;
    ModTree_Cast_Type cast_type;
    // Pointer casting
    if (source_type->type == Signature_Type::POINTER || destination_type->type == Signature_Type::POINTER)
    {
        if (source_type->type == Signature_Type::POINTER && destination_type->type == Signature_Type::POINTER)
        {
            cast_type = ModTree_Cast_Type::POINTERS;
            if (implicit_cast) {
                cast_valid = source_type == analyser->compiler->type_system.void_ptr_type ||
                    destination_type == analyser->compiler->type_system.void_ptr_type;
            }
            else {
                cast_valid = true;
            }
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
                if (source_type->options.primitive.is_signed == destination_type->options.primitive.is_signed) {
                    cast_valid = source_type->size < destination_type->size;
                }
                else {
                    if (source_type->options.primitive.is_signed) {
                        cast_valid = false;
                    }
                    else {
                        cast_valid = destination_type->size > source_type->size; // E.g. u8 to i32, u32 to i64
                    }
                }
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
    // Any casting
    else if (destination_type == analyser->compiler->type_system.any_type)
    {
        cast_valid = true;
        cast_type = ModTree_Cast_Type::TO_ANY;
    }
    else if (source_type == analyser->compiler->type_system.any_type) {
        cast_valid = !implicit_cast;
        cast_type = ModTree_Cast_Type::FROM_ANY;
    }

    if (cast_valid) return optional_make_success(cast_type);
    return optional_make_failure<ModTree_Cast_Type>();
}

// If not possible, null is returned, otherwise an expression pointer is returned and the expression argument ownership is given to this pointer
ModTree_Expression* semantic_analyser_cast_implicit_if_possible(Semantic_Analyser* analyser, ModTree_Expression* expression, Type_Signature* destination_type)
{
    Type_Signature* source_type = expression->result_type;
    if (source_type == analyser->compiler->type_system.unknown_type || destination_type == analyser->compiler->type_system.unknown_type) {
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
        return new ModTree_Expression(cast_expr);
    }

    return 0;
}

void analysis_workload_register_function_call(Semantic_Analyser* analyser, ModTree_Function* call_to)
{
    if (analyser->current_function != 0) {
        dynamic_array_push_back(&analyser->current_function->calls, call_to);
        dynamic_array_push_back(&call_to->called_from, analyser->current_function);
    }

    Function_Progress* progress = hashtable_find_element(&analyser->workload_executer.progress_functions, call_to);
    if (progress == 0) return;
    switch (analyser->current_workload->type)
    {
    case Analysis_Workload_Type::BAKE_ANALYSIS: {
        Analysis_Workload* execute_workload = analyser->current_workload->options.bake_analysis.execute_workload;
        analysis_workload_add_dependency_internal(&analyser->workload_executer, execute_workload, progress->compile_workload, 0);
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY: {
        Function_Progress* my_progress = hashtable_find_element(&analyser->workload_executer.progress_functions, analyser->current_function);
        assert(progress != 0, "");
        analysis_workload_add_cluster_dependency(&analyser->workload_executer, my_progress->compile_workload, progress->compile_workload, 0);
        break;
    }
    default: return;
    }
}

Expression_Result semantic_analyser_analyse_expression_internal(Semantic_Analyser* analyser, RC_Expression* rc_expression, Expression_Context context)
{
    Type_System* type_system = &analyser->compiler->type_system;
    bool error_exit = false;
    switch (rc_expression->type)
    {
    case RC_Expression_Type::FUNCTION_CALL:
    {
        // Analyse call expression
        Expression_Result function_expr_result = semantic_analyser_analyse_expression_any(
            analyser, rc_expression->options.function_call.call_expr, expression_context_make_auto_dereference()
        );
        SCOPE_EXIT(if (error_exit && function_expr_result.type == Expression_Result_Type::EXPRESSION)
            modtree_expression_destroy(function_expr_result.options.expression);
        );

        ModTree_Expression* expr_result = modtree_expression_create_empty(ModTree_Expression_Type::FUNCTION_CALL, 0);
        Type_Signature* function_signature = 0;
        switch (function_expr_result.type)
        {
        case Expression_Result_Type::FUNCTION:
        {
            // Function calls now get a different Code-Path, because of Polymorphic Functions
            Dynamic_Array<RC_Expression*> arguments = rc_expression->options.function_call.arguments;
            auto call = &expr_result->options.function_call;
            call->call_type = ModTree_Call_Type::FUNCTION;
            call->arguments = dynamic_array_create_empty<ModTree_Expression*>(1);
            call->options.function = function_expr_result.options.function;

            ModTree_Function* function = function_expr_result.options.function;
            int compare_error_flag_count = analyser->error_flag_count;
            if (arguments.size != function->parameters.size) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_argument_count(arguments.size, function->parameters.size));
                semantic_analyser_add_error_info(analyser, error_information_make_function_type(function->signature));
            }
            // Parse arguments
            for (int i = 0; i < arguments.size && i < function->parameters.size; i++) {
                ModTree_Expression* argument_expr = semantic_analyser_analyse_expression_value(
                    analyser, arguments[i], expression_context_make_specific_type(function->parameters[i].data_type)
                );
                dynamic_array_push_back(&call->arguments, argument_expr);
            }
            // Analyse overflowing arguments 
            for (int i = function->parameters.size; i < arguments.size; i++) {
                dynamic_array_push_back(&call->arguments,
                    semantic_analyser_analyse_expression_value(analyser, arguments[i], expression_context_make_unknown())
                );
            }

            bool instanciate_function = analyser->error_flag_count == compare_error_flag_count;
            if (function->type == ModTree_Function_Type::POLYMORPHIC_BASE && instanciate_function)
            {
                Dynamic_Array<Upp_Constant> comptime_parameters = dynamic_array_create_empty<Upp_Constant>(function->options.base.poly_argument_count);
                // Run over all parameters and evaluate their constant value
                for (int i = 0; i < function->parameters.size; i++)
                {
                    if (!function->parameters[i].is_comptime) continue;
                    Comptime_Result comptime = modtree_expression_calculate_comptime_value(analyser, call->arguments[i]);
                    switch (comptime.type)
                    {
                    case Comptime_Result_Type::AVAILABLE:
                    {
                        Constant_Result result = constant_pool_add_constant(
                            &analyser->compiler->constant_pool, call->arguments[i]->result_type,
                            array_create_static((byte*)comptime.data, comptime.data_type->size)
                        );
                        if (result.status != Constant_Status::SUCCESS) {
                            semantic_analyser_log_error(analyser, Semantic_Error_Type::COMPTIME_ARGUMENT_NOT_KNOWN_AT_COMPTIME, arguments[i]);
                            semantic_analyser_add_error_info(analyser, error_information_make_constant_status(result.status));
                            instanciate_function = false;
                        }
                        else {
                            dynamic_array_push_back(&comptime_parameters, result.constant);
                        }
                        break;
                    }
                    case Comptime_Result_Type::UNAVAILABLE:
                        instanciate_function = false;
                        break;
                    case Comptime_Result_Type::NOT_COMPTIME: {
                        semantic_analyser_log_error(analyser, Semantic_Error_Type::COMPTIME_ARGUMENT_NOT_KNOWN_AT_COMPTIME, arguments[i]);
                        instanciate_function = false;
                        break;
                    }
                    default: panic("");
                    }
                }

                // Instanciate function
                if (instanciate_function) {
                    call->options.function = modtree_function_create_poly_instance(analyser, function, comptime_parameters);
                    analysis_workload_register_function_call(analyser, call->options.function);
                }
            }
            expr_result->result_type = call->options.function->return_type;
            return expression_result_make_value(expr_result);
        }
        case Expression_Result_Type::HARDCODED_FUNCTION: {
            expr_result->options.function_call.call_type = ModTree_Call_Type::HARDCODED_FUNCTION;
            expr_result->options.function_call.options.hardcoded_function = function_expr_result.options.hardcoded_function;
            function_signature = function_expr_result.options.hardcoded_function->signature;
            break;
        }
        case Expression_Result_Type::EXTERN_FUNCTION: {
            expr_result->options.function_call.call_type = ModTree_Call_Type::EXTERN_FUNCTION;
            expr_result->options.function_call.options.extern_function = function_expr_result.options.extern_function;
            function_signature = function_expr_result.options.extern_function->extern_function.function_signature;
            break;
        }
        case Expression_Result_Type::EXPRESSION: {
            // TODO: Check if this is comptime known, then we dont need a function pointer call
            function_signature = function_expr_result.options.expression->result_type;
            if (function_signature->type == Signature_Type::FUNCTION) {
                expr_result->options.function_call.call_type = ModTree_Call_Type::FUNCTION_POINTER;
                expr_result->options.function_call.options.pointer_expression = function_expr_result.options.expression;
            }
            else {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(function_signature));
                error_exit = true;
                return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
            }
            break;
        }
        case Expression_Result_Type::MODULE:
        case Expression_Result_Type::TYPE: {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPECTED_CALLABLE, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_expression_result_type(function_expr_result.type));
            error_exit = true;
            return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
        }
        default: panic("");
        }
        expr_result->result_type = function_signature->options.function.return_type;

        // Analyse arguments
        Dynamic_Array<RC_Expression*> arguments = rc_expression->options.function_call.arguments;
        Dynamic_Array<Type_Signature*> parameters = function_signature->options.function.parameter_types;
        if (arguments.size != parameters.size) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_argument_count(arguments.size, parameters.size));
            semantic_analyser_add_error_info(analyser, error_information_make_function_type(function_signature));
        }

        expr_result->options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(arguments.size);
        for (int i = 0; i < arguments.size; i++)
        {
            Type_Signature* expected_type = i < parameters.size ? parameters[i] : type_system->unknown_type;
            ModTree_Expression* argument_expr = semantic_analyser_analyse_expression_value(
                analyser, arguments[i], expression_context_make_specific_type(expected_type)
            );
            dynamic_array_push_back(&expr_result->options.function_call.arguments, argument_expr);
        }
        return expression_result_make_value(expr_result);
    }
    case RC_Expression_Type::TYPE_INFO:
    {
        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
            analyser, rc_expression->options.type_info_expression, expression_context_make_specific_type(type_system->type_type)
        );

        ModTree_Expression* global_read = modtree_expression_create_empty(ModTree_Expression_Type::VARIABLE_READ, analyser->global_type_informations->data_type);
        global_read->options.variable_read = analyser->global_type_informations;

        ModTree_Expression* index_access = new ModTree_Expression;
        index_access->result_type = type_system->i32_type;
        index_access->expression_type = ModTree_Expression_Type::CAST;
        index_access->options.cast.cast_argument = operand;
        operand->result_type = type_system->u64_type;
        index_access->options.cast.type = ModTree_Cast_Type::INTEGERS;

        ModTree_Expression* array_access = new ModTree_Expression;
        array_access->expression_type = ModTree_Expression_Type::ARRAY_ACCESS;
        array_access->options.array_access.array_expression = global_read;
        array_access->options.array_access.index_expression = index_access;
        array_access->result_type = analyser->compiler->type_system.type_information_type;

        return expression_result_make_value(array_access);
    }
    case RC_Expression_Type::TYPE_OF:
    {
        Expression_Result any_result = semantic_analyser_analyse_expression_any(
            analyser, rc_expression->options.type_of_expression, expression_context_make_unknown()
        );
        switch (any_result.type)
        {
        case Expression_Result_Type::EXPRESSION: {
            Type_Signature* result_type = any_result.options.expression->result_type;
            modtree_expression_destroy(any_result.options.expression);
            return expression_result_make_type(result_type);
        }
        case Expression_Result_Type::EXTERN_FUNCTION: {
            return expression_result_make_type(any_result.options.extern_function->extern_function.function_signature);
        }
        case Expression_Result_Type::HARDCODED_FUNCTION: {
            return expression_result_make_type(any_result.options.hardcoded_function->signature);
        }
        case Expression_Result_Type::FUNCTION: {
            return expression_result_make_type(any_result.options.function->signature);
        }
        case Expression_Result_Type::TYPE: {
            return expression_result_make_type(analyser->compiler->type_system.type_type);
        }
        case Expression_Result_Type::MODULE: {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_EXPRESSION_TYPE, rc_expression->options.type_of_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_expression_result_type(any_result.type));
            return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
        }
        default: panic("");
        }

        panic("");
        return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
    }
    case RC_Expression_Type::SYMBOL_READ:
    {
        Symbol* symbol = rc_expression->options.symbol_read->symbol;
        assert(symbol != 0, "Must be given by dependency analysis");
        while (symbol->type == Symbol_Type::SYMBOL_ALIAS) {
            symbol = symbol->options.alias;
        }
        switch (symbol->type)
        {
        case Symbol_Type::ERROR_SYMBOL: {
            semantic_analyser_set_error_flag(analyser, true);
            //semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_EXPRESSION_TYPE, rc_expression);
            //semantic_analyser_add_error_info(analyser, error_information_make_symbol(symbol));
            return expression_result_make_error(type_system->unknown_type);
        }
        case Symbol_Type::UNRESOLVED: {
            panic("Should not happen");
        }
        case Symbol_Type::VARIABLE_UNDEFINED: {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET, rc_expression);
            return expression_result_make_error(type_system->unknown_type);
        }
        case Symbol_Type::POLYMORPHIC_PARAMETER: {
            assert(analyser->current_workload->type == Analysis_Workload_Type::FUNCTION_BODY, "");
            ModTree_Function* function = analyser->current_workload->options.function_body.function;
            switch (function->type)
            {
            case ModTree_Function_Type::NORMAL: {
                panic("Normally I dont access polymorphic parameters in a non-polymorphic context");
            }
            case ModTree_Function_Type::POLYMORPHIC_BASE: {
                ModTree_Parameter* param = &symbol->options.polymorphic.function->parameters[symbol->options.polymorphic.parameter_index];
                assert(param->is_comptime, "");
                return expression_result_make_error(param->data_type);
            }
            case ModTree_Function_Type::POLYMOPRHIC_INSTANCE: {
                Upp_Constant arg = function->options.instance.poly_arguments[symbol->options.polymorphic.parameter_index];
                ModTree_Expression* comptime_arg_read = modtree_expression_create_empty(ModTree_Expression_Type::CONSTANT_READ, arg.type);
                comptime_arg_read->options.constant_read = arg;
                return expression_result_make_value(comptime_arg_read);
            }
            default: panic("");
            }
            if (function->type == ModTree_Function_Type::POLYMORPHIC_BASE) {
            }
            panic("Here instances of the parameter must be accessed");
            break;
        }
        case Symbol_Type::EXTERN_FUNCTION: {
            return expression_result_make_extern_function(symbol->options.extern_function);
        }
        case Symbol_Type::HARDCODED_FUNCTION: {
            return expression_result_make_hardcoded(symbol->options.hardcoded_function);
        }
        case Symbol_Type::FUNCTION: {
            analysis_workload_register_function_call(analyser, symbol->options.function);
            return expression_result_make_function(symbol->options.function);
        }
        case Symbol_Type::TYPE: {
            return expression_result_make_type(symbol->options.type);
        }
        case Symbol_Type::VARIABLE: {
            ModTree_Expression* expression = modtree_expression_create_empty(ModTree_Expression_Type::VARIABLE_READ, symbol->options.variable->data_type);
            expression->options.variable_read = symbol->options.variable;
            return expression_result_make_value(expression);
        }
        case Symbol_Type::CONSTANT_VALUE: {
            ModTree_Expression* expression = modtree_expression_create_empty(ModTree_Expression_Type::CONSTANT_READ, symbol->options.constant.type);
            expression->options.constant_read = symbol->options.constant;
            return expression_result_make_value(expression);
        }
        case Symbol_Type::MODULE: {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::SYMBOL_MODULE_INVALID, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_symbol(symbol));
            return expression_result_make_error(type_system->unknown_type);
        }
        default: panic("HEY");
        }

        panic("HEY");
        break;
    }
    case RC_Expression_Type::CAST:
    {
        auto cast = &rc_expression->options.cast;
        Type_Signature* destination_type = 0;
        if (cast->type_expression != 0) {
            destination_type = semantic_analyser_analyse_expression_type(analyser, cast->type_expression);
        }

        // Determine Context and Destination Type
        Expression_Context operand_context = expression_context_make_unknown();
        switch (cast->type)
        {
        case RC_Cast_Type::TYPE_TO_TYPE: {
            assert(destination_type != 0, "");
            break;
        }
        case RC_Cast_Type::AUTO_CAST: {
            assert(destination_type == 0, "");
            if (context.type != Expression_Context_Type::SPECIFIC_TYPE) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED, rc_expression);
                return expression_result_make_error(type_system->unknown_type);
            }
            destination_type = context.signature;
            break;
        }
        case RC_Cast_Type::RAW_TO_PTR: {
            operand_context = expression_context_make_specific_type(type_system->u64_type);
            assert(destination_type != 0, "");
            if (destination_type->type != Signature_Type::POINTER) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(destination_type));
            }
            break;
        }
        case RC_Cast_Type::PTR_TO_RAW: {
            assert(destination_type == 0, "");
            destination_type = type_system->u64_type;
            break;
        }
        default: panic("");
        }

        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(analyser, cast->operand, operand_context);
        ModTree_Expression* result_expr = modtree_expression_create_empty(ModTree_Expression_Type::CAST, destination_type);
        result_expr->options.cast.cast_argument = operand;
        result_expr->options.cast.type = ModTree_Cast_Type::INTEGERS; // Placeholder

        // Determine Cast type
        switch (cast->type)
        {
        case RC_Cast_Type::AUTO_CAST:
        case RC_Cast_Type::TYPE_TO_TYPE:
        {
            assert(destination_type != 0, "");
            Optional<ModTree_Cast_Type> cast_valid = semantic_analyser_check_if_cast_possible(
                analyser, operand->result_type, destination_type, false
            );
            if (cast_valid.available) {
                result_expr->options.cast.type = cast_valid.value;
            }
            else {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_INVALID_CAST, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(operand->result_type));
                semantic_analyser_add_error_info(analyser, error_information_make_expected_type(destination_type));
            }
            break;
        }
        case RC_Cast_Type::PTR_TO_RAW: {
            if (operand->result_type->type != Signature_Type::POINTER) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_INVALID_CAST, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(operand->result_type));
                semantic_analyser_add_error_info(analyser, error_information_make_expected_type(destination_type));
            }
            result_expr->options.cast.type = ModTree_Cast_Type::POINTER_TO_U64;
            break;
        }
        case RC_Cast_Type::RAW_TO_PTR:
        {
            result_expr->options.cast.type = ModTree_Cast_Type::U64_TO_POINTER;
            break;
        }
        default: panic("");
        }
        return expression_result_make_value(result_expr);
    }
    case RC_Expression_Type::LITERAL_READ:
    {
        Token* token = &rc_expression->options.literal_read;
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

        return expression_result_make_value(modtree_expression_create_constant(
            analyser, literal_type, array_create_static<byte>((byte*)value_ptr, literal_type->size), rc_to_ast(analyser, rc_expression)
        ));
    }
    case RC_Expression_Type::ENUM:
    {
        auto rc_enum = &rc_expression->options.enumeration;
        Type_Signature* enum_type = type_system_make_enum_empty(type_system, 0);
        int next_member_value = 1;
        for (int i = 0; i < rc_enum->members.size; i++)
        {
            RC_Enum_Member* rc_member = &rc_enum->members[i];
            if (rc_member->value_expression.available)
            {
                ModTree_Expression* expression = semantic_analyser_analyse_expression_value(
                    analyser, rc_member->value_expression.value, expression_context_make_specific_type(type_system->i32_type)
                );
                Comptime_Result comptime = modtree_expression_calculate_comptime_value(analyser, expression);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    next_member_value = *(i32*)comptime.data;
                    break;
                case Comptime_Result_Type::UNAVAILABLE:
                    break;
                case Comptime_Result_Type::NOT_COMPTIME:
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN, rc_member->value_expression.value);
                    break;
                default: panic("");
                }
            }

            Enum_Member member;
            member.definition_node = rc_member->node;
            member.id = rc_member->id;
            member.value = next_member_value;
            next_member_value++;
            dynamic_array_push_back(&enum_type->options.enum_type.members, member);
        }

        // Finish up enum
        enum_type->size = type_system->i32_type->size;
        enum_type->alignment = type_system->i32_type->alignment;
        for (int i = 0; i < enum_type->options.enum_type.members.size; i++)
        {
            Enum_Member* member = &enum_type->options.enum_type.members[i];
            for (int j = i + 1; j < enum_type->options.enum_type.members.size; j++)
            {
                Enum_Member* other = &enum_type->options.enum_type.members[j];
                if (other->id == member->id) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE, other->definition_node);
                    semantic_analyser_add_error_info(analyser, error_information_make_id(other->id));
                }
                if (other->value == member->value) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE, other->definition_node);
                    semantic_analyser_add_error_info(analyser, error_information_make_id(other->id));
                }
            }
        }
        type_system_finish_type(&analyser->compiler->type_system, enum_type);
        return expression_result_make_type(enum_type);
    }
    case RC_Expression_Type::MODULE: {
        return expression_result_make_module(rc_expression->options.module_table);
    }
    case RC_Expression_Type::ANALYSIS_ITEM:
    {
        RC_Analysis_Item* item = rc_expression->options.analysis_item;
        Analysis_Workload** workload_opt = hashtable_find_element(&analyser->workload_executer.item_to_workload_mapping, item);
        assert(workload_opt != 0, "");
        Analysis_Workload* workload = *workload_opt;
        switch (item->type)
        {
        case RC_Analysis_Item_Type::FUNCTION: {
            assert(workload->type == Analysis_Workload_Type::FUNCTION_HEADER, "");
            ModTree_Function* function = workload->options.function_header;
            analysis_workload_register_function_call(analyser, function);
            return expression_result_make_function(function);
        }
        case RC_Analysis_Item_Type::STRUCTURE: {
            Type_Signature* struct_type = workload->options.struct_analysis_type;
            assert(!(struct_type->size == 0 && struct_type->alignment == 0), "");
            return expression_result_make_type(struct_type);
        }
        case RC_Analysis_Item_Type::BAKE:
        {
            assert(workload->type == Analysis_Workload_Type::BAKE_EXECUTION, "");
            auto bake_result = &workload->options.bake_execute.result;
            switch (bake_result->type)
            {
            case Comptime_Result_Type::AVAILABLE: {
                ModTree_Expression* comp_expr = modtree_expression_create_constant(
                    analyser, bake_result->data_type, array_create_static((byte*)bake_result->data, bake_result->data_type->size),
                    rc_to_ast(analyser, workload->analysis_item->options.bake.type_expression)->parent
                );
                return expression_result_make_value(comp_expr);
            }
            case Comptime_Result_Type::UNAVAILABLE:
                return expression_result_make_error(bake_result->data_type);
            case Comptime_Result_Type::NOT_COMPTIME:
                panic("Should not happen with bake!");
                break;
            default: panic("");
            }
            panic("");
            break;
        }
        case RC_Analysis_Item_Type::ROOT:
        case RC_Analysis_Item_Type::FUNCTION_BODY:
        case RC_Analysis_Item_Type::DEFINITION:
            panic("Should not be found as expression!");
        default: panic("");
        }
        panic("");
        return expression_result_make_error(type_system->unknown_type);
    }
    case RC_Expression_Type::FUNCTION_SIGNATURE:
    {
        auto rc_sig = &rc_expression->options.function_signature;
        Dynamic_Array<Type_Signature*> parameters = dynamic_array_create_empty<Type_Signature*>(math_maximum(0, rc_sig->parameters.size));
        for (int i = 0; i < rc_sig->parameters.size; i++) {
            RC_Parameter* param = &rc_sig->parameters[i];
            if (param->is_comptime) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::MISSING_FEATURE, param->param_node);
            }
            else {
                dynamic_array_push_back(&parameters, semantic_analyser_analyse_expression_type(analyser, rc_sig->parameters[i].type_expression));
            }
        }
        Type_Signature* return_type;
        if (rc_sig->return_type_expression.available) {
            return_type = semantic_analyser_analyse_expression_type(analyser, rc_sig->return_type_expression.value);
        }
        else {
            return_type = type_system->void_type;
        }
        return expression_result_make_type(type_system_make_function(type_system, parameters, return_type));
    }
    case RC_Expression_Type::ARRAY_TYPE:
    {
        auto rc_array = &rc_expression->options.array_type;
        ModTree_Expression* size_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_array->size_expression, expression_context_make_specific_type(type_system->i32_type)
        );
        SCOPE_EXIT(modtree_expression_destroy(size_expr));
        Comptime_Result comptime = modtree_expression_calculate_comptime_value(analyser, size_expr);
        int array_size = 0;
        bool array_size_known = false;
        switch (comptime.type)
        {
        case Comptime_Result_Type::AVAILABLE: {
            array_size_known = true;
            array_size = *(i32*)comptime.data;
            if (array_size <= 0) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::ARRAY_SIZE_MUST_BE_GREATER_ZERO, rc_array->size_expression);
                array_size_known = false;
            }
            break;
        }
        case Comptime_Result_Type::UNAVAILABLE:
            array_size_known = false;
            break;
        case Comptime_Result_Type::NOT_COMPTIME:
            semantic_analyser_log_error(analyser, Semantic_Error_Type::ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN, rc_array->size_expression);
            array_size_known = false;
            break;
        default: panic("");
        }

        Type_Signature* element_type = semantic_analyser_analyse_expression_type(analyser, rc_array->element_type_expression);
        Type_Signature* array_type = type_system_make_array(type_system, element_type, array_size_known, array_size);
        if (element_type->size == 0 && element_type->alignment == 0)
        {
            assert(analyser->current_workload->type == Analysis_Workload_Type::STRUCT_ANALYSIS, "");
            Struct_Progress* progress = hashtable_find_element(
                &analyser->workload_executer.progress_structs, analyser->current_workload->options.struct_analysis_type
            );
            assert(progress != 0, "");
            assert(progress->state != Struct_State::FINISHED, "Finished structs cannot be of size + alignment 0");
            Analysis_Workload* cluster = analysis_workload_find_associated_cluster(progress->reachable_resolve_workload);
            dynamic_array_push_back(&cluster->options.struct_reachable.unfinished_array_types, array_type);
        }
        return expression_result_make_type(array_type);
    }
    case RC_Expression_Type::SLICE_TYPE:
    {
        return expression_result_make_type(
            type_system_make_slice(
                type_system, semantic_analyser_analyse_expression_type(analyser, rc_expression->options.slice_type.element_type_expression)
            )
        );
    }
    case RC_Expression_Type::NEW_EXPR:
    {
        auto rc_new = &rc_expression->options.new_expression;
        Type_Signature* allocated_type = semantic_analyser_analyse_expression_type(analyser, rc_new->type_expression);
        if (allocated_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, rc_new->type_expression);
        }
        assert(!(allocated_type->size == 0 && allocated_type->alignment == 0), "HEY");

        Type_Signature* result_type = 0;
        Optional<ModTree_Expression*> count_expr;
        if (rc_new->count_expression.available)
        {
            result_type = type_system_make_slice(type_system, allocated_type);
            ModTree_Expression* count = semantic_analyser_analyse_expression_value(
                analyser, rc_new->count_expression.value, expression_context_make_specific_type(type_system->i32_type)
            );
            count_expr = optional_make_success(count);
        }
        else {
            result_type = type_system_make_pointer(type_system, allocated_type);
            count_expr = optional_make_failure<ModTree_Expression*>();
        }

        ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::NEW_ALLOCATION, result_type);
        result->options.new_allocation.allocation_size = result_type->size;
        result->options.new_allocation.element_count = count_expr;
        return expression_result_make_value(result);
    }
    case RC_Expression_Type::STRUCT_INITIALIZER:
    {
        auto rc_init = &rc_expression->options.struct_initializer;
        Type_Signature* struct_signature = 0;
        if (rc_init->type_expression.available) {
            struct_signature = semantic_analyser_analyse_expression_type(analyser, rc_init->type_expression.value);
        }
        else {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE) {
                struct_signature = context.signature;
            }
            else {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE, rc_expression);
                return expression_result_make_error(type_system->unknown_type);
            }
        }

        if (struct_signature->type != Signature_Type::STRUCT) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(struct_signature));
            return expression_result_make_error(struct_signature);
        }
        if (rc_init->member_initializers.size == 0) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, rc_expression);
            return expression_result_make_error(struct_signature);
        }
        assert(!(struct_signature->size == 0 && struct_signature->alignment == 0), "");

        Dynamic_Array<Member_Initializer> initializers = dynamic_array_create_empty<Member_Initializer>(struct_signature->size);
        for (int i = 0; i < rc_init->member_initializers.size; i++)
        {
            RC_Member_Initializer* rc_member = &rc_init->member_initializers[i];
            if (!rc_member->member_id.available) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::MISSING_FEATURE, rc_member->init_expression);
                continue;
            }
            String* member_id = rc_member->member_id.value;
            Struct_Member* found_member = 0;
            for (int i = 0; i < struct_signature->options.structure.members.size; i++) {
                if (struct_signature->options.structure.members[i].id == member_id) {
                    found_member = &struct_signature->options.structure.members[i];
                }
            }
            if (found_member != 0)
            {
                Member_Initializer initializer;
                initializer.init_expr = semantic_analyser_analyse_expression_value(
                    analyser, rc_member->init_expression, expression_context_make_specific_type(found_member->type)
                );
                initializer.init_node = rc_to_ast(analyser, rc_member->init_expression);
                initializer.member = *found_member;
                dynamic_array_push_back(&initializers, initializer);
            }
            else {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST, rc_member->init_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_id(member_id));
            }
        }

        // Check for errors
        if (struct_signature->options.structure.struct_type != Structure_Type::STRUCT)
        {
            if (rc_init->member_initializers.size > 1) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER, rc_expression);
            }
            else if (struct_signature->options.structure.struct_type == Structure_Type::UNION && initializers.size == 1)
            {
                if (initializers[0].member.offset == struct_signature->options.structure.tag_member.offset) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG, initializers[0].init_node);
                }
                else
                {
                    Type_Signature* tag_type = struct_signature->options.structure.tag_member.type;
                    assert(tag_type->type == Signature_Type::ENUM, "");
                    int value = 0;
                    for (int i = 0; i < tag_type->options.enum_type.members.size; i++) {
                        Enum_Member* member = &tag_type->options.enum_type.members[i];
                        if (member->id == initializers[0].member.id) {
                            value = member->value;
                            break;
                        }
                    }
                    // Set tag in initilaizer
                    Member_Initializer tag_init;
                    tag_init.init_node = 0;
                    tag_init.init_expr = modtree_expression_create_constant_i32(analyser, value);
                    tag_init.init_expr->result_type = tag_type;
                    tag_init.member = struct_signature->options.structure.tag_member;
                    dynamic_array_push_back(&initializers, tag_init);
                }
            }
        }
        else
        {
            // Check that all members aren't initilized more than once
            for (int i = 0; i < initializers.size; i++)
            {
                Member_Initializer* member = &initializers[i];
                for (int j = i + 1; j < initializers.size; j++)
                {
                    Member_Initializer* other = &initializers[j];
                    if (member->member.id == other->member.id) {
                        semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE, other->init_node);
                    }
                }
            }
            // Check if all members are initiliazed
            if (initializers.size != struct_signature->options.structure.members.size) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, rc_expression);
            }
        }

        ModTree_Expression* expr = modtree_expression_create_empty(ModTree_Expression_Type::STRUCT_INITIALIZER, struct_signature);
        expr->options.struct_initializer = initializers;
        return expression_result_make_value(expr);
    }
    case RC_Expression_Type::ARRAY_INITIALIZER:
    {
        auto rc_array_init = &rc_expression->options.array_initializer;
        Type_Signature* element_type = 0;
        if (rc_array_init->type_expression.available) {
            element_type = semantic_analyser_analyse_expression_type(analyser, rc_array_init->type_expression.value);
        }
        else
        {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE)
            {
                if (context.signature->type == Signature_Type::ARRAY) {
                    element_type = context.signature->options.array.element_type;
                }
                else if (context.signature->type == Signature_Type::SLICE) {
                    element_type = context.signature->options.slice.element_type;
                }
                else {
                    element_type = 0;
                }
            }
            if (element_type == 0) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE, rc_expression);
                return expression_result_make_error(type_system->unknown_type);
            }
        }

        if (element_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, rc_expression);
            return expression_result_make_error(type_system->unknown_type);
        }
        assert(!(element_type->size == 0 && element_type->alignment == 0), "");

        int array_element_count = rc_array_init->element_initializers.size;
        // There are no 0-sized arrays, only 0-sized slices. So if we encounter an empty initializer, e.g. type.[], we return an empty slice
        if (array_element_count == 0)
        {
            Upp_Slice_Base data_slice;
            data_slice.data_ptr = 0;
            data_slice.size = 0;
            return expression_result_make_value(
                modtree_expression_create_constant(
                    analyser, type_system_make_slice(&analyser->compiler->type_system, element_type),
                    array_create_static_as_bytes(&data_slice, 1), 0
                )
            );
        }

        Dynamic_Array<ModTree_Expression*> init_expressions = dynamic_array_create_empty<ModTree_Expression*>(array_element_count + 1);
        for (int i = 0; i < rc_array_init->element_initializers.size; i++)
        {
            RC_Expression* rc_element_expr = rc_array_init->element_initializers[i];
            ModTree_Expression* element_expr = semantic_analyser_analyse_expression_value(
                analyser, rc_element_expr, expression_context_make_specific_type(element_type)
            );
            dynamic_array_push_back(&init_expressions, element_expr);
        }

        ModTree_Expression* result = modtree_expression_create_empty(
            ModTree_Expression_Type::ARRAY_INITIALIZER, type_system_make_array(&analyser->compiler->type_system, element_type, true, array_element_count)
        );
        result->options.array_initializer = init_expressions;
        return expression_result_make_value(result);
    }
    case RC_Expression_Type::ARRAY_ACCESS:
    {
        auto rc_array_access = &rc_expression->options.array_access;
        ModTree_Expression* array_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_array_access->array_expression, expression_context_make_auto_dereference()
        );

        Type_Signature* array_type = array_expr->result_type;
        Type_Signature* element_type = 0;
        if (array_type->type == Signature_Type::ARRAY) {
            element_type = array_type->options.array.element_type;
        }
        else if (array_type->type == Signature_Type::SLICE) {
            element_type = array_type->options.slice.element_type;
        }
        else {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(array_type));
            element_type = type_system->unknown_type;
        }

        ModTree_Expression* index_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_array_access->index_expression, expression_context_make_specific_type(type_system->i32_type)
        );
        ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::ARRAY_ACCESS, element_type);
        result->options.array_access.array_expression = array_expr;
        result->options.array_access.index_expression = index_expr;
        return expression_result_make_value(result);
    }
    case RC_Expression_Type::MEMBER_ACCESS:
    {
        auto rc_member_access = &rc_expression->options.member_access;
        Expression_Result access_expr_result = semantic_analyser_analyse_expression_any(
            analyser, rc_member_access->expression, expression_context_make_auto_dereference()
        );
        switch (access_expr_result.type)
        {
        case Expression_Result_Type::TYPE:
        {
            Type_Signature* enum_type = access_expr_result.options.type;
            if (enum_type->type == Signature_Type::STRUCT && enum_type->options.structure.struct_type == Structure_Type::UNION) {
                enum_type = enum_type->options.structure.tag_member.type;
            }
            if (enum_type->type != Signature_Type::ENUM) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM, rc_member_access->expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(enum_type));
                return expression_result_make_error(type_system->unknown_type);
            }

            Enum_Member* found = 0;
            for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
                Enum_Member* member = &enum_type->options.enum_type.members[i];
                if (member->id == rc_member_access->member_name) {
                    found = member;
                    break;
                }
            }

            int value = 0;
            if (found == 0) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, rc_member_access->expression);
                semantic_analyser_add_error_info(analyser, error_information_make_id(rc_member_access->member_name));
            }
            else {
                value = found->value;
            }
            return expression_result_make_value(modtree_expression_create_constant_enum(analyser, enum_type, value));
        }
        case Expression_Result_Type::FUNCTION:
        case Expression_Result_Type::EXTERN_FUNCTION:
        case Expression_Result_Type::HARDCODED_FUNCTION:
        case Expression_Result_Type::MODULE: {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_EXPRESSION_TYPE, rc_member_access->expression);
            semantic_analyser_add_error_info(analyser, error_information_make_expression_result_type(access_expr_result.type));
            return expression_result_make_error(type_system->unknown_type);
        }
        case Expression_Result_Type::EXPRESSION:
        {
            ModTree_Expression* access_expr = access_expr_result.options.expression;
            SCOPE_EXIT(if (error_exit) modtree_expression_destroy(access_expr););

            Type_Signature* struct_signature = access_expr->result_type;
            if (struct_signature->type == Signature_Type::STRUCT)
            {
                Struct_Member* found = 0;
                for (int i = 0; i < struct_signature->options.structure.members.size; i++) {
                    Struct_Member* member = &struct_signature->options.structure.members[i];
                    if (member->id == rc_member_access->member_name) {
                        found = member;
                    }
                }
                if (found == 0) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, rc_expression);
                    semantic_analyser_add_error_info(analyser, error_information_make_id(rc_member_access->member_name));
                    error_exit = true;
                    return expression_result_make_error(type_system->unknown_type);
                }

                ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::MEMBER_ACCESS, found->type);
                result->options.member_access.member = *found;
                result->options.member_access.structure_expression = access_expr;
                return expression_result_make_value(result);
            }
            else if (struct_signature->type == Signature_Type::ARRAY || struct_signature->type == Signature_Type::SLICE)
            {
                if (rc_member_access->member_name != analyser->id_size && rc_member_access->member_name != analyser->id_data) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, rc_expression);
                    semantic_analyser_add_error_info(analyser, error_information_make_id(rc_member_access->member_name));
                    error_exit = true;
                    return expression_result_make_error(type_system->unknown_type);
                }

                if (struct_signature->type == Signature_Type::ARRAY)
                {
                    if (rc_member_access->member_name == analyser->id_size) {
                        return expression_result_make_value(modtree_expression_create_constant_i32(analyser, struct_signature->options.array.element_count));
                    }
                    else
                    {
                        ModTree_Expression* result = modtree_expression_create_empty(
                            ModTree_Expression_Type::UNARY_OPERATION,
                            type_system_make_pointer(&analyser->compiler->type_system, struct_signature->options.array.element_type)
                        );
                        result->options.unary_operation.operand = access_expr;
                        result->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
                        return expression_result_make_value(result);
                    }
                }
                else // Slice
                {
                    Struct_Member member;
                    if (rc_member_access->member_name == analyser->id_size) {
                        member = struct_signature->options.slice.size_member;
                    }
                    else {
                        member = struct_signature->options.slice.data_member;
                    }

                    ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::MEMBER_ACCESS, member.type);
                    result->options.member_access.structure_expression = access_expr;
                    result->options.member_access.member = member;
                    return expression_result_make_value(result);
                }
            }
            else
            {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(struct_signature));
                error_exit = true;
                return expression_result_make_error(type_system->unknown_type);
            }
            panic("");
            break;
        }
        default: panic("");
        }
        panic("Should not happen");
        return expression_result_make_error(type_system->unknown_type);
    }
    case RC_Expression_Type::AUTO_ENUM:
    {
        String* id = rc_expression->options.auto_enum_member_id;
        if (context.type != Expression_Context_Type::SPECIFIC_TYPE) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED, rc_expression);
            return expression_result_make_error(type_system->unknown_type);
        }
        if (context.signature->type != Signature_Type::ENUM) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT, rc_expression);
            return expression_result_make_error(type_system->unknown_type);
        }

        Type_Signature* enum_type = context.signature;
        Enum_Member* found = 0;
        for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
            Enum_Member* member = &enum_type->options.enum_type.members[i];
            if (member->id == id) {
                found = member;
                break;
            }
        }

        int value = 0;
        if (found == 0) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_id(id));
        }
        else {
            value = found->value;
        }

        ModTree_Expression* result = modtree_expression_create_constant_enum(analyser, enum_type, value);
        return expression_result_make_value(result);
    }
    case RC_Expression_Type::UNARY_OPERATION:
    {
        auto rc_unary = &rc_expression->options.unary_expression;
        bool is_negate = rc_unary->op_type == RC_Unary_Operation_Type::NEGATE;
        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
            analyser, rc_unary->operand,
            is_negate ? expression_context_make_auto_dereference() : expression_context_make_specific_type(type_system->bool_type)
        );

        if (is_negate) {
            bool valid = false;
            if (operand->result_type->type == Signature_Type::PRIMITIVE) {
                valid = operand->result_type->options.primitive.is_signed && operand->result_type->options.primitive.type != Primitive_Type::BOOLEAN;
            }
            if (!valid) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(operand->result_type));
            }
        }

        ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, operand->result_type);
        result->options.unary_operation.operation_type = is_negate ? ModTree_Unary_Operation_Type::NEGATE : ModTree_Unary_Operation_Type::LOGICAL_NOT;
        result->options.unary_operation.operand = operand;
        return expression_result_make_value(result);
    }
    case RC_Expression_Type::POINTER:
    {
        Expression_Result operand_result = semantic_analyser_analyse_expression_any(
            analyser, rc_expression->options.pointer_expression, expression_context_make_unknown()
        );
        switch (operand_result.type)
        {
        case Expression_Result_Type::EXPRESSION:
        {
            ModTree_Expression* operand = operand_result.options.expression;
            if (operand->result_type == type_system->void_type) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, rc_expression->options.pointer_expression);
                modtree_expression_destroy(operand);
                return expression_result_make_error(type_system->unknown_type);
            }

            if (modtree_expression_result_is_temporary(operand)) {
                semantic_analyser_log_error(
                    analyser, Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT, rc_expression->options.pointer_expression
                );
            }

            ModTree_Expression* result = modtree_expression_create_empty(
                ModTree_Expression_Type::UNARY_OPERATION, type_system_make_pointer(type_system, operand->result_type)
            );
            result->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
            result->options.unary_operation.operand = operand;
            return expression_result_make_value(result);
        }
        case Expression_Result_Type::TYPE: {
            return expression_result_make_type(type_system_make_pointer(&analyser->compiler->type_system, operand_result.options.type));
        }
        case Expression_Result_Type::FUNCTION:
        case Expression_Result_Type::EXTERN_FUNCTION:
        case Expression_Result_Type::HARDCODED_FUNCTION:
        case Expression_Result_Type::MODULE: {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_EXPRESSION_TYPE, rc_expression->options.pointer_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_expression_result_type(operand_result.type));
            return expression_result_make_error(type_system->unknown_type);
        }
        default: panic("");
        }
        panic("");
        break;
    }
    case RC_Expression_Type::ADDRESS_OF:
    {
        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
            analyser, rc_expression->options.dereference_expression, expression_context_make_unknown()
        );
        Type_Signature* result_type = type_system->unknown_type;
        if (operand->result_type->type != Signature_Type::POINTER || operand->result_type == type_system->void_ptr_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, rc_expression->options.dereference_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(operand->result_type));
        }
        else {
            result_type = operand->result_type->options.pointer_child;
        }

        ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, result_type);
        result->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
        result->options.unary_operation.operand = operand;
        return expression_result_make_value(result);
    }
    case RC_Expression_Type::BINARY_OPERATION:
    {
        auto rc_binop = &rc_expression->options.binary_operation;

        // Determine what operands are valid
        bool int_valid = false;
        bool float_valid = false;
        bool bool_valid = false;
        bool ptr_valid = false;
        bool result_type_is_bool = false;
        bool enum_valid = false;
        bool type_type_valid = false;
        Expression_Context operand_context;
        switch (rc_binop->op_type)
        {
        case RC_Binary_Operation_Type::ADDITION:
        case RC_Binary_Operation_Type::SUBTRACTION:
        case RC_Binary_Operation_Type::MULTIPLICATION:
        case RC_Binary_Operation_Type::DIVISION:
            float_valid = true;
            int_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case RC_Binary_Operation_Type::MODULO:
            int_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case RC_Binary_Operation_Type::GREATER:
        case RC_Binary_Operation_Type::GREATER_OR_EQUAL:
        case RC_Binary_Operation_Type::LESS:
        case RC_Binary_Operation_Type::LESS_OR_EQUAL:
            float_valid = true;
            int_valid = true;
            result_type_is_bool = true;
            enum_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case RC_Binary_Operation_Type::POINTER_EQUAL:
        case RC_Binary_Operation_Type::POINTER_NOT_EQUAL:
            ptr_valid = true;
            operand_context = expression_context_make_unknown();
            result_type_is_bool = true;
            break;
        case RC_Binary_Operation_Type::EQUAL:
        case RC_Binary_Operation_Type::NOT_EQUAL:
            float_valid = true;
            int_valid = true;
            bool_valid = true;
            ptr_valid = true;
            enum_valid = true;
            type_type_valid = true;
            operand_context = expression_context_make_auto_dereference();
            result_type_is_bool = true;
            break;
        case RC_Binary_Operation_Type::AND:
        case RC_Binary_Operation_Type::OR:
            bool_valid = true;
            result_type_is_bool = true;
            operand_context = expression_context_make_specific_type(type_system->bool_type);
            break;
        default: panic("");
        }

        // Evaluate operands
        ModTree_Expression* left_expr = semantic_analyser_analyse_expression_value(analyser, rc_binop->left_operand, operand_context);
        if (enum_valid && left_expr->result_type->type == Signature_Type::ENUM) {
            operand_context = expression_context_make_specific_type(left_expr->result_type);
        }
        ModTree_Expression* right_expr = semantic_analyser_analyse_expression_value(analyser, rc_binop->right_operand, operand_context);

        // Try implicit casting if types dont match
        Type_Signature* left_type = left_expr->result_type;
        Type_Signature* right_type = right_expr->result_type;
        Type_Signature* operand_type = left_type;
        bool types_are_valid = true;
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
        if (types_are_valid)
        {
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
            else if (operand_type->type == Signature_Type::TYPE_TYPE) {
                types_are_valid = type_type_valid;
            }
            else {
                types_are_valid = false;
            }
        }

        ModTree_Expression* result = modtree_expression_create_empty(
            ModTree_Expression_Type::BINARY_OPERATION, result_type_is_bool ? type_system->bool_type : operand_type
        );
        if (rc_binop->op_type == RC_Binary_Operation_Type::POINTER_EQUAL) {
            result->options.binary_operation.operation_type = ModTree_Binary_Operation_Type::EQUAL;
        }
        else if (rc_binop->op_type == RC_Binary_Operation_Type::POINTER_NOT_EQUAL) {
            result->options.binary_operation.operation_type = ModTree_Binary_Operation_Type::NOT_EQUAL;
        }
        else {
            result->options.binary_operation.operation_type = (ModTree_Binary_Operation_Type)rc_binop->op_type;
        }
        result->options.binary_operation.left_operand = left_expr;
        result->options.binary_operation.right_operand = right_expr;

        if (!types_are_valid) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_binary_op_type(left_type, right_type));
        }
        return expression_result_make_value(result);
    }
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    panic("HEY");
    return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
}

Expression_Result semantic_analyser_analyse_expression_any(Semantic_Analyser* analyser, RC_Expression* rc_expression, Expression_Context context)
{
    Expression_Result result = semantic_analyser_analyse_expression_internal(analyser, rc_expression, context);
    if (result.type != Expression_Result_Type::EXPRESSION) return result;

    ModTree_Expression* expr = result.options.expression;
    Type_Signature* inital_type = expr->result_type;
    if (expr->result_type == analyser->compiler->type_system.unknown_type ||
        (context.type == Expression_Context_Type::SPECIFIC_TYPE && context.signature == analyser->compiler->type_system.unknown_type)) {
        semantic_analyser_set_error_flag(analyser, true);
        return result;
    }

    int wanted_pointer_depth = 0;
    switch (context.type)
    {
    case Expression_Context_Type::UNKNOWN:
        return result;
    case Expression_Context_Type::AUTO_DEREFERENCE:
        wanted_pointer_depth = 0;
        break;
    case Expression_Context_Type::SPECIFIC_TYPE:
    {
        Type_Signature* temp = context.signature;
        wanted_pointer_depth = 0;
        while (temp->type == Signature_Type::POINTER) {
            wanted_pointer_depth++;
            temp = temp->options.pointer_child;
        }
        break;
    }
    default: panic("");
    }

    // Find out wanted pointer level
    int given_pointer_depth = 0;
    {
        Type_Signature* iter = expr->result_type;
        while (iter->type == Signature_Type::POINTER) {
            given_pointer_depth++;
            iter = iter->options.pointer_child;
        }
    }

    // Dereference to given level
    while (given_pointer_depth > wanted_pointer_depth)
    {
        ModTree_Expression* deref = new ModTree_Expression;
        deref->expression_type = ModTree_Expression_Type::UNARY_OPERATION;
        deref->result_type = expr->result_type->options.pointer_child;
        deref->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
        deref->options.unary_operation.operand = expr;
        expr = deref;
        given_pointer_depth--;
    }

    if (context.type != Expression_Context_Type::SPECIFIC_TYPE) {
        result.options.expression = expr;
        return result;
    }

    // Auto address of
    if (given_pointer_depth + 1 == wanted_pointer_depth)
    {
        if (!modtree_expression_result_is_temporary(expr))
        {
            ModTree_Expression* addr_of = new ModTree_Expression;
            addr_of->expression_type = ModTree_Expression_Type::UNARY_OPERATION;
            addr_of->result_type = type_system_make_pointer(&analyser->compiler->type_system, expr->result_type);
            addr_of->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
            addr_of->options.unary_operation.operand = expr;
            expr = addr_of;
            given_pointer_depth = wanted_pointer_depth;
        }
    }

    // Implicit casting
    result.options.expression = expr;
    if (expr->result_type != context.signature)
    {
        ModTree_Expression* casted = semantic_analyser_cast_implicit_if_possible(analyser, expr, context.signature);
        if (casted != 0) {
            expr = casted;
            result.options.expression = expr;
        }
        else {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(inital_type));
            semantic_analyser_add_error_info(analyser, error_information_make_expected_type(context.signature));
            modtree_expression_destroy(expr);
            result.options.expression = modtree_expression_make_error(context.signature);
        }
    }
    return result;
}

Type_Signature* semantic_analyser_analyse_expression_type(Semantic_Analyser* analyser, RC_Expression* rc_expression)
{
    Expression_Result result = semantic_analyser_analyse_expression_any(
        analyser, rc_expression, expression_context_make_auto_dereference()
    );
    switch (result.type)
    {
    case Expression_Result_Type::TYPE:
        return result.options.type;
    case Expression_Result_Type::EXPRESSION:
    {
        ModTree_Expression* expression = result.options.expression;
        SCOPE_EXIT(modtree_expression_destroy(expression));
        if (expression->result_type == analyser->compiler->type_system.unknown_type) {
            semantic_analyser_set_error_flag(analyser, true);
            return analyser->compiler->type_system.unknown_type;
        }
        if (expression->result_type != analyser->compiler->type_system.type_type)
        {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(expression->result_type));
            return analyser->compiler->type_system.unknown_type;
        }

        Comptime_Result comptime = modtree_expression_calculate_comptime_value(analyser, expression);
        Type_Signature* result_type = analyser->compiler->type_system.unknown_type;
        switch (comptime.type)
        {
        case Comptime_Result_Type::AVAILABLE:
        {
            u64 type_index = *(u64*)comptime.data;
            if (type_index >= analyser->compiler->type_system.internal_type_infos.size) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, rc_expression);
                return analyser->compiler->type_system.unknown_type;
            }
            return analyser->compiler->type_system.types[type_index];
        }
        case Comptime_Result_Type::UNAVAILABLE:
            return analyser->compiler->type_system.unknown_type;
        case Comptime_Result_Type::NOT_COMPTIME:
            semantic_analyser_log_error(analyser, Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME, rc_expression);
            return analyser->compiler->type_system.unknown_type;
        default: panic("");
        }

        panic("");
        break;
    }
    case Expression_Result_Type::MODULE:
    case Expression_Result_Type::EXTERN_FUNCTION:
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::FUNCTION: {
        semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPECTED_TYPE, rc_expression);
        semantic_analyser_add_error_info(analyser, error_information_make_expression_result_type(result.type));
        return analyser->compiler->type_system.unknown_type;
    }
    default: panic("");
    }
    panic("");
    return analyser->compiler->type_system.unknown_type;
}

ModTree_Expression* semantic_analyser_analyse_expression_value(Semantic_Analyser* analyser, RC_Expression* rc_expression, Expression_Context context)
{
    Expression_Result result = semantic_analyser_analyse_expression_any(analyser, rc_expression, context);
    switch (result.type)
    {
    case Expression_Result_Type::EXPRESSION: {
        return result.options.expression;
    }
    case Expression_Result_Type::TYPE: {
        return modtree_expression_create_constant(
            analyser, analyser->compiler->type_system.type_type,
            array_create_static_as_bytes(&result.options.type->internal_index, 1), 0
        );
    }
    case Expression_Result_Type::EXTERN_FUNCTION:
    case Expression_Result_Type::FUNCTION:
    {
        ModTree_Expression* result_expr = modtree_expression_create_empty(ModTree_Expression_Type::FUNCTION_POINTER_READ, 0);
        auto function_ptr_read = &result_expr->options.function_pointer_read;
        if (result.type == Expression_Result_Type::FUNCTION) {
            function_ptr_read->is_extern = false;
            function_ptr_read->function = result.options.function;
            function_ptr_read->extern_function = 0;
            result_expr->result_type = result.options.function->signature;
        }
        else {
            function_ptr_read->is_extern = true;
            function_ptr_read->function = 0;
            function_ptr_read->extern_function = result.options.extern_function;
            result_expr->result_type = result.options.extern_function->extern_function.function_signature;
        }
        return result_expr;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    {
        semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION, rc_expression);
        return modtree_expression_make_error(result.options.hardcoded_function->signature);
    }
    case Expression_Result_Type::MODULE:
    {
        semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPECTED_VALUE, rc_expression);
        semantic_analyser_add_error_info(analyser, error_information_make_expression_result_type(result.type));
        return modtree_expression_make_error(analyser->compiler->type_system.unknown_type);
    }
    default: panic("");
    }
    panic("");
    return modtree_expression_make_error(analyser->compiler->type_system.unknown_type);
}

void semantic_analyser_analyse_extern_definitions(Semantic_Analyser* analyser, Symbol_Table* parent_table, AST_Node* module_node)
{
    // THIS IS THE PREVIOUS ANALYSE MODULE
    /*
    assert(module_node->type == AST_Node_Type::ROOT || module_node->type == AST_Node_Type::MODULE, "");

    // Create module if not root
    ModTree_Module* module;
    if (module_node->type == AST_Node_Type::ROOT) {
        module = analyser->program->root_module;
    }
    else {
        module = modtree_module_create(analyser, 0, parent_module, 0);
        module->symbol_table = symbol_table_create(analyser, parent_table, module_node, symbol_table_origin_make_module(module));
    }

    // Analyse Definitions
    AST_Node* definitions_node = module_node->child_start;
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
        case AST_Node_Type::VARIABLE_DEFINITION:
        {
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::GLOBAL_DEFINITION;
            workload.options.global.parent_module = module;
            workload.options.global.node = top_level_node;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case AST_Node_Type::EXTERN_FUNCTION_DECLARATION:
        {
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
            dynamic_array_push_back(&analyser->compiler->extern_sources.lib_files, top_level_node->id);
            break;
        }
        default: panic("HEy");
        }
    }

    return module;
    */
}

bool inside_defer(Semantic_Analyser* analyser)
{
    for (int i = analyser->block_stack.size - 1; i > 0; i--) {
        ModTree_Block* block = analyser->block_stack[i];
        if (block->rc_block != 0 && block->rc_block->type == RC_Block_Type::DEFER_BLOCK) {
            return true;
        }
    }
    return false;
}

Control_Flow semantic_analyser_analyse_statement(Semantic_Analyser* analyser, RC_Statement* rc_statement, ModTree_Block* block)
{
    Type_System* type_system = &analyser->compiler->type_system;
    switch (rc_statement->type)
    {
    case RC_Statement_Type::RETURN_STATEMENT:
    {
        auto rc_return = &rc_statement->options.return_statement;
        assert(analyser->current_function != 0, "No statements outside of function body");
        Type_Signature* expected_return_type = analyser->current_function->signature->options.function.return_type;

        Optional<ModTree_Expression*> value_expr;
        if (rc_return->available)
        {
            value_expr.available = true;
            value_expr.value = semantic_analyser_analyse_expression_value(
                analyser, rc_return->value, expression_context_make_specific_type(expected_return_type)
            );
            // When we have defers, we need to temporarily store the return result in a variable
            if (analyser->defer_stack.size != 0)
            {
                ModTree_Variable* tmp_return_var = modtree_block_add_variable(block, expected_return_type, 0);
                ModTree_Statement* assign_stmt = modtree_block_add_statement_empty(block, ModTree_Statement_Type::ASSIGNMENT);
                assign_stmt->options.assignment.destination = modtree_expression_create_variable_read(tmp_return_var);
                assign_stmt->options.assignment.source = value_expr.value;
                value_expr.value = modtree_expression_create_variable_read(tmp_return_var);
            }
        }
        else {
            value_expr.available = false;
            if (expected_return_type != type_system->void_type) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_RETURN, rc_statement);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(type_system->void_type));
                semantic_analyser_add_error_info(analyser, error_information_make_expected_type(expected_return_type));
            }
        }
        if (inside_defer(analyser)) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED, rc_statement);
        }
        else {
            // INFO: Inside Bakes returns only work through the defers that are inside the bake
            int defer_start_index = 0;
            for (int i = analyser->block_stack.size - 1; i >= 0; i--) {
                RC_Block* block = analyser->block_stack[i]->rc_block;
                assert(block != 0, "I don't think this can happen");
                if (block->type == RC_Block_Type::DEFER_BLOCK) {
                    defer_start_index = analyser->block_stack[i]->defer_start_index;
                    break;
                }
            }
            semantic_analyser_work_through_defers(analyser, block, defer_start_index);
        }

        ModTree_Statement* return_statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::RETURN);
        return_statement->options.return_value = value_expr;
        return Control_Flow::RETURNS;
    }
    case RC_Statement_Type::BREAK_STATEMENT:
    case RC_Statement_Type::CONTINUE_STATEMENT:
    {
        bool is_continue = rc_statement->type == RC_Statement_Type::CONTINUE_STATEMENT;
        String* search_id = is_continue ? rc_statement->options.continue_id : rc_statement->options.break_id;
        ModTree_Block* found_block = 0;
        for (int i = analyser->block_stack.size - 1; i > 0; i--) // INFO: Block 0 is always the function body, which cannot be a target of break/continue
        {
            if (analyser->block_stack[i]->rc_block->block_id == search_id) {
                found_block = analyser->block_stack[i];
                break;
            }
        }

        if (found_block == 0)
        {
            semantic_analyser_log_error(
                analyser, is_continue ? Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND : Semantic_Error_Type::BREAK_LABLE_NOT_FOUND, rc_statement
            );
            semantic_analyser_add_error_info(analyser, error_information_make_id(search_id));
            return Control_Flow::SEQUENTIAL;
        }
        else
        {
            if (is_continue && found_block->rc_block->type != RC_Block_Type::WHILE_BODY) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK, rc_statement);
                return Control_Flow::SEQUENTIAL;
            }
            semantic_analyser_work_through_defers(analyser, block, found_block->defer_start_index);
        }

        ModTree_Statement* stmt = modtree_block_add_statement_empty(block, is_continue ? ModTree_Statement_Type::CONTINUE : ModTree_Statement_Type::BREAK);
        if (is_continue) {
            stmt->options.continue_to_block = found_block;
        }
        else
        {
            stmt->options.break_to_block = found_block;
            // Mark all previous Code-Blocks as Sequential flow, since they contain a path to a break
            for (int i = analyser->block_stack.size - 1; i >= 0; i--)
            {
                ModTree_Block* prev_block = analyser->block_stack[i];
                if (!prev_block->control_flow_locked && analyser->statement_reachable) {
                    prev_block->control_flow_locked = true;
                    prev_block->flow = Control_Flow::SEQUENTIAL;
                }
                if (prev_block == found_block) break;
            }
        }
        return Control_Flow::STOPS;
    }
    case RC_Statement_Type::DEFER:
    {
        if (inside_defer(analyser)) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS, rc_statement);
            return Control_Flow::SEQUENTIAL;
        }
        else {
            dynamic_array_push_back(&analyser->defer_stack, rc_statement->options.defer_block);
        }
        return Control_Flow::SEQUENTIAL;
    }
    case RC_Statement_Type::EXPRESSION_STATEMENT:
    {
        auto rc_expression = rc_statement->options.expression_statement;
        if (rc_expression->type != RC_Expression_Type::FUNCTION_CALL) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL, rc_statement);
        }
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::EXPRESSION);
        statement->options.expression = semantic_analyser_analyse_expression_value(
            analyser, rc_expression, expression_context_make_unknown()
        );
        return Control_Flow::SEQUENTIAL;
    }
    case RC_Statement_Type::STATEMENT_BLOCK:
    {
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::BLOCK);
        statement->options.block = modtree_block_create_empty(rc_statement->options.statement_block);
        semantic_analyser_fill_block(analyser, statement->options.block, rc_statement->options.statement_block);
        return statement->options.block->flow;
    }
    case RC_Statement_Type::IF_STATEMENT:
    {
        auto rc_if = &rc_statement->options.if_statement;
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::IF);

        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            analyser, rc_if->condition, expression_context_make_specific_type(analyser->compiler->type_system.bool_type)
        );
        statement->options.if_statement.condition = condition;
        statement->options.if_statement.if_block = modtree_block_create_empty(rc_if->true_block);
        semantic_analyser_fill_block(analyser, statement->options.if_statement.if_block, rc_if->true_block);
        Control_Flow true_flow = statement->options.if_statement.if_block->flow;

        Control_Flow false_flow;
        if (rc_if->false_block.available) {
            statement->options.if_statement.else_block = modtree_block_create_empty(rc_if->false_block.value);
            semantic_analyser_fill_block(analyser, statement->options.if_statement.else_block, rc_if->false_block.value);
            false_flow = statement->options.if_statement.else_block->flow;
        }
        else {
            statement->options.if_statement.else_block = modtree_block_create_empty(0);
            return Control_Flow::SEQUENTIAL; // If no else, if is always sequential
        }

        // Combine flows as given by conditional flow rules
        if (true_flow == false_flow) {
            return true_flow;
        }
        if (true_flow == Control_Flow::SEQUENTIAL || false_flow == Control_Flow::SEQUENTIAL) {
            return Control_Flow::SEQUENTIAL;
        }
        return Control_Flow::STOPS;
    }
    case RC_Statement_Type::SWITCH_STATEMENT:
    {
        auto rc_switch = &rc_statement->options.switch_statement;
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::SWITCH);

        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            analyser, rc_switch->condition, expression_context_make_auto_dereference()
        );
        if (condition->result_type->type == Signature_Type::STRUCT && condition->result_type->options.structure.struct_type == Structure_Type::UNION)
        {
            ModTree_Expression* tag_access_expr = modtree_expression_create_empty(
                ModTree_Expression_Type::MEMBER_ACCESS, condition->result_type->options.structure.tag_member.type
            );
            tag_access_expr->options.member_access.member = condition->result_type->options.structure.tag_member;
            tag_access_expr->options.member_access.structure_expression = condition;
            condition = tag_access_expr;
        }
        else if (condition->result_type->type != Signature_Type::ENUM) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_REQUIRES_ENUM, rc_switch->condition);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(condition->result_type));
        }
        statement->options.switch_statement.condition = condition;
        statement->options.switch_statement.default_block = 0;
        statement->options.switch_statement.cases = dynamic_array_create_empty<ModTree_Switch_Case>(1);

        Type_Signature* switch_type = condition->result_type;
        Expression_Context case_context = condition->result_type->type == Signature_Type::ENUM ?
            expression_context_make_specific_type(condition->result_type) : expression_context_make_unknown();
        Control_Flow switch_flow = Control_Flow::SEQUENTIAL;
        for (int i = 0; i < rc_switch->cases.size; i++)
        {
            RC_Switch_Case* rc_case = &rc_switch->cases[i];
            ModTree_Block* case_body = modtree_block_create_empty(rc_case->body);
            semantic_analyser_fill_block(analyser, case_body, rc_case->body);

            Control_Flow case_flow = case_body->flow;
            if (i == 0) {
                switch_flow = case_flow;
            }
            else
            {
                // Combine flows according to the Conditional Branch rules
                if (switch_flow != case_flow) {
                    if (switch_flow == Control_Flow::SEQUENTIAL || case_flow == Control_Flow::SEQUENTIAL) {
                        switch_flow = Control_Flow::SEQUENTIAL;
                    }
                    else {
                        switch_flow = Control_Flow::STOPS;
                    }
                }
            }

            if (rc_case->expression.available)
            {
                ModTree_Expression* case_expr = semantic_analyser_analyse_expression_value(
                    analyser, rc_case->expression.value, case_context
                );

                ModTree_Switch_Case modtree_case;
                modtree_case.expression = case_expr;
                modtree_case.body = case_body;
                modtree_case.value = -1; // Placeholder
                // Calculate case value
                Comptime_Result comptime = modtree_expression_calculate_comptime_value(analyser, case_expr);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE: {
                    if (comptime.data_type == type_system->i32_type || comptime.data_type->type == Signature_Type::ENUM) {
                        modtree_case.value = *(int*)comptime.data;
                    }
                    break;
                }
                case Comptime_Result_Type::UNAVAILABLE: {
                    break;
                }
                case Comptime_Result_Type::NOT_COMPTIME: {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN, rc_case->expression.value);
                    break;
                }
                default: panic("");
                }
                dynamic_array_push_back(&statement->options.switch_statement.cases, modtree_case);
            }
            else
            {
                // Default case
                if (statement->options.switch_statement.default_block != 0) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED, rc_statement);
                    // Switch old default so that we can analyse all given blocks
                    ModTree_Switch_Case modtree_case;
                    modtree_case.value = -420;
                    modtree_case.body = statement->options.switch_statement.default_block;
                    modtree_case.expression = modtree_expression_make_error(type_system->unknown_type);
                    dynamic_array_push_back(&statement->options.switch_statement.cases, modtree_case);
                }
                statement->options.switch_statement.default_block = case_body;
            }
        }

        // Check if given cases are unique
        int unique_count = 0;
        for (int i = 0; i < statement->options.switch_statement.cases.size; i++)
        {
            ModTree_Switch_Case* mod_case = &statement->options.switch_statement.cases[i];
            bool is_unique = true;
            for (int j = i + 1; j < statement->options.switch_statement.cases.size; j++)
            {
                ModTree_Switch_Case* other_case = &statement->options.switch_statement.cases[j];
                if (mod_case->value == other_case->value) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE, rc_statement); // TODO: Fix this (E.g. not 0)
                    is_unique = false;
                    break;
                }
            }
            if (is_unique) {
                unique_count++;
            }
        }

        // Check if all possible cases are handled
        if (statement->options.switch_statement.default_block == 0 && switch_type->type == Signature_Type::ENUM) {
            if (unique_count < switch_type->options.enum_type.members.size) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES, rc_statement);
            }
        }
        return switch_flow;
    }
    case RC_Statement_Type::WHILE_STATEMENT:
    {
        auto rc_while = &rc_statement->options.while_statement;
        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            analyser, rc_while->condition, expression_context_make_specific_type(type_system->bool_type)
        );

        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::WHILE);
        statement->options.while_statement.condition = condition;
        statement->options.while_statement.while_block = modtree_block_create_empty(rc_while->body);
        semantic_analyser_fill_block(analyser, statement->options.while_statement.while_block, rc_while->body);
        Control_Flow flow = statement->options.while_statement.while_block->flow;
        if (flow == Control_Flow::RETURNS) {
            return flow;
        }
        return Control_Flow::SEQUENTIAL;
    }
    case RC_Statement_Type::DELETE_STATEMENT:
    {
        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
            analyser, rc_statement->options.delete_expression, expression_context_make_unknown()
        );
        Type_Signature* delete_type = operand->result_type;
        if (delete_type->type != Signature_Type::POINTER && delete_type->type != Signature_Type::SLICE) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_DELETE, rc_statement->options.delete_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(delete_type));
        }

        ModTree_Expression* call_expr = new ModTree_Expression;
        call_expr->expression_type = ModTree_Expression_Type::FUNCTION_CALL;
        call_expr->result_type = analyser->compiler->type_system.void_type;
        call_expr->options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(1);
        call_expr->options.function_call.call_type = ModTree_Call_Type::HARDCODED_FUNCTION;
        call_expr->options.function_call.options.hardcoded_function = analyser->free_function;

        // Add argument
        if (delete_type->type == Signature_Type::SLICE)
        {
            ModTree_Expression* data_member_access = new ModTree_Expression;
            data_member_access->expression_type = ModTree_Expression_Type::MEMBER_ACCESS;
            data_member_access->result_type = delete_type->options.slice.data_member.type;
            data_member_access->options.member_access.structure_expression = operand;
            data_member_access->options.member_access.member = delete_type->options.slice.data_member;
            dynamic_array_push_back(&call_expr->options.function_call.arguments, data_member_access);
        }
        else {
            dynamic_array_push_back(&call_expr->options.function_call.arguments, operand);
        }

        ModTree_Statement* expr_statement = new ModTree_Statement;
        expr_statement->type = ModTree_Statement_Type::EXPRESSION;
        expr_statement->options.expression = call_expr;
        dynamic_array_push_back(&block->statements, expr_statement);
        return Control_Flow::SEQUENTIAL;
    }
    case RC_Statement_Type::ASSIGNMENT_STATEMENT:
    {
        auto rc_assignment = &rc_statement->options.assignment;
        ModTree_Expression* left_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_assignment->left_expression, expression_context_make_unknown()
        );
        ModTree_Expression* right_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_assignment->right_expression, expression_context_make_specific_type(left_expr->result_type)
        );
        Type_Signature* left_type = left_expr->result_type;
        Type_Signature* right_type = right_expr->result_type;
        if (modtree_expression_result_is_temporary(left_expr)) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS, rc_assignment->left_expression);
        }
        ModTree_Statement* assign_statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::ASSIGNMENT);
        assign_statement->options.assignment.destination = left_expr;
        assign_statement->options.assignment.source = right_expr;
        return Control_Flow::SEQUENTIAL;
    }
    case RC_Statement_Type::VARIABLE_DEFINITION:
    {
        auto rc_variable = &rc_statement->options.variable_definition;
        ModTree_Variable* variable;
        Type_Signature* type = 0;
        if (rc_variable->type_expression.available) {
            type = semantic_analyser_analyse_expression_type(analyser, rc_variable->type_expression.value);
        }
        ModTree_Expression* value = 0;
        if (rc_variable->value_expression.available)
        {
            value = semantic_analyser_analyse_expression_value(
                analyser, rc_variable->value_expression.value, type != 0 ? expression_context_make_specific_type(type) : expression_context_make_unknown()
            );
            if (type == 0) {
                type = value->result_type;
            }
        }

        variable = modtree_block_add_variable(block, type, rc_variable->symbol);
        assert(rc_variable->symbol->type == Symbol_Type::VARIABLE || rc_variable->symbol->type == Symbol_Type::VARIABLE_UNDEFINED, "");
        rc_variable->symbol->type = Symbol_Type::VARIABLE;
        rc_variable->symbol->options.variable = variable;

        if (value != 0) {
            ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::ASSIGNMENT);
            statement->options.assignment.destination = modtree_expression_create_variable_read(variable);
            statement->options.assignment.source = value;
        }
        return Control_Flow::SEQUENTIAL;
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }
    panic("HEY");
    return Control_Flow::SEQUENTIAL;
}

void semantic_analyser_fill_block(Semantic_Analyser* analyser, ModTree_Block* block, RC_Block* rc_block)
{
    block->control_flow_locked = false;
    block->flow = Control_Flow::SEQUENTIAL;
    block->defer_start_index = analyser->defer_stack.size;

    int rewind_block_count = analyser->block_stack.size;
    bool rewind_reachable = analyser->statement_reachable;
    int rewind_defer_size = analyser->defer_stack.size;
    SCOPE_EXIT(dynamic_array_rollback_to_size(&analyser->block_stack, rewind_block_count));
    SCOPE_EXIT(dynamic_array_rollback_to_size(&analyser->defer_stack, rewind_defer_size));
    SCOPE_EXIT(analyser->statement_reachable = rewind_reachable);
    dynamic_array_push_back(&analyser->block_stack, block);
    for (int i = 0; i < rc_block->statements.size; i++)
    {
        Control_Flow flow = semantic_analyser_analyse_statement(analyser, rc_block->statements[i], block);
        if (flow != Control_Flow::SEQUENTIAL) {
            analyser->statement_reachable = false;
            if (!block->control_flow_locked) {
                block->flow = flow;
                block->control_flow_locked = true;
            }
        }
    }
    // Work through defers
    if (analyser->statement_reachable) {
        semantic_analyser_work_through_defers(analyser, block, block->defer_start_index);
    }
    dynamic_array_rollback_to_size(&analyser->defer_stack, block->defer_start_index);
}

void symbol_set_type(Symbol* symbol, Type_Signature* type)
{
    symbol->type = Symbol_Type::TYPE;
    symbol->options.type = type;
}

void semantic_analyser_finish(Semantic_Analyser* analyser)
{
    // Check if main is defined
    Symbol* main_symbol = symbol_table_find_symbol(
        analyser->compiler->rc_analyser->root_symbol_table, analyser->id_main, false, 0
    );
    ModTree_Function* main_function = 0;
    if (main_symbol == 0) {
        semantic_analyser_log_error(analyser, Semantic_Error_Type::MAIN_NOT_DEFINED, (AST_Node*)0);
    }
    else
    {
        if (main_symbol->type != Symbol_Type::FUNCTION) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::MAIN_MUST_BE_FUNCTION, main_symbol->definition_node);
            semantic_analyser_add_error_info(analyser, error_information_make_symbol(main_symbol));
        }
        else {
            main_function = main_symbol->options.function;
        }
    }

    // Add type_informations loading to global init function
    if (analyser->errors.size == 0)
    {
        Type_System* type_system = &analyser->compiler->type_system;
        Type_Signature* type_info_array_signature = type_system_make_array(
            type_system, type_system->type_information_type, true, type_system->internal_type_infos.size
        );
        int internal_information_size = type_system->internal_type_infos.size;
        Constant_Result result = constant_pool_add_constant(
            &analyser->compiler->constant_pool, type_info_array_signature,
            array_create_static_as_bytes(type_system->internal_type_infos.data, type_system->internal_type_infos.size)
        );
        assert(result.status == Constant_Status::SUCCESS, "Type information must be valid");
        assert(type_system->types.size == type_system->internal_type_infos.size, "");

        // Set type_informations pointer
        {
            ModTree_Expression* global_access = modtree_expression_create_empty(ModTree_Expression_Type::VARIABLE_READ, analyser->global_type_informations->data_type);
            global_access->options.variable_read = analyser->global_type_informations;

            ModTree_Expression* data_access = modtree_expression_create_empty(
                ModTree_Expression_Type::MEMBER_ACCESS, type_system_make_pointer(type_system, type_system->type_information_type)
            );
            data_access->options.member_access.structure_expression = global_access;
            data_access->options.member_access.member = analyser->global_type_informations->data_type->options.slice.data_member;

            ModTree_Expression* constant_access = modtree_expression_create_empty(ModTree_Expression_Type::CONSTANT_READ, type_info_array_signature);
            constant_access->options.constant_read = result.constant;

            ModTree_Expression* ptr_expression = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, data_access->result_type);
            ptr_expression->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
            ptr_expression->options.unary_operation.operand = constant_access;

            ModTree_Statement* assign_statement = modtree_block_add_statement_empty(analyser->global_init_function->body, ModTree_Statement_Type::ASSIGNMENT);
            assign_statement->options.assignment.destination = data_access;
            assign_statement->options.assignment.source = ptr_expression;
        }
        // Set type_informations size
        {
            ModTree_Expression* global_access = modtree_expression_create_empty(
                ModTree_Expression_Type::VARIABLE_READ, analyser->global_type_informations->data_type
            );
            global_access->options.variable_read = analyser->global_type_informations;

            ModTree_Expression* size_access = modtree_expression_create_empty(ModTree_Expression_Type::MEMBER_ACCESS, type_system->i32_type);
            size_access->options.member_access.structure_expression = global_access;
            size_access->options.member_access.member = analyser->global_type_informations->data_type->options.slice.size_member;

            ModTree_Statement* assign_statement = modtree_block_add_statement_empty(analyser->global_init_function->body, ModTree_Statement_Type::ASSIGNMENT);
            assign_statement->options.assignment.destination = size_access;
            assign_statement->options.assignment.source = modtree_expression_create_constant_i32(analyser, internal_information_size);
        }
    }

    // Call main after end of global init function
    {
        if (main_function != 0)
        {
            ModTree_Expression* call_expr = modtree_expression_create_empty(ModTree_Expression_Type::FUNCTION_CALL, analyser->compiler->type_system.void_type);
            call_expr->options.function_call.call_type = ModTree_Call_Type::FUNCTION;
            call_expr->options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(1);
            call_expr->options.function_call.options.function = main_function;
            ModTree_Statement* call_stmt = modtree_block_add_statement_empty(analyser->global_init_function->body, ModTree_Statement_Type::EXPRESSION);
            call_stmt->options.expression = call_expr;
        }
        ModTree_Statement* exit_stmt = modtree_block_add_statement_empty(analyser->global_init_function->body, ModTree_Statement_Type::EXIT);
        exit_stmt->options.exit_code = Exit_Code::SUCCESS;

        analyser->program->entry_function = analyser->global_init_function;
    }
}

void semantic_analyser_reset(Semantic_Analyser* analyser, Compiler* compiler)
{
    // Reset analyser data
    {
        analyser->compiler = compiler;
        analyser->current_workload = 0;
        analyser->current_function = 0;
        analyser->statement_reachable = true;
        analyser->error_flag_count = 0;
        for (int i = 0; i < analyser->errors.size; i++) {
            dynamic_array_destroy(&analyser->errors[i].information);
        }
        dynamic_array_reset(&analyser->errors);
        dynamic_array_reset(&analyser->block_stack);
        dynamic_array_reset(&analyser->defer_stack);
        stack_allocator_reset(&analyser->allocator_values);
        hashset_reset(&analyser->loaded_filenames);
        hashset_reset(&analyser->visited_functions);

        workload_executer_destroy(&analyser->workload_executer);
        analyser->workload_executer = workload_executer_create(analyser);

        if (analyser->program != 0) {
            modtree_program_destroy(analyser->program);
        }
        analyser->program = modtree_program_create(analyser);
        analyser->global_init_function = 0;
    }

    // Add symbols for basic datatypes
    {
        Type_System* ts = &analyser->compiler->type_system;
        Predefined_Symbols* pre = &analyser->compiler->rc_analyser->predefined_symbols;
        symbol_set_type(pre->type_any, ts->any_type);
        symbol_set_type(pre->type_bool, ts->bool_type);
        symbol_set_type(pre->type_float, ts->f32_type);
        symbol_set_type(pre->type_byte, ts->u8_type);
        symbol_set_type(pre->type_empty, ts->empty_struct_type);
        symbol_set_type(pre->type_int, ts->i32_type);
        symbol_set_type(pre->type_string, ts->string_type);
        symbol_set_type(pre->type_type, ts->type_type);
        symbol_set_type(pre->type_type_information, ts->type_information_type);
        symbol_set_type(pre->type_void, ts->void_type);
        symbol_set_type(pre->type_u8, ts->u8_type);
        symbol_set_type(pre->type_u16, ts->u16_type);
        symbol_set_type(pre->type_u32, ts->u32_type);
        symbol_set_type(pre->type_u64, ts->u64_type);
        symbol_set_type(pre->type_i8, ts->i8_type);
        symbol_set_type(pre->type_i16, ts->i16_type);
        symbol_set_type(pre->type_i32, ts->i32_type);
        symbol_set_type(pre->type_i64, ts->i64_type);
        symbol_set_type(pre->type_f32, ts->f32_type);
        symbol_set_type(pre->type_f64, ts->f64_type);

        analyser->id_size = identifier_pool_add(&compiler->identifier_pool, string_create_static("size"));
        analyser->id_data = identifier_pool_add(&compiler->identifier_pool, string_create_static("data"));
        analyser->id_main = identifier_pool_add(&compiler->identifier_pool, string_create_static("main"));
        analyser->id_tag = identifier_pool_add(&compiler->identifier_pool, string_create_static("tag"));
        analyser->id_type_of = identifier_pool_add(&compiler->identifier_pool, string_create_static("type_of"));
        analyser->id_type_info = identifier_pool_add(&compiler->identifier_pool, string_create_static("type_info"));
    }

    {
        // Add global type_information table
        analyser->global_type_informations = modtree_program_add_global(
            analyser->program, type_system_make_slice(&analyser->compiler->type_system, analyser->compiler->type_system.type_information_type),
            analyser->compiler->rc_analyser->predefined_symbols.global_type_informations
        );
        Symbol* type_infos = analyser->compiler->rc_analyser->predefined_symbols.global_type_informations;
        type_infos->type = Symbol_Type::VARIABLE;
        type_infos->options.variable = analyser->global_type_informations;
    }

    // Initialize hardcoded_functions
    for (int i = 0; i < (int)Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT; i++)
    {
        Hardcoded_Function_Type type = (Hardcoded_Function_Type)i;
        Type_System* type_system = &analyser->compiler->type_system;

        // Create Function
        ModTree_Hardcoded_Function* hardcoded_function = new ModTree_Hardcoded_Function;
        dynamic_array_push_back(&analyser->program->hardcoded_functions, hardcoded_function);

        // Find signature
        Dynamic_Array<Type_Signature*> parameters = dynamic_array_create_empty<Type_Signature*>(2);
        Type_Signature* return_type = type_system->void_type;
        Symbol* symbol = 0;
        switch (type)
        {
        case Hardcoded_Function_Type::PRINT_I32: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_print_i32;
            dynamic_array_push_back(&parameters, type_system->i32_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_F32: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_print_f32;
            dynamic_array_push_back(&parameters, type_system->f32_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_BOOL: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_print_bool;
            dynamic_array_push_back(&parameters, type_system->bool_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_STRING: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_print_string;
            dynamic_array_push_back(&parameters, type_system->string_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_LINE: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_print_line;
            break;
        }
        case Hardcoded_Function_Type::READ_I32: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_read_i32;
            return_type = type_system->i32_type;
            break;
        }
        case Hardcoded_Function_Type::READ_F32: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_read_f32;
            return_type = type_system->f32_type;
            break;
        }
        case Hardcoded_Function_Type::READ_BOOL: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_read_bool;
            return_type = type_system->bool_type;
            break;
        }
        case Hardcoded_Function_Type::RANDOM_I32: {
            symbol = analyser->compiler->rc_analyser->predefined_symbols.hardcoded_random_i32;
            return_type = type_system->i32_type;
            break;
        }
        case Hardcoded_Function_Type::MALLOC_SIZE_I32: {
            symbol = 0;
            analyser->malloc_function = hardcoded_function;
            dynamic_array_push_back(&parameters, type_system->i32_type);
            return_type = type_system->void_ptr_type;
            break;
        }
        case Hardcoded_Function_Type::FREE_POINTER:
            symbol = 0;
            analyser->free_function = hardcoded_function;
            dynamic_array_push_back(&parameters, type_system->void_ptr_type);
            return_type = type_system->void_type;
            break;
        default:
            panic("What");
        }
        hardcoded_function->signature = type_system_make_function(type_system, parameters, return_type);
        hardcoded_function->hardcoded_type = type;

        // Set symbol data
        if (symbol != 0) {
            symbol->type = Symbol_Type::HARDCODED_FUNCTION;
            symbol->options.hardcoded_function = hardcoded_function;
        }
    }

    // Add assert function
    {
        Symbol* symbol = analyser->compiler->rc_analyser->predefined_symbols.function_assert;
        Dynamic_Array<Type_Signature*> params = dynamic_array_create_empty<Type_Signature*>(1);
        dynamic_array_push_back(&params, analyser->compiler->type_system.bool_type);
        ModTree_Function* assert_fn = modtree_function_create_empty(
            analyser->program, type_system_make_function(&analyser->compiler->type_system, params, analyser->compiler->type_system.void_type),
            symbol, 0
        );
        symbol->type = Symbol_Type::FUNCTION;
        symbol->options.function = assert_fn;
        ModTree_Variable* cond_var = modtree_function_add_normal_parameter(assert_fn, analyser->compiler->type_system.bool_type, 0);

        // Make function body
        {
            ModTree_Expression* param_read_expr = modtree_expression_create_empty(ModTree_Expression_Type::VARIABLE_READ, cond_var->data_type);
            param_read_expr->options.variable_read = cond_var;

            ModTree_Expression* cond_expr = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, cond_var->data_type);
            cond_expr->options.unary_operation.operand = param_read_expr;
            cond_expr->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::LOGICAL_NOT;

            ModTree_Statement* if_stat = modtree_block_add_statement_empty(assert_fn->body, ModTree_Statement_Type::IF);
            if_stat->options.if_statement.condition = cond_expr;
            if_stat->options.if_statement.if_block = modtree_block_create_empty(0);
            if_stat->options.if_statement.else_block = modtree_block_create_empty(0);

            ModTree_Statement* exit_stat = modtree_block_add_statement_empty(if_stat->options.if_statement.if_block, ModTree_Statement_Type::EXIT);
            exit_stat->type = ModTree_Statement_Type::EXIT;
            exit_stat->options.exit_code = Exit_Code::ASSERTION_FAILED;

            ModTree_Statement* return_stat = modtree_block_add_statement_empty(if_stat->options.if_statement.else_block, ModTree_Statement_Type::RETURN);
            return_stat->options.return_value.available = false;
        }
        analyser->assert_function = assert_fn;
    }

    // Add global init function
    analyser->global_init_function = modtree_function_create_empty(
        analyser->program, type_system_make_function(
            &analyser->compiler->type_system, dynamic_array_create_empty<Type_Signature*>(1), analyser->compiler->type_system.void_type
        ), 0, 0
    );
}

Semantic_Analyser* semantic_analyser_create()
{
    Semantic_Analyser* result = new Semantic_Analyser;
    result->errors = dynamic_array_create_empty<Semantic_Error>(64);
    result->block_stack = dynamic_array_create_empty<ModTree_Block*>(8);
    result->defer_stack = dynamic_array_create_empty<RC_Block*>(8);
    result->allocator_values = stack_allocator_create_empty(2048);
    result->loaded_filenames = hashset_create_pointer_empty<String*>(32);
    result->visited_functions = hashset_create_pointer_empty<ModTree_Function*>(32);
    result->workload_executer = workload_executer_create(result);
    result->program = 0;
    return result;
}

void semantic_analyser_destroy(Semantic_Analyser* analyser)
{
    for (int i = 0; i < analyser->errors.size; i++) {
        dynamic_array_destroy(&analyser->errors[i].information);
    }
    dynamic_array_destroy(&analyser->errors);
    dynamic_array_destroy(&analyser->block_stack);
    dynamic_array_destroy(&analyser->defer_stack);

    stack_allocator_destroy(&analyser->allocator_values);
    hashset_destroy(&analyser->loaded_filenames);
    hashset_destroy(&analyser->visited_functions);

    if (analyser->program != 0) {
        modtree_program_destroy(analyser->program);
    }
    delete analyser;
}



/*
ERRORS + Import
*/

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
    case Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT:
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE: {
        AST_Node* identifier_node = error.error_node;
        Token_Range unnamed_block_range = identifier_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(unnamed_block_range.start_index, unnamed_block_range.start_index + 1));
        dynamic_array_push_back(locations, token_range_make(unnamed_block_range.end_index - 1, unnamed_block_range.end_index));
        break;
    }
    case Semantic_Error_Type::OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index + 1, error.error_node->token_range.start_index + 2));
        break;
    }
    case Semantic_Error_Type::OTHERS_MEMBER_ACCESS_INVALID_ON_FUNCTION: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index + 1, error.error_node->token_range.start_index + 2));
        break;
    }
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::LABEL_ALREADY_IN_USE: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::BREAK_LABLE_NOT_FOUND:
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index + 1, error.error_node->token_range.start_index + 2));
        break;
    case Semantic_Error_Type::BREAK_NOT_INSIDE_LOOP_OR_SWITCH:
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    case Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND:
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index + 1, error.error_node->token_range.start_index + 2));
        break;
    case Semantic_Error_Type::CONTINUE_NOT_INSIDE_LOOP:
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    case Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK:
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index + 1, error.error_node->token_range.start_index + 2));
        break;
    case Semantic_Error_Type::INVALID_EXPRESSION_TYPE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::EXPECTED_TYPE:
    case Semantic_Error_Type::EXPECTED_VALUE:
    case Semantic_Error_Type::EXPECTED_CALLABLE: {
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
    case Semantic_Error_Type::ARRAY_INITIALIZER_REQUIRES_TYPE_SYMBOL: {
        dynamic_array_push_back(locations, error.error_node->child_start->token_range);
        break;
    }
    case Semantic_Error_Type::ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_REQUIRES_TYPE_SYMBOL: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.end_index - 1, error.error_node->token_range.end_index));
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT: {
        dynamic_array_push_back(locations, error.error_node->child_start->token_range);
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_INVALID_MEMBER_TYPE: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index + 1, error.error_node->token_range.start_index + 2));
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE: {
        dynamic_array_push_back(locations, token_range_make(error.error_node->token_range.start_index, error.error_node->token_range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::ARRAY_INITIALIZER_INVALID_TYPE: {
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
        assert(ast_node_type_is_binary_operation(binary_op_node->type), "HEY");
        Token_Range range = binary_op_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index + 1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT: {
        AST_Node* assign_node = error.error_node;
        /*
        assert(assign_node->type == AST_Node_Type::STATEMENT_ASSIGNMENT ||
            assign_node->type == AST_Node_Type::VARIABLE_DEFINE_ASSIGN ||
            assign_node->type == AST_Node_Type::PARAMETER, "hey");
        Token_Range range = assign_node->child_start->token_range;
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index + 1));
        */
        dynamic_array_push_back(locations, assign_node->token_range);
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
    case Semantic_Error_Type::SYMBOL_MODULE_INVALID: {
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
    case Semantic_Error_Type::INVALID_TYPE_ENUM_VALUE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_EXPECTED_POINTER: {
        dynamic_array_push_back(locations, error.error_node->token_range);
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
    case Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT: {
        dynamic_array_push_back(locations, error.error_node->token_range);
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
    case Semantic_Error_Type::CONSTANT_POOL_ERROR: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_COMPTIME_DEFINITION: {
        Token_Range child_range = error.error_node->child_end->token_range;
        dynamic_array_push_back(locations, token_range_make(child_range.start_index - 1, child_range.start_index));
        break;
    }
    case Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN: {
        Token_Range child_range = error.error_node->child_end->token_range;
        dynamic_array_push_back(locations, token_range_make(child_range.start_index - 1, child_range.start_index));
        break;
    }
    case Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED: {
        Token_Range child_range = error.error_node->child_end->token_range;
        dynamic_array_push_back(locations, token_range_make(child_range.start_index - 1, child_range.start_index));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_CAST_PTR_REQUIRES_U64: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
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
    case Semantic_Error_Type::CANNOT_TAKE_POINTER_OF_FUNCTION: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH: {
        AST_Node* binary_op_node = error.error_node;
        assert(ast_node_type_is_binary_operation(binary_op_node->type), "HEY");
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
    case Semantic_Error_Type::MAIN_CANNOT_BE_TEMPLATED: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::MAIN_MUST_BE_FUNCTION: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::MAIN_NOT_DEFINED: {
        break;
    }
    case Semantic_Error_Type::MAIN_UNEXPECTED_SIGNATURE: {
        break;
    }
    case Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_MAIN: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::MAIN_CANNOT_BE_CALLED: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index + 1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME: {
        dynamic_array_push_back(locations, error.error_node->token_range);
        break;
    }
    case Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE: {
        dynamic_array_push_back(locations, error.error_node->token_range);
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
    case Semantic_Error_Type::ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN: {
        Token_Range range = error.error_node->token_range;
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::ARRAY_SIZE_MUST_BE_GREATER_ZERO: {
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
    case Semantic_Error_Type::CYCLIC_DEPENDENCY_DETECTED: {
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
    switch (e.type)
    {
    case Semantic_Error_Type::INVALID_TYPE:
        string_append_formated(string, "Invalid Type");
        break;
    case Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE:
        string_append_formated(string, "Invalid Type Handle!");
        break;
    case Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME:
        string_append_formated(string, "Type not known at compile time");
        break;
    case Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE:
        string_append_formated(string, "Expression used as a type, but is not a type");
        break;
    case Semantic_Error_Type::INVALID_EXPRESSION_TYPE: {
        string_append_formated(string, "Expression type not valid in this context");
        break;
    }
    case Semantic_Error_Type::EXPECTED_CALLABLE:
        string_append_formated(string, "Expected: Callable");
        break;
    case Semantic_Error_Type::EXPECTED_TYPE:
        string_append_formated(string, "Expected: Type");
        break;
    case Semantic_Error_Type::EXPECTED_VALUE:
        string_append_formated(string, "Expected: Value");
        break;
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT:
        string_append_formated(string, "Invalid Template Argument count");
        break;
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE:
        string_append_formated(string, "Template arguments invalid, symbol is not templated");
        break;
    case Semantic_Error_Type::OTHERS_MEMBER_ACCESS_INVALID_ON_FUNCTION: {
        string_append_formated(string, "Functions do not have any members to access");
        break;
    }
    case Semantic_Error_Type::OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM: {
        string_append_formated(string, "Member access on types requires enums");
        break;
    }
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED:
        string_append_formated(string, "Symbol is templated, requires template arguments");
        break;
    case Semantic_Error_Type::SWITCH_REQUIRES_ENUM:
        string_append_formated(string, "Switch requires enum");
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
        break;
    case Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL:
        string_append_formated(string, "Extern header does not contain this symbol");
        break;
    case Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED:
        string_append_formated(string, "Parsing extern header failed");
        break;
    case Semantic_Error_Type::INVALID_TYPE_VOID_USAGE:
        string_append_formated(string, "Invalid use of void type");
        break;
        string_append_formated(string, "Cyclic workload dependencies detected");
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL:
        string_append_formated(string, "Expected function pointer type on function call");
        break;
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER:
        string_append_formated(string, "Expected function type on function import");
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARGUMENT:
        string_append_formated(string, "Argument type does not match function parameter type");
        break;
    case Semantic_Error_Type::ARRAY_INITIALIZER_REQUIRES_TYPE_SYMBOL: {
        string_append_formated(string, "Array initializer requires type");
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_REQUIRES_TYPE_SYMBOL: {
        string_append_formated(string, "Struct initilizer requires type");
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING: {
        string_append_formated(string, "Struct member/s missing");
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG: {
        string_append_formated(string, "Cannot set union tag in initializer");
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE: {
        string_append_formated(string, "Member already initialized");
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT: {
        string_append_formated(string, "Initializer type must either be struct/union/enum");
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST: {
        string_append_formated(string, "Struct does not contain member");
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_INVALID_MEMBER_TYPE: {
        string_append_formated(string, "Member type does not match");
        break;
    }
    case Semantic_Error_Type::STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER: {
        string_append_formated(string, "Only one union member may be active at one time");
        break;
    }
    case Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE: {
        string_append_formated(string, "Auto struct type could not be determined by context");
        break;
    }
    case Semantic_Error_Type::ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE: {
        string_append_formated(string, "Could not determine array type by context");
        break;
    }
    case Semantic_Error_Type::ARRAY_INITIALIZER_INVALID_TYPE: {
        string_append_formated(string, "Array initializer member invalid type");
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS:
        string_append_formated(string, "Array access only works on array types");
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX:
        string_append_formated(string, "Array access index must be of type i32");
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE:
        string_append_formated(string, "Array allocation size must be of type i32");
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE:
        string_append_formated(string, "Array size must be of type i32");
        break;
    case Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS:
        string_append_formated(string, "Member access only valid on struct/array or pointer to struct/array types");
        break;
    case Semantic_Error_Type::INVALID_TYPE_IF_CONDITION:
        string_append_formated(string, "If condition must be boolean");
        break;
    case Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION:
        string_append_formated(string, "While condition must be boolean");
        break;
    case Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR:
        string_append_formated(string, "Unary operator type invalid");
        break;
    case Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR:
        string_append_formated(string, "Binary operator types invalid");
        break;
    case Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT:
        string_append_formated(string, "Invalid assignment type");
        break;
    case Semantic_Error_Type::INVALID_TYPE_RETURN:
        string_append_formated(string, "Invalid return type");
        break;
    case Semantic_Error_Type::INVALID_TYPE_DELETE:
        string_append_formated(string, "Only pointer or unsized array types can be deleted");
        break;
    case Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER:
        string_append_formated(string, "Expected Type symbol");
        break;
    case Semantic_Error_Type::SYMBOL_MODULE_INVALID:
        string_append_formated(string, "Expected Variable or Function symbol for Variable read");
        break;
    case Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH:
        string_append_formated(string, "Expected module in indentifier path");
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL:
        string_append_formated(string, "Could not resolve symbol");
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED:
        string_append_formated(string, "Symbol already defined");
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED:
        string_append_formated(string, "Module already defined");
        break;
    case Semantic_Error_Type::INVALID_TYPE_ENUM_VALUE: {
        string_append_formated(string, "Enum value must be of i32 type!");
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_EXPECTED_POINTER: {
        string_append_formated(string, "Invalid type, expected pointer in pointer comparison!");
        break;
    }
    case Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED:
        string_append_formated(string, "Bake error");
        break;
    case Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT:
        string_append_formated(string, "Auto Member must be in used in a Context where an enum is required");
        break;
    case Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED:
        string_append_formated(string, "Auto Member must be used inside known Context");
        break;
    case Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED:
        string_append_formated(string, "Auto cast not able to extract destination type, context too vague");
        break;
    case Semantic_Error_Type::BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS:
        string_append_formated(string, "Bake function must not reference globals!");
        break;
    case Semantic_Error_Type::CONSTANT_POOL_ERROR: {
        string_append_formated(string, "Could not add value to constant pool, error:");
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_COMPTIME_DEFINITION:
        string_append_formated(string, "Value does not match given type");
        break;
    case Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN:
        string_append_formated(string, "Comptime definition value must be known at compile time");
        break;
    case Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED:
        string_append_formated(string, "Modules/Polymorphic function definitions must be infered, e.g: x :: module{}");
        break;
    case Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH:
        string_append_formated(string, "Parameter count does not match argument count");
        break;
    case Semantic_Error_Type::INVALID_TYPE_CAST_PTR_REQUIRES_U64:
        string_append_formated(string, "cast_ptr only casts from u64 to pointers, and requires an u64 value as operand");
        break;
    case Semantic_Error_Type::INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR:
        string_append_formated(string, "cast_ptr only casts from u64 to pointers, destination type must therefore be of pointer-type");
        break;
    case Semantic_Error_Type::EXPRESSION_INVALID_CAST:
        string_append_formated(string, "Invalid cast");
        break;
    case Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND:
        string_append_formated(string, "Struct/Array does not contain member");
        break;
    case Semantic_Error_Type::LABEL_ALREADY_IN_USE:
        string_append_formated(string, "Label already in use");
        break;
    case Semantic_Error_Type::BREAK_LABLE_NOT_FOUND:
        string_append_formated(string, "Label cannot be found");
        break;
    case Semantic_Error_Type::BREAK_NOT_INSIDE_LOOP_OR_SWITCH:
        string_append_formated(string, "Break is not inside a loop or switch");
        break;
    case Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND:
        string_append_formated(string, "Label cannot be found");
        break;
    case Semantic_Error_Type::CONTINUE_NOT_INSIDE_LOOP:
        string_append_formated(string, "Continue is not inside a loop");
        break;
    case Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK:
        string_append_formated(string, "Continue only works for loop lables");
        break;
    case Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH:
        string_append_formated(string, "Binary op types do not match and cannot be implicitly casted");
        break;
    case Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL:
        string_append_formated(string, "Expression does not do anything, must be function call");
        break;
    case Semantic_Error_Type::OTHERS_STRUCT_MUST_CONTAIN_MEMBER:
        string_append_formated(string, "Struct must contain at least one member");
        break;
    case Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED:
        string_append_formated(string, "Struct member is already defined");
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
    case Semantic_Error_Type::MAIN_CANNOT_BE_TEMPLATED:
        string_append_formated(string, "Main function cannot be templated");
        break;
    case Semantic_Error_Type::MAIN_MUST_BE_FUNCTION:
        string_append_formated(string, "Main must be a function");
        break;
    case Semantic_Error_Type::MAIN_NOT_DEFINED:
        string_append_formated(string, "Main function not found");
        break;
    case Semantic_Error_Type::MAIN_UNEXPECTED_SIGNATURE:
        string_append_formated(string, "Main unexpected signature");
        break;
    case Semantic_Error_Type::MAIN_CANNOT_BE_CALLED:
        string_append_formated(string, "Cannot call main function again");
        break;
    case Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE:
        string_append_formated(string, "Could not load file");
        break;
    case Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_MAIN:
        string_append_formated(string, "Cannot take address of main");
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
    case Semantic_Error_Type::ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN:
        string_append_formated(string, "Array size not known at compile time");
        break;
    case Semantic_Error_Type::ARRAY_SIZE_MUST_BE_GREATER_ZERO:
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
    case Semantic_Error_Type::CANNOT_TAKE_POINTER_OF_FUNCTION: {
        string_append_formated(string, "Cannot take pointer of function, if you need a 'C function pointer' remove the *");
        break;
    }
    case Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT: {
        string_append_formated(string, "Cannot take address of temporary result!");
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS:
        string_append_formated(string, "Nested defers not implemented yet");
        break;
    case Semantic_Error_Type::CYCLIC_DEPENDENCY_DETECTED:
        string_append_formated(string, "Cyclic workload dependencies detected");
        break;
    case Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET:
        string_append_formated(string, "Variable not defined yet");
        break;
    default: panic("ERROR");
    }

    for (int k = 0; k < e.information.size; k++)
    {
        Error_Information* info = &e.information[k];
        switch (info->type)
        {
        case Error_Information_Type::CYCLE_WORKLOAD:
            string_append_formated(string, "\n  ");
            analysis_workload_append_to_string(info->options.cycle_workload, string);
            break;
        case Error_Information_Type::ARGUMENT_COUNT:
            string_append_formated(string, "\n  Given argument count: %d, required: %d",
                info->options.invalid_argument_count.given, info->options.invalid_argument_count.expected);
            break;
        case Error_Information_Type::ID:
            string_append_formated(string, "\n  Name: %s", info->options.id->characters);
            break;
        case Error_Information_Type::INVALID_MEMBER: {
            string_append_formated(string, "\n  Accessed member name: %s", info->options.invalid_member.member_id->characters);
            string_append_formated(string, "\n  Available struct members ");
            for (int i = 0; i < info->options.invalid_member.struct_signature->options.structure.members.size; i++) {
                Struct_Member* member = &info->options.invalid_member.struct_signature->options.structure.members[i];
                string_append_formated(string, "\n\t\t%s", member->id->characters);
            }
            break;
        }
        case Error_Information_Type::SYMBOL: {
            string_append_formated(string, "\n  Symbol: ");
            symbol_append_to_string(info->options.symbol, string);
            break;
        }
        case Error_Information_Type::EXIT_CODE: {
            string_append_formated(string, "\n  Exit_Code: ");
            exit_code_append_to_string(string, info->options.exit_code);
            break;
        }
        case Error_Information_Type::GIVEN_TYPE:
            string_append_formated(string, "\n  Given Type:    ");
            type_signature_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::EXPECTED_TYPE:
            string_append_formated(string, "\n  Expected Type: ");
            type_signature_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::FUNCTION_TYPE:
            string_append_formated(string, "\n  Function Type: ");
            type_signature_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::BINARY_OP_TYPES:
            string_append_formated(string, "\n  Left Operand type:  ");
            type_signature_append_to_string(string, info->options.binary_op_types.left_type);
            string_append_formated(string, "\n  Right Operand type: ");
            type_signature_append_to_string(string, info->options.binary_op_types.right_type);
            break;
        case Error_Information_Type::EXPRESSION_RESULT_TYPE:
        {
            string_append_formated(string, "\nGiven: ");
            switch (info->options.expression_type)
            {
            case Expression_Result_Type::EXTERN_FUNCTION:
                string_append_formated(string, "Extern function");
                break;
            case Expression_Result_Type::HARDCODED_FUNCTION:
                string_append_formated(string, "Hardcoded function");
                break;
            case Expression_Result_Type::MODULE:
                string_append_formated(string, "Module");
                break;
            case Expression_Result_Type::EXPRESSION:
                string_append_formated(string, "Value");
                break;
            case Expression_Result_Type::FUNCTION:
                string_append_formated(string, "Function");
                break;
            case Expression_Result_Type::TYPE:
                string_append_formated(string, "Type");
                break;
            default: panic("");
            }
            break;
        }
        case Error_Information_Type::CONSTANT_STATUS:
            string_append_formated(string, "\n  %s", constant_status_to_string(info->options.constant_status));
            break;
        default: panic("");
        }
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
        /*
        if (type->structure.is_anonymous) {
            signature.options.structure.id = identifier_pool_add(&analyser->compiler->identifier_pool, string_create_static("__c_anon"));
        }
        else {
            signature.options.structure.id = type->structure.id;
        }
        */
        signature.options.structure.symbol = 0;
        signature.options.structure.struct_type = Structure_Type::C_UNION;
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
