#include "bytecode_generator.hpp"

#include "compiler.hpp"

int align_offset_next_multiple(int offset, int alignment) 
{
    int dist = offset % alignment;
    if (dist == 0) return offset;
    if (dist < 0) {
        return offset - dist;
    }
    return offset + (alignment - dist);
}

Bytecode_Generator bytecode_generator_create()
{
    Bytecode_Generator result;
    result.instructions = dynamic_array_create_empty<Bytecode_Instruction>(64);
    result.break_instructions_to_fill_out = dynamic_array_create_empty<int>(64);
    result.continue_instructions_to_fill_out = dynamic_array_create_empty<int>(64);
    result.function_locations = dynamic_array_create_empty<int>(64);
    result.function_calls = dynamic_array_create_empty<Function_Call_Location>(64);
    result.variable_stack_offsets = dynamic_array_create_empty<int>(256);
    result.parameter_stack_offsets = dynamic_array_create_empty<int>(256);
    result.global_offsets = dynamic_array_create_empty<int>(256);
    result.intermediate_stack_offsets = dynamic_array_create_empty<int>(256);
    result.constants_u64 = dynamic_array_create_empty<u64>(256);
    result.maximum_function_stack_depth = 0;
    return result;
}

void bytecode_generator_destroy(Bytecode_Generator* generator)
{
    dynamic_array_destroy(&generator->instructions);
    dynamic_array_destroy(&generator->break_instructions_to_fill_out);
    dynamic_array_destroy(&generator->continue_instructions_to_fill_out);
    dynamic_array_destroy(&generator->function_calls);
    dynamic_array_destroy(&generator->function_locations);
    dynamic_array_destroy(&generator->variable_stack_offsets);
    dynamic_array_destroy(&generator->global_offsets);
    dynamic_array_destroy(&generator->intermediate_stack_offsets);
    dynamic_array_destroy(&generator->parameter_stack_offsets);
    dynamic_array_destroy(&generator->constants_u64);
} 

Bytecode_Instruction instruction_make_0(Instruction_Type type) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    return instr;
}

Bytecode_Instruction instruction_make_1(Instruction_Type type, int src_1) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    return instr;
}

Bytecode_Instruction instruction_make_2(Instruction_Type type, int src_1, int src_2) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    instr.op2 = src_2;
    return instr;
}

Bytecode_Instruction instruction_make_3(Instruction_Type type, int src_1, int src_2, int src_3) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    instr.op2 = src_2;
    instr.op3 = src_3;
    return instr;
}

Bytecode_Instruction instruction_make_4(Instruction_Type type, int src_1, int src_2, int src_3, int src_4) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    instr.op2 = src_2;
    instr.op3 = src_3;
    instr.op4 = src_4;
    return instr;
}

int bytecode_generator_add_instruction(Bytecode_Generator* generator, Bytecode_Instruction instruction) {
    dynamic_array_push_back(&generator->instructions, instruction);
    return generator->instructions.size - 1;
}

int bytecode_generator_create_temporary_stack_offset(Bytecode_Generator* generator, Type_Signature* type) 
{
    generator->tmp_stack_offset = align_offset_next_multiple(generator->tmp_stack_offset, type->alignment_in_bytes);
    int result = generator->tmp_stack_offset;
    generator->tmp_stack_offset += type->size_in_bytes;
    return result;
}

int bytecode_generator_get_data_access_offset(Bytecode_Generator* generator, Data_Access access)
{
    switch (access.access_type)
    {
    case Data_Access_Type::GLOBAL_ACCESS:
        return generator->global_offsets[access.access_index];
    case Data_Access_Type::INTERMEDIATE_ACCESS:
        return generator->intermediate_stack_offsets[access.access_index];
    case Data_Access_Type::PARAMETER_ACCESS:
        return generator->parameter_stack_offsets[access.access_index];
    case Data_Access_Type::VARIABLE_ACCESS:
        return generator->variable_stack_offsets[access.access_index];
    }
    panic("Should not happen");
    return 0;
}

int bytecode_generator_data_access_to_stack_offset(Bytecode_Generator* generator, Data_Access access, int function_index)
{
    Type_Signature* access_type = intermediate_generator_get_access_signature(&generator->compiler->intermediate_generator, access, function_index);
    int result_access_offset;
    switch (access.access_type)
    {
    case Data_Access_Type::GLOBAL_ACCESS:
        result_access_offset = bytecode_generator_create_temporary_stack_offset(generator, access_type);
        bytecode_generator_add_instruction(generator,
            instruction_make_3(Instruction_Type::READ_GLOBAL, result_access_offset, generator->global_offsets[access.access_index], access_type->size_in_bytes)
        );
        break;
    case Data_Access_Type::INTERMEDIATE_ACCESS:
        result_access_offset = generator->intermediate_stack_offsets[access.access_index];
        break;
    case Data_Access_Type::PARAMETER_ACCESS:
        result_access_offset = generator->parameter_stack_offsets[access.access_index];
        break;
    case Data_Access_Type::VARIABLE_ACCESS:
        result_access_offset = generator->variable_stack_offsets[access.access_index];
        break;
    default: panic("What");
    }
    if (access.is_pointer_access) 
    {
        Type_Signature* type = access_type->child_type;
        int result_offset = bytecode_generator_create_temporary_stack_offset(generator, type);
        bytecode_generator_add_instruction(
            generator,
            instruction_make_3(
                Instruction_Type::READ_MEMORY,
                result_offset,
                result_access_offset,
                type->size_in_bytes
            )
        );
        return result_offset;
    }
    else {
        return result_access_offset;
    }
}

int bytecode_generator_add_instruction_with_destination_access(Bytecode_Generator* generator, 
    Data_Access destination, Bytecode_Instruction instr, int function_index
)
{
    if (destination.is_pointer_access)
    {
        Type_Signature* type = intermediate_generator_get_access_signature(&generator->compiler->intermediate_generator, destination, function_index)->child_type;
        int source_reg_offset = bytecode_generator_create_temporary_stack_offset(generator, type);
        instr.op1 = source_reg_offset;
        int instruction_index = bytecode_generator_add_instruction(generator, instr);

        destination.is_pointer_access = false;
        int pointer_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, destination, function_index);

        bytecode_generator_add_instruction(
            generator,
            instruction_make_3(
                Instruction_Type::WRITE_MEMORY,
                pointer_stack_offset,
                source_reg_offset,
                type->size_in_bytes
            )
        );
        return instruction_index;
    }
    else
    {
        if (destination.access_type == Data_Access_Type::GLOBAL_ACCESS)
        {
            Type_Signature* type = intermediate_generator_get_access_signature(&generator->compiler->intermediate_generator, destination, function_index);
            int source_reg_offset = bytecode_generator_create_temporary_stack_offset(generator, type);
            instr.op1 = source_reg_offset;
            int instruction_index = bytecode_generator_add_instruction(generator, instr);
            bytecode_generator_add_instruction(
                generator,
                instruction_make_3(
                    Instruction_Type::WRITE_GLOBAL,
                    generator->global_offsets[destination.access_index],
                    source_reg_offset,
                    type->size_in_bytes
                )
            );
            return instruction_index;
        }
        else {
            instr.op1 = bytecode_generator_data_access_to_stack_offset(generator, destination, function_index);
            return bytecode_generator_add_instruction(generator, instr);
        }
    }
}

void bytecode_generator_move_accesses(Bytecode_Generator* generator, Data_Access destination, Data_Access source, int function_index)
{
    Intermediate_Function* function = &generator->compiler->intermediate_generator.functions[function_index];
    int move_byte_size;
    if (destination.is_pointer_access) {
        move_byte_size = intermediate_generator_get_access_signature(&generator->compiler->intermediate_generator, destination, function_index)->child_type->size_in_bytes;
    }
    else {
        move_byte_size = intermediate_generator_get_access_signature(&generator->compiler->intermediate_generator, destination, function_index)->size_in_bytes;
    }

    int source_offset = bytecode_generator_data_access_to_stack_offset(generator, source, function_index);
    Bytecode_Instruction instr = instruction_make_3(Instruction_Type::MOVE_STACK_DATA, 0, source_offset, move_byte_size);
    bytecode_generator_add_instruction_with_destination_access(generator, destination, instr, function_index);
}

void bytecode_generator_generate_load_constant_instruction(Bytecode_Generator* generator, int function_index, int instruction_index)
{
    Intermediate_Function* function = &generator->compiler->intermediate_generator.functions[function_index];
    Intermediate_Instruction* instruction = &function->instructions[instruction_index];

    Instruction_Type result_type;
    int result_data;
    int result_size;
    if (instruction->type == Intermediate_Instruction_Type::LOAD_CONSTANT_F32) {
        result_type = Instruction_Type::LOAD_CONSTANT_F32;
        result_data = *((int*)&instruction->constant_f32_value);
        result_size = 4;
    }
    else if (instruction->type == Intermediate_Instruction_Type::LOAD_CONSTANT_I32) {
        result_type = Instruction_Type::LOAD_CONSTANT_I32;
        result_data = instruction->constant_i32_value;
        result_size = 4;
    }
    else if (instruction->type == Intermediate_Instruction_Type::LOAD_CONSTANT_BOOL) {
        result_type = Instruction_Type::LOAD_CONSTANT_BOOLEAN;
        result_data = instruction->constant_bool_value ? 1 : 0;
        result_size = 1;
    }
    else if (instruction->type == Intermediate_Instruction_Type::LOAD_NULLPTR) {
        result_type = Instruction_Type::LOAD_NULLPTR;
        result_data = 0;
        result_size = 8;
    }
    else if (instruction->type == Intermediate_Instruction_Type::LOAD_STRING_POINTER) {
        result_type = Instruction_Type::LOAD_CONSTANT_U64;
        u64 char_ptr = (u64) instruction->constant_string_value;
        dynamic_array_push_back(&generator->constants_u64, char_ptr);
        result_data = generator->constants_u64.size - 1;
        result_size = 8;
    }
    else panic("not implemented yet?!?");

    bytecode_generator_add_instruction_with_destination_access(generator, instruction->destination, instruction_make_2(result_type, 0, result_data), function_index);
}

void bytecode_generator_generate_function_instruction_slice(
    Bytecode_Generator* generator, int function_index, int instruction_start_index, int instruction_end_index_exclusive
)
{
    Intermediate_Function* function = &generator->compiler->intermediate_generator.functions[function_index];
    for (int instruction_index = instruction_start_index;
        instruction_index < function->instructions.size && instruction_index < instruction_end_index_exclusive;
        instruction_index++)
    {
        Intermediate_Instruction* instr = &function->instructions[instruction_index];

        // Binary operations
        if (intermediate_instruction_type_is_binary_operation(instr->type))
        {
            Instruction_Type result_instr_type;
            if (instr->operand_types->type == Signature_Type::POINTER)
            {
                if (instr->type == Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL) {
                    result_instr_type = Instruction_Type::BINARY_OP_COMPARISON_EQUAL_POINTER;
                }
                else if (instr->type == Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL) {
                    result_instr_type = Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_POINTER;
                }
                else panic("Should not happen");
            }
            else
            {
                assert(instr->operand_types->type == Signature_Type::PRIMITIVE, "Should not happen");

                Instruction_Type instr_block_start;
                switch (instr->operand_types->primitive_type)
                {
                case Primitive_Type::UNSIGNED_INT_8: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8; break;
                case Primitive_Type::UNSIGNED_INT_16: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U16; break;
                case Primitive_Type::UNSIGNED_INT_32: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U32; break;
                case Primitive_Type::UNSIGNED_INT_64: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U64; break;
                case Primitive_Type::SIGNED_INT_8: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I8; break;
                case Primitive_Type::SIGNED_INT_16: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I16; break;
                case Primitive_Type::SIGNED_INT_32: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32; break;
                case Primitive_Type::SIGNED_INT_64: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I64; break;
                case Primitive_Type::FLOAT_32: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32; break;
                case Primitive_Type::FLOAT_64: instr_block_start = Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F64; break;
                case Primitive_Type::BOOLEAN: instr_block_start = Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL; break;
                }

                if (instr->operand_types->primitive_type != Primitive_Type::BOOLEAN)
                {
                    switch (instr->type)
                    {
                    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 0);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 1);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 2);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 3);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 4);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 5);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 6);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 7);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 8);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 9);
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO:
                        result_instr_type = (Instruction_Type)((int)instr_block_start + 10);
                        break;
                    default: panic("Should not happen!");
                    }
                }
                else {
                    switch (instr->type)
                    {
                    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND:
                        result_instr_type = Instruction_Type::BINARY_OP_BOOLEAN_AND;
                        break;
                    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR:
                        result_instr_type = Instruction_Type::BINARY_OP_BOOLEAN_OR;
                        break;
                    default: panic("Should not happen");
                    }
                }
            }

            int operand_1_reg_offset = bytecode_generator_data_access_to_stack_offset(generator, instr->source1, function_index);
            int operand_2_reg_offset = bytecode_generator_data_access_to_stack_offset(generator, instr->source2, function_index);
            Bytecode_Instruction result_instr = instruction_make_3(result_instr_type, 0, operand_1_reg_offset, operand_2_reg_offset);
            bytecode_generator_add_instruction_with_destination_access(generator, instr->destination, result_instr, function_index);
            continue;
        }

        // Unary operations
        if (intermediate_instruction_type_is_unary_operation(instr->type))
        {
            Instruction_Type result_instr_type;
            if (instr->operand_types->primitive_type == Primitive_Type::BOOLEAN) {
                result_instr_type = Instruction_Type::UNARY_OP_BOOLEAN_NOT;
            }
            else {
                switch (instr->operand_types->primitive_type)
                {
                case Primitive_Type::SIGNED_INT_8:
                    result_instr_type = Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I8;
                    break;
                case Primitive_Type::SIGNED_INT_16:
                    result_instr_type = Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I16;
                    break;
                case Primitive_Type::SIGNED_INT_32:
                    result_instr_type = Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32;
                    break;
                case Primitive_Type::SIGNED_INT_64:
                    result_instr_type = Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I64;
                    break;
                case Primitive_Type::FLOAT_32:
                    result_instr_type = Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32;
                    break;
                case Primitive_Type::FLOAT_64:
                    result_instr_type = Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F64;
                    break;
                default: panic("Should not happen");
                }
            }
            int operand_1_reg_offset = bytecode_generator_data_access_to_stack_offset(generator, instr->source1, function_index);
            Bytecode_Instruction result_instr = instruction_make_2(result_instr_type, 0, operand_1_reg_offset);
            bytecode_generator_add_instruction_with_destination_access(generator, instr->destination, result_instr, function_index);
            continue;
        }

        switch (instr->type)
        {
        case Intermediate_Instruction_Type::MOVE_DATA:
        {
            bytecode_generator_move_accesses(generator, instr->destination, instr->source1, function_index);
            break;
        }
        case Intermediate_Instruction_Type::LOAD_CONSTANT_F32:
        case Intermediate_Instruction_Type::LOAD_CONSTANT_I32:
        case Intermediate_Instruction_Type::LOAD_NULLPTR:
        case Intermediate_Instruction_Type::LOAD_STRING_POINTER:
        case Intermediate_Instruction_Type::LOAD_CONSTANT_BOOL: {
            bytecode_generator_generate_load_constant_instruction(generator, function_index, instruction_index);
            break;
        }
        case Intermediate_Instruction_Type::LOAD_FUNCTION_POINTER: {
            bytecode_generator_add_instruction_with_destination_access(generator,
                instr->destination, instruction_make_2(Instruction_Type::LOAD_FUNCTION_LOCATION, 0, instr->intermediate_function_index),
                function_index
            );
            break;
        }
        case Intermediate_Instruction_Type::IF_BLOCK:
        {
            bytecode_generator_generate_function_instruction_slice(
                generator, function_index, instr->condition_calculation_instruction_start, instr->condition_calculation_instruction_end_exclusive
            );
            int condition_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, instr->source1, function_index);
            int jmp_instruction_index = bytecode_generator_add_instruction(
                generator,
                instruction_make_2(Instruction_Type::JUMP_ON_FALSE, 0, condition_stack_offset)
            );
            bytecode_generator_generate_function_instruction_slice(
                generator, function_index, instr->true_branch_instruction_start, instr->true_branch_instruction_end_exclusive
            );
            instruction_index = instr->true_branch_instruction_end_exclusive - 1;
            if (instr->false_branch_instruction_end_exclusive != instr->false_branch_instruction_start)
            {
                instruction_index = instr->false_branch_instruction_end_exclusive - 1;
                int jmp_over_else_instruction_index = bytecode_generator_add_instruction(
                    generator,
                    instruction_make_1(Instruction_Type::JUMP, 0)
                );
                generator->instructions[jmp_instruction_index].op1 = generator->instructions.size;
                bytecode_generator_generate_function_instruction_slice(
                    generator, function_index, instr->false_branch_instruction_start, instr->false_branch_instruction_end_exclusive
                );
                generator->instructions[jmp_over_else_instruction_index].op1 = generator->instructions.size;
            }
            else {
                generator->instructions[jmp_instruction_index].op1 = generator->instructions.size;
            }

            break;
        }
        case Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION:
        case Intermediate_Instruction_Type::CALL_FUNCTION:
        case Intermediate_Instruction_Type::CALL_FUNCTION_POINTER:
        {
            // Move registers to the right place, then generate call instruction
            int pointer_offset = bytecode_generator_create_temporary_stack_offset(generator, generator->compiler->type_system.void_ptr_type);
            int argument_stack_offset = align_offset_next_multiple(generator->tmp_stack_offset, 16); // I think 16 is the hightest i have

            Type_Signature* function_sig = 0;
            if (instr->type == Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION) {
                function_sig = generator->compiler->analyser.hardcoded_functions[(int)instr->hardcoded_function_type].function_type;
            }
            else if (instr->type == Intermediate_Instruction_Type::CALL_FUNCTION) {
                function_sig = generator->compiler->intermediate_generator.functions[instr->intermediate_function_index].function_type;
            }
            else if (instr->type == Intermediate_Instruction_Type::CALL_FUNCTION_POINTER) {
                function_sig = intermediate_generator_get_access_signature(&generator->compiler->intermediate_generator, instr->source1, function_index)->child_type;
            }
            else panic("cannot happen");

            // Put arguments into the correct place on the stack
            for (int i = 0; i < function_sig->parameter_types.size; i++)
            {
                Type_Signature* parameter_sig = function_sig->parameter_types[i];
                argument_stack_offset = align_offset_next_multiple(argument_stack_offset, parameter_sig->alignment_in_bytes);
                Data_Access* arg = &instr->arguments[i];

                if (arg->access_type != Data_Access_Type::GLOBAL_ACCESS)
                {
                    Instruction_Type instr_type;
                    if (arg->is_pointer_access) {
                        instr_type = Instruction_Type::READ_MEMORY;
                    }
                    else {
                        instr_type = Instruction_Type::MOVE_STACK_DATA;
                    }
                    bytecode_generator_add_instruction(
                        generator,
                        instruction_make_3(
                            instr_type,
                            argument_stack_offset,
                            bytecode_generator_get_data_access_offset(generator, *arg),
                            parameter_sig->size_in_bytes
                        )
                    );
                }
                else
                {
                    if (arg->is_pointer_access)
                    {
                        bytecode_generator_add_instruction(generator,
                            instruction_make_3(
                                Instruction_Type::READ_GLOBAL,
                                pointer_offset,
                                generator->global_offsets[arg->access_index],
                                8
                            )
                        );
                        bytecode_generator_add_instruction(
                            generator,
                            instruction_make_3(
                                Instruction_Type::READ_MEMORY,
                                argument_stack_offset,
                                pointer_offset,
                                parameter_sig->size_in_bytes
                            )
                        );
                    }
                    else {
                        bytecode_generator_add_instruction(generator,
                            instruction_make_3(
                                Instruction_Type::READ_GLOBAL,
                                argument_stack_offset,
                                generator->global_offsets[arg->access_index],
                                parameter_sig->size_in_bytes
                            )
                        );
                    }
                }

                argument_stack_offset += parameter_sig->size_in_bytes;
            }

            // Align argument_stack_offset for return pointer
            argument_stack_offset = align_offset_next_multiple(argument_stack_offset, 8);
            if (instr->type == Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION) {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::CALL_HARDCODED_FUNCTION, (i32)instr->hardcoded_function_type, argument_stack_offset)
                );
            }
            else if (instr->type == Intermediate_Instruction_Type::CALL_FUNCTION) {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::CALL, 0, argument_stack_offset)
                );
                Function_Call_Location call_loc;
                call_loc.call_instruction_location = generator->instructions.size - 1;
                call_loc.function_index = instr->intermediate_function_index;
                dynamic_array_push_back(&generator->function_calls, call_loc);
            }
            else if (instr->type == Intermediate_Instruction_Type::CALL_FUNCTION_POINTER) {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(
                        Instruction_Type::CALL_FUNCTION_POINTER,
                        bytecode_generator_data_access_to_stack_offset(generator, instr->source1, function_index),
                        argument_stack_offset
                    )
                );
            }
            else panic("Cannot happen");

            // Load return value to destination
            if (function_sig->return_type != generator->compiler->type_system.void_type)
            {
                Bytecode_Instruction ret_val_instr = instruction_make_2(Instruction_Type::LOAD_RETURN_VALUE, 0, function_sig->return_type->size_in_bytes);
                bytecode_generator_add_instruction_with_destination_access(generator, instr->destination, ret_val_instr, function_index);
            }
            break;
        }
        case Intermediate_Instruction_Type::RETURN:
        case Intermediate_Instruction_Type::EXIT:
        {
            Type_Signature* return_sig = generator->compiler->intermediate_generator.functions[function_index].function_type->return_type;
            int return_data_stack_offset = 0;
            if (instr->return_has_value) {
                return_data_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, instr->source1, function_index);
            }

            if (instr->type == Intermediate_Instruction_Type::EXIT) {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_3(Instruction_Type::EXIT, return_data_stack_offset, return_sig->size_in_bytes, (i32)instr->exit_code)
                );
            }
            else {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::RETURN, return_data_stack_offset, return_sig->size_in_bytes)
                );
            }
            break;
        }
        case Intermediate_Instruction_Type::WHILE_BLOCK:
        {
            instruction_index = instr->true_branch_instruction_end_exclusive - 1;
            int check_condition_instruction_index = generator->instructions.size;
            bytecode_generator_generate_function_instruction_slice(
                generator, function_index, instr->condition_calculation_instruction_start, instr->condition_calculation_instruction_end_exclusive
            );
            int condition_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, instr->source1, function_index);
            int jmp_instruction_index = bytecode_generator_add_instruction(
                generator,
                instruction_make_2(Instruction_Type::JUMP_ON_FALSE, 0, condition_stack_offset)
            );
            bytecode_generator_generate_function_instruction_slice(
                generator, function_index, instr->true_branch_instruction_start, instr->true_branch_instruction_end_exclusive
            );
            bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, check_condition_instruction_index)
            );
            generator->instructions[jmp_instruction_index].op1 = generator->instructions.size;

            for (int i = 0; i < generator->break_instructions_to_fill_out.size; i++) {
                generator->instructions[generator->break_instructions_to_fill_out[i]].op1 = generator->instructions.size;
            }
            for (int i = 0; i < generator->continue_instructions_to_fill_out.size; i++) {
                generator->instructions[generator->break_instructions_to_fill_out[i]].op1 = check_condition_instruction_index;
            }
            dynamic_array_reset(&generator->break_instructions_to_fill_out);
            dynamic_array_reset(&generator->continue_instructions_to_fill_out);

            break;
        }
        case Intermediate_Instruction_Type::BREAK: {
            int break_jump = bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, 0)
            );
            dynamic_array_push_back(&generator->break_instructions_to_fill_out, break_jump);
            break;
        }
        case Intermediate_Instruction_Type::CONTINUE: {
            int continue_jump = bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, 0)
            );
            dynamic_array_push_back(&generator->continue_instructions_to_fill_out, continue_jump);
            break;
        }
        case Intermediate_Instruction_Type::CAST_POINTERS:
        case Intermediate_Instruction_Type::CAST_U64_TO_POINTER:
        case Intermediate_Instruction_Type::CAST_POINTER_TO_U64:
        {
            bytecode_generator_move_accesses(generator, instr->destination, instr->source1, function_index);
            break;
        }
        case Intermediate_Instruction_Type::CAST_PRIMITIVE_TYPES:
        {
            Instruction_Type cast_type;
            if (primitive_type_is_integer(instr->cast_from->primitive_type) && primitive_type_is_integer(instr->cast_to->primitive_type)) {
                cast_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE;
            }
            else if (primitive_type_is_float(instr->cast_from->primitive_type) && primitive_type_is_float(instr->cast_to->primitive_type)) {
                cast_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE;
            }
            else if (primitive_type_is_float(instr->cast_from->primitive_type) && primitive_type_is_integer(instr->cast_to->primitive_type)) {
                cast_type = Instruction_Type::CAST_FLOAT_INTEGER;
            }
            else if (primitive_type_is_integer(instr->cast_from->primitive_type) && primitive_type_is_float(instr->cast_to->primitive_type)) {
                cast_type = Instruction_Type::CAST_INTEGER_FLOAT;
            }
            else panic("Should not happen!");

            bytecode_generator_add_instruction_with_destination_access(generator, instr->destination,
                instruction_make_4(
                    cast_type, 0,
                    bytecode_generator_data_access_to_stack_offset(generator, instr->source1, function_index),
                    (int)instr->cast_to->primitive_type, (int)instr->cast_from->primitive_type
                ),
                function_index
            );
            break;
        }
        case Intermediate_Instruction_Type::ADDRESS_OF:
        {
            Bytecode_Instruction load_instr;
            if (instr->source1.access_type == Data_Access_Type::GLOBAL_ACCESS) {
                load_instr.instruction_type = Instruction_Type::LOAD_GLOBAL_ADDRESS;
            }
            else {
                load_instr.instruction_type = Instruction_Type::LOAD_REGISTER_ADDRESS;
            }
            load_instr.op2 = bytecode_generator_get_data_access_offset(generator, instr->source1);
            bytecode_generator_add_instruction_with_destination_access(generator, instr->destination, load_instr, function_index);
            break;
        }
        case Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER:
        {
            int register_address_reg;
            if (instr->source1.is_pointer_access) {
                Data_Access copy = instr->source1;
                copy.is_pointer_access = false;
                register_address_reg = bytecode_generator_data_access_to_stack_offset(generator, copy, function_index);
            }
            else
            {
                Instruction_Type instr_type;
                if (instr->source1.access_type == Data_Access_Type::GLOBAL_ACCESS) {
                    instr_type = Instruction_Type::LOAD_GLOBAL_ADDRESS;
                }
                else {
                    instr_type = Instruction_Type::LOAD_REGISTER_ADDRESS;
                }
                register_address_reg = bytecode_generator_create_temporary_stack_offset(generator, generator->compiler->type_system.void_ptr_type);
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(
                        instr_type,
                        register_address_reg,
                        bytecode_generator_get_data_access_offset(generator, instr->source1)
                    )
                );
            }

            bytecode_generator_add_instruction_with_destination_access(generator, instr->destination,
                instruction_make_3(
                    Instruction_Type::U64_ADD_CONSTANT_I32,
                    0,
                    register_address_reg,
                    instr->constant_i32_value
                ),
                function_index
            );
            break;
        }
        case Intermediate_Instruction_Type::CALCULATE_ARRAY_ACCESS_POINTER:
        {
            int base_pointer_offset = bytecode_generator_data_access_to_stack_offset(generator, instr->source1, function_index);
            int index_offset = bytecode_generator_data_access_to_stack_offset(generator, instr->source2, function_index);
            bytecode_generator_add_instruction_with_destination_access(generator, instr->destination,
                instruction_make_4(
                    Instruction_Type::U64_MULTIPLY_ADD_I32,
                    0,
                    base_pointer_offset,
                    index_offset,
                    instr->constant_i32_value
                ), function_index
            );
            break;
        }
        }
    }
}

void bytecode_generator_calculate_function_variable_and_parameter_offsets(Bytecode_Generator* generator, int function_index)
{
    Intermediate_Function* function = &generator->compiler->intermediate_generator.functions[function_index];
    // Set parameter stack locations
    {
        dynamic_array_reset(&generator->parameter_stack_offsets);
        int stack_size_of_parameters = 0;
        Type_Signature* function_signature = generator->compiler->intermediate_generator.functions[function_index].function_type;
        dynamic_array_reserve(&generator->parameter_stack_offsets, function_signature->parameter_types.size);

        for (int i = 0; i < function_signature->parameter_types.size; i++)
        {
            Type_Signature* param_sig = function_signature->parameter_types[i];
            stack_size_of_parameters = align_offset_next_multiple(stack_size_of_parameters, param_sig->alignment_in_bytes);
            dynamic_array_push_back(&generator->parameter_stack_offsets, stack_size_of_parameters);
            stack_size_of_parameters += param_sig->size_in_bytes;
        }
        // Adjust for pointer alignment of return address
        stack_size_of_parameters = align_offset_next_multiple(stack_size_of_parameters, 8);
        for (int i = 0; i < generator->parameter_stack_offsets.size; i++) {
            generator->parameter_stack_offsets[i] = generator->parameter_stack_offsets[i] - stack_size_of_parameters;
        }
    }

    // Set variable stack locations
    int stack_offset = 16;
    {
        dynamic_array_reset(&generator->variable_stack_offsets);
        dynamic_array_reserve(&generator->variable_stack_offsets, function->local_variables.size);
        for (int i = 0; i < function->local_variables.size; i++)
        {
            Type_Signature* type_sig = function->local_variables[i].type;
            stack_offset = align_offset_next_multiple(stack_offset, type_sig->alignment_in_bytes);
            dynamic_array_push_back(&generator->variable_stack_offsets, stack_offset);
            stack_offset += type_sig->size_in_bytes;
        }
    }

    // Set intermediate results stack offsets
    {
        dynamic_array_reset(&generator->intermediate_stack_offsets);
        dynamic_array_reserve(&generator->intermediate_stack_offsets, function->intermediate_results.size);
        for (int i = 0; i < function->intermediate_results.size; i++)
        {
            Type_Signature* type_sig = function->intermediate_results[i];
            stack_offset = align_offset_next_multiple(stack_offset, type_sig->alignment_in_bytes);
            dynamic_array_push_back(&generator->intermediate_stack_offsets, stack_offset);
            stack_offset += type_sig->size_in_bytes;
        }
    }
    generator->tmp_stack_offset = stack_offset;
}

void bytecode_generator_generate_function_code(Bytecode_Generator* generator, int function_index)
{
    Intermediate_Function* function = &generator->compiler->intermediate_generator.functions[function_index];
    generator->function_locations[function_index] = generator->instructions.size;

    bytecode_generator_calculate_function_variable_and_parameter_offsets(generator, function_index);
    bytecode_generator_generate_function_instruction_slice(generator, function_index, 0, function->instructions.size);
    if (generator->tmp_stack_offset > generator->maximum_function_stack_depth) {
        generator->maximum_function_stack_depth = generator->tmp_stack_offset;
    }
}

void bytecode_generator_generate(Bytecode_Generator* generator, Compiler* compiler)
{
    generator->compiler = compiler;
    dynamic_array_reset(&generator->instructions);
    dynamic_array_reset(&generator->break_instructions_to_fill_out);
    dynamic_array_reset(&generator->continue_instructions_to_fill_out);
    dynamic_array_reset(&generator->function_calls);
    dynamic_array_reset(&generator->function_locations);
    dynamic_array_reset(&generator->variable_stack_offsets);
    dynamic_array_reset(&generator->global_offsets);
    dynamic_array_reset(&generator->parameter_stack_offsets);
    dynamic_array_reset(&generator->intermediate_stack_offsets);
    dynamic_array_reset(&generator->constants_u64);

    dynamic_array_reserve(&generator->function_locations, generator->compiler->intermediate_generator.functions.size);
    while (generator->function_locations.size < generator->compiler->intermediate_generator.functions.size) {
        dynamic_array_push_back(&generator->function_locations, 0);
    }

    generator->global_data_size = 0;
    dynamic_array_reserve(&generator->global_offsets, generator->compiler->intermediate_generator.global_variables.size);
    for (int i = 0; i < compiler->intermediate_generator.global_variables.size; i++) {
        Type_Signature* signature = compiler->intermediate_generator.global_variables[i].type;
        generator->global_data_size = align_offset_next_multiple(generator->global_data_size, signature->alignment_in_bytes);
        dynamic_array_push_back(&generator->global_offsets, generator->global_data_size);
        generator->global_data_size += signature->size_in_bytes;
    }

    // Generate code for all functions
    for (int i = 0; i < compiler->intermediate_generator.functions.size; i++) {
        bytecode_generator_generate_function_code(generator, i);
    }

    // Fill out all function calls
    for (int i = 0; i < generator->function_calls.size; i++) {
        Function_Call_Location& call_loc = generator->function_calls[i];
        generator->instructions[call_loc.call_instruction_location].op1 = generator->function_locations[call_loc.function_index];
    }

    generator->entry_point_index = generator->function_locations[generator->compiler->intermediate_generator.main_function_index];
}

bool instruction_type_is_binary_op(Instruction_Type type) {
    return type >= Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8 && type <= Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_POINTER;
}

bool instruction_type_is_unary_op(Instruction_Type type) {
    return type >= Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I8 && type <= Instruction_Type::UNARY_OP_BOOLEAN_NOT;
}

void instruction_type_unary_op_append_to_string(String* string, Instruction_Type type)
{
    switch (type)
    {
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I8:
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I8");
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I16:
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I16");
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I32");
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I64:
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I64");
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_F32");
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F64:
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_F64");
        break;
    case Instruction_Type::UNARY_OP_BOOLEAN_NOT:
        string_append_formated(string, "UNARY_OP_BOOLEAN_NOT");
        break;
    default: panic("Shit");
    }
}

void instruction_type_binary_op_append_to_string(String* string, Instruction_Type type)
{
    const char* operation_types[] = {
        "BINARY_OP_ARITHMETIC_ADDITION",
        "BINARY_OP_ARITHMETIC_SUBTRACTION",
        "BINARY_OP_ARITHMETIC_MULTIPLICATION",
        "BINARY_OP_ARITHMETIC_DIVISION",
        "BINARY_OP_COMPARISON_EQUAL",
        "BINARY_OP_COMPARISON_NOT_EQUAL",
        "BINARY_OP_COMPARISON_GREATER_THAN",
        "BINARY_OP_COMPARISON_GREATER_EQUAL",
        "BINARY_OP_COMPARISON_LESS_THAN",
        "BINARY_OP_COMPARISON_LESS_EQUAL",
        "BINARY_OP_ARITHMETIC_MODULO"
    };

    if (type >= Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8 && type <= Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I64)
    {
        int type_index =  (int) type - (int) Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8;
        int data_type_index = type_index / 11;
        int operation_type_index = type_index % 11;

        const char* data_types[] = {
            "U8",
            "U16",
            "U32",
            "U64",
            "I8",
            "I16",
            "I32",
            "I64",
        };

        string_append_formated(string, "%s_%s", operation_types[operation_type_index], data_types[data_type_index]);
    }
    else if (type >= Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32 && type <= Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F64)
    {
        int type_index =  (int)type - (int)Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32;
        int data_type_index = type_index / 10;
        int operation_type_index = type_index % 10;
        const char* data_types[] = {
            "F32",
            "F64",
        };
    }
    else 
    {
        switch (type)
        {
        case Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL:
            string_append_formated(string, "BINARY_OP_COMPARISON_EQUAL_BOOL");
            break;
        case Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL:
            string_append_formated(string, "BINARY_OP_COMPARISON_NOT_EQUAL_BOOL");
            break;
        case Instruction_Type::BINARY_OP_BOOLEAN_AND:
            string_append_formated(string, "BINARY_OP_BOOLEAN_AND");
            break;
        case Instruction_Type::BINARY_OP_BOOLEAN_OR:
            string_append_formated(string, "BINARY_OP_BOOLEAN_OR");
            break;
        case Instruction_Type::BINARY_OP_COMPARISON_EQUAL_POINTER:
            string_append_formated(string, "BINARY_OP_COMPARISON_EQUAL_POINTER");
            break;
        case Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_POINTER:
            string_append_formated(string, "BINARY_OP_COMPARISON_NOT_EQUAL_POINTER");
            break;
        case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I8:
            string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I8");
            break;
        case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I16:
            string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I16");
            break;
        case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:
            string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I32");
            break;
        case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I64:
            string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I64");
            break;
        case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
            string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_F32");
            break;
        case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F64:
            string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_F64");
            break;
        case Instruction_Type::UNARY_OP_BOOLEAN_NOT:
            string_append_formated(string, "UNARY_OP_BOOLEAN_NOT");
            break;
        default:
            panic("Shit");
        }
    }
}

void bytecode_instruction_append_to_string(String* string, Bytecode_Instruction instruction)
{
    if (instruction_type_is_binary_op(instruction.instruction_type)) {
        instruction_type_binary_op_append_to_string(string, instruction.instruction_type);
        string_append_formated(string, "\t dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
    }
    else if (instruction_type_is_unary_op(instruction.instruction_type)) {
        instruction_type_unary_op_append_to_string(string, instruction.instruction_type);
        string_append_formated(string, "\t dst=%d, src1=%d\n", instruction.op1, instruction.op2);
    }
    else
    {
        switch (instruction.instruction_type)
        {
        case Instruction_Type::LOAD_NULLPTR:
            string_append_formated(string, "LOAD_NULLPTR                      dest=%d\n", instruction.op1);
            break;
        case Instruction_Type::LOAD_CONSTANT_BOOLEAN:
            string_append_formated(string, "LOAD_CONSTANT_BOOLEAN             dest=%d, val=%s\n", instruction.op1, instruction.op2 ? "TRUE" : "FALSE");
            break;
        case Instruction_Type::LOAD_CONSTANT_F32:
            string_append_formated(string, "LOAD_CONSTANT_F32                 dest=%d, val=%3.2f\n", instruction.op1, *((float*)&instruction.op2));
            break;
        case Instruction_Type::LOAD_CONSTANT_I32:
            string_append_formated(string, "LOAD_CONSTANT_I32                 dest=%d, val=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_FUNCTION_LOCATION:
            string_append_formated(string, "LOAD_FUNCTION_LOCATION            dest=%d, val=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::MOVE_STACK_DATA:
            string_append_formated(string, "MOVE_REGISTER                     dest=%d, src=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::READ_MEMORY:
            string_append_formated(string, "READ_MEMORY                       dest=%d, src_addr_reg=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::WRITE_MEMORY:
            string_append_formated(string, "WRITE_MEMORY                      dest_addr_reg=%d, src=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::READ_GLOBAL:
            string_append_formated(string, "READ_GLOBAL                       dest=%d, global_index=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::WRITE_GLOBAL:
            string_append_formated(string, "WRITE_GLOBAL                      global_index=%d, src=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::MEMORY_COPY:
            string_append_formated(string, "MEMORY_COPY                       dest_addr_reg=%d, src_addr_reg=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_REGISTER_ADDRESS:
            string_append_formated(string, "LOAD_REGISTER_ADDRESS             dest=%d, reg_id=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_GLOBAL_ADDRESS:
            string_append_formated(string, "LOAD_GLOBAL_ADDRESS               dest=%d, global_id=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::U64_ADD_CONSTANT_I32:
            string_append_formated(string, "U64_ADD_CONSTANT_I32              dest=%d, reg_id=%d, offset=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::U64_MULTIPLY_ADD_I32:
            string_append_formated(string, "U64_MULTIPLY_ADD_I32              dest=%d, base_reg=%d, index_reg=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3, instruction.op4);
            break;
        case Instruction_Type::JUMP:
            string_append_formated(string, "JUMP                              dest=%d\n", instruction.op1);
            break;
        case Instruction_Type::JUMP_ON_TRUE:
            string_append_formated(string, "JUMP_ON_TRUE                      dest=%d, cond=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::JUMP_ON_FALSE:
            string_append_formated(string, "JUMP_ON_FALSE                     dest=%d, cond=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::CALL:
            string_append_formated(string, "CALL                              dest=%d, stack_offset=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::CALL_HARDCODED_FUNCTION:
            string_append_formated(string, "CALL_HARDCODED_FUNCTION           func_ind=%d, stack_offset=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::CALL_FUNCTION_POINTER:
            string_append_formated(string, "CALL_FUNCTION_POINTER             src=%d, stack_offset=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::RETURN:
            string_append_formated(string, "RETURN                            return_reg=%d, size=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_RETURN_VALUE:
            string_append_formated(string, "LOAD_RETURN_VALUE                 dst=%d, size=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_CONSTANT_U64:
            string_append_formated(string, "LOAD_CONSTANT_U64                 dst=%d, u64_index=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::EXIT:
            string_append_formated(string, "EXIT                              src=%d, size=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE:
            string_append_formated(string, "CAST_INTEGER_DIFFERENT_SIZE       dst=%d, src=%d, dst_size=%d, src_size=%d\n",
                instruction.op1, instruction.op2, instruction.op3, instruction.op4);
            break;
        case Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE:
            string_append_formated(string, "CAST_FLOAT_DIFFERENT_SIZE         dst=%d, src=%d, dst_size=%d, src_size=%d\n",

                instruction.op1, instruction.op2, instruction.op3, instruction.op4);
            break;
        case Instruction_Type::CAST_FLOAT_INTEGER:
            string_append_formated(string, "CAST_FLOAT_INTEGER                dst=%d, src=%d, dst_size=%d, src_size=%d\n",
                instruction.op1, instruction.op2, instruction.op3, instruction.op4);
            break;
        case Instruction_Type::CAST_INTEGER_FLOAT:
            string_append_formated(string, "CAST_INTEGER_FLOAT                dst=%d, src=%d, dst_size=%d, src_size=%d\n",
                instruction.op1, instruction.op2, instruction.op3, instruction.op4);
            break;
        default:
            string_append_formated(string, "FUCKING HELL\n");
            break;
        }
    }
}

void bytecode_generator_append_bytecode_to_string(Bytecode_Generator* generator, String* string)
{
    string_append_formated(string, "Functions:\n");
    for (int i = 0; i < generator->function_locations.size; i++) {
        string_append_formated(string, "\t%d: %d\n",
            i, generator->function_locations[i]
        );
    }
    string_append_formated(string, "Global size: %d\n\n", generator->global_data_size);
    string_append_formated(string, "Code: \n");

    for (int i = 0; i < generator->instructions.size; i++)
    {
        Bytecode_Instruction& instruction = generator->instructions[i];
        string_append_formated(string, "%4d: ", i);
        bytecode_instruction_append_to_string(string, generator->instructions[i]);
    }
}

