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
#include "dependency_analyser.hpp"
#include "compiler_misc.hpp"
#include "ast.hpp"
#include "ir_code.hpp"
#include "parser.hpp"
#include "source_code.hpp"

// GLOBALS
bool PRINT_DEPENDENCIES = false;
static Semantic_Analyser semantic_analyser;
static Workload_Executer workload_executer;

// PROTOTYPES
ModTree_Function* modtree_function_create_empty(Type_Signature* signature, Symbol* symbol, Analysis_Pass* body_pass);
Comptime_Result comptime_result_make_not_comptime();
Comptime_Result expression_calculate_comptime_value(AST::Expression* expr);
void analysis_workload_destroy(Analysis_Workload* workload);
Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context);
Type_Signature* semantic_analyser_analyse_expression_value(AST::Expression* rc_expression, Expression_Context context);
Type_Signature* semantic_analyser_analyse_expression_type(AST::Expression* rc_expression);
Control_Flow semantic_analyser_analyse_block(AST::Code_Block* code_block);

bool workload_executer_switch_to_workload(Analysis_Workload* workload);
void analysis_workload_add_struct_dependency(Struct_Progress* my_workload, Struct_Progress* other_progress, Dependency_Type type, AST::Symbol_Read* symbol_read);
void analysis_workload_append_to_string(Analysis_Workload* workload, String* string);



// HELPERS
Analysis_Progress* upcast(Struct_Progress* progress) {
    return &progress->base;
}
Analysis_Progress* upcast(Function_Progress* progress) {
    return &progress->base;
}
Analysis_Progress* upcast(Bake_Progress* progress) {
    return &progress->base;
}
Analysis_Progress* upcast(Definition_Progress* progress) {
    return &progress->base;
}

Type_Signature* hardcoded_type_to_signature(Hardcoded_Type type)
{
    auto& an = compiler.type_system.predefined_types;
    switch (type)
    {
    case Hardcoded_Type::ASSERT_FN: return an.type_assert;
    case Hardcoded_Type::FREE_POINTER: return an.type_free;
    case Hardcoded_Type::MALLOC_SIZE_I32: return an.type_malloc;
    case Hardcoded_Type::TYPE_INFO: return an.type_type_info;
    case Hardcoded_Type::TYPE_OF: return an.type_type_of;

    case Hardcoded_Type::PRINT_BOOL: return an.type_print_bool;
    case Hardcoded_Type::PRINT_I32: return an.type_print_i32;
    case Hardcoded_Type::PRINT_F32: return an.type_print_f32;
    case Hardcoded_Type::PRINT_STRING: return an.type_print_string;
    case Hardcoded_Type::PRINT_LINE: return an.type_print_line;

    case Hardcoded_Type::READ_I32: return an.type_print_i32;
    case Hardcoded_Type::READ_F32: return an.type_print_f32;
    case Hardcoded_Type::READ_BOOL: return an.type_print_bool;
    case Hardcoded_Type::RANDOM_I32: return an.type_random_i32;
    default: panic("HEY");
    }
    return 0;
}



// Analysis Info
Analysis_Pass* analysis_item_create_pass(Analysis_Item* item)
{
    auto pass = new Analysis_Pass;
    pass->item = item;
    if (item->ast_node_count == 0) {
        // NOTE(Martin): This was done in a quick fix for structs/unions where there was no tab afterwards...
        pass->infos = array_create_empty<Analysis_Info>(1);
    }
    else {
        pass->infos = array_create_empty<Analysis_Info>(item->ast_node_count);
        memory_set_bytes(pass->infos.data, pass->infos.size * sizeof(Analysis_Info), 0);
    }
    dynamic_array_push_back(&item->passes, pass);
    return pass;
}

Analysis_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Node* node)
{
    assert(pass != 0, "");
    auto& item = pass->item;
    return &pass->infos[node->analysis_item_index];
}

Expression_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Expression* expression) {
    return &analysis_pass_get_info(pass, AST::upcast(expression))->info_expr;
}

Case_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Switch_Case* sw_case) {
    return &analysis_pass_get_info(pass, AST::upcast(sw_case))->info_case;
}

Argument_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Argument* argument) {
    return &analysis_pass_get_info(pass, AST::upcast(argument))->arg_info;
}

Statement_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Statement* statement) {
    return &analysis_pass_get_info(pass, AST::upcast(statement))->info_stat;
}

Code_Block_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Code_Block* block) {
    return &analysis_pass_get_info(pass, AST::upcast(block))->info_block;
}


// Helpers
Expression_Info* pass_get_info(AST::Expression* expression) {
    return analysis_pass_get_info(semantic_analyser.current_workload->current_pass, expression);
}

Case_Info* pass_get_info(AST::Switch_Case* sw_case) {
    return analysis_pass_get_info(semantic_analyser.current_workload->current_pass, sw_case);
}

Argument_Info* pass_get_info(AST::Argument* argument) {
    return analysis_pass_get_info(semantic_analyser.current_workload->current_pass, argument);
}

Statement_Info* pass_get_info(AST::Statement* statement) {
    return analysis_pass_get_info(semantic_analyser.current_workload->current_pass, statement);
}

Code_Block_Info* pass_get_info(AST::Code_Block* block) {
    return analysis_pass_get_info(semantic_analyser.current_workload->current_pass, block);
}



// Analysis Progress
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

Function_Progress* analysis_progress_create_function(Analysis_Item* body_item, Analysis_Item* header_item) 
{
    assert(body_item->type == Analysis_Item_Type::FUNCTION_BODY && (header_item == 0 || header_item->type == Analysis_Item_Type::FUNCTION), "");
    auto result = analysis_progress_allocate_internal<Function_Progress>(Analysis_Progress_Type::FUNCTION);
    result->state = Function_State::DEFINED;
    result->body_pass = analysis_item_create_pass(body_item);
    if (header_item != 0) {
        result->header_pass = analysis_item_create_pass(header_item);
    }
    result->function = modtree_function_create_empty(0, (header_item == 0 ? 0 : header_item->symbol), result->body_pass);
    hashtable_insert_element(&workload_executer.progress_functions, result->function, result);
    return result;
}

Struct_Progress* analysis_progress_create_struct(Type_Signature* struct_type, Analysis_Item* item)
{
    assert(item->type == Analysis_Item_Type::STRUCTURE, "");
    auto result = analysis_progress_allocate_internal<Struct_Progress>(Analysis_Progress_Type::STRUCTURE);
    result->state = Struct_State::DEFINED;
    result->pass = analysis_item_create_pass(item);
    result->struct_type = struct_type;
    hashtable_insert_element(&workload_executer.progress_structs, struct_type, result);
    return result;
}

Bake_Progress* analysis_progress_create_bake(Analysis_Item* item)
{
    assert(item->type == Analysis_Item_Type::BAKE, "");
    auto result = analysis_progress_allocate_internal<Bake_Progress>(Analysis_Progress_Type::BAKE);
    result->result = comptime_result_make_not_comptime();
    result->pass = analysis_item_create_pass(item);
    result->bake_function = modtree_function_create_empty(
        type_system_make_function(&compiler.type_system, {}, compiler.type_system.predefined_types.void_type), 0, result->pass
    );
    return result;
}

Definition_Progress* analysis_progress_create_definition(Symbol* symbol, Analysis_Item* item)
{
    assert(item->type == Analysis_Item_Type::DEFINITION, "");
    auto result = analysis_progress_allocate_internal<Definition_Progress>(Analysis_Progress_Type::DEFINITION);
    result->symbol = symbol;
    result->pass = analysis_item_create_pass(item);
    hashtable_insert_element(&workload_executer.progress_definitions, symbol, result);
    return result;
}



// Errors
void semantic_analyser_set_error_flag(bool error_due_to_unknown)
{
    auto& analyser = semantic_analyser;
    if (analyser.current_workload != 0) {
        auto workload = analyser.current_workload;
        if (workload->current_expression != 0) {
            workload->current_expression->contains_errors = true;
        }
        if (workload->current_function != 0)
        {
            workload->current_function->is_runnable = false;
            if (!error_due_to_unknown) {
                workload->current_function->contains_errors = true;
            }
        }
    }
}

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Node* node) {
    Semantic_Error error;
    error.type = type;
    error.error_node = node;
    error.information = dynamic_array_create_empty<Error_Information>(2);
    dynamic_array_push_back(&semantic_analyser.errors, error);
    semantic_analyser_set_error_flag(false);
}

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Expression* node) {
    semantic_analyser_log_error(type, AST::upcast(node));
}

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Statement* node) {
    semantic_analyser_log_error(type, AST::upcast(node));
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

Error_Information error_information_make_text(const char* text) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXTRA_TEXT);
    info.options.extra_text = text;
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



// MOD_TREE
ModTree_Function* modtree_function_create_empty(Type_Signature* signature, Symbol* symbol, Analysis_Pass* body_pass)
{
    ModTree_Function* function = new ModTree_Function;
    function->body_pass = body_pass;
    function->symbol = symbol;
    function->signature = signature;

    function->called_from = dynamic_array_create_empty<ModTree_Function*>(1);
    function->calls = dynamic_array_create_empty<ModTree_Function*>(1);
    function->contains_errors = false;
    function->is_runnable = true;
    function->is_polymorphic = false;
    function->polymorphic_base = 0;
    function->polymorphic_instance_index = -1;

    dynamic_array_push_back(&semantic_analyser.program->functions, function);
    return function;
}

void modtree_function_destroy(ModTree_Function* function)
{
    dynamic_array_destroy(&function->called_from);
    dynamic_array_destroy(&function->calls);
    delete function;
}

ModTree_Global* modtree_program_add_global(Type_Signature* type)
{
    auto global = new ModTree_Global;
    global->type = type;
    global->has_initial_value = false;
    global->init_expr = 0;
    global->init_pass = 0;
    global->index = semantic_analyser.program->globals.size;
    dynamic_array_push_back(&semantic_analyser.program->globals, global);
    return global;
}

void modtree_global_set_init(ModTree_Global* global, Analysis_Pass* pass, AST::Expression* expr)
{
    global->has_initial_value = true;
    global->init_expr = expr;
    global->init_pass = pass;
}

ModTree_Program* modtree_program_create()
{
    ModTree_Program* result = new ModTree_Program();
    result->main_function = 0;
    result->functions = dynamic_array_create_empty<ModTree_Function*>(16);
    result->globals = dynamic_array_create_empty<ModTree_Global*>(16);
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
    delete program;
}



// Comptime Values
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

Comptime_Result comptime_result_apply_cast(Comptime_Result value, Info_Cast_Type cast_type, Type_Signature* result_type)
{
    auto& analyser = semantic_analyser;
    if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
        return comptime_result_make_not_comptime();
    }
    else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
        return comptime_result_make_unavailable(result_type);
    }

    Instruction_Type instr_type = (Instruction_Type)-1;
    switch (cast_type)
    {
    case Info_Cast_Type::INVALID: return comptime_result_make_unavailable(result_type); // Invalid means an error was already logged
    case Info_Cast_Type::NO_CAST: return value;
    case Info_Cast_Type::FLOATS: instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
    case Info_Cast_Type::FLOAT_TO_INT: instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
    case Info_Cast_Type::INT_TO_FLOAT: instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
    case Info_Cast_Type::INTEGERS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::ENUM_TO_INT: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::INT_TO_ENUM: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::POINTERS: return comptime_result_make_not_comptime();
    case Info_Cast_Type::POINTER_TO_U64: return comptime_result_make_not_comptime();
    case Info_Cast_Type::U64_TO_POINTER: return comptime_result_make_not_comptime();
    case Info_Cast_Type::ARRAY_TO_SLICE: {
        Upp_Slice_Base* slice = stack_allocator_allocate<Upp_Slice_Base>(&analyser.allocator_values);
        slice->data_ptr = value.data;
        slice->size = value.data_type->options.array.element_count;
        return comptime_result_make_available(slice, result_type);
    }
    case Info_Cast_Type::TO_ANY: {
        Upp_Any* any = stack_allocator_allocate<Upp_Any>(&analyser.allocator_values);
        any->type = result_type->internal_index;
        any->data = value.data;
        return comptime_result_make_available(any, result_type);
    }
    case Info_Cast_Type::FROM_ANY: {
        Upp_Any* given = (Upp_Any*)value.data;
        if (given->type >= compiler.type_system.types.size) {
            return comptime_result_make_not_comptime();
        }
        Type_Signature* any_type = compiler.type_system.types[given->type];
        if (!type_signature_equals(any_type, value.data_type)) {
            return comptime_result_make_not_comptime();
        }
        return comptime_result_make_available(given->data, any_type);
    }
    default: panic("");
    }
    if ((int)instr_type == -1) panic("");

    void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, result_type->size, result_type->alignment);
    bytecode_execute_cast_instr(
        instr_type, result_buffer, value.data,
        type_signature_to_bytecode_type(result_type),
        type_signature_to_bytecode_type(value.data_type)
    );
    return comptime_result_make_available(result_buffer, result_type);
}

Comptime_Result expression_calculate_comptime_value_without_context_cast(AST::Expression* expr)
{
    auto& analyser = semantic_analyser;
    Predefined_Types& types = compiler.type_system.predefined_types;

    auto info = pass_get_info(expr);
    if (info->contains_errors) {
        return comptime_result_make_unavailable(expression_info_get_type(info));
    }

    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT: {
        auto& upp_const = info->options.constant;
        return comptime_result_make_available(&compiler.constant_pool.buffer[upp_const.offset], upp_const.type);
    }
    case Expression_Result_Type::TYPE: {
        return comptime_result_make_available(&info->options.type->internal_index, types.type_type);
    }
    case Expression_Result_Type::MODULE:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::FUNCTION: // TODO: Function pointer reads should work in the future
    case Expression_Result_Type::HARDCODED_FUNCTION: {
        return comptime_result_make_not_comptime();
    }
    case Expression_Result_Type::VALUE:
        break; // Rest of function
    default:panic("");
    }

    auto result_type = expression_info_get_type(info);
    switch (expr->type)
    {
    case AST::Expression_Type::ARRAY_TYPE:
    case AST::Expression_Type::AUTO_ENUM:
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR:
    case AST::Expression_Type::ENUM_TYPE:
    case AST::Expression_Type::FUNCTION:
    case AST::Expression_Type::LITERAL_READ:
    case AST::Expression_Type::STRUCTURE_TYPE:
    case AST::Expression_Type::SLICE_TYPE:
    case AST::Expression_Type::MODULE:
        panic("Should be handled above!");
    case AST::Expression_Type::ERROR_EXPR: {
        return comptime_result_make_unavailable(types.unknown_type);
    }
    case AST::Expression_Type::SYMBOL_READ: {
        auto symbol = expr->options.symbol_read->symbol;
        if (symbol->type == Symbol_Type::COMPTIME_VALUE) {
            auto& upp_const = symbol->options.constant;
            return comptime_result_make_available(&compiler.constant_pool.buffer[upp_const.offset], upp_const.type);
        }
        else if (symbol->type == Symbol_Type::PARAMETER && symbol->options.parameter.is_polymorphic) {
            assert(
                analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_BODY ||
                analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_HEADER,
                "Accessing poly-parameters should only be possible in functions"
            );

            const auto& poly_value = analyser.current_workload->current_polymorphic_values[symbol->options.parameter.index];
            if (poly_value.is_not_set) {
                return comptime_result_make_unavailable(symbol->options.parameter.function->polymorphic_base->parameters[symbol->options.parameter.index].base_type);
            }
            else {
                return comptime_result_make_available(&compiler.constant_pool.buffer[poly_value.constant.offset], poly_value.constant.type);
            }
        }
        else if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
            return comptime_result_make_unavailable(types.unknown_type);
        }
        return comptime_result_make_not_comptime();
    }
    case AST::Expression_Type::BINARY_OPERATION:
    {
        Comptime_Result left_val = expression_calculate_comptime_value(expr->options.binop.left);
        Comptime_Result right_val = expression_calculate_comptime_value(expr->options.binop.right);
        if (left_val.type != Comptime_Result_Type::AVAILABLE || right_val.type != Comptime_Result_Type::AVAILABLE) {
            if (left_val.type == Comptime_Result_Type::NOT_COMPTIME && right_val.type != Comptime_Result_Type::NOT_COMPTIME) {
                return comptime_result_make_not_comptime();
            }
            else {
                return comptime_result_make_unavailable(result_type);
            }
        }

        Instruction_Type instr_type;
        switch (expr->options.binop.type)
        {
        case AST::Binop::ADDITION: instr_type = Instruction_Type::BINARY_OP_ADDITION; break;
        case AST::Binop::SUBTRACTION: instr_type = Instruction_Type::BINARY_OP_SUBTRACTION; break;
        case AST::Binop::DIVISION: instr_type = Instruction_Type::BINARY_OP_DIVISION; break;
        case AST::Binop::MULTIPLICATION: instr_type = Instruction_Type::BINARY_OP_MULTIPLICATION; break;
        case AST::Binop::MODULO: instr_type = Instruction_Type::BINARY_OP_MODULO; break;
        case AST::Binop::AND: instr_type = Instruction_Type::BINARY_OP_AND; break;
        case AST::Binop::OR: instr_type = Instruction_Type::BINARY_OP_OR; break;
        case AST::Binop::EQUAL: instr_type = Instruction_Type::BINARY_OP_EQUAL; break;
        case AST::Binop::NOT_EQUAL: instr_type = Instruction_Type::BINARY_OP_NOT_EQUAL; break;
        case AST::Binop::LESS: instr_type = Instruction_Type::BINARY_OP_LESS_THAN; break;
        case AST::Binop::LESS_OR_EQUAL: instr_type = Instruction_Type::BINARY_OP_LESS_EQUAL; break;
        case AST::Binop::GREATER: instr_type = Instruction_Type::BINARY_OP_GREATER_THAN; break;
        case AST::Binop::GREATER_OR_EQUAL: instr_type = Instruction_Type::BINARY_OP_GREATER_EQUAL; break;
        case AST::Binop::POINTER_EQUAL: instr_type = Instruction_Type::BINARY_OP_EQUAL; break;
        case AST::Binop::POINTER_NOT_EQUAL: instr_type = Instruction_Type::BINARY_OP_NOT_EQUAL; break;
        case AST::Binop::INVALID: return comptime_result_make_unavailable(result_type);
        default: panic("");
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, result_type->size, result_type->alignment);
        if (bytecode_execute_binary_instr(instr_type, type_signature_to_bytecode_type(left_val.data_type), result_buffer, left_val.data, right_val.data)) {
            return comptime_result_make_available(result_buffer, result_type);
        }
        else {
            return comptime_result_make_not_comptime();
        }
        break;
    }
    case AST::Expression_Type::UNARY_OPERATION:
    {
        Comptime_Result value = expression_calculate_comptime_value(expr->options.unop.expr);
        Instruction_Type instr_type;
        switch (expr->options.unop.type)
        {
        case AST::Unop::NOT:
        case AST::Unop::NEGATE: {
            instr_type = expr->options.unop.type == AST::Unop::NEGATE ?
                Instruction_Type::UNARY_OP_NEGATE : Instruction_Type::UNARY_OP_NOT;
            break;
        }
        case AST::Unop::POINTER:
            return comptime_result_make_not_comptime();
        case AST::Unop::DEREFERENCE:
            return comptime_result_make_not_comptime();
        default: panic("");
        }

        if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type);
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, result_type->size, result_type->alignment);
        bytecode_execute_unary_instr(instr_type, type_signature_to_bytecode_type(value.data_type), result_buffer, value.data);
        return comptime_result_make_available(result_buffer, result_type);
    }
    case AST::Expression_Type::CAST: {
        auto operand = expression_calculate_comptime_value(expr->options.cast.operand);
        return comptime_result_apply_cast(operand, info->specifics.cast_type, result_type);
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        Type_Signature* element_type = result_type;
        Comptime_Result value_array = expression_calculate_comptime_value(expr->options.array_access.array_expr);
        Comptime_Result value_index = expression_calculate_comptime_value(expr->options.array_access.index_expr);
        if (value_array.type == Comptime_Result_Type::NOT_COMPTIME || value_index.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value_array.type == Comptime_Result_Type::UNAVAILABLE || value_index.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type);
        }
        assert(type_signature_equals(value_index.data_type, types.i32_type), "Must be i32 currently");

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
    case AST::Expression_Type::MEMBER_ACCESS: {
        Comptime_Result value_struct = expression_calculate_comptime_value(expr->options.member_access.expr);
        if (value_struct.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value_struct.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type);
        }
        auto member = type_signature_find_member_by_id(value_struct.data_type, expr->options.member_access.name);
        assert(member.available, "I think after analysis this member should exist");
        byte* raw_data = (byte*)value_struct.data;
        return comptime_result_make_available(&raw_data[member.value.offset], result_type);
    }
    case AST::Expression_Type::FUNCTION_CALL: {
        return comptime_result_make_not_comptime();
    }
    case AST::Expression_Type::NEW_EXPR: {
        return comptime_result_make_not_comptime(); // New is always uninitialized, so it cannot have a comptime value (Future: Maybe new with values)
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        return comptime_result_make_not_comptime();
        // NOTE: Maybe this works in the future, but it dependes if we can always finish the struct size after analysis
        /*
        Type_Signature* element_type = result_type->options.array.element_type;
        if (element_type->size == 0 && element_type->alignment == 0) {
            return comptime_analysis_make_error();
        }
        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, result_type->size, result_type->alignment);
        for (int i = 0; i < expr->options.array_initializer.size; i++)
        {
            ModTree_Expression* element_expr = expr->options.array_initializer[i];
            Comptime_Analysis value_element = expression_calculate_comptime_value(analyser, element_expr);
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
        return comptime_analysis_make_success(result_buffer, result_type);
        */
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        // NOTE: Maybe this works in the future
        return comptime_result_make_not_comptime();
        /*
        Type_Signature* struct_type = result_type->options.array.element_type;
        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, result_type->size, result_type->alignment);
        for (int i = 0; i < expr->options.struct_initializer.size; i++)
        {
            Member_Initializer* initializer = &expr->options.struct_initializer[i];
            Comptime_Analysis value_member = expression_calculate_comptime_value(analyser, initializer->init_expr);
            if (!value_member.available) {
                return value_member;
            }
            // Copy member result into actual struct memory
            {
                byte* raw = (byte*)result_buffer;
                memory_copy(&raw[initializer->member.offset], value_member.data, initializer->member.type->size);
            }
        }
        return comptime_analysis_make_success(result_buffer, result_type);
        */
    }
    default: panic("");
    }

    panic("");
    return comptime_result_make_not_comptime();
}

Comptime_Result expression_calculate_comptime_value(AST::Expression* expr)
{
    auto result_no_context = expression_calculate_comptime_value_without_context_cast(expr);
    if (result_no_context.type != Comptime_Result_Type::AVAILABLE) {
        return result_no_context;
    }

    auto info = pass_get_info(expr);
    if (info->context_ops.deref_count != 0 || info->context_ops.take_address_of) {
        // Cannot handle pointers for comptime currently
        return comptime_result_make_not_comptime();
    }

    return comptime_result_apply_cast(result_no_context, info->context_ops.cast, info->context_ops.after_cast_type);
}

bool expression_has_memory_address(AST::Expression* expr)
{
    auto info = pass_get_info(expr);
    auto type = info->context_ops.after_cast_type;
    if (type->size == 0 && type->alignment != 0) return false; // I forgot if this case has any real use cases currently

    switch (info->result_type)
    {
    case Expression_Result_Type::FUNCTION:
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::TYPE:
    case Expression_Result_Type::CONSTANT: // Constant memory must not be written to. (e.g. 5 = 10)
    case Expression_Result_Type::MODULE:
        return false;
    case Expression_Result_Type::VALUE:
        break;
    default:panic("");
    }

    if (info->context_ops.cast == Info_Cast_Type::FROM_ANY) {
        // From Any is basically a pointer dereference
        return true;
    }

    switch (expr->type)
    {
    case AST::Expression_Type::SYMBOL_READ:
        return true; // If expr->result_type is value its a global or variable read
    case AST::Expression_Type::ARRAY_ACCESS: return expression_has_memory_address(expr->options.array_access.array_expr);
    case AST::Expression_Type::MEMBER_ACCESS: return expression_has_memory_address(expr->options.member_access.expr);
    case AST::Expression_Type::UNARY_OPERATION: {
        // Dereference value must, by definition, have a memory_address
        // There are special cases where memory loss is not detected, e.g. &(new int)
        return expr->options.unop.type == AST::Unop::DEREFERENCE;
    }
    case AST::Expression_Type::CAST:
        // From Any is basically a pointer dereference
        return info->specifics.cast_type == Info_Cast_Type::FROM_ANY;
    case AST::Expression_Type::ERROR_EXPR:
        // Errors shouldn't generate other errors
        return true;
    }

    // All other are expressions generating temporary results
    return false;
}



// Context
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



// Result
void expression_info_set_module(Expression_Info* info, Symbol_Table* module_table)
{
    info->result_type = Expression_Result_Type::MODULE;
    info->options.module_table = module_table;
    info->context_ops.after_cast_type = compiler.type_system.predefined_types.unknown_type;
}

void expression_info_set_value(Expression_Info* info, Type_Signature* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->context_ops.after_cast_type = result_type;
}

void expression_info_set_error(Expression_Info* info, Type_Signature* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->context_ops.after_cast_type = result_type;
    info->contains_errors = true;
}

void expression_info_set_function(Expression_Info* info, ModTree_Function* function)
{
    info->result_type = Expression_Result_Type::FUNCTION;
    info->options.function = function;
    info->context_ops.after_cast_type = function->signature;
}

void expression_info_set_hardcoded(Expression_Info* info, Hardcoded_Type hardcoded)
{
    info->result_type = Expression_Result_Type::HARDCODED_FUNCTION;
    info->options.hardcoded = hardcoded;
    info->context_ops.after_cast_type = hardcoded_type_to_signature(hardcoded);
}

void expression_info_set_type(Expression_Info* info, Type_Signature* type)
{
    info->result_type = Expression_Result_Type::TYPE;
    info->options.type = type;
    info->context_ops.after_cast_type = compiler.type_system.predefined_types.type_type;
}

void expression_info_set_constant(Expression_Info* info, Upp_Constant constant) {
    info->result_type = Expression_Result_Type::CONSTANT;
    info->options.constant = constant;
    info->context_ops.after_cast_type = constant.type;
}

void expression_info_set_polymorphic_function(Expression_Info* info, Polymorphic_Function* poly_function, int instance_index) {
    info->result_type = Expression_Result_Type::POLYMORPHIC_FUNCTION;
    info->options.polymorphic.function = poly_function;
    info->options.polymorphic.instance_index = instance_index;
    info->context_ops.after_cast_type = compiler.type_system.predefined_types.unknown_type;
}

void expression_info_set_constant(Expression_Info* info, Type_Signature* signature, Array<byte> bytes, AST::Node* error_report_node)
{
    auto& analyser = semantic_analyser;
    Constant_Result result = constant_pool_add_constant(&compiler.constant_pool, signature, bytes);
    if (result.status != Constant_Status::SUCCESS)
    {
        assert(error_report_node != 0, "Error"); // Error report node may only be null if we know that adding the constant cannot fail.
        semantic_analyser_log_error(Semantic_Error_Type::CONSTANT_POOL_ERROR, error_report_node);
        semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
        expression_info_set_error(info, signature);
        return;
    }
    expression_info_set_constant(info, result.constant);
}

void expression_info_set_constant_enum(Expression_Info* info, Type_Signature* enum_type, i32 value) {
    expression_info_set_constant(info, enum_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}

void expression_info_set_constant_i32(Expression_Info* info, i32 value) {
    expression_info_set_constant(info, compiler.type_system.predefined_types.i32_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}

// Returns result type of a value before a cast
Type_Signature* expression_info_get_type(Expression_Info* info)
{
    auto& types = compiler.type_system.predefined_types;
    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT: info->options.constant.type;
    case Expression_Result_Type::VALUE: return info->options.value_type;
    case Expression_Result_Type::FUNCTION: return info->options.function->signature;
    case Expression_Result_Type::HARDCODED_FUNCTION: return hardcoded_type_to_signature(info->options.hardcoded);
    case Expression_Result_Type::TYPE: return types.type_type;
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
        return info->options.polymorphic.function->instances[info->options.polymorphic.instance_index].function->signature;
        break;
    case Expression_Result_Type::MODULE: 
        return types.unknown_type;
    default: panic("");
    }
    return types.unknown_type;
}



//DEPENDENCY GRAPH
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
    workload_executer.all_workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    workload_executer.waiting_for_symbols_workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    workload_executer.runnable_workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
    workload_executer.finished_workloads = dynamic_array_create_empty<Analysis_Workload*>(8);
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

    for (int i = 0; i < executer.all_workloads.size; i++) {
        analysis_workload_destroy(executer.all_workloads[i]);
    }
    dynamic_array_destroy(&executer.all_workloads);
    dynamic_array_destroy(&executer.waiting_for_symbols_workloads);
    dynamic_array_destroy(&executer.runnable_workloads);
    dynamic_array_destroy(&executer.finished_workloads);

    {
        auto iter = hashtable_iterator_create(&executer.workload_dependencies);
        while (hashtable_iterator_has_next(&iter)) {
            SCOPE_EXIT(hashtable_iterator_next(&iter));
            auto& dep_info = iter.value;
            dynamic_array_destroy(&dep_info->symbol_reads);
        }
        hashtable_destroy(&executer.workload_dependencies);
    }
    hashtable_destroy(&executer.progress_items);
    hashtable_destroy(&executer.progress_definitions);
    hashtable_destroy(&executer.progress_functions);
    hashtable_destroy(&executer.progress_structs);
}

void analysis_workload_destroy(Analysis_Workload* workload)
{
    list_destroy(&workload->dependencies);
    list_destroy(&workload->dependents);
    dynamic_array_destroy(&workload->reachable_clusters);
    if (workload->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE) {
        dynamic_array_destroy(&workload->options.struct_reachable.struct_types);
        dynamic_array_destroy(&workload->options.struct_reachable.unfinished_array_types);
    }
    if (workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE) {
        dynamic_array_destroy(&workload->options.cluster_compile.functions);
    }
    if (workload->type == Analysis_Workload_Type::FUNCTION_BODY) {
        dynamic_array_destroy(&workload->options.function_body.block_stack);
    }
    delete workload;
}

void symbol_read_set_error_symbol(AST::Symbol_Read* read)
{
    auto error_symbol = semantic_analyser.predefined_symbols.error_symbol;
    while (true)
    {
        if (read->path_child.available)
        {
            if (read->symbol == 0) {
                read->symbol = error_symbol;
            }
            read = read->path_child.value;
        }
        else
        {
            read->symbol = error_symbol;
            return;
        }
    }
}

// Resolves the whole path (e.g. Symbol_Read* and all children)
Symbol* symbol_read_resolve_path(AST::Symbol_Read* path)
{
    auto& analyser = semantic_analyser;
    auto table = path->symbol_table;
    
    AST::Symbol_Read* read = path;
    while (true)
    {
        assert(read->symbol == 0, "Symbol already resolved!");
        read->symbol = symbol_table_find_symbol(table, read->name, path == read, !path->path_child.available, read);
        if (read->symbol == 0) {
            semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL, upcast(read));
            symbol_read_set_error_symbol(read);
            return read->symbol;
        }

        if (!read->path_child.available) {
            return read->symbol;
        }
        else
        {
            if (read->symbol->type == Symbol_Type::UNRESOLVED) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, upcast(read));
                semantic_analyser_add_error_info(error_information_make_text("Shouldn't happen with new dependency system!!!"));
                symbol_read_set_error_symbol(read);
                return read->symbol;
            }
            if (read->symbol->type == Symbol_Type::MODULE) {
                table = read->symbol->options.module_table;
                read = read->path_child.value;
                continue;
            }
            else {
                semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH, &read->base);
                semantic_analyser_add_error_info(error_information_make_symbol(read->symbol));
                symbol_read_set_error_symbol(read);
                return read->symbol;
            }
        }
    }
    panic("unreachable!");
    return 0;
}

void analysis_workload_add_dependency_internal(Analysis_Workload* workload, Analysis_Workload* dependency, AST::Symbol_Read* symbol_read)
{
    auto& executer = workload_executer;
    if (dependency->is_finished) return;
    Workload_Pair pair = workload_pair_create(workload, dependency);
    Dependency_Information* infos = hashtable_find_element(&executer.workload_dependencies, pair);
    if (infos == 0) {
        Dependency_Information info;
        info.dependency_node = list_add_at_end(&workload->dependencies, dependency);
        info.dependent_node = list_add_at_end(&dependency->dependents, workload);
        info.symbol_reads = dynamic_array_create_empty<AST::Symbol_Read*>(1);
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

    // Remove old dependency
    Workload_Pair original_pair = workload_pair_create(move_from, dependency);
    Dependency_Information info = *hashtable_find_element(&graph->workload_dependencies, original_pair);
    hashtable_remove_element(&graph->workload_dependencies, original_pair);
    list_remove_node(&move_from->dependencies, info.dependency_node);
    list_remove_node(&dependency->dependents, info.dependent_node);

    // Add new dependency, reusing old information in the process
    Workload_Pair new_pair = workload_pair_create(move_to, dependency);
    Dependency_Information* new_infos = hashtable_find_element(&graph->workload_dependencies, new_pair);
    if (new_infos == 0) {
        Dependency_Information new_info;
        new_info.dependency_node = list_add_at_end(&move_to->dependencies, dependency);
        new_info.dependent_node = list_add_at_end(&dependency->dependents, move_to);
        new_info.symbol_reads = info.symbol_reads; // Note: Takes ownership
        new_info.only_symbol_read_dependency = info.only_symbol_read_dependency;
        hashtable_insert_element(&graph->workload_dependencies, new_pair, new_info);
    }
    else {
        dynamic_array_append_other(&new_infos->symbol_reads, &info.symbol_reads);
        if (new_infos->only_symbol_read_dependency) {
            new_infos->only_symbol_read_dependency = info.only_symbol_read_dependency;
        }
        dynamic_array_destroy(&info.symbol_reads);
    }
}

void workload_executer_remove_dependency(Analysis_Workload* workload, Analysis_Workload* depends_on, bool allow_add_to_runnables)
{
    auto graph = &workload_executer;
    Workload_Pair pair = workload_pair_create(workload, depends_on);
    Dependency_Information* info = hashtable_find_element(&graph->workload_dependencies, pair);
    assert(info != 0, "");
    list_remove_node(&workload->dependencies, info->dependency_node);
    list_remove_node(&depends_on->dependents, info->dependent_node);
    dynamic_array_destroy(&info->symbol_reads);
    bool worked = hashtable_remove_element(&graph->workload_dependencies, pair);
    if (allow_add_to_runnables && workload->dependencies.count == 0) {
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
    hashtable_insert_element(visited, workload, false); // The boolean value nodes later if we actually find a loop
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

void analysis_workload_add_cluster_dependency(Analysis_Workload* add_to_workload, Analysis_Workload* dependency, AST::Symbol_Read* symbol_read)
{
    auto graph = &workload_executer;
    assert((add_to_workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE && dependency->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE) ||
        (add_to_workload->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE && dependency->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE), "");
    Analysis_Workload* merge_into = analysis_workload_find_associated_cluster(add_to_workload);
    Analysis_Workload* merge_from = analysis_workload_find_associated_cluster(dependency);
    if (merge_into == merge_from || merge_from->is_finished) {
        return;
    }

    // Check if clusters form loops after new dependency
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

    // Merge all workloads into merge_into workload
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
        default: panic("Clustering only on function clusters and reachable resolve cluster!");
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
            for (int j = i + 1; j < merge_into->reachable_clusters.size; j++) {
                if (merge_into->reachable_clusters[j] == reachable) {
                    dynamic_array_swap_remove(&merge_into->reachable_clusters, j);
                }
            }
        }
    }
}

void analysis_workload_add_struct_dependency(Struct_Progress* progress, Struct_Progress* other, Dependency_Type type, AST::Symbol_Read* symbol_read)
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
        loop run runnable workloads while they exist

        if no runnable workloads exist and not finished
            Check for circular dependencies
                if they exist, resolve them (E.g. set symbols to error, set member type to error, ...)
                else illegal_code_path
    */
    auto& executer = workload_executer;
    int round_no = 0;
    while (true)
    {
        SCOPE_EXIT(round_no += 1);
        executer.progress_was_made = false;
        // Print workloads and dependencies
        if (PRINT_DEPENDENCIES)
        {
            String tmp = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&tmp));
            string_append_formated(&tmp, "\n\n--------------------\nWorkload Execution Round %d\n---------------------\n", round_no);
            for (int i = 0; i < executer.runnable_workloads.size; i++)
            {
                auto workload = executer.runnable_workloads[i];
                if (i == 0) {
                    string_append_formated(&tmp, "Runnable workloads:\n");
                }
                if (workload->dependencies.count > 0) continue;
                if (workload->is_finished) continue;
                string_append_formated(&tmp, "  ");
                analysis_workload_append_to_string(workload, &tmp);
                string_append_formated(&tmp, "\n");

                // Append dependents
                List_Node<Analysis_Workload*>* dependent_node = workload->dependents.head;
                while (dependent_node != 0) {
                    SCOPE_EXIT(dependent_node = dependent_node->next);
                    Analysis_Workload* dependent = dependent_node->value;
                    string_append_formated(&tmp, "    ");
                    analysis_workload_append_to_string(dependent, &tmp);
                    string_append_formated(&tmp, "\n");
                }
            }

            for (int i = 0; i < executer.all_workloads.size; i++)
            {
                if (i == 0) {
                    string_append_formated(&tmp, "\nWorkloads with dependencies:\n");
                }
                Analysis_Workload* workload = executer.all_workloads[i];
                if (workload->is_finished || workload->dependencies.count == 0) continue;
                string_append_formated(&tmp, "  ");
                analysis_workload_append_to_string(workload, &tmp);
                string_append_formated(&tmp, "\n");
                // Print dependencies
                {
                    List_Node<Analysis_Workload*>* dependency_node = workload->dependencies.head;
                    if (dependency_node != 0) {
                        string_append_formated(&tmp, "    Depends On:\n");
                    }
                    while (dependency_node != 0) {
                        SCOPE_EXIT(dependency_node = dependency_node->next);
                        Analysis_Workload* dependency = dependency_node->value;
                        string_append_formated(&tmp, "      ");
                        analysis_workload_append_to_string(dependency, &tmp);
                        string_append_formated(&tmp, "\n");
                    }
                }
                // Dependents
                {
                    List_Node<Analysis_Workload*>* dependent_node = workload->dependents.head;
                    if (dependent_node != 0) {
                        string_append_formated(&tmp, "    Dependents:\n");
                    }
                    while (dependent_node != 0) {
                        SCOPE_EXIT(dependent_node = dependent_node->next);
                        Analysis_Workload* dependent = dependent_node->value;
                        string_append_formated(&tmp, "      ");
                        analysis_workload_append_to_string(dependent, &tmp);
                        string_append_formated(&tmp, "\n");
                    }
                }

            }

            logg("%s", tmp.characters);
        }

        // Execute runnable workloads
        for (int i = 0; i < executer.runnable_workloads.size; i++)
        {
            Analysis_Workload* workload = executer.runnable_workloads[i];
            if (workload->dependencies.count > 0) {
                continue; // Skip runnable workload
            }
            if (workload->is_finished) {
                continue;
            }
            executer.progress_was_made = true;

            if (PRINT_DEPENDENCIES) {
                String tmp = string_create_empty(128);
                analysis_workload_append_to_string(workload, &tmp);
                logg("Executing workload: %s\n", tmp.characters);
                string_destroy(&tmp);
            }

            bool finished = workload_executer_switch_to_workload(workload);
            // Note: After a workload executes, it may have added new dependencies to itself
            if (workload->dependencies.count == 0)
            {
                assert(finished, "When on dependencies remain, the fiber should have exited normally!\n");
                workload->is_finished = true;
                List_Node<Analysis_Workload*>* node = workload->dependents.head;
                // Loop over all dependents and remove this workload from that list
                while (node != 0) {
                    Analysis_Workload* dependent = node->value;
                    node = node->next; // INFO: This is required before remove_dependency, since remove will remove nodes from the list
                    workload_executer_remove_dependency(dependent, workload, true);
                }
                assert(workload->dependents.count == 0, "Remove dependency should already have cleared the list!");
            }
            else {
                assert(!finished, "If there are dependencies, the fiber must still be running!");
            }
        }
        dynamic_array_reset(&executer.runnable_workloads);
        if (executer.progress_was_made) {
            if (PRINT_DEPENDENCIES) {
                logg("Progress was made!");
            }
            continue;
        }

        // Check if all workloads finished
        {
            bool all_finished = true;
            for (int i = 0; i < executer.all_workloads.size; i++) {
                if (!executer.all_workloads[i]->is_finished) {
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
            for (int i = 0; i < executer.all_workloads.size; i++) {
                Analysis_Workload* workload = executer.all_workloads[i];
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
                    Dependency_Information infos = *hashtable_find_element(&executer.workload_dependencies, pair);
                    if (infos.only_symbol_read_dependency) {
                        only_reads_was_found = true;
                        for (int j = 0; j < infos.symbol_reads.size; j++) {
                            symbol_read_set_error_symbol(infos.symbol_reads[j]);
                            semantic_analyser_log_error(Semantic_Error_Type::CYCLIC_DEPENDENCY_DETECTED, &infos.symbol_reads[j]->base);
                            for (int k = 0; k < workload_cycle.size; k++) {
                                Analysis_Workload* workload = workload_cycle[k];
                                semantic_analyser_add_error_info(error_information_make_cycle_workload(workload));
                            }
                        }
                        workload_executer_remove_dependency(workload, depends_on, true);
                    }
                }
                assert(only_reads_was_found, "");
                executer.progress_was_made = true;
                if (PRINT_DEPENDENCIES) {
                    logg("Resolved cyclic dependency loop!");
                }
            }
        }
        if (executer.progress_was_made) {
            if (PRINT_DEPENDENCIES) {
                logg("Progress was made!");
            }
            continue;
        }

        panic("Loops must have been resolved by now, so some progress needs to be have made..\n");
    }
}

Analysis_Workload* workload_executer_add_workload_empty(Analysis_Workload_Type type, Analysis_Item* item, Analysis_Progress* progress)
{
    auto& executer = workload_executer;
    executer.progress_was_made = true;
    assert(item != 0, "");

    Analysis_Workload* workload = new Analysis_Workload;
    workload->type = type;
    workload->progress = progress;
    workload->is_finished = false;
    workload->was_started = false;
    workload->cluster = 0;
    workload->dependencies = list_create<Analysis_Workload*>();
    workload->dependents = list_create<Analysis_Workload*>();
    workload->reachable_clusters = dynamic_array_create_empty<Analysis_Workload*>(1);

    switch (type)
    {
    case Analysis_Workload_Type::PROJECT_IMPORT: break;
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
    case Analysis_Workload_Type::FUNCTION_BODY: {
        assert(progress->type == Analysis_Progress_Type::FUNCTION, "");
        ((Function_Progress*)progress)->body_workload = workload;
        workload->options.function_body.block_stack = dynamic_array_create_empty<AST::Code_Block*>(1);
        break;
    }
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
        assert(progress->type == Analysis_Progress_Type::FUNCTION, "");
        ((Function_Progress*)progress)->compile_workload = workload;
        workload->options.cluster_compile.functions = dynamic_array_create_empty<ModTree_Function*>(1);
        break;
    default:panic("");
    }
    dynamic_array_push_back(&executer.all_workloads, workload);
    dynamic_array_push_back(&executer.runnable_workloads, workload); // Note: There exists a check for dependencies before executing runnable workloads, so this is ok

    return workload;
}

void workload_executer_add_analysis_items(Code_Source* source)
{
    auto& executer = workload_executer;
    executer.progress_was_made = true;
    // Create workload
    for (int i = 0; i < source->analysis_items.size; i++)
    {
        Analysis_Item* item = source->analysis_items[i];
        Analysis_Progress* progress = 0;
        switch (item->type)
        {
        case Analysis_Item_Type::IMPORT: {
            auto workload = workload_executer_add_workload_empty(Analysis_Workload_Type::PROJECT_IMPORT, item, 0);
            workload->options.import = AST::downcast<AST::Project_Import>(item->node);
            break;
        }
        case Analysis_Item_Type::BAKE:
        {
            progress = upcast(analysis_progress_create_bake(item));
            auto analysis_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::BAKE_ANALYSIS, item, progress);
            auto execute_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::BAKE_EXECUTION, item, progress);
            analysis_workload_add_dependency_internal(execute_workload, analysis_workload, 0);
            break;
        }
        case Analysis_Item_Type::DEFINITION:
        {
            progress = upcast(analysis_progress_create_definition(item->symbol, item));
            workload_executer_add_workload_empty(Analysis_Workload_Type::DEFINITION, item, progress);
            break;
        }
        case Analysis_Item_Type::FUNCTION:
        {
            assert(item->options.function_body_item->node->type == AST::Node_Type::CODE_BLOCK, "");
            auto func_progress = analysis_progress_create_function(item->options.function_body_item, item);
            progress = upcast(func_progress);

            if (item->symbol != 0) {
                item->symbol->type = Symbol_Type::FUNCTION;
                item->symbol->options.function = func_progress->function;
            }

            auto header_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::FUNCTION_HEADER, item, progress);
            auto body_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::FUNCTION_BODY, item->options.function_body_item, progress);
            bool body_item_registered = hashtable_insert_element(&workload_executer.progress_items, item->options.function_body_item, progress);
            assert(body_item_registered, "This may need to change with templates");

            analysis_workload_add_dependency_internal(body_workload, header_workload, 0);
            auto compile_workload = workload_executer_add_workload_empty(
                Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, item->options.function_body_item, progress
            );
            compile_workload->options.cluster_compile.functions = dynamic_array_create_empty<ModTree_Function*>(1);
            dynamic_array_push_back(&compile_workload->options.cluster_compile.functions, func_progress->function);
            analysis_workload_add_dependency_internal(compile_workload, body_workload, 0);
            break;
        }
        case Analysis_Item_Type::FUNCTION_BODY:
        {
            // Should already be done inside function
            break;
        }
        case Analysis_Item_Type::STRUCTURE:
        {
            assert(item->node->type == AST::Node_Type::EXPRESSION, "");
            Type_Signature* struct_type = type_system_make_struct_empty(
                &compiler.type_system, item->symbol,
                ((AST::Expression*)item->node)->options.structure.type
            );

            progress = upcast(analysis_progress_create_struct(struct_type, item));
            auto workload = workload_executer_add_workload_empty(Analysis_Workload_Type::STRUCT_ANALYSIS, item, progress);
            auto reachable_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE, item, progress);
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

    for (int i = 0; i < source->item_dependencies.size; i++)
    {
        auto& item_dependency = source->item_dependencies[i];
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
    if (!workload->is_finished && workload->dependencies.count == 0) {
        dynamic_array_push_back(&graph->runnable_workloads, workload);
        graph->progress_was_made = true;
    }
}

void analysis_workload_entry(void* userdata)
{
    Analysis_Workload* workload = (Analysis_Workload*)userdata;
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    analyser.current_workload = workload;

    workload->current_function = 0;
    workload->current_expression = 0;
    workload->current_pass = 0;
    workload->current_polymorphic_values.data = 0;
    workload->current_polymorphic_values.size = -1;
    workload->statement_reachable = false;

    switch (workload->type)
    {
    case Analysis_Workload_Type::PROJECT_IMPORT:
    {
        if (!compiler_add_project_import(workload->options.import)) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE, AST::upcast(workload->options.import));
        }
        break;
    }
    case Analysis_Workload_Type::DEFINITION:
    {
        assert(workload->progress->type == Analysis_Progress_Type::DEFINITION, "");
        auto progress = (Definition_Progress*)workload->progress;
        assert(progress->pass->item->node->type == AST::Node_Type::DEFINITION, "");
        auto definition = (AST::Definition*)progress->pass->item->node;
        assert(!(!definition->type.available && !definition->value.available), "Syntax should not allow no type and no definition!");
        workload->current_pass = progress->pass;

        Symbol* symbol = definition->symbol;

        if (!definition->is_comptime) // Global variable definition
        {
            Type_Signature* type = 0;
            if (definition->type.available) {
                type = semantic_analyser_analyse_expression_type(definition->type.value);
            }
            if (definition->value.available)
            {
                auto value_type = semantic_analyser_analyse_expression_value(
                    definition->value.value, type != 0 ? expression_context_make_specific_type(type) : expression_context_make_unknown()
                );
                if (type == 0) {
                    if (type_signature_equals(value_type, types.void_type)) {
                    }
                    type = value_type;
                }
            }
            auto global = modtree_program_add_global(type);
            if (definition->value.available) {
                modtree_global_set_init(global, workload->current_pass, definition->value.value);
            }

            symbol->type = Symbol_Type::GLOBAL;
            symbol->options.global = global;
        }
        else // Comptime definition
        {
            if (definition->type.available) {
                semantic_analyser_analyse_expression_type(definition->type.value);
                semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED, definition->type.value);
            }
            if (!definition->value.available) {
                semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_DEFINITION_REQUIRES_INITAL_VALUE, AST::upcast(definition));
                return;
            }

            auto result = semantic_analyser_analyse_expression_any(definition->value.value, expression_context_make_unknown());
            switch (result->result_type)
            {
            case Expression_Result_Type::VALUE:
            {
                Comptime_Result comptime = expression_calculate_comptime_value(definition->value.value);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    break;
                case Comptime_Result_Type::UNAVAILABLE: {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    return;
                }
                case Comptime_Result_Type::NOT_COMPTIME: {
                    semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN, definition->value.value);
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    return;
                }
                default: panic("");
                }

                Constant_Result result = constant_pool_add_constant(
                    &compiler.constant_pool, comptime.data_type, array_create_static((byte*)comptime.data, comptime.data_type->size)
                );
                if (result.status != Constant_Status::SUCCESS) {
                    semantic_analyser_log_error(Semantic_Error_Type::CONSTANT_POOL_ERROR, definition->value.value);
                    semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    break;
                }

                symbol->type = Symbol_Type::COMPTIME_VALUE;
                symbol->options.constant = result.constant;
                break;
            }
            case Expression_Result_Type::CONSTANT: {
                symbol->type = Symbol_Type::COMPTIME_VALUE;
                symbol->options.constant = result->options.constant;
                break;
            }
            case Expression_Result_Type::HARDCODED_FUNCTION:
            {
                symbol->type = Symbol_Type::ERROR_SYMBOL;
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(definition));
                semantic_analyser_add_error_info(error_information_make_text("Creating aliases for hardcoded functions currently not supported!\n"));
                break;
                //symbol->type = Symbol_Type::HARDCODED_FUNCTION;
                //symbol->options.hardcoded = result->options.hardcoded;
                //break;
            }
            case Expression_Result_Type::FUNCTION:
            {
                ModTree_Function* function = result->options.function;
                if (function->symbol != 0) {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(definition));
                    semantic_analyser_add_error_info(error_information_make_text("Creating symbol/function aliases currently not supported!\n"));
                    break;
                }

                // Note: I dont think this code can ever execute
                panic("i guess this never executes");
                symbol->type = Symbol_Type::FUNCTION;
                symbol->options.function = result->options.function;
                break;
            }
            case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
                symbol->type = Symbol_Type::ERROR_SYMBOL;
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(definition));
                semantic_analyser_add_error_info(error_information_make_text("Creating aliases for polymorphic functions not supported!"));
                break;
            }
            case Expression_Result_Type::MODULE: {
                // TODO: Maybe also disallow this if this is an alias, as above
                symbol->type = Symbol_Type::MODULE;
                symbol->options.module_table = result->options.module_table;
                break;
            }
            case Expression_Result_Type::TYPE: {
                // TODO: Maybe also disallow this if this is an alias, as above
                symbol->type = Symbol_Type::TYPE;
                symbol->options.type = result->options.type;
                break;
            }
            default: panic("");
            }
        }
        break;
    }
    case Analysis_Workload_Type::FUNCTION_HEADER:
    {
        assert(workload->progress->type == Analysis_Progress_Type::FUNCTION, "");
        auto progress = (Function_Progress*)workload->progress;
        auto& base_node = progress->header_pass->item->node;
        assert(base_node->type == AST::Node_Type::EXPRESSION && ((AST::Expression*)base_node)->type == AST::Expression_Type::FUNCTION, "");
        auto& header = ((AST::Expression*)base_node)->options.function;

        ModTree_Function* function = progress->function;
        workload->current_function = function;
        workload->current_pass = progress->header_pass;

        // Analyser Header
        auto& signature_node = header.signature->options.function_signature;

        // Analyse polymorphic parameters
        struct Polymorphic_Dependency
        {
            AST::Parameter* node;
            int parameter_index;
            int index_in_header; // List in header
            Dynamic_Array<int> depends_on;
            Dynamic_Array<int> dependees;
            int depending_count;
        };
        Dynamic_Array<Polymorphic_Dependency> poly_params = dynamic_array_create_empty<Polymorphic_Dependency>(1);
        SCOPE_EXIT(
            for (int i = 0; i < poly_params.size; i++) {
                auto& a = poly_params[i];
                dynamic_array_destroy(&a.depends_on);
                dynamic_array_destroy(&a.dependees);
            }
            dynamic_array_destroy(&poly_params)
        );
        Dynamic_Array<int> parameter_traversal_order = dynamic_array_create_empty<int>(1);
        SCOPE_EXIT(dynamic_array_destroy(&parameter_traversal_order));
        bool poly_parameters_valid = true;
        {
            // 1. Scan through header, find polymorphic parameters
            for (int i = 0; i < signature_node.parameters.size; i++)
            {
                auto& param = signature_node.parameters[i];
                if (!param->is_comptime) {
                    continue;
                }
                Polymorphic_Dependency parameter;
                parameter.parameter_index = poly_params.size;
                parameter.index_in_header = i;
                parameter.node = param;
                parameter.depending_count = 0;
                parameter.depends_on = dynamic_array_create_empty<int>(1);
                parameter.dependees = dynamic_array_create_empty<int>(1);
                dynamic_array_push_back(&poly_params, parameter);
            }

            // 2. Find dependencies between parameters
            for (int i = 0; i < poly_params.size; i++) {
                auto& poly_param = poly_params[i];
                auto scan_fn = [&](AST::Node* base) {
                    int child_index = 0;
                    auto child = AST::base_get_child(base, 0);
                    while (child != 0) {
                        if (child->type == AST::Node_Type::SYMBOL_READ) {
                            auto symbol = AST::downcast<AST::Symbol_Read>(child)->symbol;
                            assert(symbol->type != Symbol_Type::UNRESOLVED && symbol->type != Symbol_Type::PARAMETER, "Cannot happen!");
                            if (symbol->type == Symbol_Type::VARIABLE_UNDEFINED) {
                                assert(symbol->options.variable_undefined.is_parameter, "Cannot be variable!");
                                symbol->options.variable_undefined.parameter_index; // Parameter index doesn't necessarily tell us anything about the parameter!
                                for (int j = 0; j < poly_params.size; j++) {
                                    auto& other_param = poly_params[j];
                                    if (other_param.index_in_header == symbol->options.variable_undefined.parameter_index) {
                                        dynamic_array_push_back(&poly_param.depends_on, j);
                                        poly_param.depending_count += 1;
                                        dynamic_array_push_back(&other_param.dependees, i);
                                    }
                                }
                            }
                        }
                        child_index += 1;
                        child = AST::base_get_child(base, child_index);
                    }
                };
                scan_fn(AST::upcast(poly_param.node));
            }

            // 3. Generate a Traversal order, or report error for unknown parameters --> E.g. find smallest cycle when cycle exists, try to resolve
            // 3.1 add all initial runnable parameters
            for (int i = 0; i < poly_params.size; i++) {
                if (poly_params[i].depending_count == 0) {
                    dynamic_array_push_back(&parameter_traversal_order, i);
                }
            }
            // 3.2 execute in order
            int i = 0;
            while (i < parameter_traversal_order.size)
            {
                auto& param = poly_params[parameter_traversal_order[i]];
                for (int j = 0; j < param.dependees.size; j++) {
                    auto& other = poly_params[param.dependees[j]];
                    other.depending_count -= 1;
                    if (other.depending_count == 0) {
                        dynamic_array_push_back(&parameter_traversal_order, param.dependees[j]);
                    }
                }
                i += 1;
            }

            // Check if cycles exist
            if (parameter_traversal_order.size != poly_params.size) {
                // For now just log an error at the function header
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, header.signature);
                semantic_analyser_add_error_info(error_information_make_text("Polymorphic arguments have cyclic dependencies!"));
                poly_parameters_valid = false;
            }
        }

        // Always set current_polymorphic_values (Used when looking up Poly-Symbols) to 0 after finishing this function
        SCOPE_EXIT(workload->current_polymorphic_values.data = 0; workload->current_polymorphic_values.size = -1;);

        // Two code paths for normal and polymorphic functions
        if (poly_params.size != 0)
        {
            // Create poly function
            Polymorphic_Function* poly_function = new Polymorphic_Function;
            poly_function->instances = dynamic_array_create_empty<Polymorphic_Instance>(1);
            poly_function->is_valid = poly_parameters_valid;
            poly_function->parameters = array_create_empty<Polymorphic_Parameter>(poly_params.size);
            poly_function->parameter_count = signature_node.parameters.size;
            dynamic_array_push_back(&analyser.polymorphic_functions, poly_function);
            for (int i = 0; i < parameter_traversal_order.size; i++) {
                auto poly_param = poly_params[parameter_traversal_order[i]];
                poly_function->parameters[i].index_in_header = poly_param.index_in_header;
                poly_function->parameters[i].node = poly_param.node;
            }

            // Create poly-instance
            Polymorphic_Instance base_instance;
            base_instance.recursive_instanciation_depth = 0;
            base_instance.instanciation_site = workload->current_function;
            base_instance.function = function;
            base_instance.parameter_values = array_create_empty<Polymorphic_Value>(poly_params.size);
            for (int i = 0; i < base_instance.parameter_values.size; i++) {
                base_instance.parameter_values[i].is_not_set = true;
            }
            dynamic_array_push_back(&poly_function->instances, base_instance);

            // Mark function as polymorphic instance
            function->is_runnable = false;
            if (!poly_parameters_valid) {
                function->contains_errors = true; // No code needs to be generated for this function
            }
            function->is_polymorphic = true;
            function->polymorphic_base = poly_function;
            function->polymorphic_instance_index = 0;
            if (function->symbol != 0) {
                Symbol* symbol = function->symbol;
                symbol->type = Symbol_Type::POLYMORPHIC_FUNCTION;
                symbol->options.polymorphic_function = poly_function;
            }

            // Set comptime Parameter-Symbols
            workload->current_polymorphic_values = base_instance.parameter_values;
            for (int i = 0; i < parameter_traversal_order.size; i++) {
                auto& param = poly_params[i].node;
                poly_function->parameters[i].base_type = semantic_analyser_analyse_expression_type(param->type);

                Symbol* symbol = param->symbol;
                symbol->type = Symbol_Type::PARAMETER;
                symbol->options.parameter.is_polymorphic = true;
                symbol->options.parameter.index = i;
                symbol->options.parameter.function = function;
            }
        }
        else {
            // Set function infos
            function->is_polymorphic = false;
            function->polymorphic_base = 0;
            function->polymorphic_instance_index = -1;
            if (function->symbol != 0) {
                Symbol* symbol = function->symbol;
                symbol->type = Symbol_Type::FUNCTION;
                symbol->options.function = function;
            }
        }

        // Analyse function signature
        Dynamic_Array<Function_Parameter> param_types = dynamic_array_create_empty<Function_Parameter>(math_maximum(0, signature_node.parameters.size));
        int non_comptime_param_index = 0;
        for (int i = 0; i < signature_node.parameters.size; i++)
        {
            auto param = signature_node.parameters[i];
            if (param->is_comptime) {
                continue;
            }
            Symbol* symbol = param->symbol;
            symbol->type = Symbol_Type::PARAMETER;
            symbol->options.parameter.function = function;
            symbol->options.parameter.index = non_comptime_param_index;
            non_comptime_param_index += 1;
            symbol->options.parameter.is_polymorphic = false;
    
            Function_Parameter func_param;
            func_param.name = optional_make_success(param->name);
            func_param.type = semantic_analyser_analyse_expression_type(param->type);
            dynamic_array_push_back(&param_types, func_param);
        }
        Type_Signature* return_type = types.void_type;
        if (signature_node.return_value.available) {
            return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
        }
        function->signature = type_system_make_function(&type_system, param_types, return_type);

        // Advance Progress
        progress->state = Function_State::HEADER_ANALYSED;
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY:
    {
        assert(workload->progress->type == Analysis_Progress_Type::FUNCTION, "");
        auto progress = (Function_Progress*)workload->progress;
        auto& base_node = progress->body_pass->item->node;
        assert(base_node->type == AST::Node_Type::CODE_BLOCK, "");
        auto code_block = ((AST::Code_Block*)base_node);

        auto function = progress->function;
        if (function->polymorphic_base != 0) {
            // Note: I think I cannot just set contains errors to 
            if (function->polymorphic_instance_index != 0 && function->polymorphic_base->instances[0].function->contains_errors) {
                function->contains_errors = true;
                function->is_runnable = false;
                progress->state = Function_State::BODY_ANALYSED;
                break;
            }
            workload->current_polymorphic_values = function->polymorphic_base->instances[function->polymorphic_instance_index].parameter_values;
        }
        SCOPE_EXIT(workload->current_polymorphic_values.data = 0; workload->current_polymorphic_values.size = -1;);
        workload->current_function = function;
        workload->current_pass = progress->body_pass;

        dynamic_array_reset(&workload->options.function_body.block_stack);
        workload->statement_reachable = true;

        Control_Flow flow = semantic_analyser_analyse_block(code_block);
        if (flow != Control_Flow::RETURNS && !type_signature_equals(function->signature->options.function.return_type, types.void_type)) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, base_node);
        }
        progress->state = Function_State::BODY_ANALYSED;
        break;
    }
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
    {
        assert(workload->progress->type == Analysis_Progress_Type::FUNCTION, "");
        auto progress = (Function_Progress*)workload->progress;

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
                ir_generator_queue_function(function);
            }
            Function_Progress* progress = *hashtable_find_element(&workload_executer.progress_functions, function);
            progress->state = Function_State::FINISHED;
        }
        break;
    }
    case Analysis_Workload_Type::STRUCT_ANALYSIS:
    {
        assert(workload->progress->type == Analysis_Progress_Type::STRUCTURE, "");
        auto progress = (Struct_Progress*)workload->progress;
        workload->current_pass = progress->pass;

        auto& base_node = progress->pass->item->node;
        assert(base_node->type == AST::Node_Type::EXPRESSION && ((AST::Expression*)base_node)->type == AST::Expression_Type::STRUCTURE_TYPE, "");
        auto& struct_node = ((AST::Expression*)base_node)->options.structure;
        Type_Signature* struct_signature = progress->struct_type;

        for (int i = 0; i < struct_node.members.size; i++)
        {
            auto member_node = struct_node.members[i];
            if (member_node->value.available) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_MEMBER_MUST_NOT_HAVE_VALUE, AST::upcast(member_node));
            }
            if (!member_node->type.available) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_MEMBER_REQUIRES_TYPE, AST::upcast(member_node->value.value));
                continue;
            }

            Struct_Member member;
            member.id = member_node->name;
            member.offset = 0;
            member.type = semantic_analyser_analyse_expression_type(member_node->type.value);
            assert(!(member.type->size == 0 && member.type->alignment == 0), "Must not happen with current Dependency-System");
            dynamic_array_push_back(&struct_signature->options.structure.members, member);
        }
        progress->state = Struct_State::SIZE_KNOWN;
        type_system_finish_type(&type_system, struct_signature);
        break;
    }
    case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
    {
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
        assert(workload->progress->type == Analysis_Progress_Type::BAKE, "");
        auto progress = (Bake_Progress*)workload->progress;
        workload->current_pass = progress->pass;
        workload->current_function = progress->bake_function;

        auto& base_node = progress->pass->item->node;
        assert(base_node->type == AST::Node_Type::EXPRESSION, "");
        auto expr = ((AST::Expression*) base_node);
        if (expr->type == AST::Expression_Type::BAKE_BLOCK)
        {
            auto& code_block = expr->options.bake_block;
            progress->bake_function->signature = type_system_make_function(&type_system, {}, types.void_type);
            auto flow = semantic_analyser_analyse_block(expr->options.bake_block);
            if (flow != Control_Flow::RETURNS) {
                semantic_analyser_log_error(Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, AST::upcast(expr->options.bake_block));
            }
        }
        else if (expr->type == AST::Expression_Type::BAKE_EXPR)
        {
            auto& bake_expr = expr->options.bake_expr;
            auto result_type = semantic_analyser_analyse_expression_value(expr->options.bake_expr, expression_context_make_unknown());
            // Set Function type
            progress->bake_function->signature = type_system_make_function(&type_system, {}, result_type);
        }
        else {
            panic("");
        }
        break;
    }
    case Analysis_Workload_Type::BAKE_EXECUTION:
    {
        auto& interpreter = compiler.bytecode_interpreter;
        auto& ir_gen = compiler.ir_generator;

        assert(workload->progress->type == Analysis_Progress_Type::BAKE, "");
        auto progress = (Bake_Progress*)workload->progress;
        auto bake_function = progress->bake_function;

        // Check if function compilation succeeded
        if (bake_function->contains_errors) {
            progress->result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
            return;
        }
        for (int i = 0; i < bake_function->calls.size; i++) {
            if (!bake_function->calls[i]->is_runnable) {
                bake_function->is_runnable = false;
                progress->result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
                return;
            }
        }

        // Compile
        ir_generator_queue_function(bake_function);
        ir_generator_generate_queued_items(true);

        // Set Global Type Informations
        {
            bytecode_interpreter_prepare_run(interpreter);
            Upp_Slice<Internal_Type_Information>* info_slice = (Upp_Slice<Internal_Type_Information>*)
                & interpreter->globals.data[
                    compiler.bytecode_generator->global_data_offsets[analyser.global_type_informations->index]
                ];
            info_slice->size = type_system.internal_type_infos.size;
            info_slice->data_ptr = type_system.internal_type_infos.data;
        }

        // Execute
        IR_Function* ir_func = *hashtable_find_element(&ir_gen->function_mapping, bake_function);
        int func_start_instr_index = *hashtable_find_element(&compiler.bytecode_generator->function_locations, ir_func);
        interpreter->instruction_limit_enabled = true;
        interpreter->instruction_limit = 5000;
        bytecode_interpreter_run_function(interpreter, func_start_instr_index);
        if (interpreter->exit_code != Exit_Code::SUCCESS) {
            semantic_analyser_log_error(Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED, progress->pass->item->node);
            semantic_analyser_add_error_info(error_information_make_exit_code(interpreter->exit_code));
            progress->result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
            return;
        }

        void* value_ptr = interpreter->return_register;
        progress->result = comptime_result_make_available(value_ptr, bake_function->signature->options.function.return_type);
        return;
    }
    default: panic("");
    }

    return;
    // OLD EXTERN IMPORTS
    /*
case Analysis_Workload_Type::EXTERN_HEADER_IMPORT:
{
    AST_Node* extern_node = workload.options.extern_header.node;
    String* header_name_id = extern_node->id;
    Optional<C_Import_Package> package = c_importer_import_header(&compiler.c_importer, *header_name_id, &compiler.identifier_pool);
    if (package.available)
    {
        logg("Importing header successfull: %s\n", header_name_id->characters);
        dynamic_array_push_back(&compiler.extern_sources.headers_to_include, header_name_id);
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
                    hashtable_insert_element(&compiler.extern_sources.extern_type_signatures, type, import_id);
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
                        hashtable_insert_element(&compiler.extern_sources.extern_type_signatures, type, id);
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
        dynamic_array_push_back(&compiler.extern_sources.extern_functions, extern_fn->options.extern_function);

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

bool workload_executer_switch_to_workload(Analysis_Workload* workload)
{
    if (!workload->was_started) {
        workload->fiber_handle = fiber_pool_get_handle(compiler.fiber_pool, analysis_workload_entry, workload);
        workload->was_started = true;
    }
    semantic_analyser.current_workload = workload;
    bool result = fiber_pool_switch_to_handel(workload->fiber_handle);
    semantic_analyser.current_workload = 0;
    return result;
}

void workload_executer_wait_for_dependency_resolution()
{
    Analysis_Workload* workload = semantic_analyser.current_workload;
    if (workload->dependencies.count != 0) {
        fiber_pool_switch_to_main_fiber(compiler.fiber_pool);
    }
}

void analysis_workload_append_to_string(Analysis_Workload* workload, String* string)
{
    switch (workload->type)
    {
    case Analysis_Workload_Type::PROJECT_IMPORT: {
        auto import = workload->options.import;
        string_append_formated(string, "Project Import %s\n", import->filename->characters);
        break;
    }
    case Analysis_Workload_Type::DEFINITION: {
        AST::Definition* definition = (AST::Definition*) ((Definition_Progress*)workload->progress)->pass->item->node;
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
        Symbol* symbol = ((Function_Progress*)workload->progress)->header_pass->item->symbol;
        auto function = ((Function_Progress*)workload->progress)->function;
        const char* fn_id = symbol == 0 ? "Lambda" : symbol->id->characters;
        string_append_formated(string, "Body \"%s\"", fn_id);
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
        Symbol* symbol = ((Function_Progress*)workload->progress)->header_pass->item->symbol;
        const char* fn_id = symbol == 0 ? "Anonymous" : symbol->id->characters;
        string_append_formated(string, "Header \"%s\"", fn_id);
        break;
    }
    case Analysis_Workload_Type::STRUCT_ANALYSIS: {
        Symbol* symbol = ((Struct_Progress*)workload->progress)->pass->item->symbol;
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




// EXPRESSIONS
void semantic_analyser_register_function_call(ModTree_Function* call_to)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;

    auto workload = analyser.current_workload;
    if (workload->current_function != 0) {
        dynamic_array_push_back(&workload->current_function->calls, call_to);
        dynamic_array_push_back(&call_to->called_from, workload->current_function);
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

Info_Cast_Type semantic_analyser_check_cast_type(Type_Signature* source_type, Type_Signature* destination_type, bool implicit_cast)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    if (type_signature_equals(source_type, types.unknown_type) || type_signature_equals(destination_type, types.unknown_type)) {
        semantic_analyser_set_error_flag(true);
        return Info_Cast_Type::NO_CAST;
    }
    if (type_signature_equals(source_type, destination_type)) return Info_Cast_Type::NO_CAST;
    bool cast_valid = false;
    Info_Cast_Type cast_type = Info_Cast_Type::INVALID;
    // Pointer casting
    if (source_type->type == Signature_Type::POINTER || destination_type->type == Signature_Type::POINTER)
    {
        if (source_type->type == Signature_Type::POINTER && destination_type->type == Signature_Type::POINTER)
        {
            cast_type = Info_Cast_Type::POINTERS;
            if (implicit_cast) {
                cast_valid = type_signature_equals(source_type, types.void_ptr_type) ||
                    type_signature_equals(destination_type, types.void_ptr_type);
            }
            else {
                cast_valid = true;
            }
        }
    }
    else if (source_type->type == Signature_Type::FUNCTION || destination_type->type == Signature_Type::FUNCTION) {
        // Check if types are the same
        auto& src_fn = source_type->options.function;
        auto& dst_fn = destination_type->options.function;
        cast_valid = true;
        cast_type = Info_Cast_Type::POINTERS;
        if (src_fn.parameters.size != dst_fn.parameters.size) {
            cast_valid = false;
        }
        else {
            for (int i = 0; i < src_fn.parameters.size; i++) {
                auto& param1 = src_fn.parameters[i];
                auto& param2 = dst_fn.parameters[i];
                if (!type_signature_equals(param1.type, param2.type)) {
                    cast_valid = false;
                }
            }
        }
        if (!type_signature_equals(src_fn.return_type, dst_fn.return_type)) {
            cast_valid = false;
        }
    }
    // Primitive Casting:
    else if (source_type->type == Signature_Type::PRIMITIVE && destination_type->type == Signature_Type::PRIMITIVE)
    {
        if (source_type->options.primitive.type == Primitive_Type::INTEGER && destination_type->options.primitive.type == Primitive_Type::INTEGER)
        {
            cast_type = Info_Cast_Type::INTEGERS;
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
            cast_type = Info_Cast_Type::INT_TO_FLOAT;
            cast_valid = true;
        }
        else if (destination_type->options.primitive.type == Primitive_Type::INTEGER && source_type->options.primitive.type == Primitive_Type::FLOAT)
        {
            cast_type = Info_Cast_Type::FLOAT_TO_INT;
            cast_valid = !implicit_cast;
        }
        else if (destination_type->options.primitive.type == Primitive_Type::FLOAT && source_type->options.primitive.type == Primitive_Type::FLOAT)
        {
            cast_type = Info_Cast_Type::FLOATS;
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
            cast_type = Info_Cast_Type::ARRAY_TO_SLICE;
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
            cast_type = enum_is_source ? Info_Cast_Type::ENUM_TO_INT : Info_Cast_Type::INT_TO_ENUM;
            cast_valid = !implicit_cast;
        }
    }
    // Any casting
    else if (type_signature_equals(destination_type, types.any_type))
    {
        cast_valid = true;
        cast_type = Info_Cast_Type::TO_ANY;
    }
    else if (type_signature_equals(source_type, types.any_type)) {
        cast_valid = !implicit_cast;
        cast_type = Info_Cast_Type::FROM_ANY;
    }

    if (cast_valid) return cast_type;
    return Info_Cast_Type::INVALID;
}

void expression_info_set_cast(Expression_Info* info, Info_Cast_Type cast_type, Type_Signature* result_type)
{
    info->context_ops.after_cast_type = result_type;
    info->context_ops.cast = cast_type;
}

#define SET_ACTIVE_EXPR_INFO(new_info)\
    Expression_Info* _backup_info = semantic_analyser.current_workload->current_expression; \
    semantic_analyser.current_workload->current_expression = new_info; \
    SCOPE_EXIT(semantic_analyser.current_workload->current_expression = _backup_info);

Expression_Info* semantic_analyser_analyse_expression_internal(AST::Expression* expr, Expression_Context context)
{
    auto& analyser = semantic_analyser;
    auto type_system = &compiler.type_system;
    auto& types = type_system->predefined_types;
    auto info = pass_get_info(expr);
    SET_ACTIVE_EXPR_INFO(info);
    info->context = context;
    info->context_ops.after_cast_type = 0;
    info->context_ops.cast = Info_Cast_Type::NO_CAST;
    info->context_ops.deref_count = 0;
    info->context_ops.take_address_of = false;
    expression_info_set_error(info, types.unknown_type); // Just initialize with some values
    info->contains_errors = false; // To undo the previous set error

#define EXIT_VALUE(val) expression_info_set_value(info, val); return info;
#define EXIT_TYPE(type) expression_info_set_type(info, type); return info;
#define EXIT_ERROR(type) expression_info_set_error(info, type); return info;
#define EXIT_HARDCODED(hardcoded) expression_info_set_hardcoded(info, hardcoded); return info;
#define EXIT_FUNCTION(function) expression_info_set_function(info, function); return info;

    switch (expr->type)
    {
    case AST::Expression_Type::ERROR_EXPR: {
        semantic_analyser_set_error_flag(false);// Error due to parsing, dont log error message because we already have parse error messages
        EXIT_ERROR(types.unknown_type);
    }
    case AST::Expression_Type::FUNCTION_CALL:
    {
        // Analyse call expression
        auto& call = expr->options.call;
        auto function_expr_info = semantic_analyser_analyse_expression_any(call.expr, expression_context_make_auto_dereference());

        // Initialize all argument infos as valid 
        for (int i = 0; i < call.arguments.size; i++) {
            auto argument = pass_get_info(call.arguments[i]);
            argument->valid = true;
            argument->argument_index = i;
        }
        info->specifics.function_call_signature = 0;

        // Handle Type-Of (Or in the future other compiler given functions)
        if (function_expr_info->result_type == Expression_Result_Type::HARDCODED_FUNCTION && function_expr_info->options.hardcoded == Hardcoded_Type::TYPE_OF)
        {
            if (call.arguments.size != 1) {
                semantic_analyser_log_error(Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, call.expr);
                semantic_analyser_add_error_info(error_information_make_argument_count(call.arguments.size, 1));
                EXIT_ERROR(types.unknown_type);
            }
            auto& arg = call.arguments[0];
            if (arg->name.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, &arg->base);
                semantic_analyser_add_error_info(error_information_make_text("Argument name for type_of must not be given"));
            }

            auto arg_result = semantic_analyser_analyse_expression_any(arg->value, expression_context_make_unknown());
            switch (arg_result->result_type)
            {
            case Expression_Result_Type::VALUE: {
                EXIT_TYPE(arg_result->options.type);
            }
            case Expression_Result_Type::HARDCODED_FUNCTION: {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, arg->value);
                semantic_analyser_add_error_info(error_information_make_text("Cannot use type_of on hardcoded functions!"));
                EXIT_ERROR(types.unknown_type);
            }
            case Expression_Result_Type::CONSTANT: {
                EXIT_TYPE(arg_result->options.constant.type);
            }
            case Expression_Result_Type::FUNCTION: {
                EXIT_TYPE(arg_result->options.function->signature);
            }
            case Expression_Result_Type::TYPE: {
                EXIT_TYPE(types.type_type);
            }
            case Expression_Result_Type::POLYMORPHIC_FUNCTION:
            case Expression_Result_Type::MODULE: {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, arg->value);
                semantic_analyser_add_error_info(error_information_make_expression_result_type(arg_result->result_type));
                EXIT_ERROR(types.unknown_type);
            }
            default: panic("");
            }

            panic("");
            EXIT_ERROR(arg_result->options.type);
        }

        Type_Signature* function_signature = 0; // Note: I still want to analyse all arguments even if the signature is null
        switch (function_expr_info->result_type)
        {
        case Expression_Result_Type::FUNCTION: {
            function_signature = function_expr_info->options.function->signature;
            break;
        }
        case Expression_Result_Type::HARDCODED_FUNCTION:
        {
            function_signature = hardcoded_type_to_signature(function_expr_info->options.hardcoded);
            break;
        }
        case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
            auto poly_function = function_expr_info->options.polymorphic.function;
            auto& arguments = call.arguments;
            if (arguments.size != poly_function->parameter_count) {
                // TODO: In theory I could do something smarter here, with default values and named parameters I will need to do something else
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(expr));
                semantic_analyser_add_error_info(error_information_make_text("Argument count did not match parameter count!"));

                function_signature = 0;
                break;
            }
            if (!poly_function->is_valid) {
                function_signature = 0;
                break;
            }

            // Create analysis pass for re-analysing base-header 
            auto base_progress = *hashtable_find_element(&workload_executer.progress_functions, poly_function->instances[0].function);
            Analysis_Pass* base_header_pass = analysis_item_create_pass(base_progress->header_pass->item);

            // Create and set polymorphic values array
            Array<Polymorphic_Value> poly_values = array_create_empty<Polymorphic_Value>(poly_function->parameters.size);
            bool success = true;
            SCOPE_EXIT(if (!success) { array_destroy(&poly_values); });

            auto backup_values = analyser.current_workload->current_polymorphic_values;
            SCOPE_EXIT(analyser.current_workload->current_polymorphic_values = backup_values);
            analyser.current_workload->current_polymorphic_values = poly_values;

            // Evaluate polymorphic parameters in evaluation order
            for (int i = 0; i < poly_function->parameters.size; i++) {
                auto& parameter = poly_function->parameters[i].node;
                auto& poly_value = poly_values[i];
                assert(parameter->is_comptime, "Poly function parameters need to be comptime");
                auto argument = arguments[poly_function->parameters[i].index_in_header];
                pass_get_info(argument)->valid = false;

                // Re-analyse base-header to get valid poly-argument type (Since this type can change with filled out polymorphic values)
                Type_Signature* argument_type = 0;
                {
                    auto backup_pass = semantic_analyser.current_workload->current_pass;
                    SCOPE_EXIT(semantic_analyser.current_workload->current_pass = backup_pass);
                    semantic_analyser.current_workload->current_pass = base_header_pass;
                    argument_type = semantic_analyser_analyse_expression_type(parameter->type);
                }

                semantic_analyser_analyse_expression_value(argument->value, expression_context_make_specific_type(argument_type));
                auto comptime_result = expression_calculate_comptime_value(argument->value);
                switch (comptime_result.type)
                {
                case Comptime_Result_Type::AVAILABLE: {
                    if (comptime_result.data_type == types.unknown_type) {
                        panic("Lets panic for now, I'm not quite sure if this happens in recursive instanciations -_-");
                    }
                    Constant_Result result = constant_pool_add_constant(
                        &compiler.constant_pool,
                        comptime_result.data_type,
                        array_create_static((byte*)comptime_result.data, comptime_result.data_type->size)
                    );
                    if (result.status != Constant_Status::SUCCESS) {
                        semantic_analyser_log_error(Semantic_Error_Type::CONSTANT_POOL_ERROR, AST::upcast(argument->value));
                        semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
                        poly_value.is_not_set = true;
                        success = false;
                        break;
                    }
                    poly_value.is_not_set = false;
                    poly_value.constant = result.constant;
                    break;
                }
                case Comptime_Result_Type::UNAVAILABLE: {
                    poly_value.is_not_set = true;
                    break;
                }
                case Comptime_Result_Type::NOT_COMPTIME:
                    semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(argument->value));
                    semantic_analyser_add_error_info(error_information_make_text("For instanciation values must be comptime!"));
                    success = false;
                    break;
                }
            }

            if (!success)
            {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(expr));
                semantic_analyser_add_error_info(error_information_make_text("Some values couldn't be calculated at comptime!"));
                break;
            }

            // Instanciate polymorphic function
            // Go through all created instances to check if we already have created it
            bool found_instance = false;
            for (int i = 0; i < poly_function->instances.size; i++) {
                auto& instance = poly_function->instances[i];
                bool all_matching = true;
                for (int j = 0; j < instance.parameter_values.size; j++) {
                    auto& inst_param = instance.parameter_values[j];
                    auto& current_param = poly_values[j];
                    if (inst_param.is_not_set != current_param.is_not_set) {
                        all_matching = false;
                        break;
                    }
                    if (!inst_param.is_not_set) {
                        if (!constant_pool_compare_constants(&compiler.constant_pool, inst_param.constant, current_param.constant)) {
                            all_matching = false;
                            break;
                        }
                    }
                }

                if (all_matching) {
                    // Use given instance instead of creating a new one!
                    // Set expression info to instanciation, so that ir-generator can pick the corresponding modtree-function
                    logg("Found matching instance %d, not re-instanciating!\n", i);
                    function_expr_info->options.polymorphic.instance_index = i;
                    function_signature = instance.function->signature;
                    found_instance = true;
                    break;
                }
            }
            if (found_instance) {
                break;
            }

            // Re-Analyse base function signature to get valid function
            function_signature = 0;
            {
                auto backup_pass = semantic_analyser.current_workload->current_pass;
                SCOPE_EXIT(semantic_analyser.current_workload->current_pass = backup_pass);
                semantic_analyser.current_workload->current_pass = base_header_pass;

                auto& signature_node = AST::downcast<AST::Expression>(semantic_analyser.current_workload->current_pass->item->node)->options.function.signature->options.function_signature;
                Dynamic_Array<Function_Parameter> param_types = dynamic_array_create_empty<Function_Parameter>(1);
                for (int i = 0; i < signature_node.parameters.size; i++)
                {
                    auto param = signature_node.parameters[i];
                    if (param->is_comptime) {
                        continue;
                    }
                    Function_Parameter func_param;
                    func_param.name = optional_make_success(param->name);
                    func_param.type = semantic_analyser_analyse_expression_type(param->type);
                    dynamic_array_push_back(&param_types, func_param);
                }
                Type_Signature* return_type = types.void_type;
                if (signature_node.return_value.available) {
                    return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
                }
                function_signature= type_system_make_function(type_system, param_types, return_type);
            }

            // Create new instance 
            ModTree_Function* current_function = analyser.current_workload->current_function;
            Polymorphic_Instance instance;
            instance.instanciation_site = current_function;
            instance.recursive_instanciation_depth = 0;
            if (current_function->polymorphic_base != 0) {
                auto& mother_instance = current_function->polymorphic_base->instances[current_function->polymorphic_instance_index];
                instance.recursive_instanciation_depth = mother_instance.recursive_instanciation_depth + 1;
            }

            // Stop recursive instanciates at some threshold
            if (instance.recursive_instanciation_depth > 10) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, expr);
                semantic_analyser_add_error_info(error_information_make_text("Polymorphic function instanciation reached depth limit 10!"));
                function_signature = 0;
                // TODO: Add more error information, e.g. finding a cycle or printing the mother instances!
                break;
            }

            // Create new function
            auto body_item = base_progress->body_pass->item;

            auto function_progress = analysis_progress_create_function(body_item, 0);
            function_progress->header_pass = base_header_pass;
            function_progress->state = Function_State::HEADER_ANALYSED;
            function_progress->function->polymorphic_base = poly_function;
            function_progress->function->polymorphic_instance_index = poly_function->instances.size;
            function_progress->function->signature = function_signature;

            // Add function as polymorphic instance
            instance.function = function_progress->function;
            instance.parameter_values = poly_values;
            dynamic_array_push_back(&poly_function->instances, instance);
            // Set expression info to instanciation, so that ir-generator can pick the corresponding modtree-function
            function_expr_info->options.polymorphic.instance_index = function_progress->function->polymorphic_instance_index;

            // Create body and compile workload
            auto body_workload = workload_executer_add_workload_empty(Analysis_Workload_Type::FUNCTION_BODY, body_item, upcast(function_progress));
            analysis_workload_add_dependency_internal(body_workload, base_progress->body_workload, 0);
            auto compile_workload = workload_executer_add_workload_empty(
                Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, body_item, upcast(function_progress)
            );
            compile_workload->options.cluster_compile.functions = dynamic_array_create_empty<ModTree_Function*>(1);
            dynamic_array_push_back(&compile_workload->options.cluster_compile.functions, function_progress->function);
            analysis_workload_add_dependency_internal(compile_workload, body_workload, 0);

            break;
        }
        case Expression_Result_Type::VALUE:
        {
            // TODO: Check if this is comptime known, then we dont need a function pointer call
            function_signature = function_expr_info->options.type;
            if (function_signature->type != Signature_Type::FUNCTION) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(function_signature));
                function_signature = 0;
            }
            break;
        }
        case Expression_Result_Type::CONSTANT:
        case Expression_Result_Type::MODULE:
        case Expression_Result_Type::TYPE: {
            semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_CALLABLE, expr);
            semantic_analyser_add_error_info(error_information_make_expression_result_type(function_expr_info->result_type));
            function_signature = 0;
            break;
        }
        default: panic("");
        }
        info->specifics.function_call_signature = function_signature;

        // Handle unknown function
        auto& arguments = call.arguments;
        if (function_signature == 0) {
            // Analyse all expressions with unknown context
            for (int i = 0; i < arguments.size; i++) {
                semantic_analyser_analyse_expression_value(arguments[i]->value, expression_context_make_unknown());
            }
            EXIT_ERROR(types.unknown_type);
        }

        // Analyse arguments
        auto& parameters = function_signature->options.function.parameters;
        int valid_argument_count = 0;
        for (int i = 0; i < arguments.size; i++) {
            auto info = pass_get_info(arguments[i]);
            if (info->valid) {
                semantic_analyser_analyse_expression_value(
                    arguments[i]->value,
                    expression_context_make_specific_type(function_signature->options.function.parameters[valid_argument_count].type)
                );
                info->argument_index = valid_argument_count;
                valid_argument_count += 1;
            }
        }
        if (valid_argument_count != parameters.size) {
            semantic_analyser_log_error(Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, expr);
            semantic_analyser_add_error_info(error_information_make_argument_count(arguments.size, parameters.size));
            semantic_analyser_add_error_info(error_information_make_function_type(function_signature));
        }
        EXIT_VALUE(function_signature->options.function.return_type);
    }
    case AST::Expression_Type::SYMBOL_READ:
    {
        auto read = expr->options.symbol_read;
        // NOTE: The symbol is only resolved if another workload of the !same! Analysis_item has already solved it
        //       e.g. only in Template-instanciations currently.
        //       This behaviour should probably change to be more flexible with other features (e.g. conditional compilation)
        //       Currently we also DON'T add dependencies to resolved symbols, which isn't a problem for templates _now_
        //       because instances already have to wait for their parent analysis to be completed, e.g. symbols to be resolved.
        if (read->symbol == 0)  // Symbol isn't resolved yet
        {
            Symbol* symbol = symbol_read_resolve_path(read);
            assert(symbol != 0, "In error cases this should be set to error, never 0!");
            AST::Symbol_Read* read_end = read;
            while (read_end->path_child.available) {
                read_end = read_end->path_child.value;
            }

            // Check if the symbol read introduces any dependencies
            auto& executer = *analyser.workload_executer;
            auto workload = analyser.current_workload;
            switch (symbol->type)
            {
            case Symbol_Type::UNRESOLVED:
            {
                Definition_Progress** progress = hashtable_find_element(&executer.progress_definitions, symbol);
                assert(progress != 0, "Must not happen with current dependency system!");
                analysis_workload_add_dependency_internal(analyser.current_workload, (*progress)->definition_workload, read_end);
                break;
            }
            case Symbol_Type::FUNCTION:
            {
                Function_Progress** progress = hashtable_find_element(&executer.progress_functions, symbol->options.function);
                assert(progress != 0, "Must not happen with current dependency system!");
                analysis_workload_add_dependency_internal(workload, (*progress)->header_workload, read_end);
                break;
            }
            case Symbol_Type::POLYMORPHIC_FUNCTION:
            {
                // Note: This isn't tested, not sure if this works/has a use case
                Function_Progress** progress = hashtable_find_element(&executer.progress_functions, symbol->options.polymorphic_function->instances[0].function);
                assert(progress != 0, "Must not happen with current dependency system!");
                analysis_workload_add_dependency_internal(workload, (*progress)->header_workload, read_end);
                break;
            }
            case Symbol_Type::TYPE:
            {
                Type_Signature* type = symbol->options.type;
                if (type->type == Signature_Type::STRUCT)
                {
                    Struct_Progress** progress = hashtable_find_element(&executer.progress_structs, type);
                    if (progress != 0) {
                        if (workload->progress->type == Analysis_Progress_Type::STRUCTURE) {
                            analysis_workload_add_struct_dependency((Struct_Progress*)workload->progress, *progress, read_end->type, read_end);
                        }
                        else {
                            analysis_workload_add_dependency_internal(workload, (*progress)->reachable_resolve_workload, read_end);
                        }
                    }
                    else {
                        // Progress may be 0 if its a predefined struct
                        assert(!(type->size == 0 && type->alignment == 0), "");
                    }
                }
                break;
            }
            default: break;
            }

            workload_executer_wait_for_dependency_resolution();
        }

        while (read->path_child.available) {
            read = read->path_child.value;
        }
        Symbol* symbol = read->symbol;
        assert(symbol != 0, "Must be given by dependency analysis");
        switch (symbol->type)
        {
        case Symbol_Type::ERROR_SYMBOL: {
            semantic_analyser_set_error_flag(true);
            EXIT_ERROR(types.unknown_type);
        }
        case Symbol_Type::UNRESOLVED: {
            panic("Should not happen");
        }
        case Symbol_Type::HARDCODED_FUNCTION: {
            EXIT_HARDCODED(symbol->options.hardcoded);
        }
        case Symbol_Type::FUNCTION: {
            semantic_analyser_register_function_call(symbol->options.function);
            EXIT_FUNCTION(symbol->options.function);
        }
        case Symbol_Type::GLOBAL: {
            EXIT_VALUE(symbol->options.global->type);
        }
        case Symbol_Type::TYPE: {
            EXIT_TYPE(symbol->options.type);
        }
        case Symbol_Type::VARIABLE: {
            assert(analyser.current_workload->type != Analysis_Workload_Type::FUNCTION_HEADER, "Function headers can never access variable symbols!");
            EXIT_VALUE(symbol->options.variable_type);
        }
        case Symbol_Type::VARIABLE_UNDEFINED: {
            if (analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
                if (symbol->options.variable_undefined.is_parameter) {
                    semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, expr);
                    semantic_analyser_add_error_info(error_information_make_text("Parameter hasn't been analysed yet"));
                    EXIT_ERROR(types.unknown_type);
                }
                else {
                    panic("A function header should never have access to a variable symbol!");
                }
            }
            semantic_analyser_log_error(Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET, expr);
            EXIT_ERROR(types.unknown_type);
        }
        case Symbol_Type::PARAMETER: {
            auto& param = symbol->options.parameter;
            assert((analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_BODY ||
                analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_HEADER),
                "Parameters can only be accessed in corresponding body/header!"
            );
            if (param.is_polymorphic) 
            {
                assert(param.function->is_polymorphic, "");
                const auto& poly_value = analyser.current_workload->current_polymorphic_values[param.index];
                if (poly_value.is_not_set) {
                    EXIT_ERROR(param.function->polymorphic_base->parameters[param.index].base_type);
                }
                expression_info_set_constant(info, poly_value.constant);
                return info;
            }
            else 
            {
                if (analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
                    semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, expr);
                    semantic_analyser_add_error_info(error_information_make_text("Function headers cannot access normal parameters!"));
                    EXIT_ERROR(types.unknown_type);
                }
                EXIT_VALUE(analyser.current_workload->current_function->signature->options.function.parameters[param.index].type);
            }
            panic("Cannot happen!");
        }
        case Symbol_Type::COMPTIME_VALUE: {
            expression_info_set_constant(info, symbol->options.constant);
            return info;
        }
        case Symbol_Type::MODULE: {
            semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_MODULE_INVALID, expr);
            semantic_analyser_add_error_info(error_information_make_symbol(symbol));
            EXIT_ERROR(types.unknown_type);
        }
        case Symbol_Type::POLYMORPHIC_FUNCTION: {
            expression_info_set_polymorphic_function(info, symbol->options.polymorphic_function, 0);
            return info;
            //semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, expr);
            //semantic_analyser_add_error_info(error_information_make_symbol(symbol));
            //semantic_analyser_add_error_info(error_information_make_text("Cannot address polymorphic functions currently!"));
            //EXIT_ERROR(types.unknown_type);
        }
        default: panic("HEY");
        }

        panic("HEY");
        break;
    }
    case AST::Expression_Type::CAST:
    {
        auto cast = &expr->options.cast;
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
                    semantic_analyser_log_error(Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED, expr);
                    EXIT_ERROR(types.unknown_type);
                }
                destination_type = context.signature;
            }
            break;
        }
        case AST::Cast_Type::RAW_TO_PTR:
        {
            if (destination_type == 0) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expr);
                destination_type = types.unknown_type;
            }
            else {
                operand_context = expression_context_make_specific_type(types.u64_type);
                if (destination_type->type != Signature_Type::POINTER) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR, expr);
                    semantic_analyser_add_error_info(error_information_make_given_type(destination_type));
                }
            }
            break;
        }
        case AST::Cast_Type::PTR_TO_RAW:
        {
            if (destination_type != 0) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expr);
            }
            destination_type = types.u64_type;
            break;
        }
        default: panic("");
        }

        auto operand_type = semantic_analyser_analyse_expression_value(cast->operand, operand_context);
        Info_Cast_Type cast_type;
        // Determine Cast type
        switch (cast->type)
        {
        case AST::Cast_Type::TYPE_TO_TYPE:
        {
            assert(destination_type != 0, "");
            cast_type = semantic_analyser_check_cast_type(operand_type, destination_type, false);
            if (cast_type == Info_Cast_Type::INVALID) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(operand_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(destination_type));
            }
            break;
        }
        case AST::Cast_Type::PTR_TO_RAW: {
            if (operand_type->type != Signature_Type::POINTER) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(operand_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(destination_type));
            }
            cast_type = Info_Cast_Type::POINTER_TO_U64;
            break;
        }
        case AST::Cast_Type::RAW_TO_PTR:
        {
            cast_type = Info_Cast_Type::U64_TO_POINTER;
            break;
        }
        default: panic("");
        }

        info->specifics.cast_type = cast_type;
        EXIT_VALUE(destination_type);
    }
    case AST::Expression_Type::LITERAL_READ:
    {
        auto& read = expr->options.literal_read;
        void* value_ptr;
        Type_Signature* literal_type;
        void* null_pointer = 0;
        Upp_String string_buffer;

        // Missing: float, nummptr
        switch (read.type)
        {
        case Literal_Type::BOOLEAN:
            literal_type = types.bool_type;
            value_ptr = &read.options.boolean;
            break;
        case Literal_Type::FLOAT_VAL:
            literal_type = types.f32_type;
            value_ptr = &read.options.float_val;
            break;
        case Literal_Type::INTEGER:
            literal_type = types.i32_type;
            value_ptr = &read.options.int_val;
            break;
        case Literal_Type::NULL_VAL:
            literal_type = types.void_ptr_type;
            value_ptr = &null_pointer;
            break;
        case Literal_Type::STRING: {
            String* string = read.options.string;
            string_buffer.character_buffer_data = string->characters;
            string_buffer.character_buffer_size = string->capacity;
            string_buffer.size = string->size;

            literal_type = types.string_type;
            value_ptr = &string_buffer;
            break;
        }
        default: panic("");
        }
        expression_info_set_constant(info, literal_type, array_create_static<byte>((byte*)value_ptr, literal_type->size), AST::upcast(expr));
        return info;
    }
    case AST::Expression_Type::ENUM_TYPE:
    {
        auto& members = expr->options.enum_members;
        Type_Signature* enum_type = type_system_make_enum_empty(type_system, 0);
        int next_member_value = 1;
        for (int i = 0; i < members.size; i++)
        {
            auto& member_node = members[i];
            if (member_node->value.available)
            {
                semantic_analyser_analyse_expression_value(member_node->value.value, expression_context_make_specific_type(types.i32_type));
                Comptime_Result comptime = expression_calculate_comptime_value(member_node->value.value);
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

            Enum_Item member;
            member.id = member_node->name;
            member.value = next_member_value;
            next_member_value++;
            dynamic_array_push_back(&enum_type->options.enum_type.members, member);
        }

        // Finish up enum
        enum_type->size = types.i32_type->size;
        enum_type->alignment = types.i32_type->alignment;
        for (int i = 0; i < enum_type->options.enum_type.members.size; i++)
        {
            auto member = &enum_type->options.enum_type.members[i];
            for (int j = i + 1; j < enum_type->options.enum_type.members.size; j++)
            {
                auto other = &enum_type->options.enum_type.members[j];
                if (other->id == member->id) {
                    semantic_analyser_log_error(Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE, AST::upcast(expr));
                    semantic_analyser_add_error_info(error_information_make_id(other->id));
                }
                if (other->value == member->value) {
                    semantic_analyser_log_error(Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE, AST::upcast(expr));
                    semantic_analyser_add_error_info(error_information_make_id(other->id));
                }
            }
        }
        type_system_finish_type(type_system, enum_type);
        EXIT_TYPE(enum_type);
    }
    case AST::Expression_Type::MODULE: {
        expression_info_set_module(info, expr->options.module->symbol_table);
        return info;
    }
    case AST::Expression_Type::FUNCTION:
    case AST::Expression_Type::STRUCTURE_TYPE:
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR:
    {
        Analysis_Progress* progress;
        {
            Analysis_Item** item_opt = hashtable_find_element(&compiler.dependency_analyser->mapping_ast_to_items, AST::upcast(expr));
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
            semantic_analyser_register_function_call(func->function);
            EXIT_FUNCTION(func->function);
        }
        case Analysis_Progress_Type::STRUCTURE:
        {
            auto structure = (Struct_Progress*)progress;
            Type_Signature* struct_type = structure->struct_type;
            assert(!(struct_type->size == 0 && struct_type->alignment == 0), "");
            EXIT_TYPE(struct_type);
        }
        case Analysis_Progress_Type::BAKE:
        {
            auto bake = (Bake_Progress*)progress;
            auto bake_result = &bake->result;
            switch (bake_result->type)
            {
            case Comptime_Result_Type::AVAILABLE: {
                expression_info_set_constant(
                    info, bake_result->data_type, array_create_static((byte*)bake_result->data, bake_result->data_type->size), AST::upcast(expr)
                );
                return info;
            }
            case Comptime_Result_Type::UNAVAILABLE:
                EXIT_ERROR(bake_result->data_type);
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
        EXIT_ERROR(types.unknown_type);
    }
    case AST::Expression_Type::FUNCTION_SIGNATURE:
    {
        auto& sig = expr->options.function_signature;
        Dynamic_Array<Function_Parameter> parameters = dynamic_array_create_empty<Function_Parameter>(math_maximum(1, sig.parameters.size));
        for (int i = 0; i < sig.parameters.size; i++)
        {
            auto& param = sig.parameters[i];
            if (param->is_comptime) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(param));
                semantic_analyser_add_error_info(error_information_make_text("Comptime parameters are only allowed in function definitions, not in signatures!"));
            }
            else {
                Function_Parameter func_param;
                func_param.name = optional_make_success(param->name);
                func_param.type = semantic_analyser_analyse_expression_type(param->type);
                dynamic_array_push_back(&parameters, func_param);
            }
        }
        Type_Signature* return_type;
        if (sig.return_value.available) {
            return_type = semantic_analyser_analyse_expression_type(sig.return_value.value);
        }
        else {
            return_type = types.void_type;
        }
        EXIT_TYPE(type_system_make_function(type_system, parameters, return_type));
    }
    case AST::Expression_Type::ARRAY_TYPE:
    {
        auto& array_node = expr->options.array_type;
        semantic_analyser_analyse_expression_value(
            array_node.size_expr, expression_context_make_specific_type(types.i32_type)
        );
        Comptime_Result comptime = expression_calculate_comptime_value(array_node.size_expr);
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
        EXIT_TYPE(array_type);
    }
    case AST::Expression_Type::SLICE_TYPE:
    {
        EXIT_TYPE(type_system_make_slice(type_system, semantic_analyser_analyse_expression_type(expr->options.slice_type)));
    }
    case AST::Expression_Type::NEW_EXPR:
    {
        auto& new_node = expr->options.new_expr;
        Type_Signature* allocated_type = semantic_analyser_analyse_expression_type(new_node.type_expr);
        assert(!(allocated_type->size == 0 && allocated_type->alignment == 0), "HEY");

        Type_Signature* result_type = 0;
        if (new_node.count_expr.available)
        {
            result_type = type_system_make_slice(type_system, allocated_type);
            semantic_analyser_analyse_expression_value(new_node.count_expr.value, expression_context_make_specific_type(types.i32_type));
        }
        else {
            result_type = type_system_make_pointer(type_system, allocated_type);
        }
        EXIT_VALUE(result_type);
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        auto& init_node = expr->options.struct_initializer;
        Type_Signature* struct_signature = 0;
        if (init_node.type_expr.available) {
            struct_signature = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE) {
                struct_signature = context.signature;
            }
            else {
                semantic_analyser_log_error(Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE, expr);
                EXIT_ERROR(types.unknown_type);
            }
        }

        if (struct_signature->type != Signature_Type::STRUCT) {
            semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT, expr);
            semantic_analyser_add_error_info(error_information_make_given_type(struct_signature));
            EXIT_ERROR(struct_signature);
        }
        if (init_node.arguments.size == 0) {
            semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, expr);
            EXIT_ERROR(struct_signature);
        }
        assert(!(struct_signature->size == 0 && struct_signature->alignment == 0), "");

        // Analyse arguments
        for (int i = 0; i < init_node.arguments.size; i++)
        {
            auto& argument = init_node.arguments[i];
            if (!argument->name.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE_NAMED_ARGUMENTS, AST::upcast(argument));
                continue;
            }
            String* member_id = argument->name.value;
            Struct_Member* found_member = 0;
            for (int i = 0; i < struct_signature->options.structure.members.size; i++) {
                if (struct_signature->options.structure.members[i].id == member_id) {
                    found_member = &struct_signature->options.structure.members[i];
                }
            }
            auto context = expression_context_make_unknown();
            auto arg_info = pass_get_info(argument);
            if (found_member != 0)
            {
                arg_info->valid = true;
                arg_info->member = *found_member;
                context = expression_context_make_specific_type(found_member->type);
            }
            else {
                arg_info->valid = false;
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST, AST::upcast(argument));
                semantic_analyser_add_error_info(error_information_make_id(member_id));
                // TODO: Find out if we want to analyse expressions even if we don't have the context for it
            }
            semantic_analyser_analyse_expression_value(argument->value, context);
        }

        // Check for errors (Different for unions/structs)
        if (struct_signature->options.structure.struct_type != AST::Structure_Type::STRUCT)
        {
            if (init_node.arguments.size == 0) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, expr);
            }
            else if (init_node.arguments.size != 1) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER, expr);
            }
            else if (struct_signature->options.structure.struct_type == AST::Structure_Type::UNION)
            {
                auto& member = pass_get_info(init_node.arguments[0])->member;
                if (member.offset == struct_signature->options.structure.tag_member.offset) {
                    semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG, AST::upcast(init_node.arguments[0]));
                }
            }
        }
        else
        {
            // Check that all members aren't initilized more than once
            int valid_count = 0;
            for (int i = 0; i < init_node.arguments.size; i++)
            {
                auto info = pass_get_info(init_node.arguments[i]);
                if (!info->valid) continue;
                valid_count += 1;
                for (int j = i + 1; j < init_node.arguments.size; j++)
                {
                    auto other = pass_get_info(init_node.arguments[j]);
                    if (!other->valid) continue;
                    if (info->member.id == other->member.id) {
                        semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE, AST::upcast(init_node.arguments[j]));
                        semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE, AST::upcast(init_node.arguments[i]));
                    }
                }
            }
            // Check if all members are initiliazed
            if (valid_count != struct_signature->options.structure.members.size) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, expr);
            }
        }

        EXIT_VALUE(struct_signature);
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        auto& init_node = expr->options.array_initializer;
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
                semantic_analyser_log_error(Semantic_Error_Type::ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE, expr);
                EXIT_ERROR(types.unknown_type);
            }
        }

        assert(!(element_type->size == 0 && element_type->alignment == 0), "");
        int array_element_count = init_node.values.size;
        // There are no 0-sized arrays, only 0-sized slices. So if we encounter an empty initializer, e.g. type.[], we return an empty slice
        if (array_element_count == 0) {
            EXIT_VALUE(type_system_make_slice(type_system, element_type));
        }

        for (int i = 0; i < init_node.values.size; i++) {
            semantic_analyser_analyse_expression_value(init_node.values[i], expression_context_make_specific_type(element_type));
        }
        EXIT_VALUE(type_system_make_array(type_system, element_type, true, array_element_count));
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        auto& access_node = expr->options.array_access;
        auto array_type = semantic_analyser_analyse_expression_value(
            access_node.array_expr, expression_context_make_auto_dereference()
        );

        Type_Signature* element_type = 0;
        if (array_type->type == Signature_Type::ARRAY) {
            element_type = array_type->options.array.element_type;
        }
        else if (array_type->type == Signature_Type::SLICE) {
            element_type = array_type->options.slice.element_type;
        }
        else {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS, expr);
            semantic_analyser_add_error_info(error_information_make_given_type(array_type));
            element_type = types.unknown_type;
        }

        semantic_analyser_analyse_expression_value(
            access_node.index_expr, expression_context_make_specific_type(types.i32_type)
        );
        EXIT_VALUE(element_type);
    }
    case AST::Expression_Type::MEMBER_ACCESS:
    {
        auto& member_node = expr->options.member_access;
        auto access_expr_info = semantic_analyser_analyse_expression_any(member_node.expr, expression_context_make_auto_dereference());
        switch (access_expr_info->result_type)
        {
        case Expression_Result_Type::TYPE:
        {
            Type_Signature* enum_type = access_expr_info->options.type;
            if (enum_type->type == Signature_Type::STRUCT && enum_type->options.structure.struct_type == AST::Structure_Type::UNION) {
                enum_type = enum_type->options.structure.tag_member.type;
            }
            if (enum_type->type != Signature_Type::ENUM) {
                semantic_analyser_log_error(Semantic_Error_Type::OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM, member_node.expr);
                semantic_analyser_add_error_info(error_information_make_given_type(enum_type));
                EXIT_ERROR(types.unknown_type);
            }

            Enum_Item* found = 0;
            for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
                auto member = &enum_type->options.enum_type.members[i];
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
            expression_info_set_constant_enum(info, enum_type, value);
            return info;
        }
        case Expression_Result_Type::FUNCTION:
        case Expression_Result_Type::HARDCODED_FUNCTION:
        case Expression_Result_Type::POLYMORPHIC_FUNCTION:
        case Expression_Result_Type::MODULE: {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, member_node.expr);
            semantic_analyser_add_error_info(error_information_make_expression_result_type(access_expr_info->result_type));
            EXIT_ERROR(types.unknown_type);
        }
        case Expression_Result_Type::VALUE:
        case Expression_Result_Type::CONSTANT:
        {
            auto struct_signature = access_expr_info->context_ops.after_cast_type;
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
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, expr);
                    semantic_analyser_add_error_info(error_information_make_id(member_node.name));
                    EXIT_ERROR(types.unknown_type);
                }
                EXIT_VALUE(found->type);
            }
            else if (struct_signature->type == Signature_Type::ARRAY || struct_signature->type == Signature_Type::SLICE)
            {
                if (member_node.name != compiler.id_size && member_node.name != compiler.id_data) {
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, expr);
                    semantic_analyser_add_error_info(error_information_make_id(member_node.name));
                    EXIT_ERROR(types.unknown_type);
                }

                if (struct_signature->type == Signature_Type::ARRAY)
                {
                    if (member_node.name == compiler.id_size) {
                        if (struct_signature->options.array.count_known) {
                            expression_info_set_constant_i32(info, struct_signature->options.array.element_count);
                        }
                        else {
                            EXIT_ERROR(types.i32_type);
                        }
                        return info;
                    }
                    else
                    { // Data access
                        EXIT_VALUE(type_system_make_pointer(type_system, struct_signature->options.array.element_type));
                    }
                }
                else // Slice
                {
                    Struct_Member member;
                    if (member_node.name == compiler.id_size) {
                        member = struct_signature->options.slice.size_member;
                    }
                    else {
                        member = struct_signature->options.slice.data_member;
                    }
                    EXIT_VALUE(member.type);
                }
            }
            else
            {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(struct_signature));
                EXIT_ERROR(types.unknown_type);
            }
            panic("");
            break;
        }
        default: panic("");
        }
        panic("Should not happen");
        EXIT_ERROR(types.unknown_type);
    }
    case AST::Expression_Type::AUTO_ENUM:
    {
        String* id = expr->options.auto_enum;
        if (context.type != Expression_Context_Type::SPECIFIC_TYPE) {
            semantic_analyser_log_error(Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED, expr);
            EXIT_ERROR(types.unknown_type);
        }
        if (context.signature->type != Signature_Type::ENUM) {
            semantic_analyser_log_error(Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT, expr);
            EXIT_ERROR(types.unknown_type);
        }

        Type_Signature* enum_type = context.signature;
        Enum_Item* found = 0;
        for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
            auto member = &enum_type->options.enum_type.members[i];
            if (member->id == id) {
                found = member;
                break;
            }
        }

        int value = 0;
        if (found == 0) {
            semantic_analyser_log_error(Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, expr);
            semantic_analyser_add_error_info(error_information_make_id(id));
        }
        else {
            value = found->value;
        }

        expression_info_set_constant_enum(info, enum_type, value);
        return info;
    }
    case AST::Expression_Type::UNARY_OPERATION:
    {
        auto& unary_node = expr->options.unop;
        switch (unary_node.type)
        {
        case AST::Unop::NEGATE:
        case AST::Unop::NOT:
        {
            bool is_negate = unary_node.type == AST::Unop::NEGATE;
            auto operand_type = semantic_analyser_analyse_expression_value(
                unary_node.expr,
                is_negate ? expression_context_make_auto_dereference() : expression_context_make_specific_type(types.bool_type)
            );

            if (is_negate)
            {
                bool valid = false;
                if (operand_type->type == Signature_Type::PRIMITIVE) {
                    valid = operand_type->options.primitive.is_signed && operand_type->options.primitive.type != Primitive_Type::BOOLEAN;
                }
                if (!valid) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, expr);
                    semantic_analyser_add_error_info(error_information_make_given_type(operand_type));
                }
            }

            EXIT_VALUE(is_negate ? operand_type : types.bool_type);
        }
        case AST::Unop::POINTER:
        {
            // TODO: I think I can check if the context is a specific type + pointer and continue with child type
            auto operand_result = semantic_analyser_analyse_expression_any(unary_node.expr, expression_context_make_unknown());
            switch (operand_result->result_type)
            {
            case Expression_Result_Type::VALUE:
            case Expression_Result_Type::CONSTANT:
            {
                Type_Signature* operand_type = operand_result->context_ops.after_cast_type;
                if (type_signature_equals(operand_type, types.void_type)) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, expr);
                    EXIT_ERROR(types.unknown_type);
                }

                if (!expression_has_memory_address(unary_node.expr)) {
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT, expr);
                }
                EXIT_VALUE(type_system_make_pointer(type_system, operand_type));
            }
            case Expression_Result_Type::TYPE: {
                EXIT_TYPE(type_system_make_pointer(type_system, operand_result->options.type));
            }
            case Expression_Result_Type::FUNCTION:
            case Expression_Result_Type::POLYMORPHIC_FUNCTION:
            case Expression_Result_Type::HARDCODED_FUNCTION:
            case Expression_Result_Type::MODULE: {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, expr);
                semantic_analyser_add_error_info(error_information_make_expression_result_type(operand_result->result_type));
                EXIT_ERROR(types.unknown_type);
            }
            default: panic("");
            }
            panic("");
            break;
        }
        case AST::Unop::DEREFERENCE:
        {
            auto operand_type = semantic_analyser_analyse_expression_value(unary_node.expr, expression_context_make_unknown());
            Type_Signature* result_type = types.unknown_type;
            if (operand_type->type != Signature_Type::POINTER || type_signature_equals(operand_type, types.void_ptr_type)) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(operand_type));
            }
            else {
                result_type = operand_type->options.pointer_child;
            }
            EXIT_VALUE(result_type);
        }
        default:panic("");
        }
        panic("");
        EXIT_ERROR(types.unknown_type);
    }
    case AST::Expression_Type::BINARY_OPERATION:
    {
        auto& binop_node = expr->options.binop;

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
            operand_context = expression_context_make_specific_type(types.bool_type);
            break;
        case AST::Binop::INVALID:
            operand_context = expression_context_make_unknown();
            break;
        default: panic("");
        }

        // Evaluate operands
        auto left_type = semantic_analyser_analyse_expression_value(binop_node.left, operand_context);
        if (enum_valid && left_type->type == Signature_Type::ENUM) {
            operand_context = expression_context_make_specific_type(left_type);
        }
        auto right_type = semantic_analyser_analyse_expression_value(binop_node.right, operand_context);

        // Check for unknowns
        if (type_signature_equals(left_type, types.unknown_type) || type_signature_equals(right_type, types.unknown_type)) {
            EXIT_ERROR(types.unknown_type);
        }

        // Try implicit casting if types dont match
        Type_Signature* operand_type = left_type;
        bool types_are_valid = true;

        if (!type_signature_equals(left_type, right_type))
        {
            auto left_cast = semantic_analyser_check_cast_type(left_type, right_type, true);
            auto right_cast = semantic_analyser_check_cast_type(right_type, left_type, true);
            if (left_cast != Info_Cast_Type::INVALID) {
                expression_info_set_cast(pass_get_info(binop_node.left), left_cast, right_type);
                operand_type = right_type;
            }
            else if (right_cast != Info_Cast_Type::INVALID) {
                expression_info_set_cast(pass_get_info(binop_node.right), right_cast, left_type);
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

        if (!types_are_valid) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR, expr);
            semantic_analyser_add_error_info(error_information_make_binary_op_type(left_type, right_type));
        }
        EXIT_VALUE(result_type_is_bool ? types.bool_type : operand_type);
    }
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    panic("HEY");
    EXIT_ERROR(types.unknown_type);

#undef EXIT_VALUE
#undef EXIT_TYPE
#undef EXIT_ERROR
#undef EXIT_HARDCODED
#undef EXIT_FUNCTION
}

void expression_context_apply(AST::Expression* expr, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto info = pass_get_info(expr);
    SET_ACTIVE_EXPR_INFO(info);
    auto initial_type = expression_info_get_type(info);
    auto final_type = initial_type;

    // Special Case Handling: Expression_Statements are the only things which can expect void type
    if (type_signature_equals(initial_type, types.void_type)) {
        if (!(context.type == Expression_Context_Type::SPECIFIC_TYPE && type_signature_equals(context.signature, types.void_type))) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, expr);
        }
        info->context_ops.after_cast_type = types.unknown_type;
        return;
    }
    if (context.type == Expression_Context_Type::SPECIFIC_TYPE && type_signature_equals(context.signature, types.void_type)) {
        context.type = Expression_Context_Type::UNKNOWN;
    }

    // Do nothing if context is unknown
    if (context.type == Expression_Context_Type::UNKNOWN) return;

    // Check for unknowns
    if (type_signature_equals(initial_type, types.unknown_type) ||
        (context.type == Expression_Context_Type::SPECIFIC_TYPE && type_signature_equals(context.signature, types.unknown_type))) {
        semantic_analyser_set_error_flag(true);
        info->context_ops.after_cast_type = types.unknown_type;
        return;
    }

    // Auto pointer dereferencing/address of
    {
        int wanted_pointer_depth = 0;
        switch (context.type)
        {
        case Expression_Context_Type::UNKNOWN:
            return;
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

        // Find out given pointer level
        int given_pointer_depth = 0;
        Type_Signature* iter = initial_type;
        while (iter->type == Signature_Type::POINTER) {
            given_pointer_depth++;
            iter = iter->options.pointer_child;
        }

        if (given_pointer_depth + 1 == wanted_pointer_depth && context.type == Expression_Context_Type::SPECIFIC_TYPE)
        {
            // Auto address of
            info->context_ops.take_address_of = true;
            final_type = type_system_make_pointer(&type_system, initial_type);
        }
        else
        {
            // Auto-Dereference to given level
            info->context_ops.deref_count = given_pointer_depth - wanted_pointer_depth;
            for (int i = 0; i < info->context_ops.deref_count; i++) {
                assert(final_type->type == Signature_Type::POINTER, "");
                final_type = final_type->options.pointer_child;
            }
        }
    }

    info->context_ops.after_cast_type = final_type;
    // Implicit casting
    if (context.type == Expression_Context_Type::SPECIFIC_TYPE)
    {
        if (!type_signature_equals(final_type, context.signature))
        {
            Info_Cast_Type cast_type = semantic_analyser_check_cast_type(final_type, context.signature, true);
            if (cast_type == Info_Cast_Type::INVALID) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(initial_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(context.signature));
            }
            expression_info_set_cast(info, cast_type, context.signature);
        }
    }
}

Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto result = semantic_analyser_analyse_expression_internal(expression, context);
    SET_ACTIVE_EXPR_INFO(result);
    if (result->result_type != Expression_Result_Type::VALUE && result->result_type != Expression_Result_Type::CONSTANT) return result;
    expression_context_apply(expression, context);
    return result;
}

Type_Signature* semantic_analyser_analyse_expression_type(AST::Expression* expression)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto result = semantic_analyser_analyse_expression_any(expression, expression_context_make_auto_dereference());
    SET_ACTIVE_EXPR_INFO(result);
    switch (result->result_type)
    {
    case Expression_Result_Type::TYPE:
        return result->options.type;
    case Expression_Result_Type::CONSTANT:
    case Expression_Result_Type::VALUE:
    {
        if (type_signature_equals(result->context_ops.after_cast_type, types.unknown_type)) {
            semantic_analyser_set_error_flag(true);
            return types.unknown_type;
        }
        if (!type_signature_equals(result->context_ops.after_cast_type, types.type_type))
        {
            semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE, expression);
            semantic_analyser_add_error_info(error_information_make_given_type(result->context_ops.after_cast_type));
            return types.unknown_type;
        }

        if (result->result_type == Expression_Result_Type::VALUE)
        {
            Comptime_Result comptime = expression_calculate_comptime_value(expression);
            Type_Signature* result_type = types.unknown_type;
            switch (comptime.type)
            {
            case Comptime_Result_Type::AVAILABLE:
            {
                u64 type_index = *(u64*)comptime.data;
                if (type_index >= type_system.internal_type_infos.size) {
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, expression);
                    return types.unknown_type;
                }
                return type_system.types[type_index];
            }
            case Comptime_Result_Type::UNAVAILABLE:
                return types.unknown_type;
            case Comptime_Result_Type::NOT_COMPTIME:
                semantic_analyser_log_error(Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME, expression);
                return types.unknown_type;
            default: panic("");
            }
        }
        else {
            auto type_index = upp_constant_to_value<u64>(&compiler.constant_pool, result->options.constant);
            if (type_index >= type_system.internal_type_infos.size) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, expression);
                return types.unknown_type;
            }
            return type_system.types[type_index];
        }

        panic("");
        break;
    }
    case Expression_Result_Type::MODULE:
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::FUNCTION: {
        semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_TYPE, expression);
        semantic_analyser_add_error_info(error_information_make_expression_result_type(result->result_type));
        return types.unknown_type;
    }
    default: panic("");
    }
    panic("");
    return types.unknown_type;
}

Type_Signature* semantic_analyser_analyse_expression_value(AST::Expression* expression, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto result = semantic_analyser_analyse_expression_any(expression, context);
    SET_ACTIVE_EXPR_INFO(result);
    switch (result->result_type)
    {
    case Expression_Result_Type::VALUE: {
        return result->context_ops.after_cast_type; // Here context was already applied, so we return
    }
    case Expression_Result_Type::TYPE: {
        expression_info_set_constant(result, types.type_type, array_create_static_as_bytes(&result->options.type->internal_index, 1), AST::upcast(expression));
        break;
    }
    case Expression_Result_Type::CONSTANT:
    {
        break;
    }
    case Expression_Result_Type::FUNCTION:
    {
        // Function pointer read
        break;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    {
        semantic_analyser_log_error(Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION, expression);
        return types.unknown_type;
    }
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::MODULE:
    {
        semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_VALUE, expression);
        semantic_analyser_add_error_info(error_information_make_expression_result_type(result->result_type));
        return types.unknown_type;
    }
    default: panic("");
    }

    expression_context_apply(expression, context);
    return result->context_ops.after_cast_type;
}



// STATEMENTS
bool code_block_is_while(AST::Code_Block* block)
{
    if (block != 0 && block->base.parent->type == AST::Node_Type::STATEMENT) {
        auto parent = (AST::Statement*) block->base.parent;
        return parent->type == AST::Statement_Type::WHILE_STATEMENT;
    }
    return false;
}

bool code_block_is_defer(AST::Code_Block* block)
{
    if (block != 0 && block->base.parent->type == AST::Node_Type::STATEMENT) {
        auto parent = (AST::Statement*) block->base.parent;
        return parent->type == AST::Statement_Type::DEFER;
    }
    return false;
}

bool inside_defer()
{
    assert(semantic_analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_BODY, "Must be in body otherwise no workload exists");
    auto& block_stack = semantic_analyser.current_workload->options.function_body.block_stack;
    for (int i = block_stack.size - 1; i > 0; i--)
    {
        auto block = block_stack[i];
        if (code_block_is_defer(block)) {
            return true;
        }
    }
    return false;
}

Control_Flow semantic_analyser_analyse_statement(AST::Statement* statement)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto& analyser = semantic_analyser;
    auto info = pass_get_info(statement);
    info->flow = Control_Flow::SEQUENTIAL;
#define EXIT(flow_result) { info->flow = flow_result; return flow_result; };

    switch (statement->type)
    {
    case AST::Statement_Type::RETURN_STATEMENT:
    {
        auto& return_stat = statement->options.return_value;
        ModTree_Function* current_function = semantic_analyser.current_workload->current_function;
        assert(current_function != 0, "No statements outside of function body");
        Type_Signature* expected_return_type = current_function->signature->options.function.return_type;

        if (return_stat.available)
        {
            Expression_Context context;
            if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS) {
                context = expression_context_make_unknown();
            }
            else {
                context = expression_context_make_specific_type(expected_return_type);
            }
            auto return_type = semantic_analyser_analyse_expression_value(return_stat.value, context);

            // Check bake return type
            if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS)
            {
                if (type_signature_equals(expected_return_type, types.void_type)) {
                    current_function->signature = type_system_make_function(&type_system, {}, return_type);
                }
                else if (!type_signature_equals(expected_return_type, return_type)) {
                    semantic_analyser_log_error(Semantic_Error_Type::BAKE_BLOCK_RETURN_TYPE_DIFFERS_FROM_PREVIOUS_RETURN, statement);
                    semantic_analyser_add_error_info(error_information_make_given_type(return_type));
                    semantic_analyser_add_error_info(error_information_make_expected_type(expected_return_type));
                }
            }
        }
        else
        {
            if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS) {
                semantic_analyser_log_error(Semantic_Error_Type::BAKE_BLOCK_RETURN_MUST_NOT_BE_EMPTY, statement);
            }
            else
            {
                if (!type_signature_equals(expected_return_type, types.void_type)) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_RETURN, statement);
                    semantic_analyser_add_error_info(error_information_make_given_type(types.void_type));
                    semantic_analyser_add_error_info(error_information_make_expected_type(expected_return_type));
                }
            }
        }

        if (inside_defer()) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED, statement);
        }
        EXIT(Control_Flow::RETURNS);
    }
    case AST::Statement_Type::BREAK_STATEMENT:
    case AST::Statement_Type::CONTINUE_STATEMENT:
    {
        bool is_continue = statement->type == AST::Statement_Type::CONTINUE_STATEMENT;
        String* search_id = is_continue ? statement->options.continue_name : statement->options.break_name;
        AST::Code_Block* found_block = 0;
        auto& block_stack = semantic_analyser.current_workload->options.function_body.block_stack;
        for (int i = block_stack.size - 1; i > 0; i--) // INFO: Block 0 is always the function body, which cannot be a target of break/continue
        {
            auto id = block_stack[i]->block_id;
            if (id.available && id.value == search_id) {
                found_block = block_stack[i];
                break;
            }
        }

        if (found_block == 0)
        {
            semantic_analyser_log_error(
                is_continue ? Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND : Semantic_Error_Type::BREAK_LABLE_NOT_FOUND, statement
            );
            semantic_analyser_add_error_info(error_information_make_id(search_id));
            EXIT(Control_Flow::RETURNS);
        }
        else
        {
            info->specifics.block = found_block;
            if (is_continue && !code_block_is_while(found_block)) {
                semantic_analyser_log_error(Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK, statement);
                EXIT(Control_Flow::SEQUENTIAL);
            }
        }

        if (!is_continue)
        {
            // Mark all previous Code-Blocks as Sequential flow, since they contain a path to a break
            auto& block_stack = semantic_analyser.current_workload->options.function_body.block_stack;
            for (int i = block_stack.size - 1; i >= 0; i--)
            {
                auto block = block_stack[i];
                auto prev = pass_get_info(block);
                if (!prev->control_flow_locked && semantic_analyser.current_workload->statement_reachable) {
                    prev->control_flow_locked = true;
                    prev->flow = Control_Flow::SEQUENTIAL;
                }
                if (block == found_block) break;
            }
        }
        EXIT(Control_Flow::STOPS);
    }
    case AST::Statement_Type::DEFER:
    {
        semantic_analyser_analyse_block(statement->options.defer_block);
        if (inside_defer()) {
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS, statement);
            EXIT(Control_Flow::SEQUENTIAL);
        }
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::EXPRESSION_STATEMENT:
    {
        auto& expression_node = statement->options.expression;
        if (expression_node->type != AST::Expression_Type::FUNCTION_CALL) {
            semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL, statement);
        }
        // Note(Martin): This is a special case, in expression statements a void_type is valid
        semantic_analyser_analyse_expression_value(expression_node, expression_context_make_specific_type(types.void_type));
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::BLOCK:
    {
        auto flow = semantic_analyser_analyse_block(statement->options.block);
        EXIT(flow);
    }
    case AST::Statement_Type::IF_STATEMENT:
    {
        auto& if_node = statement->options.if_statement;

        auto condition_type = semantic_analyser_analyse_expression_value(
            if_node.condition, expression_context_make_specific_type(types.bool_type)
        );
        auto true_flow = semantic_analyser_analyse_block(statement->options.if_statement.block);
        Control_Flow false_flow;
        if (if_node.else_block.available) {
            false_flow = semantic_analyser_analyse_block(statement->options.if_statement.else_block.value);
        }
        else {
            EXIT(Control_Flow::SEQUENTIAL;) // If no else, if is always sequential, since it's possible to skip the block
        }

        // Combine flows as given by conditional flow rules
        if (true_flow == false_flow) {
            return true_flow;
        }
        if (true_flow == Control_Flow::SEQUENTIAL || false_flow == Control_Flow::SEQUENTIAL) {
            EXIT(Control_Flow::SEQUENTIAL);
        }
        EXIT(Control_Flow::STOPS);
    }
    case AST::Statement_Type::SWITCH_STATEMENT:
    {
        auto& switch_node = statement->options.switch_statement;

        auto switch_type = semantic_analyser_analyse_expression_value(switch_node.condition, expression_context_make_auto_dereference());
        if (switch_type->type == Signature_Type::STRUCT && switch_type->options.structure.struct_type == AST::Structure_Type::UNION) {
            switch_type = switch_type->options.structure.tag_member.type;
        }
        else if (switch_type->type != Signature_Type::ENUM)
        {
            semantic_analyser_log_error(Semantic_Error_Type::SWITCH_REQUIRES_ENUM, switch_node.condition);
            semantic_analyser_add_error_info(error_information_make_given_type(switch_type));
        }

        Expression_Context case_context = switch_type->type == Signature_Type::ENUM ?
            expression_context_make_specific_type(switch_type) : expression_context_make_unknown();
        Control_Flow switch_flow = Control_Flow::SEQUENTIAL;
        bool default_found = false;
        for (int i = 0; i < switch_node.cases.size; i++)
        {
            auto& case_node = switch_node.cases[i];
            auto case_info = pass_get_info(case_node);
            case_info->is_valid = false;
            auto case_flow = semantic_analyser_analyse_block(case_node->block);
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

            if (case_node->value.available)
            {
                auto case_type = semantic_analyser_analyse_expression_value(case_node->value.value, case_context);
                // Calculate case value
                Comptime_Result comptime = expression_calculate_comptime_value(case_node->value.value);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    case_info->case_value = *((int*)comptime.data);
                    case_info->is_valid = true;
                    break;
                case Comptime_Result_Type::NOT_COMPTIME:
                    case_info->is_valid = false;
                    case_info->case_value = -1;
                    break;
                case Comptime_Result_Type::UNAVAILABLE:
                    semantic_analyser_set_error_flag(true);
                    break;
                default: panic("");
                }
                if (comptime.type == Comptime_Result_Type::NOT_COMPTIME) {
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN, case_node->value.value);
                    break;
                }
            }
            else
            {
                // Default case
                if (default_found) {
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED, statement);
                }
                default_found = true;
            }
        }

        // Check if given cases are unique
        int unique_count = 0;
        for (int i = 0; i < switch_node.cases.size; i++)
        {
            auto case_node = switch_node.cases[i];
            auto case_info = pass_get_info(case_node);
            if (!case_info->is_valid) continue;

            bool is_unique = true;
            for (int j = i + 1; j < statement->options.switch_statement.cases.size; j++)
            {
                auto& other_case = statement->options.switch_statement.cases[j];
                auto other_info = pass_get_info(other_case);
                if (!other_info->is_valid) continue;
                if (case_info->case_value == other_info->case_value) {
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE, AST::upcast(other_case));
                    is_unique = false;
                    break;
                }
            }
            if (is_unique) {
                unique_count++;
            }
        }

        // Check if all possible cases are handled
        if (!default_found && switch_type->type == Signature_Type::ENUM) {
            if (unique_count < switch_type->options.enum_type.members.size) {
                semantic_analyser_log_error(Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES, statement);
            }
        }
        return switch_flow;
    }
    case AST::Statement_Type::WHILE_STATEMENT:
    {
        auto& while_node = statement->options.while_statement;
        semantic_analyser_analyse_expression_value(while_node.condition, expression_context_make_specific_type(types.bool_type));

        auto flow = semantic_analyser_analyse_block(while_node.block);
        if (flow == Control_Flow::RETURNS) {
            EXIT(flow);
        }
        EXIT(Control_Flow::SEQUENTIAL); // While is always sequential, since the condition may be wrong
    }
    case AST::Statement_Type::DELETE_STATEMENT:
    {
        auto delete_type = semantic_analyser_analyse_expression_value(statement->options.delete_expr, expression_context_make_unknown());
        if (delete_type->type != Signature_Type::POINTER && delete_type->type != Signature_Type::SLICE) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_DELETE, statement->options.delete_expr);
            semantic_analyser_add_error_info(error_information_make_given_type(delete_type));
        }
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::ASSIGNMENT:
    {
        auto& assignment_node = statement->options.assignment;
        auto left_type = semantic_analyser_analyse_expression_value(assignment_node.left_side, expression_context_make_unknown());
        auto right_type = semantic_analyser_analyse_expression_value(assignment_node.right_side, expression_context_make_specific_type(left_type));
        if (!expression_has_memory_address(assignment_node.left_side)) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS, assignment_node.left_side);
        }
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::DEFINITION:
    {
        auto& definition = statement->options.definition;
        if (definition->is_comptime) {
            // Already handled by definition workload
            EXIT(Control_Flow::SEQUENTIAL);
        }
        assert(!(!definition->value.available && !definition->type.available), "");

        Type_Signature* type = 0;
        if (definition->type.available) {
            type = semantic_analyser_analyse_expression_type(definition->type.value);
        }
        if (definition->value.available)
        {
            auto value_type = semantic_analyser_analyse_expression_value(
                definition->value.value, type != 0 ? expression_context_make_specific_type(type) : expression_context_make_unknown()
            );
            if (type == 0) {
                type = value_type;
            }
        }

        assert(definition->symbol->type == Symbol_Type::VARIABLE || definition->symbol->type == Symbol_Type::VARIABLE_UNDEFINED, "");
        definition->symbol->type = Symbol_Type::VARIABLE;
        definition->symbol->options.variable_type = type;
        EXIT(Control_Flow::SEQUENTIAL);
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }
    panic("HEY");
    EXIT(Control_Flow::SEQUENTIAL);
#undef EXIT
}

Control_Flow semantic_analyser_analyse_block(AST::Code_Block* block)
{
    auto block_info = pass_get_info(block);
    block_info->control_flow_locked = false;
    block_info->flow = Control_Flow::SEQUENTIAL;

    auto& block_stack = semantic_analyser.current_workload->options.function_body.block_stack;
    if (block->block_id.available)
    {
        for (int i = 0; i < block_stack.size; i++) {
            auto prev = block_stack[i];
            if (prev != 0 && prev->block_id.available && prev->block_id.value == block->block_id.value) {
                semantic_analyser_log_error(Semantic_Error_Type::LABEL_ALREADY_IN_USE, &block->base);
            }
        }
    }

    int rewind_block_count = block_stack.size;

    bool rewind_reachable = semantic_analyser.current_workload->statement_reachable;
    SCOPE_EXIT(dynamic_array_rollback_to_size(&block_stack, rewind_block_count));
    SCOPE_EXIT(semantic_analyser.current_workload->statement_reachable = rewind_reachable);
    dynamic_array_push_back(&block_stack, block);
    for (int i = 0; i < block->statements.size; i++)
    {
        Control_Flow flow = semantic_analyser_analyse_statement(block->statements[i]);
        if (flow != Control_Flow::SEQUENTIAL) {
            semantic_analyser.current_workload->statement_reachable = false;
            if (!block_info->control_flow_locked) {
                block_info->flow = flow;
                block_info->control_flow_locked = true;
            }
        }
    }
    return block_info->flow;
}



// ANALYSER
void semantic_analyser_finish()
{
    auto& type_system = compiler.type_system;
    // Check if main is defined
    Symbol* main_symbol = symbol_table_find_symbol(
        compiler.main_source->source_parse->root->symbol_table, compiler.id_main, true, true, 0
    );
    ModTree_Function* main_function = 0;
    if (main_symbol == 0) {
        semantic_analyser_log_error(Semantic_Error_Type::MAIN_NOT_DEFINED, (AST::Node*)0);
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
    semantic_analyser.program->main_function = main_function;
}

void semantic_analyser_reset()
{
    auto& type_system = compiler.type_system;

    // Reset analyser data
    {
        semantic_analyser.current_workload = 0;
        for (int i = 0; i < semantic_analyser.errors.size; i++) {
            dynamic_array_destroy(&semantic_analyser.errors[i].information);
        }
        dynamic_array_reset(&semantic_analyser.errors);
        stack_allocator_reset(&semantic_analyser.allocator_values);

        for (int i = 0; i < semantic_analyser.polymorphic_functions.size; i++) {
            auto& poly_function = semantic_analyser.polymorphic_functions[i];
            for (int j = 0; j < poly_function->instances.size; j++) {
                auto& instance = poly_function->instances[j];
                array_destroy(&instance.parameter_values);
            }
            delete poly_function;
        }
        dynamic_array_reset(&semantic_analyser.polymorphic_functions);

        workload_executer_destroy();
        semantic_analyser.workload_executer = workload_executer_initialize();

        if (semantic_analyser.program != 0) {
            modtree_program_destroy();
        }
        semantic_analyser.program = modtree_program_create();
    }

    // Create predefined Symbols 
    {
        auto root = compiler.dependency_analyser->root_symbol_table;
        auto pool = &compiler.identifier_pool;
        auto& symbols = semantic_analyser.predefined_symbols;
        auto& types = compiler.type_system.predefined_types;

        auto define_type_symbol = [&](const char* name, Type_Signature* type) -> Symbol* {
            Symbol* result = symbol_table_define_symbol(root, identifier_pool_add(pool, string_create_static(name)), Symbol_Type::TYPE, 0, false);
            result->options.type = type;
            if (type->type == Signature_Type::STRUCT) {
                type->options.structure.symbol = result;
            }
            return result;
        };
        symbols.type_bool             = define_type_symbol("bool",             types.bool_type);
        symbols.type_int              = define_type_symbol("int",              types.i32_type);
        symbols.type_float            = define_type_symbol("float",            types.f32_type);
        symbols.type_u8               = define_type_symbol("u8",               types.u8_type);
        symbols.type_u16              = define_type_symbol("u16",              types.u16_type);
        symbols.type_u32              = define_type_symbol("u32",              types.u32_type);
        symbols.type_u64              = define_type_symbol("u64",              types.u64_type);
        symbols.type_i8               = define_type_symbol("i8",               types.i8_type);
        symbols.type_i16              = define_type_symbol("i16",              types.i16_type);
        symbols.type_i32              = define_type_symbol("i32",              types.i32_type);
        symbols.type_i64              = define_type_symbol("i64",              types.i64_type);
        symbols.type_f32              = define_type_symbol("f32",              types.f32_type);
        symbols.type_f64              = define_type_symbol("f64",              types.f64_type);
        symbols.type_byte             = define_type_symbol("byte",             types.u8_type);
        symbols.type_void             = define_type_symbol("void",             types.void_type);
        symbols.type_string           = define_type_symbol("String",           types.string_type);
        symbols.type_type             = define_type_symbol("Type",             types.type_type);
        symbols.type_type_information = define_type_symbol("Type_Information", types.type_information_type);
        symbols.type_any              = define_type_symbol("Any",              types.any_type);
        symbols.type_empty            = define_type_symbol("_",                types.empty_struct_type);

        auto define_hardcoded_symbol = [&](const char* name, Hardcoded_Type type) -> Symbol* {
            Symbol* result = symbol_table_define_symbol(root, identifier_pool_add(pool, string_create_static(name)), Symbol_Type::HARDCODED_FUNCTION, 0, false);
            result->options.hardcoded = type;
            return result;
        };
        symbols.hardcoded_print_bool = define_hardcoded_symbol("print_bool", Hardcoded_Type::PRINT_BOOL);
        symbols.hardcoded_print_i32 = define_hardcoded_symbol("print_i32", Hardcoded_Type::PRINT_I32);
        symbols.hardcoded_print_f32 = define_hardcoded_symbol("print_f32", Hardcoded_Type::PRINT_F32);
        symbols.hardcoded_print_string = define_hardcoded_symbol("print_string", Hardcoded_Type::PRINT_STRING);
        symbols.hardcoded_print_line = define_hardcoded_symbol("print_line", Hardcoded_Type::PRINT_LINE);
        symbols.hardcoded_read_i32 = define_hardcoded_symbol("read_i32", Hardcoded_Type::PRINT_I32);
        symbols.hardcoded_read_f32 = define_hardcoded_symbol("read_f32", Hardcoded_Type::READ_F32);
        symbols.hardcoded_read_bool = define_hardcoded_symbol("read_bool", Hardcoded_Type::READ_BOOL);
        symbols.hardcoded_random_i32 = define_hardcoded_symbol("random_i32", Hardcoded_Type::RANDOM_I32);
        symbols.hardcoded_type_of = define_hardcoded_symbol("type_of", Hardcoded_Type::TYPE_OF);
        symbols.hardcoded_type_info = define_hardcoded_symbol("type_info", Hardcoded_Type::TYPE_INFO);
        symbols.hardcoded_assert = define_hardcoded_symbol("assert", Hardcoded_Type::ASSERT_FN);

        // NOTE: Error symbol is required so that unresolved symbol-reads can point to something,
        //       but it shouldn't be possible to reference the error symbol by name, so the 
        //       current little 'hack' is to use the identifier 0_ERROR and because it starts with a 0, it
        //       can never be used as a symbol read. Other approaches would be to have a custom symbol-table for the error symbol
        //       that isn't connected to anything.
        symbols.error_symbol = define_type_symbol("0_ERROR_SYMBOL", types.unknown_type);
        symbols.error_symbol->type = Symbol_Type::ERROR_SYMBOL;
    }

    // Create predefined globals
    semantic_analyser.global_type_informations = modtree_program_add_global(
        type_system_make_slice(&type_system, compiler.type_system.predefined_types.type_information_type)
    );
}

Semantic_Analyser* semantic_analyser_initialize()
{
    semantic_analyser.errors = dynamic_array_create_empty<Semantic_Error>(64);
    semantic_analyser.allocator_values = stack_allocator_create_empty(2048);
    semantic_analyser.workload_executer = workload_executer_initialize();
    semantic_analyser.polymorphic_functions = dynamic_array_create_empty<Polymorphic_Function*>(1);
    semantic_analyser.program = 0;
    return &semantic_analyser;
}

void semantic_analyser_destroy()
{
    for (int i = 0; i < semantic_analyser.errors.size; i++) {
        dynamic_array_destroy(&semantic_analyser.errors[i].information);
    }
    dynamic_array_destroy(&semantic_analyser.errors);

    for (int i = 0; i < semantic_analyser.polymorphic_functions.size; i++) {
        auto& poly_function = semantic_analyser.polymorphic_functions[i];
        for (int j = 0; j < poly_function->instances.size; j++) {
            auto& instance = poly_function->instances[j];
            array_destroy(&instance.parameter_values);
        }
        delete poly_function;
    }
    dynamic_array_destroy(&semantic_analyser.polymorphic_functions);

    stack_allocator_destroy(&semantic_analyser.allocator_values);

    if (semantic_analyser.program != 0) {
        modtree_program_destroy();
    }
}



// ERRORS
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
        HANDLE_CASE(Semantic_Error_Type::BAKE_BLOCK_RETURN_MUST_NOT_BE_EMPTY, "Return inside bake must not be empty", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BAKE_BLOCK_RETURN_TYPE_DIFFERS_FROM_PREVIOUS_RETURN, "Return type differs from previous return type", Parser::Section::WHOLE);
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
        HANDLE_CASE(Semantic_Error_Type::COMPTIME_DEFINITION_REQUIRES_INITAL_VALUE, "Comptime definition requires initial value", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED, "Comptime definitions must be infered!", Parser::Section::WHOLE);
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
        HANDLE_CASE(Semantic_Error_Type::STRUCT_MUST_CONTAIN_MEMBER, "Struct must contain at least one member", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_MEMBER_ALREADY_DEFINED, "Struct member is already defined", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_MEMBER_MUST_NOT_HAVE_VALUE, "Struct member must not have value", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_MEMBER_REQUIRES_TYPE, "Struct member requires type", Parser::Section::WHOLE);
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
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_NAMED_ARGUMENTS, "Named arguments aren't supported yet", Parser::Section::IDENTIFIER);
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
        case Error_Information_Type::EXTRA_TEXT:
            string_append_formated(string, "\n %s", info->options.extra_text);
            break;
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
            case Expression_Result_Type::HARDCODED_FUNCTION:
                string_append_formated(string, "Hardcoded function");
                break;
            case Expression_Result_Type::POLYMORPHIC_FUNCTION:
                string_append_formated(string, "Polymorphic function");
                break;
            case Expression_Result_Type::MODULE:
                string_append_formated(string, "Module");
                break;
            case Expression_Result_Type::CONSTANT:
                string_append_formated(string, "Constant");
                break;
            case Expression_Result_Type::VALUE:
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


