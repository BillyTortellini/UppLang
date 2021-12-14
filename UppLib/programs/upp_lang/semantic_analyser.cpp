#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../utility/file_io.hpp"

#include "compiler.hpp"
#include "type_system.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "rc_analyser.hpp"
#include "c_importer.hpp"
#include "compiler_misc.hpp"
#include "ast_parser.hpp"

bool PRINT_DEPENDENCIES = false;

struct Expression_Result;
struct Expression_Context;

// PROTOTYPES
Type_Signature* import_c_type(Semantic_Analyser* analyser, C_Import_Type* type, Hashtable<C_Import_Type*, Type_Signature*>* type_conversions);

Expression_Result semantic_analyser_analyse_expression_any(Semantic_Analyser* analyser, RC_Expression* rc_expression, Expression_Context context);
ModTree_Expression* semantic_analyser_analyse_expression_value(Semantic_Analyser* analyser, RC_Expression* rc_expression, Expression_Context context);
Type_Signature* semantic_analyser_analyse_expression_type(Semantic_Analyser* analyser, RC_Expression* rc_expression);
void analysis_workload_add_dependency(Analysis_Workload* workload, Analysis_Workload* dependency);
void analysis_workload_destroy(Analysis_Workload* workload);
void modtree_block_destroy(ModTree_Block* block);
void modtree_function_destroy(ModTree_Function* function);
void modtree_statement_destroy(ModTree_Statement* statement);
ModTree_Expression* semantic_analyser_cast_implicit_if_possible(Semantic_Analyser* analyser, ModTree_Expression* expression, Type_Signature* destination_type);
bool analysis_workload_execute(Analysis_Workload* workload, Semantic_Analyser* analyser);

enum class Control_Flow
{
    NO_RETURN,
    RETURN,
    CONTINUE,
    BREAK
};
Control_Flow semantic_analyser_fill_block(Semantic_Analyser* analyser, ModTree_Block* block, RC_Block* rc_block);

/*
ERROR Helpers
*/
void semantic_analyser_set_error_flag(Semantic_Analyser* analyser) {
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, Semantic_Error_Type type, AST_Node* node) {
    semantic_analyser_set_error_flag(analyser);
    Semantic_Error error;
    error.type = type;
    error.error_node = node;
    error.information = dynamic_array_create_empty<Error_Information>(2);
    dynamic_array_push_back(&analyser->errors, error);
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
    info.options.invalid_member.struct_signature= struct_signature;
    return info;
}

Error_Information error_information_make_id(String* id) {
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

ModTree_Block* modtree_block_create_empty()
{
    ModTree_Block* block = new ModTree_Block;
    block->statements = dynamic_array_create_empty<ModTree_Statement*>(1);
    block->variables = dynamic_array_create_empty<ModTree_Variable*>(1);
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
    var->data_type = type;
    var->symbol = symbol;
    dynamic_array_push_back(&block->variables, var);
    return var;
}

ModTree_Function* modtree_function_create_empty(ModTree_Program* program, Type_Signature* signature, Symbol* symbol)
{
    ModTree_Function* function = new ModTree_Function;
    function->parameters = dynamic_array_create_empty<ModTree_Variable*>(1);
    function->symbol = symbol;
    function->signature = signature;
    function->body = modtree_block_create_empty();
    dynamic_array_push_back(&program->functions, function);
    return function;
}

void modtree_function_destroy(ModTree_Function* function)
{
    for (int i = 0; i < function->parameters.size; i++) {
        delete function->parameters[i];
    }
    dynamic_array_destroy(&function->parameters);
    modtree_block_destroy(function->body);
    delete function;
}

ModTree_Variable* modtree_function_add_parameter(ModTree_Function* function, Type_Signature* type, Symbol* symbol)
{
    ModTree_Variable* var = new ModTree_Variable;
    var->data_type = type;
    var->symbol = symbol;
    dynamic_array_push_back(&function->parameters, var);
    return var;
}

ModTree_Variable* modtree_program_add_global(ModTree_Program* program, Type_Signature* type, Symbol* symbol)
{
    ModTree_Variable* var = new ModTree_Variable;
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

struct Comptime_Analysis
{
    bool available;
    void* data;
    Type_Signature* type;
};

Comptime_Analysis comptime_analysis_make_success(void* data, Type_Signature* type) {
    Comptime_Analysis result;
    result.available = true;
    result.data = data;
    result.type = type;
    return result;
}

Comptime_Analysis comptime_analysis_make_error() {
    Comptime_Analysis result;
    result.available = false;
    result.data = 0;
    result.type = 0;
    return result;
}

Comptime_Analysis modtree_expression_calculate_comptime_value(Semantic_Analyser* analyser, ModTree_Expression* expr)
{
    Type_System* type_system = &analyser->compiler->type_system;
    switch (expr->expression_type)
    {
    case ModTree_Expression_Type::BINARY_OPERATION:
    {
        Comptime_Analysis left_val = modtree_expression_calculate_comptime_value(analyser, expr->options.binary_operation.left_operand);
        Comptime_Analysis right_val = modtree_expression_calculate_comptime_value(analyser, expr->options.binary_operation.right_operand);
        if (!left_val.available || !right_val.available) {
            return comptime_analysis_make_error();
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
        if (bytecode_execute_binary_instr(instr_type, type_signature_to_bytecode_type(left_val.type), result_buffer, left_val.data, right_val.data)) {
            return comptime_analysis_make_success(result_buffer, expr->result_type);
        }
        else {
            return comptime_analysis_make_error();
        }
        break;
    }
    case ModTree_Expression_Type::UNARY_OPERATION:
    {
        Comptime_Analysis value = modtree_expression_calculate_comptime_value(analyser, expr->options.unary_operation.operand);
        if (!value.available) return value;

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
            return comptime_analysis_make_error();
        case ModTree_Unary_Operation_Type::DEREFERENCE:
            return comptime_analysis_make_error();
        case ModTree_Unary_Operation_Type::TEMPORARY_TO_STACK:
            return value;
        default: panic("");
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, expr->result_type->size, expr->result_type->alignment);
        bytecode_execute_unary_instr(instr_type, type_signature_to_bytecode_type(value.type), result_buffer, value.data);
        comptime_analysis_make_success(result_buffer, expr->result_type);
        break;
    }
    break;
    case ModTree_Expression_Type::CONSTANT_READ: {
        Constant_Pool* pool = &analyser->compiler->constant_pool;
        Upp_Constant constant = expr->options.constant_read;
        return comptime_analysis_make_success(&pool->buffer[constant.offset], constant.type);
    }
    case ModTree_Expression_Type::FUNCTION_POINTER_READ: {
        return comptime_analysis_make_error(); // This will work in the future, but not currently
    }
    case ModTree_Expression_Type::VARIABLE_READ: {
        return comptime_analysis_make_error();
    }
    case ModTree_Expression_Type::CAST:
    {
        Comptime_Analysis value = modtree_expression_calculate_comptime_value(analyser, expr->options.cast.cast_argument);
        if (!value.available == 0) return value;

        Instruction_Type instr_type = (Instruction_Type)-1;
        switch (expr->options.cast.type)
        {
        case ModTree_Cast_Type::FLOATS: instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::FLOAT_TO_INT: instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
        case ModTree_Cast_Type::INT_TO_FLOAT: instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
        case ModTree_Cast_Type::INTEGERS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::ENUM_TO_INT: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::INT_TO_ENUM: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
        case ModTree_Cast_Type::POINTERS: return comptime_analysis_make_error();
        case ModTree_Cast_Type::POINTER_TO_U64: return comptime_analysis_make_error();
        case ModTree_Cast_Type::U64_TO_POINTER: return comptime_analysis_make_error();
        case ModTree_Cast_Type::ARRAY_SIZED_TO_UNSIZED: {
            Upp_Slice_Base* slice = stack_allocator_allocate<Upp_Slice_Base>(&analyser->allocator_values);
            slice->data_ptr = value.data;
            slice->size = value.type->options.array.element_count;
            return comptime_analysis_make_success(slice, expr->result_type);
        }
        case ModTree_Cast_Type::TO_ANY: {
            Upp_Any* any = stack_allocator_allocate<Upp_Any>(&analyser->allocator_values);
            any->type = expr->result_type->internal_index;
            any->data = value.data;
            return comptime_analysis_make_success(any, expr->result_type);
        }
        case ModTree_Cast_Type::FROM_ANY: {
            Upp_Any* given = (Upp_Any*)value.data;
            if (given->type >= analyser->compiler->type_system.types.size) {
                return comptime_analysis_make_error();
            }
            Type_Signature* any_type = analyser->compiler->type_system.types[given->type];
            if (any_type != value.type) {
                return comptime_analysis_make_error();
            }
            return comptime_analysis_make_success(given->data, any_type);
        }
        default: panic("");
        }
        if ((int)instr_type == -1) panic("");

        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, expr->result_type->size, expr->result_type->alignment);
        bytecode_execute_cast_instr(
            instr_type, result_buffer, value.data,
            type_signature_to_bytecode_type(expr->result_type),
            type_signature_to_bytecode_type(value.type)
        );
        return comptime_analysis_make_success(result_buffer, expr->result_type);
    }
    case ModTree_Expression_Type::ARRAY_ACCESS:
    {
        Type_Signature* element_type = expr->result_type;
        Comptime_Analysis value_array = modtree_expression_calculate_comptime_value(analyser, expr->options.array_access.array_expression);
        Comptime_Analysis value_index = modtree_expression_calculate_comptime_value(analyser, expr->options.array_access.index_expression);
        if (!value_array.available || !value_index.available) {
            return comptime_analysis_make_error();
        }
        assert(value_index.type == type_system->i32_type, "Must be i32 currently");

        byte* base_ptr = 0;
        int array_size = 0;
        if (value_array.type->type == Signature_Type::ARRAY) {
            base_ptr = (byte*)value_array.data;
            array_size = value_array.type->options.array.element_count;
        }
        else if (value_array.type->type == Signature_Type::SLICE) {
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
            return comptime_analysis_make_error();
        }
        if (!memory_is_readable(base_ptr, element_type->size)) {
            return comptime_analysis_make_error();
        }
        return comptime_analysis_make_success(&base_ptr[element_offset], element_type);
    }
    case ModTree_Expression_Type::MEMBER_ACCESS: {
        Struct_Member* member = &expr->options.member_access.member;
        Comptime_Analysis value_struct = modtree_expression_calculate_comptime_value(analyser, expr->options.member_access.structure_expression);
        if (!value_struct.available) {
            return comptime_analysis_make_error();
        }
        byte* raw_data = (byte*)value_struct.data;
        return comptime_analysis_make_success(&raw_data[member->offset], expr->result_type);
    }
    case ModTree_Expression_Type::FUNCTION_CALL: {
        return comptime_analysis_make_error();
    }
    case ModTree_Expression_Type::NEW_ALLOCATION: {
        return comptime_analysis_make_error(); // New is always uninitialized, so it cannot have a comptime value (Future: Maybe new with values)
    }
    case ModTree_Expression_Type::ARRAY_INITIALIZER:
    {
        // NOTE: Maybe this works in the futurre, but it dependes if we can always finish the struct size after analysis
        return comptime_analysis_make_error();
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
        return comptime_analysis_make_error();
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
    return comptime_analysis_make_error();
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
    TYPE_KNOWN,
    ARITHMETIC_OPERAND, // + - * / %, unary -, requires primitive type either int or float
    UNKNOWN,
    FUNCTION_CALL, // Requires some type of function
    TYPE_EXPECTED,
    ARRAY,
    MEMBER_ACCESS, // Only valid on enums, structs, slices and arrays
    SWITCH_CONDITION, // Enums
};

struct Expression_Context
{
    Expression_Context_Type type;
    Type_Signature* signature;
    bool enable_pointer_conversion;
};

// Helpers
Expression_Context expression_context_make_unknown() {
    Expression_Context context;
    context.type = Expression_Context_Type::UNKNOWN;
    return context;
}

Expression_Context expression_context_make_known_type(Type_Signature* signature, bool auto_pointer_enabled) {
    if (signature->type == Signature_Type::ERROR_TYPE) {
        return expression_context_make_unknown();
    }
    Expression_Context context;
    context.type = Expression_Context_Type::TYPE_KNOWN;
    context.enable_pointer_conversion = auto_pointer_enabled;
    context.signature = signature;
    return context;
}

Expression_Context expression_context_make_type(Expression_Context_Type type) {
    Expression_Context context;
    context.type = type;
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
Dependency_Graph dependency_graph_create(Semantic_Analyser* analyser)
{
    Dependency_Graph result;
    result.workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    result.analyser = analyser;
    result.runnable_workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    result.progress_definitions = hashtable_create_pointer_empty<Symbol*, Analysis_Workload*>(8);
    result.progress_structs = hashtable_create_pointer_empty<Type_Signature*, Struct_Progress>(8);
    result.progress_functions = hashtable_create_pointer_empty<ModTree_Function*, Function_Progress>(8);
    return result;
}

void dependency_graph_destroy(Dependency_Graph* graph)
{
    for (int i = 0; i < graph->workloads.size; i++) {
        analysis_workload_destroy(graph->workloads[i]);
    }
    dynamic_array_destroy(&graph->workloads);
    dynamic_array_destroy(&graph->runnable_workloads);
    hashtable_destroy(&graph->progress_definitions);
    hashtable_destroy(&graph->progress_functions);
    hashtable_destroy(&graph->progress_structs);
}

void analysis_workload_destroy(Analysis_Workload* workload)
{
    list_destroy(&workload->dependencies);
    dynamic_array_destroy(&workload->dependents);
    dynamic_array_destroy(&workload->symbol_dependencies);
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
        Symbol* symbol = symbol_table_find_symbol(table, identifier_node->id, identifier_node == symbol_read->identifier_node, is_path ? 0 : symbol_read);
        if (is_path)
        {
            if (symbol == 0) {
                return 0; // Did not find module
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

void dependency_graph_resolve(Dependency_Graph* graph)
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

    while (true)
    {
        bool progress_was_made = false;
        // Check if symbols can be resolved
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
                }

                bool symbol_read_ready = true;
                switch (symbol_read->symbol->type)
                {
                case Symbol_Type::UNRESOLVED:
                {
                    Analysis_Workload** definition_workload = hashtable_find_element(&graph->progress_definitions, symbol_read->symbol);
                    if (definition_workload != 0) {
                        symbol_read_ready = true;
                        analysis_workload_add_dependency(workload, *definition_workload);
                    }
                    else {
                        symbol_read_ready = false;
                    }
                    break;
                }
                case Symbol_Type::FUNCTION:
                {
                    Function_Progress* progress_opt = hashtable_find_element(&graph->progress_functions, symbol_read->symbol->options.function);
                    if (progress_opt != 0 && progress_opt->state == Function_State::HEADER_WAITING) {
                        analysis_workload_add_dependency(workload, progress_opt->header_workload);
                    }
                    break;
                }
                case Symbol_Type::TYPE:
                {
                    Type_Signature* type = symbol_read->symbol->options.type;
                    if (type->type == Signature_Type::STRUCT)
                    {
                        Struct_Progress* progress = hashtable_find_element(&graph->progress_structs, type);
                        if (progress != 0) {
                            if (symbol_read->type == Symbol_Dependency_Type::NORMAL && progress->state != Struct_State::FINISHED) {
                                analysis_workload_add_dependency(workload, progress->reachable_resolve_workload);
                            }
                        }
                        else {
                            assert(type->size != 0 && type->alignment != 0, "");
                        }
                    }
                    break;
                }
                case Symbol_Type::VARIABLE_UNDEFINED: {
                    break;
                }
                default: break;
                }

                if (symbol_read_ready) {
                    dynamic_array_swap_remove(&workload->symbol_dependencies, j);
                    j = j - 1;
                }
            }

            if (workload->symbol_dependencies.size == 0) {
                dynamic_array_swap_remove(&unresolved_symbols_workloads, i);
                i = i - 1; // So that we properly iterate
                if (workload->dependencies.count == 0) {
                    dynamic_array_push_back(&graph->runnable_workloads, workload);
                }
                progress_was_made = true;
            }
        }

        // Execute runnable workloads
        for (int i = 0; i < graph->runnable_workloads.size; i++)
        {
            Analysis_Workload* workload = graph->runnable_workloads[i];
            assert(workload->symbol_dependencies.size == 0, "");
            assert(workload->dependencies.count == 0, "");
            assert(!workload->is_finished, "");
            progress_was_made = true;

            analysis_workload_execute(workload, graph->analyser);
            if (workload->dependencies.count == 0) 
            {
                workload->is_finished = true;
                for (int j = 0; j < workload->dependents.size; j++) {
                    Dependent_Workload* dependent = &workload->dependents[j];
                    assert(workload->dependencies.count != 0, "");
                    list_remove_node(&workload->dependencies, dependent->node);
                    if (dependent->workload->dependencies.count == 0 && dependent->workload->symbol_dependencies.size == 0) {
                        dynamic_array_push_back(&graph->runnable_workloads, dependent->workload);
                    }
                }
                dynamic_array_reset(&workload->dependents);
            }
        }
        dynamic_array_reset(&graph->runnable_workloads);

        if (!progress_was_made)
        {
            // Check if all workloads finished
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

            /*
                At some point I want to resolve circular dependencies by finishing one workload,
                but I won't implement it now so that we may compile again
            */

            // Resolve an unresolved symbol to error to continue analysis
            if (unresolved_symbols_workloads.size == 0) {
                semantic_analyser_log_error(graph->analyser, Semantic_Error_Type::MISSING_FEATURE, (AST_Node*)0);
                break;
            }
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
                lowest->symbol_dependencies[i]->symbol = graph->analyser->compiler->rc_analyser->predefined_symbols.error_symbol;
            }
            dynamic_array_reset(&lowest->symbol_dependencies);
            dynamic_array_push_back(&graph->runnable_workloads, lowest);
        }
    }

}

void analysis_workload_add_dependency(Analysis_Workload* workload, Analysis_Workload* dependency)
{
    if (dependency->is_finished) return;
    Dependent_Workload dependent;
    dependent.node = list_add_at_end(&workload->dependencies, dependency);
    dependent.workload = workload;
    dynamic_array_push_back(&dependency->dependents, dependent);
}

Analysis_Workload* dependency_graph_add_workload_empty(Dependency_Graph* graph, RC_Analysis_Item* item, Analysis_Workload_Type type)
{
    Analysis_Workload* workload = new Analysis_Workload;
    workload->dependencies = list_create<Analysis_Workload*>();
    workload->dependents = dynamic_array_create_empty<Dependent_Workload>(1);
    workload->is_finished = false;
    workload->symbol_dependencies = dynamic_array_create_empty<RC_Symbol_Read*>(1);
    workload->analysis_item = item;
    workload->type = type;
    for (int i = 0; i < item->dependencies_symbols.size; i++) {
        dynamic_array_push_back(&workload->symbol_dependencies, item->dependencies_symbols[i]);
    }
    dynamic_array_push_back(&graph->workloads, workload);

    return workload;
}

Analysis_Workload* dependency_graph_add_workload_from_item(Dependency_Graph* graph, RC_Analysis_Item* item)
{
    // Create workload
    Analysis_Workload* workload = 0;
    switch (item->type)
    {
    case RC_Analysis_Item_Type::ROOT: {
        break;
    }
    case RC_Analysis_Item_Type::DEFINITION:
    {
        Symbol* symbol = item->options.definition.symbol;
        workload = dependency_graph_add_workload_empty(graph, item, Analysis_Workload_Type::DEFINITION);
        hashtable_insert_element(&graph->progress_definitions, symbol, workload);
        break;
    }
    case RC_Analysis_Item_Type::FUNCTION:
    {
        ModTree_Function* function = modtree_function_create_empty(graph->analyser->program, 0, item->options.function.symbol);
        workload = dependency_graph_add_workload_empty(graph, item, Analysis_Workload_Type::FUNCTION_HEADER);
        workload->options.function = function;

        Analysis_Workload* body_workload = dependency_graph_add_workload_from_item(graph, item->options.function.body_item);
        body_workload->options.function = workload->options.function;
        analysis_workload_add_dependency(body_workload, workload);

        Function_Progress initial_state;
        initial_state.state = Function_State::HEADER_WAITING;
        initial_state.body_workload = body_workload;
        initial_state.header_workload = workload;
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
        workload = dependency_graph_add_workload_empty(graph, item, Analysis_Workload_Type::FUNCTION_BODY);
        break;
    }
    case RC_Analysis_Item_Type::STRUCTURE:
    {
        Type_Signature* struct_type = type_system_make_struct_empty(
            &graph->analyser->compiler->type_system, item->options.structure.symbol, item->options.structure.structure_type
        );
        workload = dependency_graph_add_workload_empty(graph, item, Analysis_Workload_Type::STRUCT_ANALYSIS);
        workload->options.struct_type = struct_type;

        Analysis_Workload* size_workload = dependency_graph_add_workload_empty(graph, item, Analysis_Workload_Type::STRUCT_SIZE);
        size_workload->options.struct_type = workload->options.struct_type;
        analysis_workload_add_dependency(size_workload, workload);

        Analysis_Workload* reachable_workload = dependency_graph_add_workload_empty(graph, item, Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE);
        reachable_workload->options.struct_type = workload->options.struct_type;
        reachable_workload->options.reachable_cluster = 0;
        analysis_workload_add_dependency(reachable_workload, size_workload);

        Struct_Progress initial_state;
        initial_state.state = Struct_State::DEFINED;
        initial_state.member_workload = workload;
        initial_state.reachable_resolve_workload = reachable_workload;
        initial_state.size_workload = size_workload;
        hashtable_insert_element(&graph->progress_structs, struct_type, initial_state);

        if (item->options.structure.symbol != 0) {
            Symbol* symbol = item->options.structure.symbol;
            symbol->type = Symbol_Type::TYPE;
            symbol->options.type = struct_type;
        }
        break;
    }
    case RC_Analysis_Item_Type::BAKE: {
        workload = dependency_graph_add_workload_empty(graph, item, Analysis_Workload_Type::BAKE);
        break;
    }
    default: panic("");
    }

    // Create child workloads
    for (int i = 0; i < item->dependencies_items.size; i++) {
        Analysis_Workload* dependency = dependency_graph_add_workload_from_item(graph, item->dependencies_items[i]);
        if (workload != 0) {
            analysis_workload_add_dependency(workload, dependency);
        }
    }
    return workload;
}




void semantic_analyser_work_through_defers(Semantic_Analyser* analyser)
{
    panic("DO SOMETHING HERE");
}

void struct_size_add_member_dependencies(Dependency_Graph* graph, Struct_Progress* struct_progress,
    Type_Signature* member_signature, Hashset<Type_Signature*>* visited, bool size_required)
{
    {
        if (hashset_contains(visited, member_signature)) {
            return;
        }
        hashset_insert_element(visited, member_signature);
    }

    switch (member_signature->type)
    {
    case Signature_Type::STRUCT:
    {
        Struct_Progress* member_progress = hashtable_find_element(&graph->progress_structs, member_signature);
        if (member_progress == 0) {
            assert(member_signature->size != 0 && member_signature->alignment != 0, "Predefined structs (No progress registered) must be finished");
            break;
        }
        if (size_required) {
            analysis_workload_add_dependency(struct_progress->size_workload, member_progress->size_workload);
        }
        else {
            analysis_workload_add_dependency(struct_progress->size_workload, member_progress->member_workload);
        }
        // Reachable resolve
        Analysis_Workload* my_cluster = struct_progress->reachable_resolve_workload;
        while (my_cluster->options.reachable_cluster != 0) {
            my_cluster = my_cluster->options.reachable_cluster;
        }
        Analysis_Workload* member_cluster = member_progress->reachable_resolve_workload;
        while (member_cluster->options.reachable_cluster != 0) {
            member_cluster = member_cluster->options.reachable_cluster;
        }
        if (my_cluster != member_cluster && member_progress->state != Struct_State::FINISHED)
        {
            // Combine workloads
            list_add_list(&my_cluster->dependencies, &member_cluster->dependencies);
            for (int i = 0; i < member_cluster->dependents.size; i++) {
                member_cluster->dependents[i].workload = my_cluster;
            }
            analysis_workload_add_dependency(member_cluster, my_cluster);
            member_cluster->options.reachable_cluster = my_cluster;
        }
        break;
    }
    case Signature_Type::ARRAY:
    {
        struct_size_add_member_dependencies(graph, struct_progress, member_signature->options.array.element_type, visited, size_required);
        break;
    }
    case Signature_Type::POINTER: {
        struct_size_add_member_dependencies(graph, struct_progress, member_signature->options.array.element_type, visited, false);
        break;
    }
    case Signature_Type::SLICE: {
        struct_size_add_member_dependencies(graph, struct_progress, member_signature->options.array.element_type, visited, false);
        break;
    }
    case Signature_Type::FUNCTION: {
        for (int i = 0; i < member_signature->options.function.parameter_types.size; i++) {
            Type_Signature* param_type = member_signature->options.function.parameter_types[i];
            struct_size_add_member_dependencies(graph, struct_progress, param_type, visited, false);
        }
        struct_size_add_member_dependencies(graph, struct_progress, member_signature->options.function.return_type, visited, false);
        break;
    }
    default: break; // We can ignore others
    }
}

bool analysis_workload_execute(Analysis_Workload* workload, Semantic_Analyser* analyser)
{
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
                    analyser, definition->value_expression.value, type == 0 ? expression_context_make_known_type(type, true) : expression_context_make_unknown()
                );
                if (type != 0)
                {
                    if (value->result_type != type)
                    {
                        ModTree_Expression* casted = semantic_analyser_cast_implicit_if_possible(analyser, value, type);
                        if (casted == 0) {
                            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT, definition->value_expression.value);
                            semantic_analyser_add_error_info(analyser, error_information_make_given_type(value->result_type));
                            semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type));
                        }
                        else {
                            value = casted;
                        }
                    }
                }
                else {
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
                Comptime_Analysis comptime = modtree_expression_calculate_comptime_value(analyser, result.options.expression);
                if (!comptime.available) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN, definition->type_expression.value);
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    break;
                }
                Constant_Result result = constant_pool_add_constant(
                    &analyser->compiler->constant_pool, comptime.type, array_create_static((byte*)comptime.data, comptime.type->size)
                );
                if (result.status != Constant_Status::SUCCESS) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::CONSTANT_POOL_ERROR, definition->type_expression.value);
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
        ModTree_Function* function = workload->options.function;

        Type_Signature* signature = semantic_analyser_analyse_expression_type(analyser, header->signature_expression);
        assert(signature->type == Signature_Type::FUNCTION, "");
        assert(header->parameter_symbols.size == signature->options.function.parameter_types.size, "");
        for (int i = 0; i < header->parameter_symbols.size; i++) {
            ModTree_Variable* param = modtree_function_add_parameter(function, signature->options.function.parameter_types[i], header->parameter_symbols[i]);
            header->parameter_symbols[i]->type = Symbol_Type::VARIABLE;
            header->parameter_symbols[i]->options.variable = param;
        }

        Function_Progress* progress = hashtable_find_element(&analyser->dependency_graph.progress_functions, function);
        assert(progress != 0, "");
        progress->state = Function_State::BODY_WAITING;
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY:
    {
        auto rc_body_block = workload->analysis_item->options.function_body;
        ModTree_Function* function = workload->options.function;
        Control_Flow flow = semantic_analyser_fill_block(analyser, function->body, rc_body_block);
        if (flow == Control_Flow::NO_RETURN) {
            if (function->signature->options.function.return_type == analyser->compiler->type_system.void_type) {
                semantic_analyser_work_through_defers(analyser);
                ModTree_Statement* return_statement = modtree_block_add_statement_empty(function->body, ModTree_Statement_Type::RETURN);
            }
            else {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, (AST_Node*)0);
            }
        }
        break;
    }
    case Analysis_Workload_Type::STRUCT_ANALYSIS:
    {
        auto rc_struct = &workload->analysis_item->options.structure;
        Type_Signature* struct_signature = workload->options.struct_type;
        Struct_Progress* progress = hashtable_find_element(&analyser->dependency_graph.progress_structs, struct_signature);
        assert(progress != 0, "");
        Hashset<Type_Signature*> visited_members = hashset_create_pointer_empty<Type_Signature*>(4);
        SCOPE_EXIT(hashset_destroy(&visited_members));

        for (int i = 0; i < rc_struct->members.size; i++) {
            RC_Struct_Member* rc_member = &rc_struct->members[i];
            Struct_Member member;
            member.id = rc_member->id;
            member.offset = 0;
            member.type = semantic_analyser_analyse_expression_type(analyser, rc_member->type_expression);
            dynamic_array_push_back(&struct_signature->options.structure.members, member);
            struct_size_add_member_dependencies(&analyser->dependency_graph, progress, member.type, &visited_members, true);
        }
        progress->state = Struct_State::MEMBERS_ANALYSED;
        break;
    }
    case Analysis_Workload_Type::STRUCT_SIZE:
    {
        Type_Signature* struct_signature = workload->options.struct_type;
        Struct_Progress* progress = hashtable_find_element(&analyser->dependency_graph.progress_structs, struct_signature);
        assert(progress != 0, "");
        type_system_finish_type(&analyser->compiler->type_system, struct_signature);
        progress->state = Struct_State::SIZE_KNOWN;
        break;
    }
    case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
    {
        // This does not need to do anything
        Type_Signature* struct_signature = workload->options.struct_type;
        Struct_Progress* progress = hashtable_find_element(&analyser->dependency_graph.progress_structs, struct_signature);
        assert(progress != 0, "");
        progress->state = Struct_State::FINISHED;
        break;
    }
    case Analysis_Workload_Type::BAKE:
    {
        panic("NOT IMPLEMENTED YET");
        // THIS IS QUEUE FUNCTIONS
    /*
    if (hashtable_find_element(&analyser->compiler->ir_generator->function_mapping, function) != 0 || hashset_contains(&analyser->visited_functions, function)) {
    }

    ir_generator_queue_function(&analyser->compiler->ir_generator, function);
    hashset_insert_element(&analyser->visited_functions, function);

    for (int i = 0; i < function->options.function.dependency_globals.size; i++)
    {
        ModTree_Variable* global = function->options.function.dependency_globals[i];
        if (global != analyser->global_type_informations) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS;
            error.function = function;
            error.error_node = bake_node;
            semantic_analyser_log_error(analyser, error);

            Partial_Compile_Result result;
            result.type = Analysis_Result_Type::ERROR_OCCURED;
            return result;
        }
    }
    ir_generator_queue_global(&analyser->compiler->ir_generator, analyser->global_type_informations);

    for (int i = 0; i < function->options.function.dependency_functions.size; i++)
    {
        ModTree_Function* dependent_fn = function->options.function.dependency_functions[i];
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
    */

    // Other stuff
// TODO: This needs to be done with error flags of modtree functions
/*
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
*/

/* Executing bake

assert(analyser->current_workload->type == Analysis_Workload_Type::CODE, "");
Analysis_Workload_Code* work = &analyser->current_workload->options.code_block;
// Check if we have already created the function
Expression_Location location = expression_location_make(analyser, expression_node);
Cached_Expression* cached_expr = hashtable_find_element(&analyser->cached_expressions, location);
if (cached_expr != 0)
{
    ModTree_Function* function = cached_expr->bake_function;
    Partial_Compile_Result comp_result = partial_compilation_compile_function_for_bake(analyser, function, expression_node);
    switch (comp_result.type)
    {
    case Analysis_Result_Type::SUCCESS:
    {
        // Update type information table
        {
            bytecode_interpreter_prepare_run(&analyser->compiler->bytecode_interpreter);
            IR_Data_Access* global_access = hashtable_find_element(&analyser->compiler->ir_generator.variable_mapping, analyser->global_type_informations);
            assert(global_access != 0 && global_access->type == IR_Data_Access_Type::GLOBAL_DATA, "");
            Upp_Slice<Internal_Type_Information>* info_slice = (Upp_Slice<Internal_Type_Information>*)
                & analyser->compiler->bytecode_interpreter.globals.data[
                    analyser->compiler->bytecode_generator.global_data_offsets[global_access->index]
                ];
            info_slice->size = analyser->compiler->type_system.internal_type_infos.size;
            info_slice->data_ptr = analyser->compiler->type_system.internal_type_infos.data;
        }

        IR_Function* ir_func = *hashtable_find_element(&analyser->compiler->ir_generator.function_mapping, function);
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
            return expression_result_make_error();
        }

        void* value_ptr = analyser->compiler->bytecode_interpreter.return_register;
        Type_Signature* result_type = function->signature->options.function.return_type;
        ModTree_Expression* expression = modtree_expression_create_constant(
            analyser, result_type, array_create_static<byte>((byte*)value_ptr, (u64)result_type->size), expression_node
        );
        return expression_result_make_value(expression);
    }
    case Analysis_Result_Type::ERROR_OCCURED:
        return expression_result_make_error();
    case Analysis_Result_Type::DEPENDENCY:
        return expression_result_make_dependency(comp_result.dependency);
    default: panic("");
    }

    panic("");
    return expression_result_make_error();
}
else
{
    Expression_Result_Type type_result = semantic_analyser_analyse_expression_type(analyser, symbol_table, expression_node->child_start);
    if (type_result.type != Analysis_Result_Type::SUCCESS) return expression_result_make_from(type_result);

    // Create function and analyse it
    ModTree_Function* function = modtree_function_make_empty(
        analyser, work->function->parent_module, work->function->parent_module->symbol_table,
        type_system_make_function(type_system, dynamic_array_create_empty<Type_Signature*>(1), type_result.options.type),
        0, expression_node
    );
    dynamic_array_push_back(&analyser->active_workloads, analysis_workload_make_code_block(function->options.function.body, expression_node->child_end));

    Cached_Expression cached;
    cached.bake_function = function;
    hashtable_insert_element(&analyser->cached_expressions, location, cached);

    return expression_result_make_dependency(workload_dependency_make_code_block_finished(function->options.function.body, expression_node));
}
panic("Should not happen");
return expression_result_make_error();
*/
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




/*
    SEMANTIC ANALYSER
*/
// ANALYSER FUNCTIONS
Optional<ModTree_Cast_Type> semantic_analyser_check_if_cast_possible(
    Semantic_Analyser* analyser, Type_Signature* source_type, Type_Signature* destination_type, bool implicit_cast)
{
    Type_System* type_system = &analyser->compiler->type_system;
    if (source_type == type_system->error_type || destination_type == type_system->error_type) {
        semantic_analyser_set_error_flag(analyser);
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
        return new ModTree_Expression(cast_expr);
    }

    return 0;
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
            analyser, rc_expression->options.function_call.call_expr, expression_context_make_type(Expression_Context_Type::FUNCTION_CALL)
        );
        SCOPE_EXIT(if (error_exit && function_expr_result.type == Expression_Result_Type::EXPRESSION)
            modtree_expression_destroy(function_expr_result.options.expression);
        );

        ModTree_Expression* expr_result = modtree_expression_create_empty(ModTree_Expression_Type::FUNCTION_CALL, 0);
        Type_Signature* function_signature = 0;
        switch (function_expr_result.type)
        {
        case Expression_Result_Type::FUNCTION: {
            expr_result->options.function_call.call_type = ModTree_Call_Type::FUNCTION;
            expr_result->options.function_call.options.function = function_expr_result.options.function;
            function_signature = function_expr_result.options.function->signature;
            break;
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
                return expression_result_make_value(modtree_expression_make_error(type_system->error_type));
            }
            break;
        }
        case Expression_Result_Type::MODULE:
        case Expression_Result_Type::TYPE: {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPECTED_CALLABLE, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_expression_result_type(function_expr_result.type));
            error_exit = true;
            return expression_result_make_value(modtree_expression_make_error(type_system->error_type));
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
            Type_Signature* expected_type = i < parameters.size ? parameters[i] : type_system->error_type;
            ModTree_Expression* argument_expr = semantic_analyser_analyse_expression_value(
                analyser, arguments[i], expression_context_make_known_type(expected_type, true)
            );
            // Cast if necessary
            {
                ModTree_Expression* casted = semantic_analyser_cast_implicit_if_possible(analyser, argument_expr, expected_type);
                if (casted != 0) {
                    argument_expr = casted;
                }
                else {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ARGUMENT, arguments[i]);
                    semantic_analyser_add_error_info(analyser, error_information_make_expected_type(expected_type));
                    semantic_analyser_add_error_info(analyser, error_information_make_given_type(argument_expr->result_type));
                    semantic_analyser_add_error_info(analyser, error_information_make_function_type(function_signature));
                }
            }
            dynamic_array_push_back(&expr_result->options.function_call.arguments, argument_expr);
            break;
        }
        return expression_result_make_value(expr_result);
    }
    case RC_Expression_Type::TYPE_INFO:
    {
        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
            analyser, rc_expression->options.type_info_expression, expression_context_make_known_type(type_system->type_type, true)
        );
        if (operand->result_type != analyser->compiler->type_system.type_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ARGUMENT, rc_expression->options.type_info_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type_system->type_type));
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(operand->result_type));
            modtree_expression_destroy(operand);
            return expression_result_make_value(modtree_expression_make_error(type_system->type_information_type));
        }

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
            return expression_result_make_value(modtree_expression_make_error(type_system->error_type));
        }
        default: panic("");
        }

        panic("");
        return expression_result_make_value(modtree_expression_make_error(type_system->error_type));
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
            return expression_result_make_error(type_system->error_type);
        }
        case Symbol_Type::UNRESOLVED: {
            panic("Should not happen");
        }
        case Symbol_Type::VARIABLE_UNDEFINED: {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET, rc_expression);
            return expression_result_make_error(type_system->error_type);
        }
        case Symbol_Type::EXTERN_FUNCTION: {
            return expression_result_make_extern_function(symbol->options.extern_function);
        }
        case Symbol_Type::HARDCODED_FUNCTION: {
            return expression_result_make_hardcoded(symbol->options.hardcoded_function);
        }
        case Symbol_Type::FUNCTION: {
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
            return expression_result_make_error(type_system->error_type);
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

        Expression_Context context = expression_context_make_unknown();
        switch (cast->type)
        {
        case RC_Cast_Type::TYPE_TO_TYPE: {
            assert(destination_type != 0, "");
            break;
        }
        case RC_Cast_Type::AUTO_CAST: {
            assert(destination_type == 0, "");
            if (context.type != Expression_Context_Type::TYPE_KNOWN) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED, rc_expression);
                return expression_result_make_error(type_system->error_type);
            }
            destination_type = context.signature;
            break;
        }
        case RC_Cast_Type::RAW_TO_PTR: {
            context = expression_context_make_known_type(type_system->u64_type, true);
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

        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(analyser, cast->operand, context);
        ModTree_Expression* result_expr = modtree_expression_create_empty(ModTree_Expression_Type::CAST, destination_type);
        result_expr->options.cast.cast_argument = operand;
        result_expr->options.cast.type = ModTree_Cast_Type::INTEGERS; // Placeholder

        switch (cast->type)
        {
        case RC_Cast_Type::AUTO_CAST:
        case RC_Cast_Type::PTR_TO_RAW:
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
        case RC_Cast_Type::RAW_TO_PTR:
        {
            result_expr->options.cast.type = ModTree_Cast_Type::U64_TO_POINTER;
            if (operand->result_type != analyser->compiler->type_system.u64_type) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_CAST_PTR_REQUIRES_U64, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(operand->result_type));
            }
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
                    analyser, rc_member->value_expression.value, expression_context_make_known_type(type_system->i32_type, true)
                );
                if (expression->result_type != analyser->compiler->type_system.i32_type) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ENUM_VALUE, rc_member->value_expression.value);
                    semantic_analyser_add_error_info(analyser, error_information_make_given_type(expression->result_type));
                    semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type_system->i32_type));
                }
                else {
                    Comptime_Analysis comptime = modtree_expression_calculate_comptime_value(analyser, expression);
                    if (!comptime.available) {
                        semantic_analyser_log_error(analyser, Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN, rc_member->value_expression.value);
                    }
                    else {
                        next_member_value = *(i32*)comptime.data;
                    }
                }
            }

            Enum_Member member;
            member.definition_node = rc_to_ast(analyser, rc_member->value_expression.value);
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
        panic("FUCK");
        /*
        auto rc_struct = &rc_expression->options.structure;
        Type_Signature* struct_type = type_system_make_struct_empty(type_system, rc_to_ast(analyser, rc_expression));
        for (int i = 0; i < rc_struct->members.size; i++)
        {
            RC_Struct_Member* rc_member = &rc_struct->members[i];
            for (int j = 0; j < struct_type->options.structure.members.size; j++) {
                if (struct_type->options.structure.members[j].id == rc_member->id) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED, rc_member->type_expression);
                    semantic_analyser_add_error_info(analyser, error_information_make_id(rc_member->id));
                }
            }

            Struct_Member member;
            member.type = semantic_analyser_analyse_expression_type(analyser, rc_member->type_expression);
            member.id = rc_member->id;
            member.offset = -1;
            dynamic_array_push_back(&struct_type->options.structure.members, member);
        }

        // I dont know what to do here exactly, buts its not this, since this requires dependencies to be finished
        //type_system_finish_type(&analyser->compiler->type_system, struct_type);
        return expression_result_make_type(struct_type);
        */
    }
    case RC_Expression_Type::FUNCTION_SIGNATURE:
    {
        auto rc_sig = &rc_expression->options.function_signature;
        Dynamic_Array<Type_Signature*> parameters = dynamic_array_create_empty<Type_Signature*>(math_maximum(0, rc_sig->parameters.size));
        for (int i = 0; i < rc_sig->parameters.size; i++) {
            dynamic_array_push_back(&parameters, semantic_analyser_analyse_expression_type(analyser, rc_sig->parameters[i].type_expression));
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
            analyser, rc_array->size_expression, expression_context_make_known_type(type_system->i32_type, true)
        );
        if (size_expr->result_type != type_system->i32_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE, rc_array->size_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(size_expr->result_type));
            semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type_system->i32_type));
            modtree_expression_destroy(size_expr);
            return expression_result_make_error(type_system->error_type);
        }
        Comptime_Analysis comptime = modtree_expression_calculate_comptime_value(analyser, size_expr);
        if (!comptime.available) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN, rc_array->size_expression);
            modtree_expression_destroy(size_expr);
            return expression_result_make_error(type_system->error_type);
        }
        int array_size = *(i32*)comptime.data;
        if (array_size <= 0) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::ARRAY_SIZE_MUST_BE_GREATER_ZERO, rc_array->size_expression);
            modtree_expression_destroy(size_expr);
            return expression_result_make_error(type_system->error_type);
        }

        Type_Signature* element_type = semantic_analyser_analyse_expression_type(analyser, rc_array->element_type_expression);
        if (element_type->size == 0 && element_type->alignment == 0) {
            // TODO: Theres probably something we should do in this case, but well see after struct analysis works
        }
        Type_Signature* array_type = type_system_make_array_finished(type_system, element_type, array_size);
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
                analyser, rc_new->count_expression.value, expression_context_make_known_type(type_system->i32_type, true)
            );
            if (count->result_type != type_system->i32_type) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE, rc_new->count_expression.value);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(count->result_type));
                semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type_system->i32_type));
            }
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
            if (context.type == Expression_Context_Type::TYPE_KNOWN) {
                struct_signature = context.signature;
            }
            else {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE, rc_expression);
                return expression_result_make_error(struct_signature);
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
            Struct_Member* found_member = 0;
            for (int i = 0; i < struct_signature->options.structure.members.size; i++) {
                if (struct_signature->options.structure.members[i].id == rc_member->member_id) {
                    found_member = &struct_signature->options.structure.members[i];
                }
            }
            if (found_member != 0)
            {
                ModTree_Expression* init_expr = semantic_analyser_analyse_expression_value(
                    analyser, rc_member->init_expression, expression_context_make_known_type(found_member->type, true)
                );
                Member_Initializer initializer;
                initializer.init_expr = init_expr;
                initializer.init_node = rc_to_ast(analyser, rc_member->init_expression);
                initializer.member = *found_member;
                if (init_expr->result_type != found_member->type)
                {
                    ModTree_Expression* cast_expr = semantic_analyser_cast_implicit_if_possible(analyser, init_expr, found_member->type);
                    if (cast_expr != 0) {
                        initializer.init_expr = cast_expr;
                    }
                    else {
                        semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_INVALID_MEMBER_TYPE, rc_member->init_expression);
                        semantic_analyser_add_error_info(analyser, error_information_make_given_type(init_expr->result_type));
                        semantic_analyser_add_error_info(analyser, error_information_make_expected_type(found_member->type));
                    }
                }
                dynamic_array_push_back(&initializers, initializer);
            }
            else {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST, rc_member->init_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_id(rc_member->member_id));
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
            if (context.type == Expression_Context_Type::TYPE_KNOWN)
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
                return expression_result_make_error(type_system->error_type);
            }
        }

        if (element_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, rc_expression);
            return expression_result_make_error(type_system->error_type);
        }
        assert(element_type->size != 0 && element_type->alignment != 0, "");

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
                analyser, rc_element_expr, expression_context_make_known_type(element_type, true)
            );
            if (element_expr->result_type != element_type)
            {
                ModTree_Expression* casted = semantic_analyser_cast_implicit_if_possible(analyser, element_expr, element_type);
                if (casted == 0) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::ARRAY_INITIALIZER_INVALID_TYPE, rc_element_expr);
                    semantic_analyser_add_error_info(analyser, error_information_make_expected_type(element_type));
                    semantic_analyser_add_error_info(analyser, error_information_make_given_type(element_expr->result_type));
                }
                else {
                    element_expr = casted;
                }
            }
            dynamic_array_push_back(&init_expressions, element_expr);
        }

        ModTree_Expression* result = modtree_expression_create_empty(
            ModTree_Expression_Type::ARRAY_INITIALIZER, type_system_make_array_finished(&analyser->compiler->type_system, element_type, array_element_count)
        );
        result->options.array_initializer = init_expressions;
        return expression_result_make_value(result);
    }
    case RC_Expression_Type::ARRAY_ACCESS:
    {
        auto rc_array_access = &rc_expression->options.array_access;
        ModTree_Expression* array_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_array_access->array_expression, expression_context_make_type(Expression_Context_Type::ARRAY)
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
            element_type = type_system->error_type;
        }

        ModTree_Expression* index_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_array_access->index_expression, expression_context_make_known_type(type_system->i32_type, true)
        );
        if (index_expr->result_type != analyser->compiler->type_system.i32_type) { // Todo: Try cast
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX, rc_array_access->index_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(index_expr->result_type));
            semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type_system->i32_type));
        }

        ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::ARRAY_ACCESS, element_type);
        result->options.array_access.array_expression = array_expr;
        result->options.array_access.index_expression = index_expr;
        return expression_result_make_value(result);
    }
    case RC_Expression_Type::MEMBER_ACCESS:
    {
        auto rc_member_access = &rc_expression->options.member_access;
        Expression_Result access_expr_result = semantic_analyser_analyse_expression_any(
            analyser, rc_member_access->expression, expression_context_make_type(Expression_Context_Type::MEMBER_ACCESS)
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
                return expression_result_make_error(type_system->error_type);
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
            return expression_result_make_error(type_system->error_type);
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
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, rc_member_access->expression);
                    semantic_analyser_add_error_info(analyser, error_information_make_id(rc_member_access->member_name));
                    error_exit = true;
                    return expression_result_make_error(type_system->error_type);
                }

                ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::MEMBER_ACCESS, found->type);
                result->options.member_access.member = *found;
                result->options.member_access.structure_expression = access_expr;
                return expression_result_make_value(result);
            }
            else if (struct_signature->type == Signature_Type::ARRAY || struct_signature->type == Signature_Type::SLICE)
            {
                if (rc_member_access->member_name != analyser->id_size && rc_member_access->member_name != analyser->id_data) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, rc_member_access->expression);
                    semantic_analyser_add_error_info(analyser, error_information_make_id(rc_member_access->member_name));
                    error_exit = true;
                    return expression_result_make_error(type_system->error_type);
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
                return expression_result_make_error(type_system->error_type);
            }
            panic("");
            break;
        }
        default: panic("");
        }
        panic("Should not happen");
        return expression_result_make_error(type_system->error_type);
    }
    case RC_Expression_Type::AUTO_ENUM:
    {
        String* id= rc_expression->options.auto_enum_member_id;
        if (context.type != Expression_Context_Type::TYPE_KNOWN) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED, rc_expression);
            return expression_result_make_error(type_system->error_type);
        }
        if (context.signature->type != Signature_Type::ENUM) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT, rc_expression);
            return expression_result_make_error(type_system->error_type);
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
            is_negate ? expression_context_make_type(Expression_Context_Type::ARITHMETIC_OPERAND) : expression_context_make_known_type(type_system->bool_type, true) 
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
        else {
            if (operand->result_type != type_system->bool_type) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, rc_expression);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(operand->result_type));
                semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type_system->bool_type));
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
                return expression_result_make_error(type_system->error_type);
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
            return expression_result_make_error(type_system->error_type);
        }
        default: panic("");
        }
        panic("");
        break;
    }
    case RC_Expression_Type::DEREFERENCE:
    {
        ModTree_Expression* operand = semantic_analyser_analyse_expression_value(
            analyser, rc_expression->options.dereference_expression, expression_context_make_unknown()
        );
        Type_Signature* result_type = type_system->error_type;
        if (operand->result_type->type != Signature_Type::POINTER || operand->result_type == type_system->void_ptr_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, rc_expression->options.dereference_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(operand->result_type));
        }
        else {
            result_type = operand->result_type->options.pointer_child;
        }

        ModTree_Expression* result = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, result_type);
        result->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::DEREFERENCE;
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
            operand_context = expression_context_make_type(Expression_Context_Type::ARITHMETIC_OPERAND);
            break;
        case RC_Binary_Operation_Type::MODULO:
            int_valid = true;
            operand_context = expression_context_make_type(Expression_Context_Type::ARITHMETIC_OPERAND);
            break;
        case RC_Binary_Operation_Type::GREATER:
        case RC_Binary_Operation_Type::GREATER_OR_EQUAL:
        case RC_Binary_Operation_Type::LESS:
        case RC_Binary_Operation_Type::LESS_OR_EQUAL:
            float_valid = true;
            int_valid = true;
            result_type_is_bool = true;
            enum_valid = true;
            operand_context = expression_context_make_type(Expression_Context_Type::ARITHMETIC_OPERAND);
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
            operand_context = expression_context_make_type(Expression_Context_Type::ARITHMETIC_OPERAND);
            result_type_is_bool = true;
            break;
        case RC_Binary_Operation_Type::AND:
        case RC_Binary_Operation_Type::OR:
            bool_valid = true;
            result_type_is_bool = true;
            operand_context = expression_context_make_known_type(type_system->bool_type, true);
            break;
        default: panic("");
        }

        // Evaluate operands
        ModTree_Expression* left_expr = semantic_analyser_analyse_expression_value(analyser, rc_binop->left_operand, operand_context);
        if (enum_valid && left_expr->result_type->type == Signature_Type::ENUM) {
            operand_context = expression_context_make_known_type(left_expr->result_type, true);
        }
        ModTree_Expression* right_expr = semantic_analyser_analyse_expression_value(analyser, rc_binop->right_operand, operand_context);

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
        if (types_are_valid)
        {
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
    return expression_result_make_value(modtree_expression_make_error(type_system->error_type));
}

Expression_Result semantic_analyser_analyse_expression_any(Semantic_Analyser* analyser, RC_Expression* rc_expression, Expression_Context context)
{
    Expression_Result result = semantic_analyser_analyse_expression_internal(analyser, rc_expression, context);
    if (result.type != Expression_Result_Type::EXPRESSION) return result;

    // Constant analysis + Context Conversions
    ModTree_Expression* expr = result.options.expression;
    if (expr->result_type == analyser->compiler->type_system.error_type) {
        return result;
    }

    // Context conversions from pointer into something else
    if (expr->result_type->type == Signature_Type::POINTER)
    {
        Type_Signature* given_pointer_type = expr->result_type;
        Type_Signature* underlying_type = given_pointer_type;
        int pointer_depth = 0;
        while (underlying_type->type == Signature_Type::POINTER) {
            pointer_depth++;
            underlying_type = underlying_type->options.pointer_child;
        }

        int wanted_pointer_depth = pointer_depth;
        switch (context.type)
        {
        case Expression_Context_Type::UNKNOWN: break;
        case Expression_Context_Type::FUNCTION_CALL: wanted_pointer_depth = 0; break; // This is for function pointer pointers
        case Expression_Context_Type::TYPE_KNOWN:
        {
            if (context.enable_pointer_conversion)
            {
                Type_Signature* temp = context.signature;
                wanted_pointer_depth = 0;
                while (temp->type == Signature_Type::POINTER) {
                    wanted_pointer_depth++;
                    temp = temp->options.pointer_child;
                }
                if (temp != underlying_type) { // Dont dereference if underlying type does not match
                    wanted_pointer_depth = pointer_depth;
                }
            }
            break;
        }
        case Expression_Context_Type::TYPE_EXPECTED: {
            wanted_pointer_depth = 0;
            break;
        }
        case Expression_Context_Type::ARITHMETIC_OPERAND: {
            // When we have operator overloading, this needs to change
            wanted_pointer_depth = 0;
            break;
        }
        case Expression_Context_Type::MEMBER_ACCESS: {
            if (underlying_type->type == Signature_Type::ARRAY ||
                underlying_type->type == Signature_Type::STRUCT ||
                underlying_type->type == Signature_Type::SLICE) {
                wanted_pointer_depth = 0;
            }
            break;
        }
        case Expression_Context_Type::ARRAY: {
            if (underlying_type->type == Signature_Type::STRUCT ||
                underlying_type->type == Signature_Type::SLICE) {
                wanted_pointer_depth = 0;
            }
            break;
        }
        case Expression_Context_Type::SWITCH_CONDITION: {
            if (underlying_type->type == Signature_Type::ENUM ||
                (underlying_type->type == Signature_Type::STRUCT && underlying_type->options.structure.struct_type == Structure_Type::UNION))
            {
                wanted_pointer_depth = 0;
            }
            break;
        }
        default: panic("");
        }

        while (pointer_depth > wanted_pointer_depth)
        {
            ModTree_Expression* deref = new ModTree_Expression;
            deref->expression_type = ModTree_Expression_Type::UNARY_OPERATION;
            deref->result_type = expr->result_type->options.pointer_child;
            deref->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::DEREFERENCE;
            deref->options.unary_operation.operand = expr;
            expr = deref;
            pointer_depth--;
        }
    }

    // Context conversions Address-Of
    if (context.type == Expression_Context_Type::TYPE_KNOWN && context.enable_pointer_conversion)
    {
        Type_Signature* type_current = expr->result_type;
        Type_Signature* type_wanted = context.signature;

        // Do implict address of
        if (type_wanted->type == Signature_Type::POINTER && type_wanted->options.pointer_child == type_current)
        {
            if (!modtree_expression_result_is_temporary(expr))
            {
                ModTree_Expression* addr_of = new ModTree_Expression;
                addr_of->expression_type = ModTree_Expression_Type::UNARY_OPERATION;
                addr_of->result_type = type_wanted;
                addr_of->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::ADDRESS_OF;
                addr_of->options.unary_operation.operand = expr;
                expr = addr_of;
            }
        }
    }

    result.options.expression = expr;
    return result;
}

Type_Signature* semantic_analyser_analyse_expression_type(Semantic_Analyser* analyser, RC_Expression* rc_expression)
{
    Expression_Result result = semantic_analyser_analyse_expression_any(
        analyser, rc_expression, expression_context_make_type(Expression_Context_Type::TYPE_EXPECTED)
    );
    switch (result.type)
    {
    case Expression_Result_Type::TYPE:
        return result.options.type;
    case Expression_Result_Type::EXPRESSION:
    {
        ModTree_Expression* expression = result.options.expression;
        SCOPE_EXIT(modtree_expression_destroy(expression));

        if (expression->result_type != analyser->compiler->type_system.type_type)
        {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE, rc_expression);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(expression->result_type));
            return analyser->compiler->type_system.error_type;
        }
        Comptime_Analysis comptime = modtree_expression_calculate_comptime_value(analyser, expression);
        if (!comptime.available) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME, rc_expression);
            return analyser->compiler->type_system.error_type;
        }
        u64 type_index = *(u64*)comptime.data;
        if (type_index >= analyser->compiler->type_system.internal_type_infos.size) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, rc_expression);
            return analyser->compiler->type_system.error_type;
        }

        return analyser->compiler->type_system.types[type_index];
    }
    case Expression_Result_Type::MODULE:
    case Expression_Result_Type::EXTERN_FUNCTION:
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::FUNCTION: {
        semantic_analyser_log_error(analyser, Semantic_Error_Type::EXPECTED_TYPE, rc_expression);
        semantic_analyser_add_error_info(analyser, error_information_make_expression_result_type(result.type));
        return analyser->compiler->type_system.error_type;
    }
    default: panic("");
    }
    panic("");
    return analyser->compiler->type_system.error_type;
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
        return modtree_expression_make_error(analyser->compiler->type_system.error_type);
    }
    default: panic("");
    }
    panic("");
    return modtree_expression_make_error(analyser->compiler->type_system.error_type);
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

Control_Flow semantic_analyser_analyse_statement(Semantic_Analyser* analyser, RC_Statement* rc_statement, ModTree_Block* block)
{
    Type_System* type_system = &analyser->compiler->type_system;
    switch (rc_statement->type)
    {
    case RC_Statement_Type::RETURN_STATEMENT:
    {
        auto rc_return = &rc_statement->options.return_statement;
        ModTree_Statement* return_statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::RETURN);

        Type_Signature* expected_return_type = analyser->current_function->signature->options.function.return_type;
        Type_Signature* return_type = 0;
        if (rc_return->available) 
        {
            ModTree_Expression* return_expr = semantic_analyser_analyse_expression_value(
                analyser, rc_return->value, expression_context_make_known_type(expected_return_type, true)
            );
            return_type = return_expr->result_type;
            return_statement->options.return_value.available = true;
            return_statement->options.return_value.value = return_expr;
        }
        else {
            return_type = type_system->void_type;
            return_statement->options.return_value.available = false;
        }

        if (return_type != expected_return_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_RETURN, rc_statement);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(return_type));
            semantic_analyser_add_error_info(analyser, error_information_make_expected_type(expected_return_type));
        }
        return Control_Flow::RETURN;

        // Check if inside defer
        /*
        if (inside_defer(code_workload))
        {
            Semantic_Error error;
            error.type = Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED;
            error.error_node = statement_node;
            semantic_analyser_log_error(analyser, error);
            break;
        }

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
            read_expr->result_type = variable->data_type;
            read_expr->expression_type = ModTree_Expression_Type::VARIABLE_READ;
            read_expr->options.variable_read = variable;
            return_statement.options.return_value.value = read_expr;
        }
        workload_code_block_work_through_defers(analyser, code_workload, 0);
        dynamic_array_push_back(&block->statements, new ModTree_Statement(return_statement));
        */
    }
    case RC_Statement_Type::BREAK_STATEMENT:
    case RC_Statement_Type::CONTINUE_STATEMENT:
    {
        semantic_analyser_log_error(analyser, Semantic_Error_Type::MISSING_FEATURE, rc_statement);
        return Control_Flow::NO_RETURN;
        /*
        bool is_continue = statement_node->type == AST_Node_Type::STATEMENT_CONTINUE;
        ModTree_Block* break_block = 0;
        List_Node<Block_Analysis>* node = code_workload->block_queue.tail;
        while (node != 0)
        {
            if (node->value.block->type == ModTree_Block_Type::DEFER_BLOCK) {
                break;
            }
            if (statement_node->id != 0) {
                if (node->value.block_node->id == statement_node->id) {
                    break_block = node->value.block;
                    break;
                }
            }
            else {
                if (node->value.block->type == ModTree_Block_Type::SWITCH_CASE ||
                    node->value.block->type == ModTree_Block_Type::SWITCH_DEFAULT_CASE ||
                    node->value.block->type == ModTree_Block_Type::WHILE_BODY) {
                    break_block = node->value.block;
                    break;
                }
            }
            node = node->prev;
        }

        if (break_block == 0)
        {
            Semantic_Error error;
            if (is_continue) {
                error.type = statement_node->id == 0 ?
                    Semantic_Error_Type::CONTINUE_NOT_INSIDE_LOOP : Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND;
            }
            else {
                error.type = statement_node->id == 0 ?
                    Semantic_Error_Type::BREAK_NOT_INSIDE_LOOP_OR_SWITCH : Semantic_Error_Type::BREAK_LABLE_NOT_FOUND;
            }
            error.error_node = statement_node;
            semantic_analyser_log_error(analyser, error);
            break;
        }
        else if (is_continue && break_block->type != ModTree_Block_Type::WHILE_BODY) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK;
            error.error_node = statement_node;
            semantic_analyser_log_error(analyser, error);
            break;
        }

        workload_code_block_work_through_defers(analyser, code_workload, node->value.defer_count_block_start);

        ModTree_Statement* stmt = new ModTree_Statement;
        if (is_continue) {
            stmt->type = ModTree_Statement_Type::CONTINUE;
            stmt->options.continue_to_block = break_block;
            code_workload->active_block->block_flow = Control_Flow::CONTINUE;
        }
        else {
            stmt->type = ModTree_Statement_Type::BREAK;
            stmt->options.break_to_block = break_block;
            code_workload->active_block->block_flow = Control_Flow::BREAK;
        }
        dynamic_array_push_back(&block->statements, stmt);
        break;
        */
    }
    case RC_Statement_Type::DEFER:
    {
        semantic_analyser_log_error(analyser, Semantic_Error_Type::MISSING_FEATURE, rc_statement);
        return Control_Flow::NO_RETURN;
        /*
        if (inside_defer(code_workload)) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS;
            error.error_node = statement_node;
            semantic_analyser_log_error(analyser, error);
        }
        else {
            dynamic_array_push_back(&code_workload->defer_nodes, statement_node->child_start);
        }
        break;
        */
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
        return Control_Flow::NO_RETURN;
    }
    case RC_Statement_Type::STATEMENT_BLOCK:
    {
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::BLOCK);
        statement->options.block = modtree_block_create_empty();
        return semantic_analyser_fill_block(analyser, statement->options.block, rc_statement->options.statement_block);
    }
    case RC_Statement_Type::IF_STATEMENT:
    {
        auto rc_if = &rc_statement->options.if_statement;
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::IF);

        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            analyser, rc_if->condition, expression_context_make_known_type(analyser->compiler->type_system.bool_type, true)
        );
        if (condition->result_type != type_system->bool_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_IF_CONDITION, rc_if->condition);
        }
        statement->options.if_statement.if_block = modtree_block_create_empty();
        statement->options.if_statement.else_block = modtree_block_create_empty();
        Control_Flow if_flow = semantic_analyser_fill_block(analyser, statement->options.if_statement.if_block, rc_if->true_block);
        Control_Flow else_flow = Control_Flow::NO_RETURN;
        if (rc_if->false_block.available) {
            else_flow = semantic_analyser_fill_block(analyser, statement->options.if_statement.if_block, rc_if->false_block.value);
        }
        if (if_flow == else_flow) {
            return if_flow;
        }
        return Control_Flow::NO_RETURN;
    }
    case RC_Statement_Type::SWITCH_STATEMENT:
    {
        auto rc_switch = &rc_statement->options.switch_statement;
        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::SWITCH);

        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            analyser, rc_switch->condition, expression_context_make_type(Expression_Context_Type::SWITCH_CONDITION)
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

        Type_Signature* switch_type = condition->result_type;
        Expression_Context case_context = condition->result_type->type == Signature_Type::ENUM ?
            expression_context_make_known_type(condition->result_type, true) : expression_context_make_unknown();
        for (int i = 0; i < rc_switch->cases.size; i++)
        {
            RC_Switch_Case* rc_case = &rc_switch->cases[i];
            ModTree_Block* case_body = modtree_block_create_empty();
            semantic_analyser_fill_block(analyser, case_body, rc_case->body);
            if (rc_case->expression.available) 
            {
                ModTree_Expression* case_expr = semantic_analyser_analyse_expression_value(
                    analyser, rc_case->expression.value, case_context
                );

                ModTree_Switch_Case modtree_case;
                modtree_case.expression = case_expr;
                modtree_case.body = case_body;
                modtree_case.value = -1; // Placeholder
                if (switch_type->type == Signature_Type::ENUM && case_expr->result_type != switch_type) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_CASE_TYPE_INVALID, rc_case->expression.value);
                    semantic_analyser_add_error_info(analyser, error_information_make_given_type(case_expr->result_type));
                    semantic_analyser_add_error_info(analyser, error_information_make_given_type(switch_type));
                }
                else
                {
                    Comptime_Analysis comptime = modtree_expression_calculate_comptime_value(analyser, case_expr);
                    if (!comptime.available) {
                        semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN, rc_case->expression.value);
                    }
                    else {
                        modtree_case.value = *(int*)comptime.data;
                    }
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
                    modtree_case.expression = modtree_expression_make_error(type_system->error_type);
                    dynamic_array_push_back(&statement->options.switch_statement.cases, modtree_case);
                }
                statement->options.switch_statement.default_block = case_body;
            }
        }

        // Check if all cases are unique
        int unique_count = 0;
        for (int i = 0; i < statement->options.switch_statement.cases.size; i++) 
        {
            ModTree_Switch_Case* mod_case = &statement->options.switch_statement.cases[i];
            for (int j = i+1; j < statement->options.switch_statement.cases.size; j++)
            {
                ModTree_Switch_Case* other_case = &statement->options.switch_statement.cases[j];
                if (mod_case->value == other_case->value) {
                    semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE, rc_statement); // TODO: Fix this (E.g. not 0)
                    break;
                }
                else {
                    unique_count++;
                }
            }
        }

        // Check if all cases are handled
        if (statement->options.switch_statement.default_block == 0 && switch_type->type == Signature_Type::ENUM) {
            if (unique_count < switch_type->options.enum_type.members.size) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES, rc_statement);
            }
        }
        return Control_Flow::NO_RETURN;
    }
    case RC_Statement_Type::WHILE_STATEMENT:
    {
        auto rc_while = &rc_statement->options.while_statement;
        ModTree_Expression* condition = semantic_analyser_analyse_expression_value(
            analyser, rc_while->condition, expression_context_make_known_type(type_system->bool_type, true)
        );
        if (condition->result_type != type_system->bool_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION, rc_while->condition);
            semantic_analyser_add_error_info(analyser, error_information_make_given_type(condition->result_type));
            semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type_system->bool_type));
        }

        ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::WHILE);
        statement->options.while_statement.condition = condition;
        statement->options.while_statement.while_block = modtree_block_create_empty();
        Control_Flow flow = semantic_analyser_fill_block(analyser, statement->options.while_statement.while_block, rc_while->body);
        switch (flow)
        {
        case Control_Flow::NO_RETURN:
            break;
        case Control_Flow::RETURN:
            semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS, rc_statement);
            break;
        case Control_Flow::CONTINUE:
            semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS, rc_statement);
            break;
        case Control_Flow::BREAK:
            semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE, rc_statement);
            break;
        default: panic("");
        }
        return flow;
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
        return Control_Flow::NO_RETURN;
    }
    case RC_Statement_Type::ASSIGNMENT_STATEMENT:
    {
        auto rc_assignment = &rc_statement->options.assignment;
        ModTree_Expression* left_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_assignment->left_expression, expression_context_make_unknown()
        );
        ModTree_Expression* right_expr = semantic_analyser_analyse_expression_value(
            analyser, rc_assignment->right_expression,expression_context_make_known_type(left_expr->result_type, false)
        );
        Type_Signature* left_type = left_expr->result_type;
        Type_Signature* right_type = right_expr->result_type;
        if (right_type == type_system->void_type) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, rc_assignment->right_expression);
        }
        if (modtree_expression_result_is_temporary(left_expr)) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS, rc_assignment->left_expression);
        }
        if (left_type != right_type)
        {
            ModTree_Expression* cast_expr = semantic_analyser_cast_implicit_if_possible(analyser, right_expr, left_type);
            if (cast_expr == 0) {
                semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT, rc_statement);
                semantic_analyser_add_error_info(analyser, error_information_make_given_type(left_type));
                semantic_analyser_add_error_info(analyser, error_information_make_expected_type(right_type));
            }
            else {
                right_expr = cast_expr;
            }
        }
        ModTree_Statement* assign_statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::ASSIGNMENT);
        assign_statement->options.assignment.destination = left_expr;
        assign_statement->options.assignment.source = right_expr;
        return Control_Flow::NO_RETURN;
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
                analyser, rc_variable->value_expression.value, type == 0 ? expression_context_make_known_type(type, true) : expression_context_make_unknown()
            );
            if (type != 0)
            {
                if (value->result_type != type)
                {
                    ModTree_Expression* casted = semantic_analyser_cast_implicit_if_possible(analyser, value, type);
                    if (casted == 0) {
                        semantic_analyser_log_error(analyser, Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT, rc_variable->value_expression.value);
                        semantic_analyser_add_error_info(analyser, error_information_make_given_type(value->result_type));
                        semantic_analyser_add_error_info(analyser, error_information_make_expected_type(type));
                    }
                    else {
                        value = casted;
                    }
                }
            }
            else {
                type = value->result_type;
            }
        }

        variable = modtree_block_add_variable(block, type, rc_variable->symbol);
        if (rc_variable->symbol->type == Symbol_Type::VARIABLE_UNDEFINED) {
            rc_variable->symbol->type = Symbol_Type::VARIABLE;
            rc_variable->symbol->options.variable = variable;
        }
        else {
            panic("I dont think this can happen!");
        }

        if (value != 0) {
            ModTree_Statement* statement = modtree_block_add_statement_empty(block, ModTree_Statement_Type::ASSIGNMENT);
            statement->options.assignment.destination = modtree_expression_create_variable_read(variable);
            statement->options.assignment.source = value;
        }
        return Control_Flow::NO_RETURN;
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }
    panic("HEY");
    return Control_Flow::NO_RETURN;
}

Control_Flow semantic_analyser_fill_block(Semantic_Analyser* analyser, ModTree_Block* block, RC_Block* rc_block)
{
    Control_Flow flow = Control_Flow::NO_RETURN;
    for (int i = 0; i < rc_block->statements.size; i++)
    {
        RC_Statement* statement = rc_block->statements[i];
        if (flow != Control_Flow::NO_RETURN) {
            semantic_analyser_log_error(analyser, Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE, statement);
        }
        flow = semantic_analyser_analyse_statement(analyser, statement, block);
    }
    return flow;
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
    {
        Type_System* type_system = &analyser->compiler->type_system;
        Type_Signature* type_info_array_signature = type_system_make_array_finished(
            type_system, type_system->type_information_type, type_system->internal_type_infos.size
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
        for (int i = 0; i < analyser->errors.size; i++) {
            dynamic_array_destroy(&analyser->errors[i].information);
        }
        dynamic_array_reset(&analyser->errors);
        stack_allocator_reset(&analyser->allocator_values);
        hashset_reset(&analyser->loaded_filenames);
        hashset_reset(&analyser->visited_functions);

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
            symbol
        );
        symbol->type = Symbol_Type::FUNCTION;
        symbol->options.function = assert_fn;
        ModTree_Variable* cond_var = modtree_function_add_parameter(assert_fn, analyser->compiler->type_system.bool_type, 0);

        // Make function body
        {
            ModTree_Expression* param_read_expr = modtree_expression_create_empty(ModTree_Expression_Type::VARIABLE_READ, cond_var->data_type);
            param_read_expr->options.variable_read = cond_var;

            ModTree_Expression* cond_expr = modtree_expression_create_empty(ModTree_Expression_Type::UNARY_OPERATION, cond_var->data_type);
            cond_expr->options.unary_operation.operand = param_read_expr;
            cond_expr->options.unary_operation.operation_type = ModTree_Unary_Operation_Type::LOGICAL_NOT;

            ModTree_Statement* if_stat = modtree_block_add_statement_empty(assert_fn->body, ModTree_Statement_Type::IF);
            if_stat->options.if_statement.condition = cond_expr;
            if_stat->options.if_statement.if_block = modtree_block_create_empty();
            if_stat->options.if_statement.else_block = modtree_block_create_empty();

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
        ), 0
    );
}

Semantic_Analyser semantic_analyser_create()
{
    Semantic_Analyser result;
    result.errors = dynamic_array_create_empty<Semantic_Error>(64);
    result.allocator_values = stack_allocator_create_empty(2048);
    result.loaded_filenames = hashset_create_pointer_empty<String*>(32);
    result.visited_functions = hashset_create_pointer_empty<ModTree_Function*>(32);
    result.program = 0;
    return result;
}

void semantic_analyser_destroy(Semantic_Analyser* analyser)
{
    for (int i = 0; i < analyser->errors.size; i++) {
        dynamic_array_destroy(&analyser->errors[i].information);
    }
    dynamic_array_destroy(&analyser->errors);

    stack_allocator_destroy(&analyser->allocator_values);
    hashset_destroy(&analyser->loaded_filenames);
    hashset_destroy(&analyser->visited_functions);

    if (analyser->program != 0) {
        modtree_program_destroy(analyser->program);
    }
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
        assert(assign_node->type == AST_Node_Type::STATEMENT_ASSIGNMENT ||
            assign_node->type == AST_Node_Type::VARIABLE_DEFINE_ASSIGN ||
            assign_node->type == AST_Node_Type::PARAMETER, "hey");
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
        string_append_formated(string, "Parameter count does not match argument count, expected: %d, given: %d");
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
    default: panic("ERROR");
    }

    for (int k = 0; k < e.information.size; k++)
    {
        Error_Information* info = &e.information[k];
        switch (info->type)
        {
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

