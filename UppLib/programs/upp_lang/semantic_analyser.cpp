#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/dependency_graph.hpp"
#include "../../utility/file_io.hpp"

#include "compiler.hpp"
#include "type_system.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "rc_analyser.hpp"
#include "compiler_misc.hpp"
#include "ast.hpp"
#include "ir_code.hpp"
#include "parser.hpp"

struct Expression_Result;
struct Expression_Context;

// GLOBALS
bool PRINT_DEPENDENCIES = false;
static Semantic_Analyser semantic_analyser;
static Workload_Executer workload_executer;

// PROTOTYPES
Expression_Result semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context);
ModTree_Expression* semantic_analyser_analyse_expression_value( AST::Expression* rc_expression, Expression_Context context);
Type_Signature* semantic_analyser_analyse_expression_type( AST::Expression* rc_expression);
void analysis_workload_add_dependency_internal(Analysis_Workload* workload, Analysis_Workload* dependency, Symbol_Dependency* symbol_read);
void analysis_workload_destroy(Analysis_Workload* workload);
void modtree_block_destroy(ModTree_Block* block);
void modtree_function_destroy(ModTree_Function* function);
void modtree_statement_destroy(ModTree_Statement* statement);
ModTree_Expression* semantic_analyser_cast_implicit_if_possible( ModTree_Expression* expression, Type_Signature* destination_type);
bool analysis_workload_execute(Analysis_Workload* workload);
void analysis_workload_add_struct_dependency(Struct_Progress* my_workload, Struct_Progress* other_progress, Dependency_Type type, Symbol_Dependency* symbol_read);
void semantic_analyser_fill_block( ModTree_Block* block, AST::Code_Block* code_block);
void analysis_workload_append_to_string(Analysis_Workload* workload, String* string);
Analysis_Workload* workload_executer_add_workload_empty(Analysis_Workload_Type type, Analysis_Item* item, Analysis_Progress* progress, bool add_symbol_dependencies);
void analysis_workload_check_if_runnable(Analysis_Workload* workload);
Comptime_Result comptime_result_make_not_comptime();
ModTree_Function* modtree_function_create_empty(Type_Signature* signature, Symbol* symbol, AST::Code_Block* body_block);

/*
ERROR Helpers
*/
Structure_Type ast_structure_type_to_type(AST::Structure_Type type)
{
    switch (type)
    {
    case AST::Structure_Type::C_UNION: return Structure_Type::C_UNION;
    case AST::Structure_Type::STRUCT: return Structure_Type::STRUCT;
    case AST::Structure_Type::UNION: return Structure_Type::UNION;
    }
    panic("");
    return Structure_Type::UNION;
}

#define to_base(x) (&x->base)

template<typename T>
T* analysis_progress_allocate_internal(Analysis_Progress_Type type)
{
    auto progress = new T;
    Analysis_Progress* base = &progress->base;
    dynamic_array_push_back(&workload_executer.allocated_progresses, base);
    memory_zero(progress);
    base->type = type;
    return progress;
}

Function_Progress* analysis_progress_create_function(ModTree_Function* function) {
    auto result = analysis_progress_allocate_internal<Function_Progress>(Analysis_Progress_Type::FUNCTION);
    result->state = Function_State::DEFINED;
    result->function = function;
    hashtable_insert_element(&workload_executer.progress_functions, function, result);
    return result;
}

Struct_Progress* analysis_progress_create_struct(Type_Signature* struct_type) {
    auto result = analysis_progress_allocate_internal<Struct_Progress>(Analysis_Progress_Type::STRUCTURE);
    result->state = Struct_State::DEFINED;
    result->struct_type = struct_type;
    hashtable_insert_element(&workload_executer.progress_structs, struct_type, result);
    return result;
}

Bake_Progress* analysis_progress_create_bake() 
{
    auto result = analysis_progress_allocate_internal<Bake_Progress>(Analysis_Progress_Type::BAKE);
    result->bake_function = modtree_function_create_empty(
        type_system_make_function(&compiler.type_system, dynamic_array_create_empty<Type_Signature*>(1), compiler.type_system.void_type), 0, 0
    );
    result->result = comptime_result_make_not_comptime();
    return result;
}

Definition_Progress* analysis_progress_create_definition(Symbol* symbol) {
    auto result = analysis_progress_allocate_internal<Definition_Progress>(Analysis_Progress_Type::DEFINITION);
    result->symbol = symbol;
    hashtable_insert_element(&workload_executer.progress_definitions, symbol, result);
    return result;
}

void semantic_analyser_set_error_flag(bool error_due_to_unknown)
{
    auto& analyser = semantic_analyser;
    analyser.error_flag_count += 1;
    if (analyser.current_function != 0)
    {
        if (analyser.current_function->type == ModTree_Function_Type::POLYMORPHIC_BASE) {
            if (!error_due_to_unknown) {
                analyser.current_function->contains_errors = true;
            }
        }
        else {
            analyser.current_function->contains_errors = true;
        }
        analyser.current_function->is_runnable = false;
    }
}

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Base* node) {
    Semantic_Error error;
    error.type = type;
    error.error_node = node;
    error.information = dynamic_array_create_empty<Error_Information>(2);
    dynamic_array_push_back(&semantic_analyser.errors, error);
    semantic_analyser_set_error_flag(false);
}

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Expression* node) {
    semantic_analyser_log_error(type, to_base(node));
}

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Statement* node) {
    semantic_analyser_log_error(type, to_base(node));
}

void semantic_analyser_add_error_info(Error_Information info) {
    auto& errors = semantic_analyser.errors;
    assert(errors.size != 0, "");
    Semantic_Error* last_error = &errors[errors.size - 1];
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

ModTree_Block* modtree_block_create_empty(AST::Code_Block* code_block)
{
    ModTree_Block* block = new ModTree_Block;
    block->statements = dynamic_array_create_empty<ModTree_Statement*>(1);
    block->variables = dynamic_array_create_empty<ModTree_Variable*>(1);
    block->control_flow_locked = false;
    block->flow = Control_Flow::SEQUENTIAL;
    block->code_block = code_block;
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

ModTree_Function* modtree_function_create_empty(Type_Signature* signature, Symbol* symbol, AST::Code_Block* body_block)
{
    ModTree_Function* function = new ModTree_Function;
    function->type = ModTree_Function_Type::NORMAL;
    function->parameters = dynamic_array_create_empty<ModTree_Parameter>(1);
    function->symbol = symbol;
    function->signature = signature;
    function->called_from = dynamic_array_create_empty<ModTree_Function*>(1);
    function->calls = dynamic_array_create_empty<ModTree_Function*>(1);
    function->body = modtree_block_create_empty(body_block);
    function->contains_errors = false;
    function->is_runnable = true;
    dynamic_array_push_back(&semantic_analyser.program->functions, function);
    return function;
}

ModTree_Function* modtree_function_create_poly_instance(ModTree_Function* base_function, Dynamic_Array<Upp_Constant> arguments)
{
    auto& analyser = semantic_analyser;
    ModTree_Function* instance = modtree_function_create_empty(base_function->signature, base_function->symbol, base_function->body->code_block);
    instance->type = ModTree_Function_Type::POLYMOPRHIC_INSTANCE;
    for (int i = 0; i < base_function->parameters.size; i++) {
        if (base_function->parameters[i].is_comptime) continue;
        dynamic_array_push_back(&instance->parameters, base_function->parameters[i]);
    }
    instance->options.instance.instance_base_function = base_function;
    instance->options.instance.poly_arguments = arguments;
    assert(arguments.size == base_function->options.base.poly_argument_count, "");

    // Create Workloads
    auto base_progress = *hashtable_find_element(&workload_executer.progress_functions, base_function);
    Function_Progress* new_progress = analysis_progress_create_function(instance);
    new_progress->state = Function_State::HEADER_ANALYSED;

    Analysis_Workload* body_workload = workload_executer_add_workload_empty(
        Analysis_Workload_Type::FUNCTION_BODY, base_progress->header_workload->analysis_item, to_base(new_progress), false
    );
    analysis_workload_add_dependency_internal(body_workload, base_progress->body_workload, 0);
    analysis_workload_check_if_runnable(body_workload); // Required so that progress made is set

    Analysis_Workload* compile_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, base_progress->header_workload->analysis_item, to_base(new_progress), false);
    compile_workload->options.cluster_compile.functions = dynamic_array_create_empty<ModTree_Function*>(1);
    dynamic_array_push_back(&compile_workload->options.cluster_compile.functions, instance);
    analysis_workload_add_dependency_internal(compile_workload, body_workload, 0);
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

ModTree_Variable* modtree_program_add_global(Type_Signature* type, Symbol* symbol)
{
    ModTree_Variable* var = new ModTree_Variable;
    assert(type != 0, "");
    var->data_type = type;
    var->symbol = symbol;
    dynamic_array_push_back(&semantic_analyser.program->globals, var);
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

ModTree_Program* modtree_program_create()
{
    ModTree_Program* result = new ModTree_Program();
    result->entry_function = 0;
    result->functions = dynamic_array_create_empty<ModTree_Function*>(16);
    result->globals = dynamic_array_create_empty<ModTree_Variable*>(16);
    result->extern_functions = dynamic_array_create_empty<ModTree_Extern_Function*>(16);
    result->hardcoded_functions = dynamic_array_create_empty<ModTree_Hardcoded_Function*>(16);
    return result;
}

void modtree_program_destroy()
{
    auto& program = semantic_analyser.program;
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

Comptime_Result modtree_expression_calculate_comptime_value(ModTree_Expression* expr)
{
    auto& analyser = semantic_analyser;
    Type_System* type_system = &analyser.compiler->type_system;
    switch (expr->expression_type)
    {
    case ModTree_Expression_Type::ERROR_EXPR: {
        return comptime_result_make_unavailable(analyser.compiler->type_system.unknown_type);
    }
    case ModTree_Expression_Type::BINARY_OPERATION:
    {
        Comptime_Result left_val = modtree_expression_calculate_comptime_value(expr->options.binary_operation.left_operand);
        Comptime_Result right_val = modtree_expression_calculate_comptime_value(expr->options.binary_operation.right_operand);
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

        void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, expr->result_type->size, expr->result_type->alignment);
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
        Comptime_Result value = modtree_expression_calculate_comptime_value(expr->options.unary_operation.operand);
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
        case ModTree_Unary_Operation_Type::DEREFERENCE:
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

        void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, expr->result_type->size, expr->result_type->alignment);
        bytecode_execute_unary_instr(instr_type, type_signature_to_bytecode_type(value.data_type), result_buffer, value.data);
        comptime_result_make_available(result_buffer, expr->result_type);
        break;
    }
    break;
    case ModTree_Expression_Type::CONSTANT_READ: {
        Constant_Pool* pool = &analyser.compiler->constant_pool;
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
        Comptime_Result value = modtree_expression_calculate_comptime_value(expr->options.cast.cast_argument);
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
            Upp_Slice_Base* slice = stack_allocator_allocate<Upp_Slice_Base>(&analyser.allocator_values);
            slice->data_ptr = value.data;
            slice->size = value.data_type->options.array.element_count;
            return comptime_result_make_available(slice, expr->result_type);
        }
        case ModTree_Cast_Type::TO_ANY: {
            Upp_Any* any = stack_allocator_allocate<Upp_Any>(&analyser.allocator_values);
            any->type = expr->result_type->internal_index;
            any->data = value.data;
            return comptime_result_make_available(any, expr->result_type);
        }
        case ModTree_Cast_Type::FROM_ANY: {
            Upp_Any* given = (Upp_Any*)value.data;
            if (given->type >= analyser.compiler->type_system.types.size) {
                return comptime_result_make_not_comptime();
            }
            Type_Signature* any_type = analyser.compiler->type_system.types[given->type];
            if (any_type != value.data_type) {
                return comptime_result_make_not_comptime();
            }
            return comptime_result_make_available(given->data, any_type);
        }
        default: panic("");
        }
        if ((int)instr_type == -1) panic("");

        void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, expr->result_type->size, expr->result_type->alignment);
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
        Comptime_Result value_array = modtree_expression_calculate_comptime_value(expr->options.array_access.array_expression);
        Comptime_Result value_index = modtree_expression_calculate_comptime_value(expr->options.array_access.index_expression);
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
        Comptime_Result value_struct = modtree_expression_calculate_comptime_value(expr->options.member_access.structure_expression);
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
    case ModTree_Expression_Type::CAST: {
        if (expression->options.cast.type == ModTree_Cast_Type::FROM_ANY) return false;
        return true;
    }
    case ModTree_Expression_Type::ARRAY_INITIALIZER: return true;
    case ModTree_Expression_Type::STRUCT_INITIALIZER: return true;
    case ModTree_Expression_Type::ERROR_EXPR: return true;
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

ModTree_Expression* modtree_expression_create_constant(Type_Signature* signature, Array<byte> bytes, AST::Base* error_report_node)
{
    auto& analyser = semantic_analyser;
    Constant_Result result = constant_pool_add_constant(&analyser.compiler->constant_pool, signature, bytes);
    if (result.status != Constant_Status::SUCCESS)
    {
        assert(error_report_node != 0, "Error"); // Error report node may only be null if we know that adding the constant cannot fail.
        semantic_analyser_log_error(Semantic_Error_Type::CONSTANT_POOL_ERROR, error_report_node);
        semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
        return modtree_expression_make_error(signature);
    }
    ModTree_Expression* expression = new ModTree_Expression;
    expression->expression_type = ModTree_Expression_Type::CONSTANT_READ;
    expression->options.constant_read = result.constant;
    expression->result_type = signature;
    return expression;
}

ModTree_Expression* modtree_expression_create_constant_enum(Type_Signature* enum_type, i32 value) {
    return modtree_expression_create_constant(enum_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}

ModTree_Expression* modtree_expression_create_constant_i32(i32 value) {
    return modtree_expression_create_constant(semantic_analyser.compiler->type_system.i32_type, array_create_static((byte*)&value, sizeof(i32)), 0);
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

AST::Base* rc_to_ast(AST::Expression* expression)
{
    return &expression->base;
}

AST::Base* rc_to_ast(AST::Statement* statement)
{
    return &statement->base;
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

Workload_Executer* workload_executer_initialize()
{
    workload_executer.workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    workload_executer.runnable_workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    workload_executer.workload_dependencies = hashtable_create_empty<Workload_Pair, Dependency_Information>(8, workload_pair_hash, workload_pair_equals);

    workload_executer.allocated_progresses = dynamic_array_create_empty<Analysis_Progress*>(8);
    workload_executer.progress_items = hashtable_create_pointer_empty<Analysis_Item*, Analysis_Progress*>(8);
    workload_executer.progress_definitions = hashtable_create_pointer_empty<Symbol*, Definition_Progress*>(8);
    workload_executer.progress_structs = hashtable_create_pointer_empty<Type_Signature*, Struct_Progress*>(8);
    workload_executer.progress_functions = hashtable_create_pointer_empty<ModTree_Function*, Function_Progress*>(8);

    workload_executer.progress_was_made = false;
    return &workload_executer;
}

void workload_executer_destroy()
{
    auto& executer = workload_executer;
    for (int i = 0; i < executer.allocated_progresses.size; i++) {
        delete executer.allocated_progresses[i];
    }
    dynamic_array_destroy(&executer.allocated_progresses);
    for (int i = 0; i < executer.workloads.size; i++) {
        analysis_workload_destroy(executer.workloads[i]);
    }
    {
        auto iter = hashtable_iterator_create(&executer.workload_dependencies);
        while (hashtable_iterator_has_next(&iter)) {
            SCOPE_EXIT(hashtable_iterator_next(&iter));
            dynamic_array_destroy(&iter.value->symbol_reads);
        }
        hashtable_destroy(&executer.workload_dependencies);
    }
    dynamic_array_destroy(&executer.workloads);
    dynamic_array_destroy(&executer.runnable_workloads);
    hashtable_destroy(&executer.progress_items);
    hashtable_destroy(&executer.progress_definitions);
    hashtable_destroy(&executer.progress_functions);
    hashtable_destroy(&executer.progress_structs);
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

void symbol_dependency_set_error_symbol(Symbol_Dependency* symbol_dep)
{
    symbol_dep->resolved_symbol = compiler.dependency_analyser->predefined_symbols.error_symbol;
    AST::Symbol_Read* read = symbol_dep->read;
    while (read != 0)
    {
        if (read->resolved_symbol == 0 || !read->path_child.available) {
            read->resolved_symbol = compiler.dependency_analyser->predefined_symbols.error_symbol;
        }
        if (read->path_child.available) {
            read = read->path_child.value;
        }
        else {
            read = 0;
        }
    }
}

bool symbol_dependency_try_resolve(Symbol_Dependency* symbol_dep)
{
    if (symbol_dep->resolved_symbol != 0) return true;
    auto& analyser = semantic_analyser;
    AST::Symbol_Read* read = symbol_dep->read;
    Symbol_Table* table = symbol_dep->symbol_table;
    while (true)
    {
        bool is_path = read->path_child.available;
        auto& symbol = read->resolved_symbol;
        if (symbol == 0) {
            symbol = symbol_table_find_symbol(table, read->name, read != symbol_dep->read, is_path ? 0 : symbol_dep);
        }
        if (is_path)
        {
            if (symbol == 0) {
                return false;
            }
            if (symbol->type == Symbol_Type::UNRESOLVED) {
                return false;
            }
            if (symbol->type == Symbol_Type::MODULE) {
                read->resolved_symbol = symbol;
                table = symbol->options.module_table;
                read = read->path_child.value;
                continue;
            }

            semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH, &read->base);
            semantic_analyser_add_error_info(error_information_make_symbol(symbol));
            symbol_dependency_set_error_symbol(symbol_dep);
            return true;
        }
        else {
            symbol_dep->resolved_symbol = symbol;
            read->resolved_symbol = symbol;
            return true;
        }
    }
}

void analysis_workload_add_dependency_internal(Analysis_Workload* workload, Analysis_Workload* dependency, Symbol_Dependency* symbol_read)
{
    auto& executer = workload_executer;
    if (dependency->is_finished) return;
    Workload_Pair pair = workload_pair_create(workload, dependency);
    Dependency_Information* infos = hashtable_find_element(&executer.workload_dependencies, pair);
    if (infos == 0) {
        Dependency_Information info;
        info.dependency_node = list_add_at_end(&workload->dependencies, dependency);
        info.dependent_node = list_add_at_end(&dependency->dependents, workload);
        info.symbol_reads = dynamic_array_create_empty<Symbol_Dependency*>(1);
        info.only_symbol_read_dependency = false;
        if (symbol_read != 0) {
            info.only_symbol_read_dependency = true;
            dynamic_array_push_back(&info.symbol_reads, symbol_read);
        }
        bool inserted = hashtable_insert_element(&executer.workload_dependencies, pair, info);
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

void workload_executer_move_dependency(Analysis_Workload* move_from, Analysis_Workload* move_to, Analysis_Workload* dependency)
{
    auto graph = &workload_executer;
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

void workload_executer_remove_dependency(Analysis_Workload* workload, Analysis_Workload* depends_on, bool add_if_runnable)
{
    auto graph = &workload_executer;
    Workload_Pair pair = workload_pair_create(workload, depends_on);
    Dependency_Information* info = hashtable_find_element(&graph->workload_dependencies, pair);
    assert(info != 0, "");
    list_remove_node(&workload->dependencies, info->dependency_node);
    list_remove_node(&depends_on->dependents, info->dependent_node);
    dynamic_array_destroy(&info->symbol_reads);
    bool worked = hashtable_remove_element(&graph->workload_dependencies, pair);
    if (add_if_runnable && workload->dependencies.count == 0 && workload->symbol_dependencies.size == 0) {
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


void analysis_workload_add_cluster_dependency(Analysis_Workload* add_to_workload, Analysis_Workload* dependency, Symbol_Dependency* symbol_read)
{
    auto graph = &workload_executer;
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
        analysis_workload_add_dependency_internal(merge_into, merge_from, symbol_read);
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
            if (merge_dependency == merge_into || merge_dependency == merge_from) {
                keep_dependency = false;
            }
            else
            {
                bool* contains_loop = hashtable_find_element(&visited, merge_dependency);
                if (contains_loop != 0) {
                    keep_dependency = !*contains_loop;
                }
            }

            auto next = node->next;
            if (keep_dependency) {
                workload_executer_move_dependency(merge_cluster, merge_into, merge_dependency);
            }
            else {
                workload_executer_remove_dependency(merge_cluster, merge_dependency, false);
            }
            node = next;
        }
        list_reset(&merge_cluster->dependencies);

        // Merge all analysis item values
        switch (merge_into->type)
        {
        case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
        {
            dynamic_array_append_other(&merge_into->options.cluster_compile.functions, &merge_cluster->options.cluster_compile.functions);
            dynamic_array_reset(&merge_cluster->options.cluster_compile.functions);
            break;
        }
        case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
        {
            dynamic_array_append_other(&merge_into->options.struct_reachable.struct_types, &merge_cluster->options.struct_reachable.struct_types);
            dynamic_array_reset(&merge_cluster->options.struct_reachable.unfinished_array_types);
            dynamic_array_append_other(&merge_into->options.struct_reachable.unfinished_array_types, &merge_cluster->options.struct_reachable.unfinished_array_types);
            dynamic_array_reset(&merge_cluster->options.struct_reachable.struct_types);
            break;
        }
        case Analysis_Workload_Type::DEFINITION:
        case Analysis_Workload_Type::FUNCTION_BODY:
        case Analysis_Workload_Type::FUNCTION_HEADER:
        case Analysis_Workload_Type::STRUCT_ANALYSIS:
            panic("Clustering only on function clusters and reachable resolve cluster!");
        default: panic("");
        }

        // Add reachables to merged
        dynamic_array_append_other(&merge_into->reachable_clusters, &merge_cluster->reachable_clusters);
        dynamic_array_reset(&merge_cluster->reachable_clusters);
        analysis_workload_add_dependency_internal(merge_cluster, merge_into, 0);
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

void analysis_workload_add_struct_dependency(Struct_Progress* progress, Struct_Progress* other, Dependency_Type type, Symbol_Dependency* symbol_read)
{
    auto graph = &workload_executer;
    if (progress->state == Struct_State::FINISHED || other->state == Struct_State::FINISHED) return;

    switch (type)
    {
    case Dependency_Type::NORMAL: {
        // Struct member references another struct, but not as a type
        // E.g. Foo :: struct { value: Bar.{...}; }
        analysis_workload_add_dependency_internal(progress->analysis_workload, other->reachable_resolve_workload, symbol_read);
        break;
    }
    case Dependency_Type::MEMBER_IN_MEMORY: {
        // Struct member references other member in memory
        // E.g. Foo :: struct { value: Bar; }
        analysis_workload_add_dependency_internal(progress->analysis_workload, other->analysis_workload, symbol_read);
        analysis_workload_add_cluster_dependency(progress->reachable_resolve_workload, other->reachable_resolve_workload, symbol_read);
        break;
    }
    case Dependency_Type::MEMBER_REFERENCE:
    {
        // Struct member contains some sort of reference to other member
        // E.g. Foo :: struct { value: *Bar; }
        // This means we need to unify the Reachable-Clusters
        analysis_workload_add_cluster_dependency(progress->reachable_resolve_workload, other->reachable_resolve_workload, symbol_read);
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

void workload_executer_resolve()
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
    auto graph = &workload_executer;
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
                Symbol_Dependency* dep = workload->symbol_dependencies[j];
                if (dep->resolved_symbol == 0) {
                    symbol_dependency_try_resolve(dep);
                    if (dep->resolved_symbol == 0) {
                        continue;
                    }
                    graph->progress_was_made = true;
                }
                auto& symbol = dep->resolved_symbol;

                bool symbol_read_ready = true;
                switch (symbol->type)
                {
                case Symbol_Type::UNRESOLVED:
                {
                    Definition_Progress** progress = hashtable_find_element(&graph->progress_definitions, symbol);
                    if (progress != 0) {
                        symbol_read_ready = true;
                        analysis_workload_add_dependency_internal(workload, (*progress)->definition_workload, dep);
                    }
                    else {
                        symbol_read_ready = false;
                    }
                    break;
                }
                case Symbol_Type::FUNCTION:
                {
                    Function_Progress** progress = hashtable_find_element(&graph->progress_functions, symbol->options.function);
                    if (progress != 0) {
                        analysis_workload_add_dependency_internal(workload, (*progress)->header_workload, dep);
                    }
                    break;
                }
                case Symbol_Type::TYPE:
                {
                    Type_Signature* type = symbol->options.type;
                    if (type->type == Signature_Type::STRUCT && workload->progress->type == Analysis_Progress_Type::STRUCTURE)
                    {
                        Struct_Progress** progress = hashtable_find_element(&graph->progress_structs, type);
                        if (progress != 0) {
                            analysis_workload_add_struct_dependency((Struct_Progress*)workload->progress, *progress, dep->type, dep);
                        }
                        else {
                            assert(type->size != 0 && type->alignment != 0, "");
                        }
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
        if (false)
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
            //logg("Executing workload: %s\n", tmp.characters);
            string_destroy(&tmp);

            analysis_workload_execute(workload);
            // Note: After a workload executes, it may have added new dependencies to itself
            if (workload->dependencies.count == 0)
            {
                workload->is_finished = true;
                List_Node<Analysis_Workload*>* node = workload->dependents.head;
                while (node != 0) {
                    Analysis_Workload* dependent = node->value;
                    node = node->next; // INFO: This is required before remove_dependency, since remove will remove items from the list
                    workload_executer_remove_dependency(dependent, workload, true);
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
                            symbol_dependency_set_error_symbol(infos.symbol_reads[j]);
                            semantic_analyser_log_error(Semantic_Error_Type::CYCLIC_DEPENDENCY_DETECTED, &infos.symbol_reads[j]->read->base);
                            for (int k = 0; k < workload_cycle.size; k++) {
                                Analysis_Workload* workload = workload_cycle[k];
                                semantic_analyser_add_error_info(error_information_make_cycle_workload(workload));
                            }
                        }
                        workload_executer_remove_dependency(workload, depends_on, true);
                    }
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
            semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL, &lowest->symbol_dependencies[i]->read->base);
            symbol_dependency_set_error_symbol(lowest->symbol_dependencies[i]);
        }
        dynamic_array_reset(&lowest->symbol_dependencies);
        dynamic_array_push_back(&graph->runnable_workloads, lowest);
    }

}

Analysis_Workload* workload_executer_add_workload_empty(Analysis_Workload_Type type, Analysis_Item* item, Analysis_Progress* progress, bool add_symbol_dependencies)
{
    auto& executer = workload_executer;
    executer.progress_was_made = true;
    assert(item != 0, "");

    Analysis_Workload* workload = new Analysis_Workload;
    workload->type = type;
    workload->analysis_item = item;
    workload->progress = progress;
    workload->is_finished = false;
    workload->cluster = 0;
    workload->dependencies = list_create<Analysis_Workload*>();
    workload->dependents = list_create<Analysis_Workload*>();
    workload->reachable_clusters = dynamic_array_create_empty<Analysis_Workload*>(1);
    workload->symbol_dependencies = dynamic_array_create_empty<Symbol_Dependency*>(1);
    if (item != 0 && add_symbol_dependencies) {
        for (int i = 0; i < item->symbol_dependencies.size; i++) {
            dynamic_array_push_back(&workload->symbol_dependencies, &item->symbol_dependencies[i]);
        }
    }
    switch (type)
    {
    case Analysis_Workload_Type::DEFINITION:
        assert(progress->type == Analysis_Progress_Type::DEFINITION, "");
        ((Definition_Progress*)progress)->definition_workload = workload;
        break;
    case Analysis_Workload_Type::STRUCT_ANALYSIS:
        assert(progress->type == Analysis_Progress_Type::STRUCTURE, "");
        ((Struct_Progress*)progress)->analysis_workload = workload;
        break;
    case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
        assert(progress->type == Analysis_Progress_Type::STRUCTURE, "");
        ((Struct_Progress*)progress)->reachable_resolve_workload = workload;
        workload->options.struct_reachable.struct_types = dynamic_array_create_empty<Type_Signature*>(1);
        workload->options.struct_reachable.unfinished_array_types = dynamic_array_create_empty<Type_Signature*>(1);
        break;
    case Analysis_Workload_Type::BAKE_ANALYSIS:
        assert(progress->type == Analysis_Progress_Type::BAKE, "");
        ((Bake_Progress*)progress)->analysis_workload = workload;
        break;
    case Analysis_Workload_Type::BAKE_EXECUTION:
        assert(progress->type == Analysis_Progress_Type::BAKE, "");
        ((Bake_Progress*)progress)->execute_workload = workload;
        break;
    case Analysis_Workload_Type::FUNCTION_HEADER:
        assert(progress->type == Analysis_Progress_Type::FUNCTION, "");
        ((Function_Progress*)progress)->header_workload = workload;
        break;
    case Analysis_Workload_Type::FUNCTION_BODY:
        assert(progress->type == Analysis_Progress_Type::FUNCTION, "");
        ((Function_Progress*)progress)->body_workload = workload;
        break;
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
        assert(progress->type == Analysis_Progress_Type::FUNCTION, "");
        ((Function_Progress*)progress)->compile_workload = workload;
        workload->options.cluster_compile.functions = dynamic_array_create_empty<ModTree_Function*>(1);
        break;
    default:panic("");
    }
    dynamic_array_push_back(&executer.workloads, workload);

    return workload;
}

void workload_executer_add_analysis_items(Dependency_Analyser* dependency_analyser)
{
    auto& executer = workload_executer;
    // Create workload
    for (int i = 0; i < dependency_analyser->analysis_items.size; i++)
    {
        Analysis_Item* item = dependency_analyser->analysis_items[i];
        Analysis_Progress* progress = 0;
        switch (item->type)
        {
        case Analysis_Item_Type::ROOT:
        {
            break;
        }
        case Analysis_Item_Type::BAKE:
        {
            progress = to_base(analysis_progress_create_bake());
            auto analysis_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::BAKE_ANALYSIS, item, progress, true);
            auto execute_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::BAKE_EXECUTION, item, progress, false);
            analysis_workload_add_dependency_internal(execute_workload, analysis_workload, 0);
            break;
        }
        case Analysis_Item_Type::DEFINITION:
        {
            progress = to_base(analysis_progress_create_definition(item->symbol));
            workload_executer_add_workload_empty(Analysis_Workload_Type::DEFINITION, item, progress, true);
            break;
        }
        case Analysis_Item_Type::FUNCTION:
        {
            assert(item->options.function_body_item->node->type == AST::Base_Type::CODE_BLOCK, "");
            ModTree_Function* function = modtree_function_create_empty(
                0, item->symbol, (AST::Code_Block*) item->options.function_body_item->node
            );
            progress = to_base(analysis_progress_create_function(function));
            auto header_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::FUNCTION_HEADER, item, progress, true);
            auto body_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::FUNCTION_BODY, item->options.function_body_item, progress, true);
            bool body_item_registered = hashtable_insert_element(&workload_executer.progress_items, item->options.function_body_item, progress);
            assert(body_item_registered, "This may need to change with templates");

            analysis_workload_add_dependency_internal(body_workload, header_workload, 0);
            auto compile_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, item->options.function_body_item, progress, false);
            compile_workload->options.cluster_compile.functions = dynamic_array_create_empty<ModTree_Function*>(1);
            dynamic_array_push_back(&compile_workload->options.cluster_compile.functions, function);
            analysis_workload_add_dependency_internal(compile_workload, body_workload, 0);

            if (item->symbol != 0) {
                Symbol* symbol = item->symbol;
                symbol->type = Symbol_Type::FUNCTION;
                symbol->options.function = function;
            }
            break;
        }
        case Analysis_Item_Type::FUNCTION_BODY:
        {
            // Should already be done inside function
            break;
        }
        case Analysis_Item_Type::STRUCTURE:
        {
            assert(item->node->type == AST::Base_Type::EXPRESSION, "");
            Type_Signature* struct_type = type_system_make_struct_empty(
                &semantic_analyser.compiler->type_system, item->symbol,
                ast_structure_type_to_type(((AST::Expression*)item->node)->options.structure.type)
            );

            progress = to_base(analysis_progress_create_struct(struct_type));
            auto workload = workload_executer_add_workload_empty(Analysis_Workload_Type::STRUCT_ANALYSIS, item, progress, true);
            auto reachable_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE, item, progress, false);
            reachable_workload->options.struct_reachable.struct_types = dynamic_array_create_empty<Type_Signature*>(1);
            reachable_workload->options.struct_reachable.unfinished_array_types = dynamic_array_create_empty<Type_Signature*>(1);
            dynamic_array_push_back(&reachable_workload->options.struct_reachable.struct_types, struct_type);
            analysis_workload_add_dependency_internal(reachable_workload, workload, 0);

            if (item->symbol != 0) {
                Symbol* symbol = item->symbol;
                symbol->type = Symbol_Type::TYPE;
                symbol->options.type = struct_type;
            }
            break;
        }
        default: panic("");
        }

        if (progress != 0) {
            assert(item != 0, "Hey");
            bool worked = hashtable_insert_element(&workload_executer.progress_items, item, progress);
            assert(worked, "This may need to change with templates");
        }
    }

    for (int i = 0; i < dependency_analyser->item_dependencies.size; i++)
    {
        auto& item_dependency = dependency_analyser->item_dependencies[i];
        auto prog_dependent = *hashtable_find_element(&executer.progress_items, item_dependency.dependent);
        auto prog_dependency = *hashtable_find_element(&executer.progress_items, item_dependency.depends_on);
        auto type = item_dependency.dependent->type;
        auto dep_type = item_dependency.depends_on->type;

        if (type == Analysis_Item_Type::STRUCTURE && dep_type == Analysis_Item_Type::STRUCTURE) {
            analysis_workload_add_struct_dependency((Struct_Progress*)prog_dependent, (Struct_Progress*)prog_dependency, item_dependency.type, 0);
        }
        Analysis_Workload* workload = 0;
        switch (prog_dependent->type)
        {
        case Analysis_Progress_Type::BAKE: {
            auto bake = (Bake_Progress*)prog_dependent;
            workload = bake->analysis_workload;
            break;
        }
        case Analysis_Progress_Type::DEFINITION: {
            auto def = (Definition_Progress*)prog_dependent;
            workload = def->definition_workload;
            break;
        }
        case Analysis_Progress_Type::STRUCTURE: {
            auto stru = (Struct_Progress*)prog_dependent;
            workload = stru->analysis_workload;
            break;
        }
        case Analysis_Progress_Type::FUNCTION: {
            auto fun = (Function_Progress*)prog_dependent;
            if (item_dependency.dependent->type == Analysis_Item_Type::FUNCTION) {
                workload = fun->header_workload;
            }
            else {
                workload = fun->body_workload;
            }
            break;
        }
        default:panic("");
        }

        Analysis_Workload* dep_workload = 0;
        switch (prog_dependency->type)
        {
        case Analysis_Progress_Type::BAKE: {
            auto bake = (Bake_Progress*)prog_dependency;
            dep_workload = bake->execute_workload;
            break;
        }
        case Analysis_Progress_Type::DEFINITION: {
            auto def = (Definition_Progress*)prog_dependency;
            dep_workload = def->definition_workload;
            break;
        }
        case Analysis_Progress_Type::STRUCTURE: {
            auto stru = (Struct_Progress*)prog_dependency;
            dep_workload = stru->reachable_resolve_workload;
            break;
        }
        case Analysis_Progress_Type::FUNCTION: {
            auto fun = (Function_Progress*)prog_dependency;
            if (item_dependency.depends_on->type == Analysis_Item_Type::FUNCTION) {
                dep_workload = fun->header_workload;
            }
            else {
                dep_workload = fun->body_workload;
            }
            break;
        }
        default:panic("");
        }
        analysis_workload_add_dependency_internal(workload, dep_workload, 0);
    }
}

void analysis_workload_check_if_runnable(Analysis_Workload* workload)
{
    auto graph = &workload_executer;
    if (!workload->is_finished && workload->symbol_dependencies.size == 0 && workload->dependencies.count == 0) {
        dynamic_array_push_back(&graph->runnable_workloads, workload);
        graph->progress_was_made = true;
    }
}

void semantic_analyser_work_through_defers(ModTree_Block* modtree_block, int defer_start_index)
{
    assert(defer_start_index != -1, "");
    for (int i = semantic_analyser.defer_stack.size - 1; i >= defer_start_index; i--)
    {
        AST::Code_Block* code_block = semantic_analyser.defer_stack[i];
        ModTree_Statement* statement = modtree_block_add_statement_empty(modtree_block, ModTree_Statement_Type::BLOCK);
        statement->options.block = modtree_block_create_empty(code_block);
        semantic_analyser_fill_block(statement->options.block, code_block);
    }
}

bool analysis_workload_execute(Analysis_Workload* workload)
{
    auto& analyser = semantic_analyser;
    auto& type_system = analyser.compiler->type_system;
    analyser.current_workload = workload;
    analyser.current_function = 0;
    switch (workload->type)
    {
    case Analysis_Workload_Type::DEFINITION:
    {
        assert(workload->analysis_item->type == Analysis_Item_Type::DEFINITION, "");
        assert(workload->analysis_item->node->type == AST::Base_Type::DEFINITION, "");
        auto definition = (AST::Definition*)workload->analysis_item->node;
        if (!definition->type.available && !definition->value.available) {
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, to_base(definition));
            return true;
        }

        Symbol* symbol = definition->symbol;
        if (!definition->is_comptime) // Global variable definition
        {
            Type_Signature* type = 0;
            if (definition->type.available) {
                type = semantic_analyser_analyse_expression_type(definition->type.value);
            }
            ModTree_Expression* value = 0;
            if (definition->value.available)
            {
                value = semantic_analyser_analyse_expression_value(
                    definition->value.value, type != 0 ? expression_context_make_specific_type(type) : expression_context_make_unknown()
                );
                if (type == 0) {
                    type = value->result_type;
                }
            }
            ModTree_Variable* variable = modtree_program_add_global(type, definition->symbol);
            symbol->type = Symbol_Type::VARIABLE;
            symbol->options.variable = variable;
            if (value != 0) {
                ModTree_Statement* statement = modtree_block_add_statement_empty(analyser.global_init_function->body, ModTree_Statement_Type::ASSIGNMENT);
                statement->options.assignment.destination = modtree_expression_create_variable_read(variable);
                statement->options.assignment.source = value;
            }
            ir_generator_queue_global(analyser.compiler->ir_generator, variable);
        }
        else // Constant definition
        {
            if (definition->type.available) {
                Type_Signature* type = semantic_analyser_analyse_expression_type(definition->type.value);
                semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED, definition->type.value);
            }
            if (!definition->value.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, to_base(definition));
                return true;
            }
            Expression_Result result = semantic_analyser_analyse_expression_any(definition->value.value, expression_context_make_unknown());
            switch (result.type)
            {
            case Expression_Result_Type::EXPRESSION:
            {
                Comptime_Result comptime = modtree_expression_calculate_comptime_value(result.options.expression);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    break;
                case Comptime_Result_Type::UNAVAILABLE: {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    return true;
                }
                case Comptime_Result_Type::NOT_COMPTIME: {
                    semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN, definition->value.value);
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    return true;
                }
                default: panic("");
                }

                Constant_Result result = constant_pool_add_constant(
                    &semantic_analyser.compiler->constant_pool, comptime.data_type, array_create_static((byte*)comptime.data, comptime.data_type->size)
                );
                if (result.status != Constant_Status::SUCCESS) {
                    semantic_analyser_log_error(Semantic_Error_Type::CONSTANT_POOL_ERROR, definition->value.value);
                    semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
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
        assert(workload->analysis_item->type == Analysis_Item_Type::FUNCTION, "");
        auto& base_node = workload->analysis_item->node;
        assert(base_node->type == AST::Base_Type::EXPRESSION && ((AST::Expression*)base_node)->type == AST::Expression_Type::FUNCTION, "");
        auto& header = ((AST::Expression*)base_node)->options.function;
        Function_Progress* progress = (Function_Progress*)workload->progress;
        ModTree_Function* function = progress->function;
        analyser.current_function = function;

        // Analyser Header
        auto& signature_node = header.signature->options.function_signature;
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
        for (int i = 0; i < signature_node.parameters.size; i++)
        {
            auto param = signature_node.parameters[i];
            Symbol* symbol = param->symbol;
            ModTree_Parameter modtree_param;
            modtree_param.data_type = semantic_analyser_analyse_expression_type(param->type);
            modtree_param.is_comptime = param->is_comptime;
            modtree_param.name = param->name;
            if (param->is_comptime) {
                symbol->type = Symbol_Type::POLYMORPHIC_PARAMETER;
                symbol->options.polymorphic.parameter_index = i;
                symbol->options.polymorphic.function = function;
                polymorphic_count += 1;
            }
            else {
                modtree_param.options.variable = modtree_variable_create(modtree_param.data_type, symbol);
                symbol->type = Symbol_Type::VARIABLE;
                symbol->options.variable = modtree_param.options.variable;
            }
            dynamic_array_push_back(&function->parameters, modtree_param);
        }

        Type_Signature* return_type = analyser.compiler->type_system.void_type;
        if (signature_node.return_value.available) {
            return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
        }
        if (polymorphic_count > 0) {
            function->type = ModTree_Function_Type::POLYMORPHIC_BASE;
            function->options.base.poly_argument_count = polymorphic_count;
            function->is_runnable = false;
        }

        // Create function signature
        Dynamic_Array<Type_Signature*> param_types = dynamic_array_create_empty<Type_Signature*>(math_maximum(0, signature_node.parameters.size));
        for (int i = 0; i < function->parameters.size; i++) {
            if (!function->parameters[i].is_comptime) {
                dynamic_array_push_back(&param_types, function->parameters[i].data_type);
            }
        }
        function->signature = type_system_make_function(&type_system, param_types, return_type);

        // Advance Progress
        progress->state = Function_State::HEADER_ANALYSED;
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY:
    {
        assert(workload->analysis_item->type == Analysis_Item_Type::FUNCTION_BODY, "");
        auto& base_node = workload->analysis_item->node;
        assert(base_node->type == AST::Base_Type::CODE_BLOCK, "");
        auto code_block = ((AST::Code_Block*)base_node);
        Function_Progress* progress = (Function_Progress*)workload->progress;
        ModTree_Function* function = progress->function;
        analyser.current_function = function;

        ModTree_Block* block = function->body;
        if (function->type == ModTree_Function_Type::POLYMOPRHIC_INSTANCE) {
            ModTree_Function* base = function->options.instance.instance_base_function;
            if (base->contains_errors) {
                function->contains_errors = true;
                break;
            }
        }

        dynamic_array_reset(&analyser.block_stack);
        analyser.statement_reachable = true;
        semantic_analyser_fill_block(block, code_block);
        Control_Flow flow = block->flow;
        if (flow != Control_Flow::RETURNS) {
            if (function->signature->options.function.return_type == type_system.void_type) {
                semantic_analyser_work_through_defers(function->body, 0);
                ModTree_Statement* return_statement = modtree_block_add_statement_empty(function->body, ModTree_Statement_Type::RETURN);
                return_statement->options.return_value.available = false;
            }
            else {
                semantic_analyser_log_error(Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, base_node);
            }
        }
        progress->state = Function_State::BODY_ANALYSED;
        break;
    }
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
    {
        Function_Progress* progress = (Function_Progress*)workload->progress;

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
                ir_generator_queue_function(analyser.compiler->ir_generator, function);
            }
            Function_Progress* progress = *hashtable_find_element(&workload_executer.progress_functions, function);
            progress->state = Function_State::FINISHED;
        }
        break;
    }
    case Analysis_Workload_Type::STRUCT_ANALYSIS:
    {
        assert(workload->analysis_item->type == Analysis_Item_Type::STRUCTURE, "");
        auto& base_node = workload->analysis_item->node;
        assert(base_node->type == AST::Base_Type::EXPRESSION && ((AST::Expression*)base_node)->type == AST::Expression_Type::STRUCTURE_TYPE, "");
        auto& struct_node = ((AST::Expression*)base_node)->options.structure;
        Struct_Progress* progress = (Struct_Progress*)workload->progress;
        Type_Signature* struct_signature = progress->struct_type;

        for (int i = 0; i < struct_node.members.size; i++)
        {
            auto member_node = struct_node.members[i];
            if (member_node->value.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, to_base(member_node->value.value));
            }
            if (!member_node->type.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, to_base(member_node));
                continue;
            }

            Struct_Member member;
            member.id = member_node->name;
            member.offset = 0;
            member.type = semantic_analyser_analyse_expression_type(member_node->type.value);
            assert(!(member.type->size == 0 && member.type->alignment == 0), "Must not happen with Dependency_Type system");
            dynamic_array_push_back(&struct_signature->options.structure.members, member);
        }
        progress->state = Struct_State::SIZE_KNOWN;
        type_system_finish_type(&type_system, struct_signature);
        break;
    }
    case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
    {
        assert(workload->analysis_item->type == Analysis_Item_Type::STRUCTURE, "");
        auto& reachable = workload->options.struct_reachable;
        for (int i = 0; i < reachable.unfinished_array_types.size; i++)
        {
            Type_Signature* array_type = reachable.unfinished_array_types[i];
            assert(array_type->type == Signature_Type::ARRAY, "");
            assert(!(array_type->options.array.element_type->size == 0 && array_type->options.array.element_type->alignment == 0), "");
            array_type->size = array_type->options.array.element_type->size * array_type->options.array.element_count;
            array_type->alignment = array_type->options.array.element_type->alignment;
            type_system_finish_type(&type_system, array_type);
        }
        for (int i = 0; i < reachable.struct_types.size; i++)
        {
            Type_Signature* struct_type = reachable.struct_types[i];
            Struct_Progress* progress = *hashtable_find_element(&workload_executer.progress_structs, struct_type);
            progress->state = Struct_State::FINISHED;
        }
        break;
    }
    case Analysis_Workload_Type::BAKE_ANALYSIS:
    {
        assert(workload->analysis_item->type == Analysis_Item_Type::BAKE, "");
        auto& base_node = workload->analysis_item->node;
        assert(base_node->type == AST::Base_Type::EXPRESSION, "");
        auto expr = ((AST::Expression*) base_node);
        auto code_block = ((AST::Code_Block*)base_node);
        Bake_Progress* progress = (Bake_Progress*)workload->progress;
        if (expr->type == AST::Expression_Type::BAKE_BLOCK) {
            auto& code_block = expr->options.bake_block;
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, expr);
            /*
            semantic_analyser_fill_block(semantic_analyser.current_function->body, rc_bake->body);
            Control_Flow flow = bake_function->body->flow;
            if (flow != Control_Flow::RETURNS) {
                semantic_analyser_log_error(Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, rc_to_ast(rc_bake->type_expression)->parent);
            }
            */
        }
        else if (expr->type == AST::Expression_Type::BAKE_EXPR)
        {
            auto& bake_expr = expr->options.bake_expr;
            analyser.current_function = progress->bake_function;
            auto result_expr = semantic_analyser_analyse_expression_value(expr->options.bake_expr, expression_context_make_unknown());

            // Set Function type
            progress->bake_function->signature = type_system_make_function(&type_system, dynamic_array_create_empty<Type_Signature*>(1), result_expr->result_type);
            // Fill function
            auto return_stat = modtree_block_add_statement_empty(progress->bake_function->body, ModTree_Statement_Type::RETURN);
            return_stat->options.return_value = optional_make_success(result_expr);
        }
        else {
            panic("");
        }
        break;
    }
    case Analysis_Workload_Type::BAKE_EXECUTION:
    {
        assert(workload->analysis_item->type == Analysis_Item_Type::BAKE, "");
        auto& base_node = workload->analysis_item->node;
        assert(workload->progress->type == Analysis_Progress_Type::BAKE, "");
        Bake_Progress* progress = (Bake_Progress*)workload->progress;
        auto compiler = analyser.compiler;
        ModTree_Function* bake_function = progress->bake_function;
        Comptime_Result* result = &progress->result;

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
        ir_generator_queue_function(compiler->ir_generator, bake_function);
        ir_generator_queue_global(compiler->ir_generator, analyser.global_type_informations);
        ir_generator_generate_queued_items(compiler->ir_generator);

        // Set Global Type Informations
        {
            bytecode_interpreter_prepare_run(compiler->bytecode_interpreter);
            IR_Data_Access* global_access = hashtable_find_element(&compiler->ir_generator->variable_mapping, analyser.global_type_informations);
            assert(global_access != 0 && global_access->type == IR_Data_Access_Type::GLOBAL_DATA, "");
            Upp_Slice<Internal_Type_Information>* info_slice = (Upp_Slice<Internal_Type_Information>*)
                & compiler->bytecode_interpreter->globals.data[
                    compiler->bytecode_generator->global_data_offsets[global_access->index]
                ];
            info_slice->size = compiler->type_system.internal_type_infos.size;
            info_slice->data_ptr = compiler->type_system.internal_type_infos.data;
        }

        // Execute
        IR_Function* ir_func = *hashtable_find_element(&compiler->ir_generator->function_mapping, bake_function);
        int func_start_instr_index = *hashtable_find_element(&compiler->bytecode_generator->function_locations, ir_func);
        compiler->bytecode_interpreter->instruction_limit_enabled = true;
        compiler->bytecode_interpreter->instruction_limit = 5000;
        bytecode_interpreter_run_function(compiler->bytecode_interpreter, func_start_instr_index);
        if (compiler->bytecode_interpreter->exit_code != Exit_Code::SUCCESS) {
            semantic_analyser_log_error(Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED, base_node);
            semantic_analyser_add_error_info(error_information_make_exit_code(compiler->bytecode_interpreter->exit_code));
            *result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
            return true;
        }

        void* value_ptr = compiler->bytecode_interpreter->return_register;
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
        AST::Definition* definition = (AST::Definition*)workload->analysis_item->node;
        if (definition->is_comptime) {
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
        Symbol* symbol = workload->analysis_item->symbol;
        auto function = ((Function_Progress*)workload->progress)->function;
        const char* fn_id = symbol == 0 ? "Lambda" : symbol->id->characters;
        const char* appendix = "";
        if (function->type == ModTree_Function_Type::POLYMORPHIC_BASE) {
            appendix = "(Polymorphic Base)";
        }
        else if (function->type == ModTree_Function_Type::POLYMOPRHIC_INSTANCE) {
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
        Symbol* symbol = workload->analysis_item->symbol;
        const char* fn_id = symbol == 0 ? "Anonymous" : symbol->id->characters;
        string_append_formated(string, "Header \"%s\"", fn_id);
        break;
    }
    case Analysis_Workload_Type::STRUCT_ANALYSIS: {
        Symbol* symbol = workload->analysis_item->symbol;
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
    Type_Signature* source_type, Type_Signature* destination_type, bool implicit_cast)
{
    auto& analyser = semantic_analyser;
    auto& type_system = analyser.compiler->type_system;
    if (source_type == type_system.unknown_type || destination_type == type_system.unknown_type) {
        semantic_analyser_set_error_flag(true);
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
                cast_valid = source_type == type_system.void_ptr_type ||
                    destination_type == type_system.void_ptr_type;
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
    else if (destination_type == type_system.any_type)
    {
        cast_valid = true;
        cast_type = ModTree_Cast_Type::TO_ANY;
    }
    else if (source_type == type_system.any_type) {
        cast_valid = !implicit_cast;
        cast_type = ModTree_Cast_Type::FROM_ANY;
    }

    if (cast_valid) return optional_make_success(cast_type);
    return optional_make_failure<ModTree_Cast_Type>();
}

// If not possible, null is returned, otherwise an expression pointer is returned and the expression argument ownership is given to this pointer
ModTree_Expression* semantic_analyser_cast_implicit_if_possible(ModTree_Expression* expression, Type_Signature* destination_type)
{
    auto& analyser = semantic_analyser;
    auto& type_system = analyser.compiler->type_system;
    Type_Signature* source_type = expression->result_type;
    if (source_type == type_system.unknown_type || destination_type == type_system.unknown_type) {
        return expression;
    }
    if (source_type == destination_type) {
        return expression;
    }
    Optional<ModTree_Cast_Type> cast_type = semantic_analyser_check_if_cast_possible(expression->result_type, destination_type, true);
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

void analysis_workload_register_function_call(ModTree_Function* call_to)
{
    auto& analyser = semantic_analyser;
    auto& type_system = analyser.compiler->type_system;

    if (analyser.current_function != 0) {
        dynamic_array_push_back(&analyser.current_function->calls, call_to);
        dynamic_array_push_back(&call_to->called_from, analyser.current_function);
    }

    Function_Progress** progress_opt = hashtable_find_element(&workload_executer.progress_functions, call_to);
    if (progress_opt == 0) return;
    auto progress = *progress_opt;

    switch (analyser.current_workload->type)
    {
    case Analysis_Workload_Type::BAKE_ANALYSIS: {
        Analysis_Workload* execute_workload = ((Bake_Progress*)analyser.current_workload->progress)->execute_workload;
        analysis_workload_add_dependency_internal(execute_workload, progress->compile_workload, 0);
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY: {
        Function_Progress* my_progress = (Function_Progress*)analyser.current_workload->progress;
        analysis_workload_add_cluster_dependency(my_progress->compile_workload, progress->compile_workload, 0);
        break;
    }
    default: return;
    }
}

Expression_Result semantic_analyser_analyse_expression_internal(AST::Expression* expression_node, Expression_Context context)
{
    auto& analyser = semantic_analyser;
    Type_System* type_system = &analyser.compiler->type_system;
    bool error_exit = false;
    switch (expression_node->type)
    {
    case AST::Expression_Type::ERROR_EXPR: {
        return expression_result_make_error(type_system->unknown_type);
    }
    case AST::Expression_Type::FUNCTION_CALL:
    {
        // Analyse call expression
        auto& call = expression_node->options.call;
        Expression_Result function_expr_result = semantic_analyser_analyse_expression_any(call.expr, expression_context_make_auto_dereference());
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
            auto& arguments = call.arguments;
            auto call = &expr_result->options.function_call;
            call->call_type = ModTree_Call_Type::FUNCTION;
            call->arguments = dynamic_array_create_empty<ModTree_Expression*>(1);
            call->options.function = function_expr_result.options.function;

            ModTree_Function* function = function_expr_result.options.function;
            int compare_error_flag_count = analyser.error_flag_count;
            if (arguments.size != function->parameters.size) {
                semantic_analyser_log_error(Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, expression_node);
                semantic_analyser_add_error_info(error_information_make_argument_count(arguments.size, function->parameters.size));
                semantic_analyser_add_error_info(error_information_make_function_type(function->signature));
            }
            // Parse arguments
            for (int i = 0; i < arguments.size && i < function->parameters.size; i++) {
                ModTree_Expression* argument_expr = semantic_analyser_analyse_expression_value(
                    arguments[i]->value, expression_context_make_specific_type(function->parameters[i].data_type)
                );
                dynamic_array_push_back(&call->arguments, argument_expr);
            }
            // Analyse overflowing arguments 
            for (int i = function->parameters.size; i < arguments.size; i++) {
                dynamic_array_push_back(&call->arguments,
                    semantic_analyser_analyse_expression_value(arguments[i]->value, expression_context_make_unknown())
                );
            }

            bool instanciate_function = analyser.error_flag_count == compare_error_flag_count;
            if (function->type == ModTree_Function_Type::POLYMORPHIC_BASE && instanciate_function)
            {
                Dynamic_Array<Upp_Constant> comptime_parameters = dynamic_array_create_empty<Upp_Constant>(function->options.base.poly_argument_count);
                // Run over all parameters and evaluate their constant value
                for (int i = 0; i < function->parameters.size; i++)
                {
                    if (!function->parameters[i].is_comptime) continue;
                    Comptime_Result comptime = modtree_expression_calculate_comptime_value(call->arguments[i]);
                    switch (comptime.type)
                    {
                    case Comptime_Result_Type::AVAILABLE:
                    {
                        Constant_Result result = constant_pool_add_constant(
                            &analyser.compiler->constant_pool, call->arguments[i]->result_type,
                            array_create_static((byte*)comptime.data, comptime.data_type->size)
                        );
                        if (result.status != Constant_Status::SUCCESS) {
                            semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_ARGUMENT_NOT_KNOWN_AT_COMPTIME, arguments[i]->value);
                            semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
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
                        semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_ARGUMENT_NOT_KNOWN_AT_COMPTIME, arguments[i]->value);
                        instanciate_function = false;
                        break;
                    }
                    default: panic("");
                    }
                }

                // Instanciate function
                if (instanciate_function) {
                    call->options.function = modtree_function_create_poly_instance(function, comptime_parameters);
                    analysis_workload_register_function_call(call->options.function);
                }
            }
            expr_result->result_type = call->options.function->signature->options.function.return_type;
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
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL, expression_node);
                semantic_analyser_add_error_info(error_information_make_given_type(function_signature));
                error_exit = true;
                return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
            }
            break;
        }
        case Expression_Result_Type::MODULE:
        case Expression_Result_Type::TYPE: {
            semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_CALLABLE, expression_node);
            semantic_analyser_add_error_info(error_information_make_expression_result_type(function_expr_result.type));
            error_exit = true;
            return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
        }
        default: panic("");
        }
        expr_result->result_type = function_signature->options.function.return_type;

        // Analyse arguments
        auto& arguments = call.arguments;
        Dynamic_Array<Type_Signature*> parameters = function_signature->options.function.parameter_types;
        if (arguments.size != parameters.size) {
            semantic_analyser_log_error(Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, expression_node);
            semantic_analyser_add_error_info(error_information_make_argument_count(arguments.size, parameters.size));
            semantic_analyser_add_error_info(error_information_make_function_type(function_signature));
        }

        expr_result->options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(arguments.size);
        for (int i = 0; i < arguments.size; i++)
        {
            Type_Signature* expected_type = i < parameters.size ? parameters[i] : type_system->unknown_type;
            ModTree_Expression* argument_expr = semantic_analyser_analyse_expression_value(
                arguments[i]->value, expression_context_make_specific_type(expected_type)
            );
            dynamic_array_push_back(&expr_result->options.function_call.arguments, argument_expr);
        }
        return expression_result_make_value(expr_result);
    }
    /*
    case AST::Expression_Type::TYPE_INFO:
    {
        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
            expression_node->options.type_info_expression, expression_context_make_specific_type(type_system->type_type)
        );

        ModTree_Expression* global_read = modtree_expression_create_empty(ModTree_Expression_Type::VARIABLE_READ, analyser.global_type_informations->data_type);
        global_read->options.variable_read = analyser.global_type_informations;

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
        array_access->result_type = analyser.compiler->type_system.type_information_type;

        return expression_result_make_value(array_access);
    }
    case AST::Expression_Type::TYPE_OF:
    {
        Expression_Result any_result = semantic_analyser_analyse_expression_any(
            expression_node->options.type_of_expression, expression_context_make_unknown()
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
            return expression_result_make_type(type_system->type_type);
        }
        case Expression_Result_Type::MODULE: {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, expression_node->options.type_of_expression);
            semantic_analyser_add_error_info(error_information_make_expression_result_type(any_result.type));
            return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
        }
        default: panic("");
        }

        panic("");
        return expression_result_make_value(modtree_expression_make_error(type_system->unknown_type));
    }
    */
    case AST::Expression_Type::SYMBOL_READ:
    {
        auto read = expression_node->options.symbol_read;
        while (read->path_child.available) {
            read = read->path_child.value;
        }
        Symbol* symbol = read->resolved_symbol;
        assert(symbol != 0, "Must be given by dependency analysis");
        while (symbol->type == Symbol_Type::SYMBOL_ALIAS) {
            symbol = symbol->options.alias;
        }
        switch (symbol->type)
        {
        case Symbol_Type::ERROR_SYMBOL: {
            semantic_analyser_set_error_flag(true);
            //semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_EXPRESSION_TYPE, rc_expression);
            //semantic_analyser_add_error_info(analyser, error_information_make_symbol(symbol));
            return expression_result_make_error(type_system->unknown_type);
        }
        case Symbol_Type::UNRESOLVED: {
            panic("Should not happen");
        }
        case Symbol_Type::VARIABLE_UNDEFINED: {
            semantic_analyser_log_error(Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET, expression_node);
            return expression_result_make_error(type_system->unknown_type);
        }
        case Symbol_Type::POLYMORPHIC_PARAMETER: {
            assert(analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_BODY, "");
            auto function = ((Function_Progress*)analyser.current_workload->progress)->function;
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
            analysis_workload_register_function_call(symbol->options.function);
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
            semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_MODULE_INVALID, expression_node);
            semantic_analyser_add_error_info(error_information_make_symbol(symbol));
            return expression_result_make_error(type_system->unknown_type);
        }
        default: panic("HEY");
        }

        panic("HEY");
        break;
    }
    case AST::Expression_Type::CAST:
    {
        auto cast = &expression_node->options.cast;
        Type_Signature* destination_type = 0;
        if (cast->to_type.available) {
            destination_type = semantic_analyser_analyse_expression_type(cast->to_type.value);
        }

        // Determine Context and Destination Type
        Expression_Context operand_context = expression_context_make_unknown();
        switch (cast->type)
        {
        case AST::Cast_Type::TYPE_TO_TYPE:
        {
            if (destination_type == 0)
            {
                if (context.type != Expression_Context_Type::SPECIFIC_TYPE) {
                    semantic_analyser_log_error(Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED, expression_node);
                    return expression_result_make_error(type_system->unknown_type);
                }
                destination_type = context.signature;
            }
            break;
        }
        case AST::Cast_Type::RAW_TO_PTR:
        {
            if (destination_type == 0) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expression_node);
                destination_type = type_system->unknown_type;
            }
            else {
                operand_context = expression_context_make_specific_type(type_system->u64_type);
                if (destination_type->type != Signature_Type::POINTER) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR, expression_node);
                    semantic_analyser_add_error_info(error_information_make_given_type(destination_type));
                }
            }
            break;
        }
        case AST::Cast_Type::PTR_TO_RAW:
        {
            if (destination_type != 0) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expression_node);
            }
            destination_type = type_system->u64_type;
            break;
        }
        default: panic("");
        }

        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(cast->operand, operand_context);
        ModTree_Expression* result_expr = modtree_expression_create_empty(ModTree_Expression_Type::CAST, destination_type);
        result_expr->options.cast.cast_argument = operand;
        result_expr->options.cast.type = ModTree_Cast_Type::INTEGERS; // Placeholder

        // Determine Cast type
        switch (cast->type)
        {
        case AST::Cast_Type::TYPE_TO_TYPE:
        {
            assert(destination_type != 0, "");
            Optional<ModTree_Cast_Type> cast_valid = semantic_analyser_check_if_cast_possible(
                operand->result_type, destination_type, false
            );
            if (cast_valid.available) {
                result_expr->options.cast.type = cast_valid.value;
            }
            else {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expression_node);
                semantic_analyser_add_error_info(error_information_make_given_type(operand->result_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(destination_type));
            }
            break;
        }
        case AST::Cast_Type::PTR_TO_RAW: {
            if (operand->result_type->type != Signature_Type::POINTER) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expression_node);
                semantic_analyser_add_error_info(error_information_make_given_type(operand->result_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(destination_type));
            }
            result_expr->options.cast.type = ModTree_Cast_Type::POINTER_TO_U64;
            break;
        }
        case AST::Cast_Type::RAW_TO_PTR:
        {
            result_expr->options.cast.type = ModTree_Cast_Type::U64_TO_POINTER;
            break;
        }
        default: panic("");
        }
        return expression_result_make_value(result_expr);
    }
    case AST::Expression_Type::LITERAL_READ:
    {
        auto& read = expression_node->options.literal_read;
        void* value_ptr;
        Type_Signature* literal_type;
        void* null_pointer = 0;
        Upp_String string_buffer;

        // Missing: float, nummptr
        switch (read.type)
        {
        case AST::Literal_Type::BOOLEAN:
            literal_type = type_system->bool_type;
            value_ptr = &read.options.boolean;
            break;
        case AST::Literal_Type::FLOAT_VAL:
            literal_type = type_system->f32_type;
            value_ptr = &read.options.float_val;
            break;
        case AST::Literal_Type::INTEGER:
            literal_type = type_system->i32_type;
            value_ptr = &read.options.int_val;
            break;
            /*
            cast NULLPTR:

                literal_type = type_system->void_ptr_type;
                value_ptr = &null_pointer;
                break
            */
        case AST::Literal_Type::STRING: {
            String* string = read.options.string;
            string_buffer.character_buffer_data = string->characters;
            string_buffer.character_buffer_size = string->capacity;
            string_buffer.size = string->size;

            literal_type = type_system->string_type;
            value_ptr = &string_buffer;
            break;
        }
        default: panic("");
        }
        return expression_result_make_value(modtree_expression_create_constant(
            literal_type, array_create_static<byte>((byte*)value_ptr, literal_type->size), rc_to_ast(expression_node)
        ));
    }
    case AST::Expression_Type::ENUM_TYPE:
    {
        auto& members = expression_node->options.enum_members;
        Type_Signature* enum_type = type_system_make_enum_empty(type_system, 0);
        int next_member_value = 1;
        for (int i = 0; i < members.size; i++)
        {
            auto& member_node = members[i];
            /*
            if (member_node->value.available)
            {
                ModTree_Expression* expression = semantic_analyser_analyse_expression_value(
                    member_node->value.value, expression_context_make_specific_type(type_system->i32_type)
                );
                Comptime_Result comptime = modtree_expression_calculate_comptime_value(expression);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    next_member_value = *(i32*)comptime.data;
                    break;
                case Comptime_Result_Type::UNAVAILABLE:
                    break;
                case Comptime_Result_Type::NOT_COMPTIME:
                    semantic_analyser_log_error(Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN, member_node->value.value);
                    break;
                default: panic("");
                }
            }
            */

            Enum_Member member;
            //member.definition_node = &expression_node->base;
            member.id = member_node;
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
                    semantic_analyser_log_error(Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE, to_base(expression_node));
                    semantic_analyser_add_error_info(error_information_make_id(other->id));
                }
                if (other->value == member->value) {
                    semantic_analyser_log_error(Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE, to_base(expression_node));
                    semantic_analyser_add_error_info(error_information_make_id(other->id));
                }
            }
        }
        type_system_finish_type(type_system, enum_type);
        return expression_result_make_type(enum_type);
    }
    case AST::Expression_Type::MODULE: {
        return expression_result_make_module(expression_node->options.module->symbol_table);
    }
    case AST::Expression_Type::FUNCTION:
    case AST::Expression_Type::STRUCTURE_TYPE:
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR:
    {
        Analysis_Progress* progress;
        {
            Analysis_Item** item_opt = hashtable_find_element(&analyser.compiler->dependency_analyser->mapping_ast_to_items, to_base(expression_node));
            assert(item_opt != 0, "");
            Analysis_Progress** progress_opt = hashtable_find_element(&workload_executer.progress_items, *item_opt);
            assert(progress_opt != 0, "");
            progress = *progress_opt;
        }
        switch (progress->type)
        {
        case Analysis_Progress_Type::FUNCTION:
        {
            auto func = (Function_Progress*)progress;
            analysis_workload_register_function_call(func->function);
            return expression_result_make_function(func->function);
        }
        case Analysis_Progress_Type::STRUCTURE:
        {
            auto structure = (Struct_Progress*)progress;
            Type_Signature* struct_type = structure->struct_type;
            assert(!(struct_type->size == 0 && struct_type->alignment == 0), "");
            return expression_result_make_type(struct_type);
        }
        case Analysis_Progress_Type::BAKE:
        {
            auto bake = (Bake_Progress*)progress;
            auto bake_result = &bake->result;
            switch (bake_result->type)
            {
            case Comptime_Result_Type::AVAILABLE: {
                ModTree_Expression* comp_expr = modtree_expression_create_constant(
                    bake_result->data_type, array_create_static((byte*)bake_result->data, bake_result->data_type->size), to_base(expression_node)
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
            break;
        }
        case Analysis_Progress_Type::DEFINITION: {
            panic("Shouldn't happen with an expression node!");
            break;
        }
        default: panic("");
        }
        return expression_result_make_error(type_system->unknown_type);
    }
    case AST::Expression_Type::FUNCTION_SIGNATURE:
    {
        auto& sig = expression_node->options.function_signature;
        Dynamic_Array<Type_Signature*> parameters = dynamic_array_create_empty<Type_Signature*>(math_maximum(0, sig.parameters.size));
        for (int i = 0; i < sig.parameters.size; i++)
        {
            auto& param = sig.parameters[i];
            if (param->is_comptime) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, to_base(param));
            }
            else {
                dynamic_array_push_back(&parameters, semantic_analyser_analyse_expression_type(param->type));
            }
        }
        Type_Signature* return_type;
        if (sig.return_value.available) {
            return_type = semantic_analyser_analyse_expression_type(sig.return_value.value);
        }
        else {
            return_type = type_system->void_type;
        }
        return expression_result_make_type(type_system_make_function(type_system, parameters, return_type));
    }
    case AST::Expression_Type::ARRAY_TYPE:
    {
        auto& array_node = expression_node->options.array_type;
        ModTree_Expression* size_expr = semantic_analyser_analyse_expression_value(
            array_node.size_expr, expression_context_make_specific_type(type_system->i32_type)
        );
        SCOPE_EXIT(modtree_expression_destroy(size_expr));
        Comptime_Result comptime = modtree_expression_calculate_comptime_value(size_expr);
        int array_size = 0;
        bool array_size_known = false;
        switch (comptime.type)
        {
        case Comptime_Result_Type::AVAILABLE: {
            array_size_known = true;
            array_size = *(i32*)comptime.data;
            if (array_size <= 0) {
                semantic_analyser_log_error(Semantic_Error_Type::ARRAY_SIZE_MUST_BE_GREATER_ZERO, array_node.size_expr);
                array_size_known = false;
            }
            break;
        }
        case Comptime_Result_Type::UNAVAILABLE:
            array_size_known = false;
            break;
        case Comptime_Result_Type::NOT_COMPTIME:
            semantic_analyser_log_error(Semantic_Error_Type::ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN, array_node.size_expr);
            array_size_known = false;
            break;
        default: panic("");
        }

        Type_Signature* element_type = semantic_analyser_analyse_expression_type(array_node.type_expr);
        Type_Signature* array_type = type_system_make_array(type_system, element_type, array_size_known, array_size);
        if (element_type->size == 0 && element_type->alignment == 0)
        {
            assert(analyser.current_workload->type == Analysis_Workload_Type::STRUCT_ANALYSIS, "");
            auto progress = (Struct_Progress*)analyser.current_workload->progress;
            assert(progress->state != Struct_State::FINISHED, "Finished structs cannot be of size + alignment 0");
            Analysis_Workload* cluster = analysis_workload_find_associated_cluster(progress->reachable_resolve_workload);
            dynamic_array_push_back(&cluster->options.struct_reachable.unfinished_array_types, array_type);
        }
        return expression_result_make_type(array_type);
    }
    case AST::Expression_Type::SLICE_TYPE:
    {
        return expression_result_make_type(
            type_system_make_slice(
                type_system, semantic_analyser_analyse_expression_type(expression_node->options.slice_type)
            )
        );
    }
    case AST::Expression_Type::NEW_EXPR:
    {
        auto& new_node = expression_node->options.new_expr;
        Type_Signature* allocated_type = semantic_analyser_analyse_expression_type(new_node.type_expr);
        if (allocated_type == type_system->void_type) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, new_node.type_expr);
        }
        assert(!(allocated_type->size == 0 && allocated_type->alignment == 0), "HEY");

        Type_Signature* result_type = 0;
        Optional<ModTree_Expression*> count_expr;
        if (new_node.count_expr.available)
        {
            result_type = type_system_make_slice(type_system, allocated_type);
            ModTree_Expression* count = semantic_analyser_analyse_expression_value(
                new_node.count_expr.value, expression_context_make_specific_type(type_system->i32_type)
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
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        auto& init_node = expression_node->options.struct_initializer;
        Type_Signature* struct_signature = 0;
        if (init_node.type_expr.available) {
            struct_signature = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE) {
                struct_signature = context.signature;
            }
            else {
                semantic_analyser_log_error(Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE, expression_node);
                return expression_result_make_error(type_system->unknown_type);
            }
        }

        if (struct_signature->type != Signature_Type::STRUCT) {
            semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT, expression_node);
            semantic_analyser_add_error_info(error_information_make_given_type(struct_signature));
            return expression_result_make_error(struct_signature);
        }
        if (init_node.arguments.size == 0) {
            semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, expression_node);
            return expression_result_make_error(struct_signature);
        }
        assert(!(struct_signature->size == 0 && struct_signature->alignment == 0), "");

        Dynamic_Array<Member_Initializer> initializers = dynamic_array_create_empty<Member_Initializer>(struct_signature->size);
        for (int i = 0; i < init_node.arguments.size; i++)
        {
            auto& argument = init_node.arguments[i];
            if (!argument->name.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, to_base(argument));
                continue;
            }
            String* member_id = argument->name.value;
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
                    argument->value, expression_context_make_specific_type(found_member->type)
                );
                initializer.init_node = to_base(argument);
                initializer.member = *found_member;
                dynamic_array_push_back(&initializers, initializer);
            }
            else {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST, to_base(argument));
                semantic_analyser_add_error_info(error_information_make_id(member_id));
            }
        }

        // Check for errors
        if (struct_signature->options.structure.struct_type != Structure_Type::STRUCT)
        {
            if (init_node.arguments.size > 1) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER, expression_node);
            }
            else if (struct_signature->options.structure.struct_type == Structure_Type::UNION && initializers.size == 1)
            {
                if (initializers[0].member.offset == struct_signature->options.structure.tag_member.offset) {
                    semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG, initializers[0].init_node);
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
                    tag_init.init_expr = modtree_expression_create_constant_i32(value);
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
                        semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE, other->init_node);
                    }
                }
            }
            // Check if all members are initiliazed
            if (initializers.size != struct_signature->options.structure.members.size) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, expression_node);
            }
        }

        ModTree_Expression* expr = modtree_expression_create_empty(ModTree_Expression_Type::STRUCT_INITIALIZER, struct_signature);
        expr->options.struct_initializer = initializers;
        return expression_result_make_value(expr);
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        auto& init_node = expression_node->options.array_initializer;
        Type_Signature* element_type = 0;
        if (init_node.type_expr.available) {
            element_type = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
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
                semantic_analyser_log_error(Semantic_Error_Type::ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE, expression_node);
                return expression_result_make_error(type_system->unknown_type);
            }
        }

        if (element_type == type_system->void_type) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, expression_node);
            return expression_result_make_error(type_system->unknown_type);
        }
        assert(!(element_type->size == 0 && element_type->alignment == 0), "");

        int array_element_count = init_node.values.size;
        // There are no 0-sized arrays, only 0-sized slices. So if we encounter an empty initializer, e.g. type.[], we return an empty slice
        if (array_element_count == 0)
        {
            Upp_Slice_Base data_slice;
            data_slice.data_ptr = 0;
            data_slice.size = 0;
            return expression_result_make_value(
                modtree_expression_create_constant(
                    type_system_make_slice(type_system, element_type),
                    array_create_static_as_bytes(&data_slice, 1), 0
                )
            );
        }

        Dynamic_Array<ModTree_Expression*> init_expressions = dynamic_array_create_empty<ModTree_Expression*>(array_element_count + 1);
        for (int i = 0; i < init_node.values.size; i++)
        {
            ModTree_Expression* element_expr = semantic_analyser_analyse_expression_value(
                init_node.values[i], expression_context_make_specific_type(element_type)
            );
            dynamic_array_push_back(&init_expressions, element_expr);
        }

        ModTree_Expression* result = modtree_expression_create_empty(
            ModTree_Expression_Type::ARRAY_INITIALIZER, type_system_make_array(type_system, element_type, true, array_element_count)
        );
        result->options.array_initializer = init_expressions;
        return expression_result_make_value(result);
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        auto& access_node = expression_node->options.array_access;
        ModTree_Expression* array_expr = semantic_analyser_analyse_expression_value(
            access_node.array_expr, expression_context_make_auto_dereference()
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
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS, expression_node);
            semantic_analyser_add_error_info(error_information_make_given_type(array_type));
            element_type = type_system->unknown_type;
        }

        ModTree_Expression* index_expr = semantic_analyser_analyse_expression_value(
            access_node.index_expr, expression_context_make_specific_type(type_system->i32_type)
        );
        ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::ARRAY_ACCESS, element_type);
        result->options.array_access.array_expression = array_expr;
        result->options.array_access.index_expression = index_expr;
        return expression_result_make_value(result);
    }
    case AST::Expression_Type::MEMBER_ACCESS:
    {
        auto& member_node = expression_node->options.member_access;
        Expression_Result access_expr_result = semantic_analyser_analyse_expression_any(
            member_node.expr, expression_context_make_auto_dereference()
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
                semantic_analyser_log_error(Semantic_Error_Type::OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM, member_node.expr);
                semantic_analyser_add_error_info(error_information_make_given_type(enum_type));
                return expression_result_make_error(type_system->unknown_type);
            }

            Enum_Member* found = 0;
            for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
                Enum_Member* member = &enum_type->options.enum_type.members[i];
                if (member->id == member_node.name) {
                    found = member;
                    break;
                }
            }

            int value = 0;
            if (found == 0) {
                semantic_analyser_log_error(Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, member_node.expr);
                semantic_analyser_add_error_info(error_information_make_id(member_node.name));
            }
            else {
                value = found->value;
            }
            return expression_result_make_value(modtree_expression_create_constant_enum(enum_type, value));
        }
        case Expression_Result_Type::FUNCTION:
        case Expression_Result_Type::EXTERN_FUNCTION:
        case Expression_Result_Type::HARDCODED_FUNCTION:
        case Expression_Result_Type::MODULE: {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, member_node.expr);
            semantic_analyser_add_error_info(error_information_make_expression_result_type(access_expr_result.type));
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
                    if (member->id == member_node.name) {
                        found = member;
                    }
                }
                if (found == 0) {
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, expression_node);
                    semantic_analyser_add_error_info(error_information_make_id(member_node.name));
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
                if (member_node.name != analyser.id_size && member_node.name != analyser.id_data) {
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, expression_node);
                    semantic_analyser_add_error_info(error_information_make_id(member_node.name));
                    error_exit = true;
                    return expression_result_make_error(type_system->unknown_type);
                }

                if (struct_signature->type == Signature_Type::ARRAY)
                {
                    if (member_node.name == analyser.id_size) {
                        return expression_result_make_value(modtree_expression_create_constant_i32(struct_signature->options.array.element_count));
                    }
                    else
                    {
                        ModTree_Expression* result = modtree_expression_create_empty(
                            ModTree_Expression_Type::UNARY_OPERATION,
                            type_system_make_pointer(type_system, struct_signature->options.array.element_type)
                        );
                        result->options.unary_operation.operand = access_expr;
                        result->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
                        return expression_result_make_value(result);
                    }
                }
                else // Slice
                {
                    Struct_Member member;
                    if (member_node.name == semantic_analyser.id_size) {
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
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS, expression_node);
                semantic_analyser_add_error_info(error_information_make_given_type(struct_signature));
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
    case AST::Expression_Type::AUTO_ENUM:
    {
        String* id = expression_node->options.auto_enum;
        if (context.type != Expression_Context_Type::SPECIFIC_TYPE) {
            semantic_analyser_log_error(Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED, expression_node);
            return expression_result_make_error(type_system->unknown_type);
        }
        if (context.signature->type != Signature_Type::ENUM) {
            semantic_analyser_log_error(Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT, expression_node);
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
            semantic_analyser_log_error(Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, expression_node);
            semantic_analyser_add_error_info(error_information_make_id(id));
        }
        else {
            value = found->value;
        }

        ModTree_Expression* result = modtree_expression_create_constant_enum(enum_type, value);
        return expression_result_make_value(result);
    }
    case AST::Expression_Type::UNARY_OPERATION:
    {
        auto& unary_node = expression_node->options.unop;
        switch (unary_node.type)
        {
        case AST::Unop::NEGATE:
        case AST::Unop::NOT:
        {
            bool is_negate = unary_node.type == AST::Unop::NEGATE;
            ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
                unary_node.expr,
                is_negate ? expression_context_make_auto_dereference() : expression_context_make_specific_type(type_system->bool_type)
            );

            if (is_negate) {
                bool valid = false;
                if (operand->result_type->type == Signature_Type::PRIMITIVE) {
                    valid = operand->result_type->options.primitive.is_signed && operand->result_type->options.primitive.type != Primitive_Type::BOOLEAN;
                }
                if (!valid) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, expression_node);
                    semantic_analyser_add_error_info(error_information_make_given_type(operand->result_type));
                }
            }

            ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, operand->result_type);
            result->options.unary_operation.operation_type = is_negate ? ModTree_Unary_Operation_Type::NEGATE : ModTree_Unary_Operation_Type::LOGICAL_NOT;
            result->options.unary_operation.operand = operand;
            return expression_result_make_value(result);
        }
        case AST::Unop::POINTER:
        {
            Expression_Result operand_result = semantic_analyser_analyse_expression_any(unary_node.expr, expression_context_make_unknown());
            switch (operand_result.type)
            {
            case Expression_Result_Type::EXPRESSION:
            {
                ModTree_Expression* operand = operand_result.options.expression;
                if (operand->result_type == type_system->void_type) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, expression_node);
                    modtree_expression_destroy(operand);
                    return expression_result_make_error(type_system->unknown_type);
                }

                if (modtree_expression_result_is_temporary(operand)) {
                    semantic_analyser_log_error(
                        Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT, expression_node
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
                return expression_result_make_type(type_system_make_pointer(type_system, operand_result.options.type));
            }
            case Expression_Result_Type::FUNCTION:
            case Expression_Result_Type::EXTERN_FUNCTION:
            case Expression_Result_Type::HARDCODED_FUNCTION:
            case Expression_Result_Type::MODULE: {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, expression_node);
                semantic_analyser_add_error_info(error_information_make_expression_result_type(operand_result.type));
                return expression_result_make_error(type_system->unknown_type);
            }
            default: panic("");
            }
            panic("");
            break;
        }
        case AST::Unop::DEREFERENCE:
        {
            ModTree_Expression* operand = semantic_analyser_analyse_expression_value(unary_node.expr, expression_context_make_unknown());
            Type_Signature* result_type = type_system->unknown_type;
            if (operand->result_type->type != Signature_Type::POINTER || operand->result_type == type_system->void_ptr_type) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, expression_node);
                semantic_analyser_add_error_info(error_information_make_given_type(operand->result_type));
            }
            else {
                result_type = operand->result_type->options.pointer_child;
            }

            ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, result_type);
            result->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::DEREFERENCE;
            result->options.unary_operation.operand = operand;
            return expression_result_make_value(result);
        }
        default:panic("");
        }
        panic("");
        return expression_result_make_error(type_system->unknown_type);
    }
    case AST::Expression_Type::BINARY_OPERATION:
    {
        auto& binop_node = expression_node->options.binop;

        // Determine what operands are valid
        bool int_valid = false;
        bool float_valid = false;
        bool bool_valid = false;
        bool ptr_valid = false;
        bool result_type_is_bool = false;
        bool enum_valid = false;
        bool type_type_valid = false;
        Expression_Context operand_context;
        switch (binop_node.type)
        {
        case AST::Binop::ADDITION:
        case AST::Binop::SUBTRACTION:
        case AST::Binop::MULTIPLICATION:
        case AST::Binop::DIVISION:
            float_valid = true;
            int_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case AST::Binop::MODULO:
            int_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case AST::Binop::GREATER:
        case AST::Binop::GREATER_OR_EQUAL:
        case AST::Binop::LESS:
        case AST::Binop::LESS_OR_EQUAL:
            float_valid = true;
            int_valid = true;
            result_type_is_bool = true;
            enum_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case AST::Binop::POINTER_EQUAL:
        case AST::Binop::POINTER_NOT_EQUAL:
            ptr_valid = true;
            operand_context = expression_context_make_unknown();
            result_type_is_bool = true;
            break;
        case AST::Binop::EQUAL:
        case AST::Binop::NOT_EQUAL:
            float_valid = true;
            int_valid = true;
            bool_valid = true;
            ptr_valid = true;
            enum_valid = true;
            type_type_valid = true;
            operand_context = expression_context_make_auto_dereference();
            result_type_is_bool = true;
            break;
        case AST::Binop::AND:
        case AST::Binop::OR:
            bool_valid = true;
            result_type_is_bool = true;
            operand_context = expression_context_make_specific_type(type_system->bool_type);
            break;
        case AST::Binop::INVALID:
            operand_context = expression_context_make_unknown();
            break;
        default: panic("");
        }

        // Evaluate operands
        ModTree_Expression* left_expr = semantic_analyser_analyse_expression_value(binop_node.left, operand_context);
        if (enum_valid && left_expr->result_type->type == Signature_Type::ENUM) {
            operand_context = expression_context_make_specific_type(left_expr->result_type);
        }
        ModTree_Expression* right_expr = semantic_analyser_analyse_expression_value(binop_node.right, operand_context);

        // Try implicit casting if types dont match
        Type_Signature* left_type = left_expr->result_type;
        Type_Signature* right_type = right_expr->result_type;
        Type_Signature* operand_type = left_type;
        bool types_are_valid = true;
        if (left_type != right_type)
        {
            if (semantic_analyser_check_if_cast_possible(left_type, right_type, true).available) {
                left_expr = semantic_analyser_cast_implicit_if_possible(left_expr, right_type);
                operand_type = right_type;
            }
            else if (semantic_analyser_check_if_cast_possible(right_type, left_type, true).available) {
                right_expr = semantic_analyser_cast_implicit_if_possible(right_expr, left_type);
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
        if (binop_node.type == AST::Binop::POINTER_EQUAL) {
            result->options.binary_operation.operation_type = ModTree_Binary_Operation_Type::EQUAL;
        }
        else if (binop_node.type == AST::Binop::POINTER_NOT_EQUAL) {
            result->options.binary_operation.operation_type = ModTree_Binary_Operation_Type::NOT_EQUAL;
        }
        else {
            result->options.binary_operation.operation_type = (ModTree_Binary_Operation_Type)binop_node.type;
        }
        result->options.binary_operation.left_operand = left_expr;
        result->options.binary_operation.right_operand = right_expr;

        if (!types_are_valid) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR, expression_node);
            semantic_analyser_add_error_info(error_information_make_binary_op_type(left_type, right_type));
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

Expression_Result semantic_analyser_analyse_expression_any(AST::Expression* expression_node, Expression_Context context)
{
    auto& type_system = semantic_analyser.compiler->type_system;
    Expression_Result result = semantic_analyser_analyse_expression_internal(expression_node, context);
    if (result.type != Expression_Result_Type::EXPRESSION) return result;

    ModTree_Expression* expr = result.options.expression;
    Type_Signature* inital_type = expr->result_type;
    if (expr->result_type == type_system.unknown_type ||
        (context.type == Expression_Context_Type::SPECIFIC_TYPE && context.signature == type_system.unknown_type)) {
        semantic_analyser_set_error_flag(true);
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
        deref->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::DEREFERENCE;
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
            addr_of->result_type = type_system_make_pointer(&type_system, expr->result_type);
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
        ModTree_Expression* casted = semantic_analyser_cast_implicit_if_possible(expr, context.signature);
        if (casted != 0) {
            expr = casted;
            result.options.expression = expr;
        }
        else {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE, expression_node);
            semantic_analyser_add_error_info(error_information_make_given_type(inital_type));
            semantic_analyser_add_error_info(error_information_make_expected_type(context.signature));
            modtree_expression_destroy(expr);
            result.options.expression = modtree_expression_make_error(context.signature);
        }
    }
    return result;
}

Type_Signature* semantic_analyser_analyse_expression_type(AST::Expression* expression_node)
{
    auto& type_system = semantic_analyser.compiler->type_system;
    Expression_Result result = semantic_analyser_analyse_expression_any(
        expression_node, expression_context_make_auto_dereference()
    );
    switch (result.type)
    {
    case Expression_Result_Type::TYPE:
        return result.options.type;
    case Expression_Result_Type::EXPRESSION:
    {
        ModTree_Expression* expression = result.options.expression;
        SCOPE_EXIT(modtree_expression_destroy(expression));
        if (expression->result_type == type_system.unknown_type) {
            semantic_analyser_set_error_flag(true);
            return type_system.unknown_type;
        }
        if (expression->result_type != type_system.type_type)
        {
            semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE, expression_node);
            semantic_analyser_add_error_info(error_information_make_given_type(expression->result_type));
            return type_system.unknown_type;
        }

        Comptime_Result comptime = modtree_expression_calculate_comptime_value(expression);
        Type_Signature* result_type = type_system.unknown_type;
        switch (comptime.type)
        {
        case Comptime_Result_Type::AVAILABLE:
        {
            u64 type_index = *(u64*)comptime.data;
            if (type_index >= type_system.internal_type_infos.size) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, expression_node);
                return type_system.unknown_type;
            }
            return type_system.types[type_index];
        }
        case Comptime_Result_Type::UNAVAILABLE:
            return type_system.unknown_type;
        case Comptime_Result_Type::NOT_COMPTIME:
            semantic_analyser_log_error(Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME, expression_node);
            return type_system.unknown_type;
        default: panic("");
        }

        panic("");
        break;
    }
    case Expression_Result_Type::MODULE:
    case Expression_Result_Type::EXTERN_FUNCTION:
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::FUNCTION: {
        semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_TYPE, expression_node);
        semantic_analyser_add_error_info(error_information_make_expression_result_type(result.type));
        return type_system.unknown_type;
    }
    default: panic("");
    }
    panic("");
    return type_system.unknown_type;
}

ModTree_Expression* semantic_analyser_analyse_expression_value(AST::Expression* expression_node, Expression_Context context)
{
    auto& type_system = semantic_analyser.compiler->type_system;
    Expression_Result result = semantic_analyser_analyse_expression_any(expression_node, context);
    switch (result.type)
    {
    case Expression_Result_Type::EXPRESSION: {
        return result.options.expression;
    }
    case Expression_Result_Type::TYPE: {
        return modtree_expression_create_constant(
            type_system.type_type,
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
        semantic_analyser_log_error(Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION, expression_node);
        return modtree_expression_make_error(result.options.hardcoded_function->signature);
    }
    case Expression_Result_Type::MODULE:
    {
        semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_VALUE, expression_node);
        semantic_analyser_add_error_info(error_information_make_expression_result_type(result.type));
        return modtree_expression_make_error(type_system.unknown_type);
    }
    default: panic("");
    }
    panic("");
    return modtree_expression_make_error(type_system.unknown_type);
}

void semantic_analyser_analyse_extern_definitions(Symbol_Table* parent_table, AST::Module* module_node)
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

bool modtree_block_is_while(ModTree_Block* block)
{
    if (block->code_block != 0 && block->code_block->base.parent->type == AST::Base_Type::STATEMENT) {
        auto parent = (AST::Statement*) block->code_block->base.parent;
        return parent->type == AST::Statement_Type::WHILE_STATEMENT;
    }
    return false;
}

bool modtree_block_is_defer(ModTree_Block* block)
{
    if (block->code_block != 0 && block->code_block->base.parent->type == AST::Base_Type::STATEMENT) {
        auto parent = (AST::Statement*) block->code_block->base.parent;
        return parent->type == AST::Statement_Type::DEFER;
    }
    return false;
}

bool inside_defer()
{
    for (int i = semantic_analyser.block_stack.size - 1; i > 0; i--)
    {
        ModTree_Block* block = semantic_analyser.block_stack[i];
        if (modtree_block_is_defer(semantic_analyser.block_stack[i])) {
            return true;
        }
    }
    return false;
}

Control_Flow semantic_analyser_analyse_statement(AST::Statement* statement_node, ModTree_Block* block)
{
    auto& type_system = semantic_analyser.compiler->type_system;
    switch (statement_node->type)
    {
    case AST::Statement_Type::RETURN_STATEMENT:
    {
        auto& return_stat = statement_node->options.return_value;
        assert(semantic_analyser.current_function != 0, "No statements outside of function body");
        Type_Signature* expected_return_type = semantic_analyser.current_function->signature->options.function.return_type;

        Optional<ModTree_Expression*> value_expr;
        if (return_stat.available)
        {
            value_expr.available = true;
            value_expr.value = semantic_analyser_analyse_expression_value(
                return_stat.value, expression_context_make_specific_type(expected_return_type)
            );
            // When we have defers, we need to temporarily store the return result in a variable
            if (semantic_analyser.defer_stack.size != 0)
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
            if (expected_return_type != type_system.void_type) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_RETURN, statement_node);
                semantic_analyser_add_error_info(error_information_make_given_type(type_system.void_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(expected_return_type));
            }
        }
        if (inside_defer()) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED, statement_node);
        }
        else
        {
            // INFO: Inside Bakes returns only work through the defers that are inside the bake
            int defer_start_index = 0;
            for (int i = semantic_analyser.block_stack.size - 1; i >= 0; i--)
            {
                if (modtree_block_is_defer(semantic_analyser.block_stack[i])) {
                    defer_start_index = semantic_analyser.block_stack[i]->defer_start_index;
                    break;
                }
            }
            semantic_analyser_work_through_defers(block, defer_start_index);
        }

        ModTree_Statement* return_statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::RETURN);
        return_statement->options.return_value = value_expr;
        return Control_Flow::RETURNS;
    }
    case AST::Statement_Type::BREAK_STATEMENT:
    case AST::Statement_Type::CONTINUE_STATEMENT:
    {
        bool is_continue = statement_node->type == AST::Statement_Type::CONTINUE_STATEMENT;
        String* search_id = is_continue ? statement_node->options.continue_name : statement_node->options.break_name;
        ModTree_Block* found_block = 0;
        for (int i = semantic_analyser.block_stack.size - 1; i > 0; i--) // INFO: Block 0 is always the function body, which cannot be a target of break/continue
        {
            auto id = semantic_analyser.block_stack[i]->code_block->block_id;
            if (id.available && id.value == search_id) {
                found_block = semantic_analyser.block_stack[i];
                break;
            }
        }

        if (found_block == 0)
        {
            semantic_analyser_log_error(
                is_continue ? Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND : Semantic_Error_Type::BREAK_LABLE_NOT_FOUND, statement_node
            );
            semantic_analyser_add_error_info(error_information_make_id(search_id));
            return Control_Flow::SEQUENTIAL;
        }
        else
        {
            if (is_continue && !modtree_block_is_while(found_block)) {
                semantic_analyser_log_error(Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK, statement_node);
                return Control_Flow::SEQUENTIAL;
            }
            semantic_analyser_work_through_defers(block, found_block->defer_start_index);
        }

        ModTree_Statement* stmt = modtree_block_add_statement_empty(block, is_continue ? ModTree_Statement_Type::CONTINUE : ModTree_Statement_Type::BREAK);
        if (is_continue) {
            stmt->options.continue_to_block = found_block;
        }
        else
        {
            stmt->options.break_to_block = found_block;
            // Mark all previous Code-Blocks as Sequential flow, since they contain a path to a break
            for (int i = semantic_analyser.block_stack.size - 1; i >= 0; i--)
            {
                ModTree_Block* prev_block = semantic_analyser.block_stack[i];
                if (!prev_block->control_flow_locked && semantic_analyser.statement_reachable) {
                    prev_block->control_flow_locked = true;
                    prev_block->flow = Control_Flow::SEQUENTIAL;
                }
                if (prev_block == found_block) break;
            }
        }
        return Control_Flow::STOPS;
    }
    case AST::Statement_Type::DEFER:
    {
        if (inside_defer()) {
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS, statement_node);
            return Control_Flow::SEQUENTIAL;
        }
        else {
            dynamic_array_push_back(&semantic_analyser.defer_stack, statement_node->options.defer_block);
        }
        return Control_Flow::SEQUENTIAL;
    }
    case AST::Statement_Type::EXPRESSION_STATEMENT:
    {
        auto& expression_node = statement_node->options.expression;
        if (expression_node->type != AST::Expression_Type::FUNCTION_CALL) {
            semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL, statement_node);
        }
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::EXPRESSION);
        statement->options.expression = semantic_analyser_analyse_expression_value(
            expression_node, expression_context_make_unknown()
        );
        return Control_Flow::SEQUENTIAL;
    }
    case AST::Statement_Type::BLOCK:
    {
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::BLOCK);
        statement->options.block = modtree_block_create_empty(statement_node->options.block);
        semantic_analyser_fill_block(statement->options.block, statement_node->options.block);
        return statement->options.block->flow;
    }
    case AST::Statement_Type::IF_STATEMENT:
    {
        auto& if_node = statement_node->options.if_statement;
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::IF);

        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            if_node.condition, expression_context_make_specific_type(type_system.bool_type)
        );
        statement->options.if_statement.condition = condition;
        statement->options.if_statement.if_block = modtree_block_create_empty(if_node.block);
        semantic_analyser_fill_block(statement->options.if_statement.if_block, if_node.block);
        Control_Flow true_flow = statement->options.if_statement.if_block->flow;

        Control_Flow false_flow;
        if (if_node.else_block.available) {
            statement->options.if_statement.else_block = modtree_block_create_empty(if_node.else_block.value);
            semantic_analyser_fill_block(statement->options.if_statement.else_block, if_node.else_block.value);
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
    case AST::Statement_Type::SWITCH_STATEMENT:
    {
        auto& switch_node = statement_node->options.switch_statement;
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::SWITCH);

        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            switch_node.condition, expression_context_make_auto_dereference()
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
            semantic_analyser_log_error(Semantic_Error_Type::SWITCH_REQUIRES_ENUM, switch_node.condition);
            semantic_analyser_add_error_info(error_information_make_given_type(condition->result_type));
        }
        statement->options.switch_statement.condition = condition;
        statement->options.switch_statement.default_block = 0;
        statement->options.switch_statement.cases = dynamic_array_create_empty<ModTree_Switch_Case>(1);

        Type_Signature* switch_type = condition->result_type;
        Expression_Context case_context = condition->result_type->type == Signature_Type::ENUM ?
            expression_context_make_specific_type(condition->result_type) : expression_context_make_unknown();
        Control_Flow switch_flow = Control_Flow::SEQUENTIAL;
        for (int i = 0; i < switch_node.cases.size; i++)
        {
            auto& case_node = switch_node.cases[i];
            ModTree_Block* case_body = modtree_block_create_empty(case_node.block);
            semantic_analyser_fill_block(case_body, case_node.block);

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

            if (case_node.value.available)
            {
                ModTree_Expression* case_expr = semantic_analyser_analyse_expression_value(
                    case_node.value.value, case_context
                );

                ModTree_Switch_Case modtree_case;
                modtree_case.expression = case_expr;
                modtree_case.body = case_body;
                modtree_case.value = -1; // Placeholder
                // Calculate case value
                Comptime_Result comptime = modtree_expression_calculate_comptime_value(case_expr);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE: {
                    if (comptime.data_type == type_system.i32_type || comptime.data_type->type == Signature_Type::ENUM) {
                        modtree_case.value = *(int*)comptime.data;
                    }
                    break;
                }
                case Comptime_Result_Type::UNAVAILABLE: {
                    break;
                }
                case Comptime_Result_Type::NOT_COMPTIME: {
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN, case_node.value.value);
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
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED, statement_node);
                    // Switch old default so that we can analyse all given blocks
                    ModTree_Switch_Case modtree_case;
                    modtree_case.value = -420;
                    modtree_case.body = statement->options.switch_statement.default_block;
                    modtree_case.expression = modtree_expression_make_error(type_system.unknown_type);
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
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE, statement_node); // TODO: Fix this (E.g. not 0)
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
                semantic_analyser_log_error(Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES, statement_node);
            }
        }
        return switch_flow;
    }
    case AST::Statement_Type::WHILE_STATEMENT:
    {
        auto& while_node = statement_node->options.while_statement;
        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            while_node.condition, expression_context_make_specific_type(type_system.bool_type)
        );

        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::WHILE);
        statement->options.while_statement.condition = condition;
        statement->options.while_statement.while_block = modtree_block_create_empty(while_node.block);
        semantic_analyser_fill_block(statement->options.while_statement.while_block, while_node.block);
        Control_Flow flow = statement->options.while_statement.while_block->flow;
        if (flow == Control_Flow::RETURNS) {
            return flow;
        }
        return Control_Flow::SEQUENTIAL;
    }
    case AST::Statement_Type::DELETE_STATEMENT:
    {
        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
            statement_node->options.delete_expr, expression_context_make_unknown()
        );
        Type_Signature* delete_type = operand->result_type;
        if (delete_type->type != Signature_Type::POINTER && delete_type->type != Signature_Type::SLICE) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_DELETE, statement_node->options.delete_expr);
            semantic_analyser_add_error_info(error_information_make_given_type(delete_type));
        }

        ModTree_Expression* call_expr = new ModTree_Expression;
        call_expr->expression_type = ModTree_Expression_Type::FUNCTION_CALL;
        call_expr->result_type = type_system.void_type;
        call_expr->options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(1);
        call_expr->options.function_call.call_type = ModTree_Call_Type::HARDCODED_FUNCTION;
        call_expr->options.function_call.options.hardcoded_function = semantic_analyser.free_function;

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
    case AST::Statement_Type::ASSIGNMENT:
    {
        auto& assignment_node = statement_node->options.assignment;
        ModTree_Expression* left_expr = semantic_analyser_analyse_expression_value(
            assignment_node.left_side, expression_context_make_unknown()
        );
        ModTree_Expression* right_expr = semantic_analyser_analyse_expression_value(
            assignment_node.right_side, expression_context_make_specific_type(left_expr->result_type)
        );
        Type_Signature* left_type = left_expr->result_type;
        Type_Signature* right_type = right_expr->result_type;
        if (modtree_expression_result_is_temporary(left_expr)) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS, assignment_node.left_side);
        }
        ModTree_Statement* assign_statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::ASSIGNMENT);
        assign_statement->options.assignment.destination = left_expr;
        assign_statement->options.assignment.source = right_expr;
        return Control_Flow::SEQUENTIAL;
    }
    case AST::Statement_Type::DEFINITION:
    {
        auto& definition = statement_node->options.definition;
        if (definition->is_comptime) {
            // Already handled by definition workload
            return Control_Flow::SEQUENTIAL;
        }
        assert(!(!definition->value.available && !definition->type.available), "");

        ModTree_Variable* variable;
        Type_Signature* type = 0;
        if (definition->type.available) {
            type = semantic_analyser_analyse_expression_type(definition->type.value);
        }
        ModTree_Expression* value = 0;
        if (definition->value.available)
        {
            value = semantic_analyser_analyse_expression_value(
                definition->value.value, type != 0 ? expression_context_make_specific_type(type) : expression_context_make_unknown()
            );
            if (type == 0) {
                type = value->result_type;
            }
        }

        variable = modtree_block_add_variable(block, type, definition->symbol);
        assert(definition->symbol->type == Symbol_Type::VARIABLE || definition->symbol->type == Symbol_Type::VARIABLE_UNDEFINED, "");
        definition->symbol->type = Symbol_Type::VARIABLE;
        definition->symbol->options.variable = variable;

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

void semantic_analyser_fill_block(ModTree_Block* block, AST::Code_Block* code_block)
{
    block->control_flow_locked = false;
    block->flow = Control_Flow::SEQUENTIAL;
    block->defer_start_index = semantic_analyser.defer_stack.size;

    int rewind_block_count = semantic_analyser.block_stack.size;
    bool rewind_reachable = semantic_analyser.statement_reachable;
    int rewind_defer_size = semantic_analyser.defer_stack.size;
    SCOPE_EXIT(dynamic_array_rollback_to_size(&semantic_analyser.block_stack, rewind_block_count));
    SCOPE_EXIT(dynamic_array_rollback_to_size(&semantic_analyser.defer_stack, rewind_defer_size));
    SCOPE_EXIT(semantic_analyser.statement_reachable = rewind_reachable);
    dynamic_array_push_back(&semantic_analyser.block_stack, block);
    for (int i = 0; i < code_block->statements.size; i++)
    {
        Control_Flow flow = semantic_analyser_analyse_statement(code_block->statements[i], block);
        if (flow != Control_Flow::SEQUENTIAL) {
            semantic_analyser.statement_reachable = false;
            if (!block->control_flow_locked) {
                block->flow = flow;
                block->control_flow_locked = true;
            }
        }
    }
    // Work through defers
    if (semantic_analyser.statement_reachable) {
        semantic_analyser_work_through_defers(block, block->defer_start_index);
    }
    dynamic_array_rollback_to_size(&semantic_analyser.defer_stack, block->defer_start_index);
}

void symbol_set_type(Symbol* symbol, Type_Signature* type)
{
    symbol->type = Symbol_Type::TYPE;
    symbol->options.type = type;
}

void semantic_analyser_finish()
{
    auto& type_system = semantic_analyser.compiler->type_system;
    // Check if main is defined
    Symbol* main_symbol = symbol_table_find_symbol(
        semantic_analyser.compiler->main_source->ast->symbol_table, semantic_analyser.id_main, false, 0
    );
    ModTree_Function* main_function = 0;
    if (main_symbol == 0) {
        semantic_analyser_log_error(Semantic_Error_Type::MAIN_NOT_DEFINED, (AST::Base*)0);
    }
    else
    {
        if (main_symbol->type != Symbol_Type::FUNCTION) {
            semantic_analyser_log_error(Semantic_Error_Type::MAIN_MUST_BE_FUNCTION, main_symbol->definition_node);
            semantic_analyser_add_error_info(error_information_make_symbol(main_symbol));
        }
        else {
            main_function = main_symbol->options.function;
        }
    }

    // Add type_informations loading to global init function
    if (semantic_analyser.errors.size == 0)
    {
        Type_Signature* type_info_array_signature = type_system_make_array(
            &type_system, type_system.type_information_type, true, type_system.internal_type_infos.size
        );
        int internal_information_size = type_system.internal_type_infos.size;
        Constant_Result result = constant_pool_add_constant(
            &semantic_analyser.compiler->constant_pool, type_info_array_signature,
            array_create_static_as_bytes(type_system.internal_type_infos.data, type_system.internal_type_infos.size)
        );
        assert(result.status == Constant_Status::SUCCESS, "Type information must be valid");
        assert(type_system.types.size == type_system.internal_type_infos.size, "");

        // Set type_informations pointer
        {
            ModTree_Expression* global_access = modtree_expression_create_empty(ModTree_Expression_Type::VARIABLE_READ, semantic_analyser.global_type_informations->data_type);
            global_access->options.variable_read = semantic_analyser.global_type_informations;

            ModTree_Expression* data_access = modtree_expression_create_empty(
                ModTree_Expression_Type::MEMBER_ACCESS, type_system_make_pointer(&type_system, type_system.type_information_type)
            );
            data_access->options.member_access.structure_expression = global_access;
            data_access->options.member_access.member = semantic_analyser.global_type_informations->data_type->options.slice.data_member;

            ModTree_Expression* constant_access = modtree_expression_create_empty(ModTree_Expression_Type::CONSTANT_READ, type_info_array_signature);
            constant_access->options.constant_read = result.constant;

            ModTree_Expression* ptr_expression = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, data_access->result_type);
            ptr_expression->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
            ptr_expression->options.unary_operation.operand = constant_access;

            ModTree_Statement* assign_statement = modtree_block_add_statement_empty(semantic_analyser.global_init_function->body, ModTree_Statement_Type::ASSIGNMENT);
            assign_statement->options.assignment.destination = data_access;
            assign_statement->options.assignment.source = ptr_expression;
        }
        // Set type_informations size
        {
            ModTree_Expression* global_access = modtree_expression_create_empty(
                ModTree_Expression_Type::VARIABLE_READ, semantic_analyser.global_type_informations->data_type
            );
            global_access->options.variable_read = semantic_analyser.global_type_informations;

            ModTree_Expression* size_access = modtree_expression_create_empty(ModTree_Expression_Type::MEMBER_ACCESS, type_system.i32_type);
            size_access->options.member_access.structure_expression = global_access;
            size_access->options.member_access.member = semantic_analyser.global_type_informations->data_type->options.slice.size_member;

            ModTree_Statement* assign_statement = modtree_block_add_statement_empty(semantic_analyser.global_init_function->body, ModTree_Statement_Type::ASSIGNMENT);
            assign_statement->options.assignment.destination = size_access;
            assign_statement->options.assignment.source = modtree_expression_create_constant_i32(internal_information_size);
        }
    }

    // Call main after end of global init function
    {
        if (main_function != 0)
        {
            ModTree_Expression* call_expr = modtree_expression_create_empty(ModTree_Expression_Type::FUNCTION_CALL, type_system.void_type);
            call_expr->options.function_call.call_type = ModTree_Call_Type::FUNCTION;
            call_expr->options.function_call.arguments = dynamic_array_create_empty<ModTree_Expression*>(1);
            call_expr->options.function_call.options.function = main_function;
            ModTree_Statement* call_stmt = modtree_block_add_statement_empty(semantic_analyser.global_init_function->body, ModTree_Statement_Type::EXPRESSION);
            call_stmt->options.expression = call_expr;
        }
        ModTree_Statement* exit_stmt = modtree_block_add_statement_empty(semantic_analyser.global_init_function->body, ModTree_Statement_Type::EXIT);
        exit_stmt->options.exit_code = Exit_Code::SUCCESS;

        semantic_analyser.program->entry_function = semantic_analyser.global_init_function;
    }
}

void semantic_analyser_reset(Compiler* compiler)
{
    auto& type_system = compiler->type_system;
    Predefined_Symbols* pre = &compiler->dependency_analyser->predefined_symbols;

    // Reset analyser data
    {
        semantic_analyser.compiler = compiler;
        semantic_analyser.current_workload = 0;
        semantic_analyser.current_function = 0;
        semantic_analyser.statement_reachable = true;
        semantic_analyser.error_flag_count = 0;
        for (int i = 0; i < semantic_analyser.errors.size; i++) {
            dynamic_array_destroy(&semantic_analyser.errors[i].information);
        }
        dynamic_array_reset(&semantic_analyser.errors);
        dynamic_array_reset(&semantic_analyser.block_stack);
        dynamic_array_reset(&semantic_analyser.defer_stack);
        stack_allocator_reset(&semantic_analyser.allocator_values);
        hashset_reset(&semantic_analyser.loaded_filenames);
        hashset_reset(&semantic_analyser.visited_functions);

        workload_executer_destroy();
        semantic_analyser.workload_executer = workload_executer_initialize();

        if (semantic_analyser.program != 0) {
            modtree_program_destroy();
        }
        semantic_analyser.program = modtree_program_create();
        semantic_analyser.global_init_function = 0;
    }

    // Add symbols for basic datatypes
    {
        Type_System* ts = &semantic_analyser.compiler->type_system;
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

        semantic_analyser.id_size = identifier_pool_add(&compiler->identifier_pool, string_create_static("size"));
        semantic_analyser.id_data = identifier_pool_add(&compiler->identifier_pool, string_create_static("data"));
        semantic_analyser.id_main = identifier_pool_add(&compiler->identifier_pool, string_create_static("main"));
        semantic_analyser.id_tag = identifier_pool_add(&compiler->identifier_pool, string_create_static("tag"));
        semantic_analyser.id_type_of = identifier_pool_add(&compiler->identifier_pool, string_create_static("type_of"));
        semantic_analyser.id_type_info = identifier_pool_add(&compiler->identifier_pool, string_create_static("type_info"));
    }

    {
        // Add global type_information table
        semantic_analyser.global_type_informations = modtree_program_add_global(
            type_system_make_slice(&type_system, type_system.type_information_type),
            pre->global_type_informations
        );
        Symbol* type_infos = pre->global_type_informations;
        type_infos->type = Symbol_Type::VARIABLE;
        type_infos->options.variable = semantic_analyser.global_type_informations;
    }

    // Initialize hardcoded_functions
    for (int i = 0; i < (int)Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT; i++)
    {
        Hardcoded_Function_Type type = (Hardcoded_Function_Type)i;

        // Create Function
        ModTree_Hardcoded_Function* hardcoded_function = new ModTree_Hardcoded_Function;
        dynamic_array_push_back(&semantic_analyser.program->hardcoded_functions, hardcoded_function);

        // Find signature
        Dynamic_Array<Type_Signature*> parameters = dynamic_array_create_empty<Type_Signature*>(2);
        Type_Signature* return_type = type_system.void_type;
        Symbol* symbol = 0;
        switch (type)
        {
        case Hardcoded_Function_Type::PRINT_I32: {
            symbol = pre->hardcoded_print_i32;
            dynamic_array_push_back(&parameters, type_system.i32_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_F32: {
            symbol = pre->hardcoded_print_f32;
            dynamic_array_push_back(&parameters, type_system.f32_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_BOOL: {
            symbol = pre->hardcoded_print_bool;
            dynamic_array_push_back(&parameters, type_system.bool_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_STRING: {
            symbol = pre->hardcoded_print_string;
            dynamic_array_push_back(&parameters, type_system.string_type);
            break;
        }
        case Hardcoded_Function_Type::PRINT_LINE: {
            symbol = pre->hardcoded_print_line;
            break;
        }
        case Hardcoded_Function_Type::READ_I32: {
            symbol = pre->hardcoded_read_i32;
            return_type = type_system.i32_type;
            break;
        }
        case Hardcoded_Function_Type::READ_F32: {
            symbol = pre->hardcoded_read_f32;
            return_type = type_system.f32_type;
            break;
        }
        case Hardcoded_Function_Type::READ_BOOL: {
            symbol = pre->hardcoded_read_bool;
            return_type = type_system.bool_type;
            break;
        }
        case Hardcoded_Function_Type::RANDOM_I32: {
            symbol = pre->hardcoded_random_i32;
            return_type = type_system.i32_type;
            break;
        }
        case Hardcoded_Function_Type::MALLOC_SIZE_I32: {
            symbol = 0;
            semantic_analyser.malloc_function = hardcoded_function;
            dynamic_array_push_back(&parameters, type_system.i32_type);
            return_type = type_system.void_ptr_type;
            break;
        }
        case Hardcoded_Function_Type::FREE_POINTER:
            symbol = 0;
            semantic_analyser.free_function = hardcoded_function;
            dynamic_array_push_back(&parameters, type_system.void_ptr_type);
            return_type = type_system.void_type;
            break;
        default:
            panic("What");
        }
        hardcoded_function->signature = type_system_make_function(&type_system, parameters, return_type);
        hardcoded_function->hardcoded_type = type;

        // Set symbol data
        if (symbol != 0) {
            symbol->type = Symbol_Type::HARDCODED_FUNCTION;
            symbol->options.hardcoded_function = hardcoded_function;
        }
    }

    // Add assert function
    {
        Symbol* symbol = pre->function_assert;
        Dynamic_Array<Type_Signature*> params = dynamic_array_create_empty<Type_Signature*>(1);
        dynamic_array_push_back(&params, type_system.bool_type);
        ModTree_Function* assert_fn = modtree_function_create_empty(
            type_system_make_function(&type_system, params, type_system.void_type),
            symbol, 0
        );
        symbol->type = Symbol_Type::FUNCTION;
        symbol->options.function = assert_fn;
        ModTree_Variable* cond_var = modtree_function_add_normal_parameter(assert_fn, type_system.bool_type, 0);

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
        semantic_analyser.assert_function = assert_fn;
    }

    // Add global init function
    semantic_analyser.global_init_function = modtree_function_create_empty(
        type_system_make_function(
            &type_system, dynamic_array_create_empty<Type_Signature*>(1), type_system.void_type
        ), 0, 0
    );
}

Semantic_Analyser* semantic_analyser_initialize()
{
    semantic_analyser.errors = dynamic_array_create_empty<Semantic_Error>(64);
    semantic_analyser.block_stack = dynamic_array_create_empty<ModTree_Block*>(8);
    semantic_analyser.defer_stack = dynamic_array_create_empty<AST::Code_Block*>(8);
    semantic_analyser.allocator_values = stack_allocator_create_empty(2048);
    semantic_analyser.loaded_filenames = hashset_create_pointer_empty<String*>(32);
    semantic_analyser.visited_functions = hashset_create_pointer_empty<ModTree_Function*>(32);
    semantic_analyser.workload_executer = workload_executer_initialize();
    semantic_analyser.program = 0;
    return &semantic_analyser;
}

void semantic_analyser_destroy()
{
    for (int i = 0; i < semantic_analyser.errors.size; i++) {
        dynamic_array_destroy(&semantic_analyser.errors[i].information);
    }
    dynamic_array_destroy(&semantic_analyser.errors);
    dynamic_array_destroy(&semantic_analyser.block_stack);
    dynamic_array_destroy(&semantic_analyser.defer_stack);

    stack_allocator_destroy(&semantic_analyser.allocator_values);
    hashset_destroy(&semantic_analyser.loaded_filenames);
    hashset_destroy(&semantic_analyser.visited_functions);

    if (semantic_analyser.program != 0) {
        modtree_program_destroy();
    }
}



/*
ERRORS + Import
*/
void semantic_error_get_infos_internal(Semantic_Error e, const char** result_str, Parser::Section* result_section)
{
    const char* string = "";
    Parser::Section section;
#define HANDLE_CASE(type, msg, sec) \
    case type: \
        string = msg;\
        section = sec; \
        break;

    switch (e.type)
    {
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE, "Invalid Type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, "Invalid Type Handle!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME, "Type not known at compile time", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE, "Expression used as a type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, "Expression type not valid in this context", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPECTED_CALLABLE, "Expected: Callable", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPECTED_TYPE, "Expected: Type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPECTED_VALUE, "Expected: Value", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT, "Invalid Template Argument count", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE, "Template arguments invalid", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_MEMBER_ACCESS_INVALID_ON_FUNCTION, "Functions do not have any members to access", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM, "Member access on types requires enums", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED, "Symbol is templated", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_REQUIRES_ENUM, "Switch requires enum", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN, "Switch case must be compile time known", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES, "Switch does not handle all cases", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_MUST_NOT_BE_EMPTY, "Switch must not be empty", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED, "Switch only one default case allowed", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE, "Switch case must be unique", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_CASE_TYPE_INVALID, "Switch case type must be enum value", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL, "Extern header does not contain this symbol", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED, "Parsing extern header failed", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, "Invalid use of void type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL, "Expected function pointer type on function call", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER, "Expected function type on function import", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARGUMENT, "Argument type does not match function parameter type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_INITIALIZER_REQUIRES_TYPE_SYMBOL, "Array initializer requires type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_REQUIRES_TYPE_SYMBOL, "Struct initilizer requires type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, "Struct member/s missing", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG, "Cannot set union tag in initializer", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE, "Member already initialized", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT, "Initializer type must either be struct/union/enum", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST, "Struct does not contain member", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_INVALID_MEMBER_TYPE, "Member type does not match", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER, "Only one union member may be active at one time", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE, "Auto struct type could not be determined by context", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE, "Could not determine array type by context", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_INITIALIZER_INVALID_TYPE, "Array initializer member invalid type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS, "Array access only works on array types", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX, "Array access index must be of type i32", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE, "Array allocation size must be of type i32", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE, "Array size must be of type i32", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS, "Member access only valid on struct/array or pointer to struct/array types", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_IF_CONDITION, "If condition must be boolean", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION, "While condition must be boolean", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, "Unary operator type invalid", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR, "Binary operator types invalid", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT, "Invalid assignment type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_RETURN, "Invalid return type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_DELETE, "Only pointer or unsized array types can be deleted", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER, "Expected Type symbol", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_MODULE_INVALID, "Expected Variable or Function symbol for Variable read", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH, "Expected module in indentifier path", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL, "Could not resolve symbol", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED, "Symbol already defined", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED, "Module already defined", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ENUM_VALUE, "Enum value must be of i32 type!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_EXPECTED_POINTER, "Invalid type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED, "Bake error", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT, "Auto Member must be in used in a Context where an enum is required", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED, "Auto Member must be used inside known Context", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED, "Auto cast not able to extract destination type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS, "Bake function must not reference globals!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CONSTANT_POOL_ERROR, "Could not add value to constant pool", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_COMPTIME_DEFINITION, "Value does not match given type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN, "Comptime definition value must be known at compile time", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED, "Modules/Polymorphic function definitions must be infered", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, "Parameter count does not match argument count", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_CAST_PTR_REQUIRES_U64, "cast_ptr only casts from u64 to pointers", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR, "cast_ptr only casts from u64 to pointers", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_INVALID_CAST, "Invalid cast", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, "Struct/Array does not contain member", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::LABEL_ALREADY_IN_USE, "Label already in use", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BREAK_LABLE_NOT_FOUND, "Label cannot be found", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BREAK_NOT_INSIDE_LOOP_OR_SWITCH, "Break is not inside a loop or switch", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND, "Label cannot be found", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CONTINUE_NOT_INSIDE_LOOP, "Continue is not inside a loop", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK, "Continue only works for loop lables", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH, "Binary op types do not match and cannot be implicitly casted", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL, "Expression does not do anything", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_STRUCT_MUST_CONTAIN_MEMBER, "Struct must contain at least one member", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED, "Struct member is already defined", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE, "While loop always exits", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS, "While loop always returns", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS, "While loop always continues", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE, "Unreachable statement", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED, "No returns allowed inside of defer", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, "Function is missing a return statement", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_FUNCTION_HEADER, "Unfinished workload function header", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK, "Unfinished workload code block", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE, "Unfinished workload type size", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_CANNOT_BE_TEMPLATED, "Main function cannot be templated", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_MUST_BE_FUNCTION, "Main must be a function", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_NOT_DEFINED, "Main function not found", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_UNEXPECTED_SIGNATURE, "Main unexpected signature", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_CANNOT_BE_CALLED, "Cannot call main function again", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE, "Could not load file", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_MAIN, "Cannot take address of main", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION, "Cannot take address of hardcoded function", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS, "Left side of assignment does not have a memory address", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_TEMPLATED_GLOBALS, "Templated globals not implemented yet", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN, "Array size not known at compile time", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_SIZE_MUST_BE_GREATER_ZERO, "Array size must be greater zero!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES, "Nested template modules not implemented yet", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES, "Extern imports inside templates not allowed", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT, "Extern global variable import not implemented yet", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE, "Missing feature", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE, "Enum member name must be unique", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN, "enum value must be compile time known", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE, "Enum value must be unique", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, "Enum member does not exist", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CANNOT_TAKE_POINTER_OF_FUNCTION, "Cannot take pointer of function", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT, "Cannot take address of temporary result!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS, "Nested defers not implemented yet", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CYCLIC_DEPENDENCY_DETECTED, "Cyclic workload dependencies detected", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET, "Variable not defined yet", Parser::Section::WHOLE);
    default: panic("ERROR");
    }
#undef HANDLE_CASE
    if (result_str != 0) {
        *result_str = string;
    }
    if (result_section != 0) {
        *result_section = section;
    }
}

Parser::Section semantic_error_get_section(Semantic_Error e)
{
    Parser::Section result;
    semantic_error_get_infos_internal(e, 0, &result);
    return result;
}

void semantic_error_append_to_string(Semantic_Error e, String* string)
{
    const char* type_text;
    semantic_error_get_infos_internal(e, &type_text, 0);
    string_append_formated(string, type_text);

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


