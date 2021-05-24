#include "intermediate_code.hpp"

Data_Access data_access_make_empty() {
    Data_Access d;
    d.is_pointer_access = false;
    d.access_index = 0;
    d.access_type = Data_Access_Type::GLOBAL_ACCESS;
    return d;
}

Data_Access data_access_make_by_name(Intermediate_Generator* generator, int name_id)
{
    Data_Access access;
    access.is_pointer_access = false;
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    for (int i = generator->name_mappings.size - 1; i >= 0; i--) 
    {
        Name_Mapping* mapping = &generator->name_mappings[i];
        if (mapping->name_handle == name_id) 
        {
            access.access_type = mapping->access_type;
            access.access_index = mapping->access_index;
            return access;
        }
    }
    panic("Cannot happen after semantic analysis");
    return data_access_make_empty();
}

Data_Access intermediate_generator_create_intermediate_result(Intermediate_Generator* generator, Type_Signature* type_signature);
Data_Access data_access_create_const_i32(Intermediate_Generator* generator, int size)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Intermediate_Instruction load_constant_instr;
    load_constant_instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
    load_constant_instr.destination = intermediate_generator_create_intermediate_result(generator, generator->analyser->type_system.i32_type);
    load_constant_instr.constant_i32_value = size;
    dynamic_array_push_back(&function->instructions, load_constant_instr);
    return load_constant_instr.destination;
}

Data_Access data_access_create_member_access(Intermediate_Generator* generator, Data_Access access, int offset, Type_Signature* result_type)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    Intermediate_Instruction calc_member_access;
    calc_member_access.type = Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER;
    calc_member_access.source1 = access;
    calc_member_access.destination = intermediate_generator_create_intermediate_result(generator,
        type_system_make_pointer(&generator->analyser->type_system, result_type)
    );
    calc_member_access.constant_i32_value = offset;
    dynamic_array_push_back(&function->instructions, calc_member_access);

    calc_member_access.destination.is_pointer_access = true;
    return calc_member_access.destination;
}

Type_Signature* data_access_get_type_signature(Intermediate_Generator* generator, Data_Access access, int function_index)
{
    Intermediate_Function* function = &generator->functions[function_index];
    switch (access.access_type)
    {
    case Data_Access_Type::GLOBAL_ACCESS: return generator->global_variables[access.access_index].type;
    case Data_Access_Type::VARIABLE_ACCESS: return function->local_variables[access.access_index].type;
    case Data_Access_Type::INTERMEDIATE_ACCESS: return function->intermediate_results[access.access_index];
    case Data_Access_Type::PARAMETER_ACCESS: return function->function_type->parameter_types[access.access_index];
    }
    panic("Should not happen");
    return generator->analyser->type_system.error_type;
}

void intermediate_generator_create_name_mapping(Intermediate_Generator* generator, Data_Access access, int name_id)
{
    Name_Mapping m;
    m.name_handle = name_id;
    m.access_index = access.access_index;
    m.access_type = access.access_type;
    dynamic_array_push_back(&generator->name_mappings, m);
}

Data_Access intermediate_generator_create_intermediate_result(Intermediate_Generator* generator, Type_Signature* type_signature)
{
    if (type_signature == generator->analyser->type_system.void_type) panic("Should not happen");
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Data_Access result;
    dynamic_array_push_back(&function->intermediate_results, type_signature);
    result.access_index = function->intermediate_results.size - 1;
    result.access_type = Data_Access_Type::INTERMEDIATE_ACCESS;
    result.is_pointer_access = false;
    return result;
}

Data_Access intermediate_generator_create_global_variable(Intermediate_Generator* generator, int name_id, Type_Signature* type_signature)
{
    Data_Access result;
    Intermediate_Variable var;
    var.name_handle = name_id;
    var.type = type_signature;
    dynamic_array_push_back(&generator->global_variables, var);
    result.access_index = generator->global_variables.size - 1;
    result.access_type = Data_Access_Type::GLOBAL_ACCESS;
    result.is_pointer_access = false;
    intermediate_generator_create_name_mapping(generator, result, name_id);
    return result;
}

Data_Access intermediate_generator_create_local_variable(Intermediate_Generator* generator, int name_id, Type_Signature* type_signature)
{
    if (type_signature == generator->analyser->type_system.void_type) panic("Should not happen");
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Data_Access result;
    Intermediate_Variable var;
    var.name_handle = name_id;
    var.type = type_signature;
    dynamic_array_push_back(&function->local_variables, var);
    result.access_index = function->local_variables.size - 1;
    result.access_type = Data_Access_Type::VARIABLE_ACCESS;
    result.is_pointer_access = false;
    intermediate_generator_create_name_mapping(generator, result, name_id);
    return result;
}

int intermediate_generator_find_function_by_name(Intermediate_Generator* generator, int name_id)
{
    for (int i = 0; i < generator->function_to_ast_node_mapping.size; i++) {
        int name_handle = generator->analyser->parser->nodes[generator->function_to_ast_node_mapping[i]].name_id;
        if (name_handle == name_id) {
            return i;
        }
    }
    panic("Should not happen!");
    return 0;
}

struct Block_Recorder
{
    Intermediate_Generator* generator;
    int instruction_index;
    int running_index;
};

Block_Recorder block_recorder_0_start_record_condition(Intermediate_Generator* generator, Intermediate_Instruction_Type type) 
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Intermediate_Instruction i;
    i.type = type;
    i.condition_calculation_instruction_start = function->instructions.size + 1;
    dynamic_array_push_back(&function->instructions, i);

    Block_Recorder result;
    result.generator = generator;
    result.instruction_index = function->instructions.size - 1;
    result.running_index = function->instructions.size;
    return result;
}

void block_recorder_1_stop_record_condition(Block_Recorder* recorder, Data_Access condition_access)
{
    Intermediate_Generator* generator = recorder->generator;
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    function->instructions[recorder->instruction_index].source1 = condition_access;
    function->instructions[recorder->instruction_index].condition_calculation_instruction_start = recorder->running_index;
    function->instructions[recorder->instruction_index].condition_calculation_instruction_end_exclusive = function->instructions.size;
    recorder->running_index = function->instructions.size;
}

void block_recorder_2_stop_record_true_block(Block_Recorder* recorder)
{
    Intermediate_Generator* generator = recorder->generator;
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    function->instructions[recorder->instruction_index].true_branch_instruction_start = recorder->running_index;
    function->instructions[recorder->instruction_index].true_branch_instruction_end_exclusive = function->instructions.size;
    recorder->running_index = function->instructions.size;
    function->instructions[recorder->instruction_index].false_branch_instruction_start = function->instructions.size;
    function->instructions[recorder->instruction_index].false_branch_instruction_end_exclusive = function->instructions.size;
}

void block_recorder_3_stop_record_false_block(Block_Recorder* recorder)
{
    Intermediate_Generator* generator = recorder->generator;
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    function->instructions[recorder->instruction_index].false_branch_instruction_start = recorder->running_index;
    function->instructions[recorder->instruction_index].false_branch_instruction_end_exclusive = function->instructions.size;
    recorder->running_index = function->instructions.size;
}

Type_Signature* intermediate_instruction_binary_operation_get_result_type(Intermediate_Instruction_Type instr_type, Intermediate_Generator* generator)
{
    switch (instr_type)
    {
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32:
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:
        return generator->analyser->type_system.i32_type;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32:
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
        return generator->analyser->type_system.f32_type;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL:
    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND:
    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR:
    case Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT:
        return generator->analyser->type_system.bool_type;
    }
    panic("Sheit\n");
    return 0;
}

Intermediate_Instruction_Type binary_operation_get_instruction_type(Intermediate_Generator* generator, AST_Node_Type::ENUM op_type, Type_Signature* operand_types)
{
    switch (op_type)
    {
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F64;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F64;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F64;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F64;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I64;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
        return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
        return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
        if (operand_types->type == Signature_Type::POINTER) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_POINTER;
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F64;
        if (operand_types == generator->analyser->type_system.bool_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
        if (operand_types->type == Signature_Type::POINTER) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_POINTER;
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F64;
        if (operand_types == generator->analyser->type_system.bool_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F64;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F64;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F64;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
        if (operand_types == generator->analyser->type_system.u8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_U8;
        if (operand_types == generator->analyser->type_system.u16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_U16;
        if (operand_types == generator->analyser->type_system.u32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_U32;
        if (operand_types == generator->analyser->type_system.u64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_U64;
        if (operand_types == generator->analyser->type_system.i8_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I8;
        if (operand_types == generator->analyser->type_system.i16_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I16;
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32;
        if (operand_types == generator->analyser->type_system.i64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I64;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32;
        if (operand_types == generator->analyser->type_system.f64_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F64;
        panic("Not valid, should have been caught!");
    }
    panic("This should not happen :)\n");
    return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND;
}

Data_Access intermediate_generator_generate_cast(Intermediate_Generator* generator, Data_Access source_access,
    Type_Signature* source_type, Type_Signature* destination_type, bool force_destination, Data_Access destination)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    if (source_type->type == Signature_Type::ARRAY_SIZED && destination_type->type == Signature_Type::ARRAY_UNSIZED)
    {
        Data_Access sized_array_access;
        Type_Signature* child_ptr_type = type_system_make_pointer(&generator->analyser->type_system, destination_type->child_type);
        if (force_destination) sized_array_access = destination;
        else {
            sized_array_access = intermediate_generator_create_intermediate_result(generator, destination_type);
        }

        Data_Access ptr_access = data_access_create_member_access(generator, sized_array_access, 0, child_ptr_type);
        Data_Access size_access = data_access_create_member_access(generator, sized_array_access, 8,
            generator->analyser->type_system.i32_type
        );

        Intermediate_Instruction ptr_move_instr;
        ptr_move_instr.type = Intermediate_Instruction_Type::ADDRESS_OF;
        ptr_move_instr.destination = ptr_access;
        ptr_move_instr.source1 = source_access;
        dynamic_array_push_back(&function->instructions, ptr_move_instr);

        Intermediate_Instruction set_size_instr;
        set_size_instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
        set_size_instr.destination = size_access;
        set_size_instr.constant_i32_value = source_type->array_element_count;
        dynamic_array_push_back(&function->instructions, set_size_instr);

        return sized_array_access;
    }

    Intermediate_Instruction instr;
    if (force_destination) {
        instr.destination = destination;
    }
    else {
        instr.destination = intermediate_generator_create_intermediate_result(generator, destination_type);
    }
    instr.source1 = source_access;
    instr.cast_from = source_type;
    instr.cast_to = destination_type;

    if (instr.cast_from == generator->analyser->type_system.u64_type && instr.cast_to->type == Signature_Type::POINTER) {
        instr.type = Intermediate_Instruction_Type::CAST_U64_TO_POINTER;
    }
    else if (instr.cast_to == generator->analyser->type_system.u64_type && instr.cast_from->type == Signature_Type::POINTER) {
        instr.type = Intermediate_Instruction_Type::CAST_POINTER_TO_U64;
    }
    else if (destination_type->type == Signature_Type::POINTER && source_type->type == Signature_Type::POINTER) {
        instr.type = Intermediate_Instruction_Type::CAST_POINTERS;
    }
    else if (source_type->type == Signature_Type::PRIMITIVE && destination_type->type == Signature_Type::PRIMITIVE) {
        instr.type = Intermediate_Instruction_Type::CAST_PRIMITIVE_TYPES;
    }
    else panic("Should not happen!");

    dynamic_array_push_back(&function->instructions, instr);
    return instr.destination;
}


Data_Access intermediate_generator_generate_expression(Intermediate_Generator* generator, int expression_index,
    bool force_destination, Data_Access destination);
Data_Access intermediate_generator_generate_expression_without_casting(Intermediate_Generator* generator, int expression_index,
    bool force_destination, Data_Access destination)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    AST_Node* expression = &generator->analyser->parser->nodes[expression_index];
    Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->semantic_information[expression_index].symbol_table_index];

    switch (expression->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
    {
        Symbol* function_symbol = symbol_table_find_symbol_of_type(
            generator->analyser->symbol_tables[generator->analyser->semantic_information[expression_index].symbol_table_index],
            expression->name_id, Symbol_Type::FUNCTION);
        if (function_symbol == 0) panic("Should not happen, maybe semantic information isnt complete yet!");

        Intermediate_Instruction instr;
        // Check if its an hardcoded function
        bool is_hardcoded = false;
        for (int i = 0; i < generator->analyser->hardcoded_functions.size; i++) {
            if (expression->name_id == generator->analyser->hardcoded_functions[i].name_handle) {
                instr.type = Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION;
                instr.hardcoded_function_type = generator->analyser->hardcoded_functions[i].type;
                is_hardcoded = true;
            }
        }
        if (!is_hardcoded)
        {
            instr.type = Intermediate_Instruction_Type::CALL_FUNCTION;
            instr.intermediate_function_index = intermediate_generator_find_function_by_name(generator, expression->name_id);
        }
        instr.arguments = dynamic_array_create_empty<Data_Access>(expression->children.size);

        // Generate argument Expressions
        for (int i = 0; i < expression->children.size; i++) {
            Data_Access argument = intermediate_generator_generate_expression(
                generator, expression->children[i], false, data_access_make_empty()
            );
            dynamic_array_push_back(&instr.arguments, argument);
        }
        // Generate destination
        if (generator->analyser->semantic_information[expression_index].expression_result_type != generator->analyser->type_system.void_type)
        {
            if (force_destination) {
                instr.destination = destination;
            }
            else {
                instr.destination = intermediate_generator_create_intermediate_result(
                    generator,
                    generator->analyser->semantic_information[expression_index].expression_result_type
                );
            }
        }
        else {
            instr.destination = data_access_make_empty();
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_CAST:
    {
        Type_Signature* cast_to_type = generator->analyser->semantic_information[expression_index].expression_result_type;
        Type_Signature* cast_from_type = generator->analyser->semantic_information[expression->children[1]].expression_result_type;
        if (cast_to_type == cast_from_type)
        {
            if (force_destination) {
                return intermediate_generator_generate_expression_without_casting(generator, expression->children[1], true, destination);
            }
            else {
                return intermediate_generator_generate_expression_without_casting(generator, expression->children[1], false, data_access_make_empty());
            }
        }

        Data_Access source = intermediate_generator_generate_expression_without_casting(generator, expression->children[1], false, data_access_make_empty());
        return intermediate_generator_generate_cast(generator, source, cast_from_type, cast_to_type, force_destination, destination);
    }
    case AST_Node_Type::EXPRESSION_LITERAL:
    {
        Intermediate_Instruction instr;
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_result(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }

        Token& token = generator->analyser->parser->lexer->tokens[generator->analyser->parser->token_mapping[expression_index].start_index];
        if (token.type == Token_Type::FLOAT_LITERAL) {
            instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_F32;
            instr.constant_f32_value = token.attribute.float_value;
        }
        else if (token.type == Token_Type::INTEGER_LITERAL) {
            instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
            instr.constant_i32_value = token.attribute.integer_value;
        }
        else if (token.type == Token_Type::BOOLEAN_LITERAL) {
            instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_BOOL;
            instr.constant_bool_value = token.attribute.bool_value;
        }
        else if (token.type == Token_Type::NULLPTR) {
            instr.type = Intermediate_Instruction_Type::LOAD_NULLPTR;
        }
        else panic("what");

        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_NEW:
    {
        Type_Signature* type = generator->analyser->semantic_information[expression_index].expression_result_type;
        int allocate_size = type->child_type->size_in_bytes;

        Intermediate_Instruction load_size_instr;
        load_size_instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
        load_size_instr.constant_i32_value = allocate_size;
        load_size_instr.destination = intermediate_generator_create_intermediate_result(generator, generator->analyser->type_system.i32_type);
        dynamic_array_push_back(&function->instructions, load_size_instr);

        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION;
        i.hardcoded_function_type = Hardcoded_Function_Type::MALLOC_SIZE_I32;
        i.arguments = dynamic_array_create_empty<Data_Access>(1);
        dynamic_array_push_back(&i.arguments, load_size_instr.destination);
        if (force_destination) {
            i.destination = destination;
        }
        else {
            i.destination = intermediate_generator_create_intermediate_result(generator, type);
        }
        dynamic_array_push_back(&function->instructions, i);
        return i.destination;
    }
    case AST_Node_Type::EXPRESSION_NEW_ARRAY:
    {
        Type_Signature* type = generator->analyser->semantic_information[expression_index].expression_result_type;
        int element_size = type->child_type->size_in_bytes;

        Data_Access result_access;
        if (force_destination) {
            result_access = destination;
        }
        else {
            result_access = intermediate_generator_create_intermediate_result(generator, type);
        }

        Type_Signature* element_pointer_type = type_system_make_pointer(&generator->analyser->type_system, type->child_type);
        Data_Access pointer_access = data_access_create_member_access(generator, destination, 0, element_pointer_type);

        Data_Access element_count_access = data_access_create_member_access(generator, result_access, 8, generator->analyser->type_system.i32_type);
        intermediate_generator_generate_expression(generator, expression->children[0], true, element_count_access);

        Intermediate_Instruction load_element_size_instr;
        load_element_size_instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
        load_element_size_instr.constant_i32_value = element_size;
        load_element_size_instr.destination = intermediate_generator_create_intermediate_result(generator, generator->analyser->type_system.i32_type);
        dynamic_array_push_back(&function->instructions, load_element_size_instr);

        Intermediate_Instruction calc_array_byte_size_instr;
        calc_array_byte_size_instr.type = Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32;
        calc_array_byte_size_instr.source1 = element_count_access;
        calc_array_byte_size_instr.source2 = load_element_size_instr.destination;
        calc_array_byte_size_instr.destination = intermediate_generator_create_intermediate_result(generator, generator->analyser->type_system.i32_type);
        dynamic_array_push_back(&function->instructions, calc_array_byte_size_instr);

        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION;
        i.hardcoded_function_type = Hardcoded_Function_Type::MALLOC_SIZE_I32;
        i.destination = pointer_access;
        i.arguments = dynamic_array_create_empty<Data_Access>(1);
        dynamic_array_push_back(&i.arguments, calc_array_byte_size_instr.destination);
        dynamic_array_push_back(&function->instructions, i);
        return result_access;
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ:
    {
        Data_Access access = data_access_make_by_name(generator, expression->name_id);
        if (force_destination) {
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            instr.destination = destination;
            instr.source1 = access;
            dynamic_array_push_back(&function->instructions, instr);
            return destination;
        }
        return access;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
    {
        Data_Access access = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        if (access.is_pointer_access)
        {
            access.is_pointer_access = false;
            if (force_destination) {
                Intermediate_Instruction instr;
                instr.type = Intermediate_Instruction_Type::MOVE_DATA;
                instr.destination = destination;
                instr.source1 = access;
                dynamic_array_push_back(&function->instructions, instr);
                return destination;
            }
            return access;
        }

        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::ADDRESS_OF;
        instr.source1 = access;
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_result(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
    {
        Data_Access pointer_access = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        Data_Access result_access;
        if (pointer_access.is_pointer_access)
        {
            // This is the case for multiple dereferences
            result_access = intermediate_generator_create_intermediate_result(generator,
                generator->analyser->semantic_information[expression->children[0]].expression_result_type);
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            instr.destination = result_access;
            instr.source1 = pointer_access;
            dynamic_array_push_back(&function->instructions, instr);

            result_access.is_pointer_access = true;
        }
        else {
            result_access = pointer_access;
            result_access.is_pointer_access = true;
        }

        if (force_destination) {
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            instr.destination = destination;
            instr.source1 = result_access;
            dynamic_array_push_back(&function->instructions, instr);
            return instr.destination;
        }
        else {
            return result_access;
        }
    }
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
    {
        Data_Access structure_data = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        Semantic_Node_Information* node_info = &generator->analyser->semantic_information[expression_index];

        if (node_info->member_access_needs_pointer_dereference) // Access with . on pointers
        {
            if (structure_data.is_pointer_access)
            {
                Intermediate_Instruction instr;
                instr.type = Intermediate_Instruction_Type::MOVE_DATA;
                instr.source1 = structure_data;
                instr.destination = intermediate_generator_create_intermediate_result(generator,
                    type_system_make_pointer(&generator->analyser->type_system, node_info->expression_result_type)
                );
                dynamic_array_push_back(&function->instructions, instr);
                structure_data = instr.destination;
            }
            structure_data.is_pointer_access = true;
        }

        if (node_info->member_access_is_address_of) // .data on sized arrays
        {
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::ADDRESS_OF;
            instr.source1 = structure_data;
            if (force_destination) {
                instr.destination = destination;
            }
            else {
                instr.destination = intermediate_generator_create_intermediate_result(generator, node_info->expression_result_type);
            }
            dynamic_array_push_back(&function->instructions, instr);
            return instr.destination;
        }
        else if (node_info->member_access_is_constant_size) // .size on sized arrays
        {
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
            if (force_destination) {
                instr.destination = destination;
            }
            else {
                instr.destination = intermediate_generator_create_intermediate_result(generator, generator->analyser->type_system.i32_type);
            }
            instr.constant_i32_value = node_info->member_access_offset;
            dynamic_array_push_back(&function->instructions, instr);
            return instr.destination;
        }

        Data_Access member_access = data_access_create_member_access(generator, structure_data, node_info->member_access_offset,
            type_system_make_pointer(&generator->analyser->type_system, node_info->expression_result_type)
        );

        if (force_destination) {
            Intermediate_Instruction move_instr;
            move_instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            move_instr.source1 = member_access;
            move_instr.destination = destination;
            dynamic_array_push_back(&function->instructions, move_instr);
            return move_instr.destination;
        }
        else {
            return member_access;
        }
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
    {
        Type_Signature* array_type_signature = generator->analyser->semantic_information[expression->children[0]].expression_result_type;
        Type_Signature* element_type_signature = array_type_signature->child_type;
        Type_Signature* element_pointer_type = type_system_make_pointer(&generator->analyser->type_system, array_type_signature->child_type);

        Data_Access array_data = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        Data_Access index_data = intermediate_generator_generate_expression(generator, expression->children[1], false, data_access_make_empty());

        Data_Access base_pointer_access = data_access_create_member_access(generator, array_data, 0, element_pointer_type);
        if (array_type_signature->type == Signature_Type::ARRAY_UNSIZED) {
            base_pointer_access = data_access_create_member_access(generator, array_data, 0, element_pointer_type);
        }
        else {
            Intermediate_Instruction instr_addr_off;
            instr_addr_off.type = Intermediate_Instruction_Type::ADDRESS_OF;
            instr_addr_off.destination = intermediate_generator_create_intermediate_result(generator, element_pointer_type);
            instr_addr_off.source1 = array_data;
            dynamic_array_push_back(&function->instructions, instr_addr_off);
            base_pointer_access = instr_addr_off.destination;
        }

        // Array bounds check
        if (false)
        {
            Data_Access size_data;
            if (array_type_signature->type == Signature_Type::ARRAY_SIZED) {
                size_data = data_access_create_const_i32(generator, array_type_signature->array_element_count);
            }
            else {
                Type_Signature* int_ptr_type = type_system_make_pointer(&generator->analyser->type_system, generator->analyser->type_system.i32_type);
                size_data = data_access_create_member_access(generator, array_data, 8, int_ptr_type);
            }
            Block_Recorder recorder = block_recorder_0_start_record_condition(generator, Intermediate_Instruction_Type::IF_BLOCK);
            Intermediate_Instruction condition_instr;
            condition_instr.type = Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32;
            condition_instr.destination = intermediate_generator_create_intermediate_result(generator, generator->analyser->type_system.bool_type);
            condition_instr.source1 = index_data;
            condition_instr.source2 = size_data;
            dynamic_array_push_back(&function->instructions, condition_instr);
            block_recorder_1_stop_record_condition(&recorder, condition_instr.destination);

            // True block
            Intermediate_Instruction error_instr;
            error_instr.source1 = index_data;
            error_instr.type = Intermediate_Instruction_Type::EXIT;
            error_instr.exit_code = Exit_Code::OUT_OF_BOUNDS;
            dynamic_array_push_back(&function->instructions, error_instr);
            block_recorder_2_stop_record_true_block(&recorder);
        }

        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::CALCULATE_ARRAY_ACCESS_POINTER;
        instr.constant_i32_value = element_type_signature->size_in_bytes;
        instr.destination = intermediate_generator_create_intermediate_result(
            generator,
            element_pointer_type
        );
        instr.source1 = base_pointer_access;
        instr.source2 = index_data;
        dynamic_array_push_back(&function->instructions, instr);
        instr.destination.is_pointer_access = true;

        if (force_destination) {
            Intermediate_Instruction move_instr;
            move_instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            move_instr.source1 = instr.destination;
            move_instr.destination = destination;
            dynamic_array_push_back(&function->instructions, move_instr);
            return move_instr.destination;
        }
        else {
            return instr.destination;
        }
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
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
    {
        Type_Signature* left_type = generator->analyser->semantic_information[expression->children[0]].expression_result_type;
        Intermediate_Instruction instr;
        instr.type = binary_operation_get_instruction_type(generator, expression->type, left_type);
        instr.source1 = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        instr.source2 = intermediate_generator_generate_expression(generator, expression->children[1], false, data_access_make_empty());
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_result(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
    {
        Intermediate_Instruction_Type instr_type;
        Type_Signature* operand_type = generator->analyser->semantic_information[expression->children[0]].expression_result_type;
        if (operand_type == generator->analyser->type_system.f32_type) {
            instr_type = Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32;
        }
        else if (operand_type == generator->analyser->type_system.i32_type) {
            instr_type = Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32;
        }
        else panic("Should not happen");

        Intermediate_Instruction instr;
        instr.type = instr_type;
        instr.source1 = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_result(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
    {
        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT;
        instr.source1 = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_result(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    }

    panic("Shit this is not something that should happen!\n");
    return data_access_make_empty();
}

Data_Access intermediate_generator_generate_expression(Intermediate_Generator* generator, int expression_index,
    bool force_destination, Data_Access destination)
{
    Semantic_Node_Information* info = &generator->analyser->semantic_information[expression_index];
    if (!info->needs_casting_to_cast_type) return intermediate_generator_generate_expression_without_casting(generator, expression_index, force_destination, destination);

    Data_Access source_access = intermediate_generator_generate_expression_without_casting(generator, expression_index, false, data_access_make_empty());
    return intermediate_generator_generate_cast(generator, source_access, info->expression_result_type, info->cast_result_type, force_destination, destination);
}

void intermediate_generator_generate_statement_block(Intermediate_Generator* generator, int block_index);
void intermediate_generator_generate_statement(Intermediate_Generator* generator, int statement_index)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    Semantic_Node_Information* info = &generator->analyser->semantic_information[statement_index];

    AST_Node* statement = &generator->analyser->parser->nodes[statement_index];
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_BLOCK: {
        intermediate_generator_generate_statement_block(generator, statement_index);
        break;
    }
    case AST_Node_Type::STATEMENT_DELETE:
    {
        Data_Access delete_access = intermediate_generator_generate_expression(generator, statement->children[0], false, data_access_make_empty());
        if (info->delete_is_array_delete) {
            Type_Signature* type = generator->analyser->semantic_information[statement->children[0]].expression_result_type;
            Type_Signature* element_pointer_type = type_system_make_pointer(&generator->analyser->type_system, type->child_type);
            delete_access = data_access_create_member_access(generator, delete_access, 0, element_pointer_type);
        }
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION;
        i.hardcoded_function_type = Hardcoded_Function_Type::FREE_POINTER;
        i.arguments = dynamic_array_create_empty<Data_Access>(1);
        i.destination = data_access_make_empty();
        dynamic_array_push_back(&i.arguments, delete_access);
        dynamic_array_push_back(&function->instructions, i);
        break;
    }
    case AST_Node_Type::STATEMENT_BREAK:
    {
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::BREAK;
        dynamic_array_push_back(&function->instructions, i);
        break;
    }
    case AST_Node_Type::STATEMENT_CONTINUE:
    {
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::CONTINUE;
        dynamic_array_push_back(&function->instructions, i);
        break;
    }
    case AST_Node_Type::STATEMENT_RETURN: 
    {
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::RETURN;
        if (generator->current_function_index == generator->main_function_index) {
            i.type = Intermediate_Instruction_Type::EXIT;
            i.exit_code = Exit_Code::SUCCESS;
        }
        i.return_has_value = false;
        if (function->function_type->return_type != generator->analyser->type_system.void_type) {
            i.return_has_value = true;
            i.source1 = intermediate_generator_generate_expression(generator, statement->children[0], false, data_access_make_empty());
        }
        dynamic_array_push_back(&function->instructions, i);
        break;
    }
    case AST_Node_Type::STATEMENT_IF:
    case AST_Node_Type::STATEMENT_IF_ELSE:
    case AST_Node_Type::STATEMENT_WHILE:
    {
        Intermediate_Instruction_Type instr_type;
        if (statement->type == AST_Node_Type::STATEMENT_IF) {
            instr_type = Intermediate_Instruction_Type::IF_BLOCK;
        }
        else if (statement->type == AST_Node_Type::STATEMENT_IF_ELSE) {
            instr_type = Intermediate_Instruction_Type::IF_BLOCK;
        }
        else if (statement->type == AST_Node_Type::STATEMENT_WHILE) {
            instr_type = Intermediate_Instruction_Type::WHILE_BLOCK;
        }
        else panic("Cannot happen");

        Block_Recorder recorder = block_recorder_0_start_record_condition(generator, instr_type);
        Data_Access condition_access = intermediate_generator_generate_expression(generator, statement->children[0], false, data_access_make_empty());
        block_recorder_1_stop_record_condition(&recorder, condition_access);
        intermediate_generator_generate_statement_block(generator, statement->children[1]);
        block_recorder_2_stop_record_true_block(&recorder);
        if (statement->type == AST_Node_Type::STATEMENT_IF_ELSE) {
            intermediate_generator_generate_statement_block(generator, statement->children[2]);
            block_recorder_3_stop_record_false_block(&recorder);
        }
        break;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION: {
        intermediate_generator_generate_expression(generator, statement->children[0], false, data_access_make_empty());
        break;
    }
    case AST_Node_Type::STATEMENT_ASSIGNMENT:
    {
        Data_Access destination_register = intermediate_generator_generate_expression(
            generator, statement->children[0], false, data_access_make_empty()
        );
        intermediate_generator_generate_expression(generator, statement->children[1], true, destination_register);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN: {
        intermediate_generator_generate_expression(generator, statement->children[1], true, data_access_make_by_name(generator, statement->name_id));
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: {
        intermediate_generator_generate_expression(generator, statement->children[0], true, data_access_make_by_name(generator, statement->name_id));
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION: {
        break;
    }
    }

    return;
}

void intermediate_generator_generate_statement_block(Intermediate_Generator* generator, int block_index)
{
    AST_Node* block = &generator->analyser->parser->nodes[block_index];
    int size_rollback = generator->name_mappings.size;

    // Generate Variable Registers
    Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->semantic_information[block_index].symbol_table_index];
    for (int i = 0; i < table->symbols.size; i++)
    {
        Symbol s = table->symbols[i];
        if (s.symbol_type != Symbol_Type::VARIABLE) {
            panic("Shouldnt happen now, only when we have inline functions and types\n");
            continue;
        }
        intermediate_generator_create_local_variable(generator, s.name_handle, s.type);
    }
    for (int i = 0; i < block->children.size; i++) {
        intermediate_generator_generate_statement(generator, block->children[i]);
    }
    dynamic_array_rollback_to_size(&generator->name_mappings, size_rollback);
}

void intermediate_generator_generate_function_code(Intermediate_Generator* generator, int function_index)
{
    generator->current_function_index = function_index;
    Intermediate_Function* im_function = &generator->functions[function_index];
    int function_node_index = generator->function_to_ast_node_mapping[function_index];
    AST_Node* function = &generator->analyser->parser->nodes[function_node_index];
    Symbol_Table* function_table = generator->analyser->symbol_tables[generator->analyser->semantic_information[function_node_index].symbol_table_index];
    int size_rollback = generator->name_mappings.size;

    // Generate Parameter Mappings
    for (int i = 0; i < function_table->symbols.size; i++) {
        Symbol* s = &function_table->symbols[i];
        if (s->symbol_type != Symbol_Type::VARIABLE) {
            panic("Should not happen i think");
        }
        Name_Mapping mapping;
        mapping.access_type = Data_Access_Type::PARAMETER_ACCESS;
        mapping.access_index = i;
        mapping.name_handle = s->name_handle;
        dynamic_array_push_back(&generator->name_mappings, mapping);
    }

    // Generate function code
    intermediate_generator_generate_statement_block(generator, function->children[2]);

    if (generator->analyser->semantic_information[function_node_index].needs_empty_return_at_end) 
    {
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::RETURN;
        if (generator->current_function_index == generator->main_function_index) {
            i.type = Intermediate_Instruction_Type::EXIT;
            i.exit_code = Exit_Code::SUCCESS;
        }
        i.return_has_value = false;
        dynamic_array_push_back(&im_function->instructions, i);
    }

    dynamic_array_rollback_to_size(&generator->name_mappings, size_rollback);
}

void intermediate_instruction_destroy(Intermediate_Instruction* instruction)
{
    switch (instruction->type)
    {
    case Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION:
    case Intermediate_Instruction_Type::CALL_FUNCTION: {
        dynamic_array_destroy(&instruction->arguments);
        return;
    }
    }
}

Intermediate_Function intermediate_function_create(int name_handle, Type_Signature* function_signature)
{
    Intermediate_Function result;
    result.instructions = dynamic_array_create_empty<Intermediate_Instruction>(64);
    result.local_variables = dynamic_array_create_empty<Intermediate_Variable>(64);
    result.intermediate_results = dynamic_array_create_empty<Type_Signature*>(64);

    result.instruction_to_ast_node_mapping = dynamic_array_create_empty<int>(64);
    result.register_to_ast_mapping = dynamic_array_create_empty<int>(64);

    result.name_handle = name_handle;
    result.function_type = function_signature;
    return result;
}

void intermediate_function_destroy(Intermediate_Function* function)
{
    for (int i = 0; i < function->instructions.size; i++) {
        intermediate_instruction_destroy(&function->instructions[i]);
    }
    dynamic_array_destroy(&function->instructions);
    dynamic_array_destroy(&function->instruction_to_ast_node_mapping);
    dynamic_array_destroy(&function->intermediate_results);
    dynamic_array_destroy(&function->local_variables);
    dynamic_array_destroy(&function->register_to_ast_mapping);
}

Intermediate_Generator intermediate_generator_create()
{
    Intermediate_Generator result;
    result.functions = dynamic_array_create_empty<Intermediate_Function>(64);
    result.name_mappings = dynamic_array_create_empty<Name_Mapping>(64);
    result.global_variables = dynamic_array_create_empty<Intermediate_Variable>(64);
    result.function_to_ast_node_mapping = dynamic_array_create_empty<int>(16);
    return result;
}

void intermediate_generator_destroy(Intermediate_Generator* generator)
{
    for (int i = 0; i < generator->functions.size; i++) {
        intermediate_function_destroy(&generator->functions[i]);
    }
    dynamic_array_destroy(&generator->functions);
    dynamic_array_destroy(&generator->function_to_ast_node_mapping);
    dynamic_array_destroy(&generator->name_mappings);
    dynamic_array_destroy(&generator->global_variables);
}

void intermediate_generator_generate(Intermediate_Generator* generator, Semantic_Analyser* analyser)
{
    // TODO: Do reset better, kinda tricky because of intermediate functions
    intermediate_generator_destroy(generator);
    *generator = intermediate_generator_create();

    generator->analyser = analyser;
    generator->main_function_index = -1;

    // Generate empty functions, so that they can be searched and called from other functions
    for (int i = 0; i < analyser->parser->nodes[0].children.size; i++)
    {
        AST_Node_Index function_node_index = analyser->parser->nodes[0].children[i];
        AST_Node* function_node = &analyser->parser->nodes[function_node_index];
        if (function_node->type != AST_Node_Type::FUNCTION) continue;
        dynamic_array_push_back(
            &generator->functions,
            intermediate_function_create(function_node->name_id, analyser->semantic_information[function_node_index].function_signature)
        );
        dynamic_array_push_back(&generator->function_to_ast_node_mapping, function_node_index);
        if (analyser->parser->nodes[function_node_index].name_id == analyser->main_token_index) {
            generator->main_function_index = generator->functions.size - 1;
        }
    }

    // Generate all globals
    for (int i = 0; i < analyser->parser->nodes[0].children.size; i++)
    {
        AST_Node_Index variable_node_index = analyser->parser->nodes[0].children[i];
        AST_Node* variable_node = &analyser->parser->nodes[variable_node_index];
        if (variable_node->type != AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN &&
            variable_node->type != AST_Node_Type::STATEMENT_VARIABLE_DEFINITION &&
            variable_node->type != AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER) continue;

        Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->semantic_information[0].symbol_table_index];
        if (table == 0) {
            logg("Just checking if this works with 0");
        }
        Data_Access global_access = intermediate_generator_create_global_variable(generator, variable_node->name_id,
            symbol_table_find_symbol_of_type(table, variable_node->name_id, Symbol_Type::VARIABLE)->type
        );
        // Generate initialization for global data in main function
        generator->current_function_index = generator->main_function_index;
        if (variable_node->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN) {
            intermediate_generator_generate_expression(
                generator, variable_node->children[1], true, global_access
            );
        }
        else if (variable_node->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER) {
            intermediate_generator_generate_expression(
                generator, variable_node->children[0], true, global_access
            );
        }
    }

    // Now generate all functions
    for (int i = 0; i < generator->functions.size; i++) {
        intermediate_generator_generate_function_code(generator, i);
    }
}

void data_access_append_to_string(String* string, Data_Access access, int function_index, Intermediate_Generator* generator)
{
    Intermediate_Function* function = &generator->functions[function_index];
    Type_Signature* type = generator->analyser->type_system.error_type;
    if (access.is_pointer_access) {
        string_append_formated(string, "MEMORY_ACCESS through ");
    }
    switch (access.access_type)
    {
    case Data_Access_Type::GLOBAL_ACCESS:
        string_append_formated(string, "%s (Global #%d)", 
            lexer_identifer_to_string(generator->analyser->parser->lexer, generator->global_variables[access.access_index].name_handle).characters,
            access.access_index
        );
        type = generator->global_variables[access.access_index].type;
        break;
    case Data_Access_Type::VARIABLE_ACCESS:
        string_append_formated(string, "%s (Local #%d)", 
            lexer_identifer_to_string(generator->analyser->parser->lexer, function->local_variables[access.access_index].name_handle).characters,
            access.access_index
        );
        type = function->local_variables[access.access_index].type;
        break;
    case Data_Access_Type::INTERMEDIATE_ACCESS:
        string_append_formated(string, "Intermediate #%d", access.access_index);
        type = function->intermediate_results[access.access_index];
        break;
    case Data_Access_Type::PARAMETER_ACCESS:
        AST_Node* parameter_block_node = 
            &generator->analyser->parser->nodes[generator->analyser->parser->nodes[generator->function_to_ast_node_mapping[function_index]].children[0]];
        string_append_formated(string, "%s (Param %d)", 
            lexer_identifer_to_string(generator->analyser->parser->lexer, 
                generator->analyser->parser->nodes[parameter_block_node->children[access.access_index]].name_id
            ).characters,
            access.access_index
        );
        type = function->function_type->parameter_types[access.access_index];
        break;
    }
    string_append_formated(string, ": ");
    type_signature_append_to_string(string, type);
}

void exit_code_append_to_string(String* string, Exit_Code code)
{
    switch (code)
    {
    case Exit_Code::SUCCESS:
        string_append_formated(string, "SUCCESS");
        break;
    case Exit_Code::OUT_OF_BOUNDS:
        string_append_formated(string, "OUT OF BOUNDS");
        break;
    case Exit_Code::STACK_OVERFLOW:
        string_append_formated(string, "STACK_OVERFLOW");
        break;
    case Exit_Code::RETURN_VALUE_OVERFLOW:
        string_append_formated(string, "STACK_OVERFLOW");
        break;
    default: panic("");
    }
}

bool intermediate_instruction_type_is_unary_operation(Intermediate_Instruction_Type instruction_type)
{
    if ((i32)instruction_type >= (i32)Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I8 &&
        (i32)instruction_type <= (i32)Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT) {
        return true;
    }
    return false;
}

bool intermediate_instruction_type_is_binary_operation(Intermediate_Instruction_Type instruction_type)
{
    if ((i32)instruction_type >= (i32)Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8 &&
        (i32)instruction_type <= (i32)Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_POINTER) {
        return true;
    }
    return false;
}

void intermediate_instruction_unary_operation_append_to_string(String* string, Intermediate_Instruction_Type instruction_type)
{
    switch (instruction_type)
    {
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I8: {
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I8");
        break;
    }
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I16: {
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I16");
        break;
    }
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32: {
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I32");
        break;
    }
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I64: {
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I64");
        break;
    }
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32: {
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_F32");
        break;
    }
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F64: {
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_F64");
        break;
    }
    case Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT: {
        string_append_formated(string, "UNARY_OP_BOOLEAN_NOT");
        break;
    }
    default: panic("what");
    }
    return;
}

void intermediate_instruction_binop_append_to_string(String* string, Intermediate_Instruction_Type instruction_type)
{
    if ((i32)instruction_type >= (i32)Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8 &&
        (i32)instruction_type <= (i32)Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F64)
    {
        const char* instruction_prefix = "Error";
        const char* type_postfix = "_Error";
        const char* prefixes[] = {
            "BINARY_OP_ARITHMETIC_ADDITION_",
            "BINARY_OP_ARITHMETIC_SUBTRACTION_",
            "BINARY_OP_ARITHMETIC_MULTIPLICATION_",
            "BINARY_OP_ARITHMETIC_DIVISION_",
            "BINARY_OP_ARITHMETIC_MODULO_",
            "BINARY_OP_COMPARISON_EQUAL_",
            "BINARY_OP_COMPARISON_NOT_EQUAL_",
            "BINARY_OP_COMPARISON_GREATER_THAN_",
            "BINARY_OP_COMPARISON_GREATER_EQUAL_",
            "BINARY_OP_COMPARISON_LESS_THAN_",
            "BINARY_OP_COMPARISON_LESS_EQUAL_",
        };
        const char* types[] = { "U8", "U16", "U32", "U64", "I8", "I16", "I32", "I64", "F32", "F64" };
        i32 type_index = (i32)instruction_type - (i32)Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8;
        int prefix_index = type_index % 11;
        int postfix_index = type_index / 11;
        instruction_prefix = prefixes[prefix_index];
        type_postfix = types[postfix_index];
        string_append_formated(string, "%s", instruction_prefix);
        string_append_formated(string, "%s", type_postfix);
    }

    switch (instruction_type)
    {
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL: {
        string_append_formated(string, "BINARY_OP_COMPARISON_EQUAL_BOOL");
        break;
    }
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL: {
        string_append_formated(string, "BINARY_OP_COMPARISON_NOT_EQUAL_BOOL");
        break;
    }
    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND: {
        string_append_formated(string, "BINARY_OP_BOOLEAN_AND");
        break;
    }
    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR: {
        string_append_formated(string, "BINARY_OP_BOOLEAN_OR");
        break;
    }
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_POINTER: {
        string_append_formated(string, "BINARY_OP_COMPARISON_EQUAL_POINTER");
        break;
    }
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_POINTER: {
        string_append_formated(string, "BINARY_OP_COMPARISON_NOT_EQUAL_POINTER");
        break;
    }
    }
    return;
}

void intermediate_instruction_append_to_string(String* string, Intermediate_Instruction* instruction, int function_index, Intermediate_Generator* generator)
{
    Intermediate_Function* function = &generator->functions[function_index];
    bool append_source_destination = false;
    bool append_binary = false;
    bool append_destination = false;
    bool append_src_1 = false;

    if (intermediate_instruction_type_is_binary_operation(instruction->type)) {
        intermediate_instruction_binop_append_to_string(string, instruction->type);
        string_append_formated(string, " ");
        append_binary = true;
    }
    else if (intermediate_instruction_type_is_unary_operation(instruction->type)) {
        intermediate_instruction_unary_operation_append_to_string(string, instruction->type);
        string_append_formated(string, " ");
        append_source_destination = true;
    }
    else
    {
        switch (instruction->type)
        {
        case Intermediate_Instruction_Type::ADDRESS_OF:
            string_append_formated(string, "ADDRESS_OF");
            append_source_destination = true;
            break;
        case Intermediate_Instruction_Type::IF_BLOCK:
            string_append_formated(string, "IF_BLOCK, \n\t\tcond_start: %d, cond_end: %d\n\t\ttrue_start: %d, true_end: %d\n\t\t, false_start: %d, false_end: %d",
                instruction->condition_calculation_instruction_start, instruction->condition_calculation_instruction_end_exclusive,
                instruction->true_branch_instruction_start, instruction->true_branch_instruction_end_exclusive,
                instruction->false_branch_instruction_start, instruction->false_branch_instruction_end_exclusive
            );
            string_append_formated(string, "\n\t\tcondition: ");
            data_access_append_to_string(string, instruction->source1, function_index, generator);
            break;
        case Intermediate_Instruction_Type::WHILE_BLOCK:
            string_append_formated(string, "WHILE_BLOCK, \n\t\tcond_start: %d, cond_end: %d\n\t\ttrue_start: %d, true_end: %d",
                instruction->condition_calculation_instruction_start, instruction->condition_calculation_instruction_end_exclusive,
                instruction->true_branch_instruction_start, instruction->true_branch_instruction_end_exclusive
            );
            string_append_formated(string, "\n\t\tcondition: ");
            data_access_append_to_string(string, instruction->source1, function_index, generator);
            break;
        case Intermediate_Instruction_Type::CAST_U64_TO_POINTER:
            string_append_formated(string, "CAST_U64_TO_POINTER, ");
            type_signature_append_to_string(string, instruction->cast_to);
            string_append_formated(string, " <-- ");
            type_signature_append_to_string(string, instruction->cast_from);
            append_source_destination = true;
            break;
        case Intermediate_Instruction_Type::CAST_POINTER_TO_U64:
            string_append_formated(string, "CAST_POINTER_TO_U64, ");
            type_signature_append_to_string(string, instruction->cast_to);
            string_append_formated(string, " <-- ");
            type_signature_append_to_string(string, instruction->cast_from);
            append_source_destination = true;
            break;
        case Intermediate_Instruction_Type::CAST_PRIMITIVE_TYPES:
            string_append_formated(string, "CAST_PRIMITIVE_TYPES, ");
            type_signature_append_to_string(string, instruction->cast_to);
            string_append_formated(string, " <-- ");
            type_signature_append_to_string(string, instruction->cast_from);
            append_source_destination = true;
            break;
        case Intermediate_Instruction_Type::CAST_POINTERS:
            string_append_formated(string, "CAST_POINTERS, ");
            type_signature_append_to_string(string, instruction->cast_to);
            string_append_formated(string, " <-- ");
            type_signature_append_to_string(string, instruction->cast_from);
            append_source_destination = true;
            break;
        case Intermediate_Instruction_Type::CALL_FUNCTION:
            string_append_formated(string, "CALL_FUNCTION, function_index: %d, \n\t\treturn_data: ", instruction->intermediate_function_index);
            if (generator->functions[instruction->intermediate_function_index].function_type->return_type != generator->analyser->type_system.void_type) {
                data_access_append_to_string(string, instruction->destination, function_index, generator);
            }
            else {
                string_append_formated(string, "void");
            }
            for (int i = 0; i < instruction->arguments.size; i++) {
                string_append_formated(string, "\n\t\t#%d: ", i);
                data_access_append_to_string(string, instruction->arguments[i], function_index, generator);
            }
            break;
        case Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION:
            string_append_formated(string, "CALL_HARDCODED_FUNCTION, function_id: %d, \n\t\treturn_data: ", (i32)instruction->hardcoded_function_type);
            if (generator->analyser->hardcoded_functions[(int)instruction->hardcoded_function_type].function_type->return_type
                != generator->analyser->type_system.void_type) {
                data_access_append_to_string(string, instruction->destination, function_index, generator);
            }
            else {
                string_append_formated(string, "void");
            }
            for (int i = 0; i < instruction->arguments.size; i++) {
                string_append_formated(string, "\n\t\t#%d: ", i);
                data_access_append_to_string(string, instruction->arguments[i], function_index, generator);
            }
            break;
        case Intermediate_Instruction_Type::RETURN:
            string_append_formated(string, "RETURN, return_data: ");
            if (instruction->return_has_value)
                append_src_1 = true;
            else {
                string_append_formated(string, "void");
            }
            break;
        case Intermediate_Instruction_Type::EXIT:
            string_append_formated(string, "EXIT ");
            if (instruction->exit_code == Exit_Code::SUCCESS && instruction->return_has_value)
                append_src_1 = true;
            exit_code_append_to_string(string, instruction->exit_code);
            break;
        case Intermediate_Instruction_Type::BREAK:
            string_append_formated(string, "BREAK");
            break;
        case Intermediate_Instruction_Type::CONTINUE:
            string_append_formated(string, "CONTINUE");
            break;
        case Intermediate_Instruction_Type::CALCULATE_ARRAY_ACCESS_POINTER:
            string_append_formated(string, "CALCULATE_ARRAY_ACCESS_POINTER, type_size: %d,  ", instruction->constant_i32_value);
            append_binary = true;
            break;
        case Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER:
            string_append_formated(string, "CALCULATE_MEMBER_ACCESS_POINTER, offset: %d ", instruction->constant_i32_value);
            append_source_destination = true;
            break;
        case Intermediate_Instruction_Type::MOVE_DATA:
            string_append_formated(string, "MOVE_DATA");
            append_source_destination = true;
            break;
        case Intermediate_Instruction_Type::LOAD_CONSTANT_F32:
            string_append_formated(string, "LOAD_CONSTANT_F32, value: %3.2f ", instruction->constant_f32_value);
            append_destination = true;
            break;
        case Intermediate_Instruction_Type::LOAD_CONSTANT_I32:
            string_append_formated(string, "LOAD_CONSTANT_I32, value: %d ", instruction->constant_i32_value);
            append_destination = true;
            break;
        case Intermediate_Instruction_Type::LOAD_CONSTANT_BOOL:
            string_append_formated(string, "LOAD_CONSTANT_BOOL, value: %s ", instruction->constant_bool_value ? "TRUE" : "FALSE");
            append_destination = true;
            break;
        case Intermediate_Instruction_Type::LOAD_NULLPTR:
            string_append_formated(string, "LOAD_NULLPTR ");
            append_destination = true;
            break;
        default:
            logg("Should not fucking happen!");
            break;
        }
    }

    if (append_binary) {
        string_append_formated(string, "\n\t\tleft = ");
        data_access_append_to_string(string, instruction->source1, function_index, generator);
        string_append_formated(string, "\n\t\tright = ");
        data_access_append_to_string(string, instruction->source2, function_index, generator);
        string_append_formated(string, "\n\t\tdest = ");
        data_access_append_to_string(string, instruction->destination, function_index, generator);
    }
    if (append_source_destination) {
        string_append_formated(string, "\n\t\tsrc = ");
        data_access_append_to_string(string, instruction->source1, function_index, generator);
        string_append_formated(string, "\n\t\tdest = ");
        data_access_append_to_string(string, instruction->destination, function_index, generator);
    }
    if (append_destination) {
        string_append_formated(string, "\n\t\tdest = ");
        data_access_append_to_string(string, instruction->destination, function_index, generator);
    }
    if (append_src_1) {
        string_append_formated(string, "\n\t\tsrc = ");
        data_access_append_to_string(string, instruction->source1, function_index, generator);
    }
}

void intermediate_function_append_to_string(String* string, Intermediate_Generator* generator, int index)
{
    Intermediate_Function* function = &generator->functions[index];
    string_append_formated(string, "Function #%d: %s\n", index,
        lexer_identifer_to_string(generator->analyser->parser->lexer,
            generator->analyser->parser->nodes[generator->function_to_ast_node_mapping[index]].name_id).characters);
    string_append_formated(string, "Instructions:\n");
    for (int i = 0; i < function->instructions.size; i++) {
        string_append_formated(string, "\t#%d: ", i);
        intermediate_instruction_append_to_string(string, &function->instructions[i], index, generator);
        string_append_formated(string, "\n");
    }
}

void intermediate_generator_append_to_string(String* string, Intermediate_Generator* generator)
{
    string_append_formated(string, "Function count: %d\n\n", generator->functions.size);
    for (int i = 0; i < generator->functions.size; i++) {
        intermediate_function_append_to_string(string, generator, i);
    }
}