#include "bytecode.hpp"

int align_offset_next_multiple(int offset, int alignment) {
    // Check if this works if offset is negative
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
    result.register_stack_locations = dynamic_array_create_empty<int>(2048); // I hope this never makes any problems, only if we have more that 2048 register
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
    dynamic_array_destroy(&generator->register_stack_locations);
} 

Bytecode_Instruction instruction_make_0(Instruction_Type::ENUM type) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    return instr;
}

Bytecode_Instruction instruction_make_1(Instruction_Type::ENUM type, int src_1) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    return instr;
}

Bytecode_Instruction instruction_make_2(Instruction_Type::ENUM type, int src_1, int src_2) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    instr.op2 = src_2;
    return instr;
}

Bytecode_Instruction instruction_make_3(Instruction_Type::ENUM type, int src_1, int src_2, int src_3) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    instr.op2 = src_2;
    instr.op3 = src_3;
    return instr;
}

Bytecode_Instruction instruction_make_4(Instruction_Type::ENUM type, int src_1, int src_2, int src_3, int src_4) {
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

void bytecode_generator_generate_load_constant_instruction(Bytecode_Generator* generator, int function_index, int instruction_index)
{
    Intermediate_Function* function = &generator->im_generator->functions[function_index];
    Intermediate_Instruction* instruction = &function->instructions[instruction_index];

    Instruction_Type::ENUM result_type;
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
    else panic("not implemented yet?!?");

    if (instruction->destination.type == Data_Access_Type::REGISTER_ACCESS)
    {
        bytecode_generator_add_instruction(
            generator,
            instruction_make_2(
                result_type,
                generator->register_stack_locations[instruction->destination.register_index],
                result_data
            )
        );
    }
    else
    {
        int temporary_stack_location = align_offset_next_multiple(generator->stack_offset_end_of_variables, result_size);
        bytecode_generator_add_instruction(
            generator,
            instruction_make_2(
                result_type,
                temporary_stack_location,
                result_data
            )
        );
        bytecode_generator_add_instruction(
            generator,
            instruction_make_3(
                Instruction_Type::WRITE_MEMORY,
                generator->register_stack_locations[instruction->destination.register_index],
                temporary_stack_location,
                result_size
            )
        );
    }
}

void bytecode_generator_move_accesses(Bytecode_Generator* generator, Data_Access destination, Data_Access source, int function_index)
{
    Intermediate_Function* function = &generator->im_generator->functions[function_index];
    int move_byte_size;
    if (destination.type == Data_Access_Type::REGISTER_ACCESS) {
        move_byte_size = function->registers[destination.register_index].type_signature->size_in_bytes;
    }
    else {
        Type_Signature* pointer_sig = function->registers[destination.register_index].type_signature;
        move_byte_size = pointer_sig->child_type->size_in_bytes;
    }

    Instruction_Type::ENUM instr_type;
    if (destination.type == Data_Access_Type::REGISTER_ACCESS) {
        if (source.type == Data_Access_Type::REGISTER_ACCESS)
            instr_type = Instruction_Type::MOVE_REGISTERS;
        else
            instr_type = Instruction_Type::READ_MEMORY;
    }
    else if (destination.type == Data_Access_Type::MEMORY_ACCESS) {
        if (source.type == Data_Access_Type::REGISTER_ACCESS)
            instr_type = Instruction_Type::WRITE_MEMORY;
        else
            instr_type = Instruction_Type::MEMORY_COPY;
    }
    else panic("LOL");

    bytecode_generator_add_instruction(
        generator,
        instruction_make_3(
            instr_type,
            generator->register_stack_locations[destination.register_index],
            generator->register_stack_locations[source.register_index],
            move_byte_size
        )
    );
}

int bytecode_generator_read_access_stack_offset(Bytecode_Generator* generator, Data_Access access, int function_index)
{
    if (access.type == Data_Access_Type::REGISTER_ACCESS) {
        return generator->register_stack_locations[access.register_index];
    }
    else {
        Type_Signature* type = generator->im_generator->functions[function_index].registers[access.register_index].type_signature->child_type;
        generator->tmp_stack_offset = align_offset_next_multiple(generator->tmp_stack_offset, type->alignment_in_bytes);
        int result_reg_offset = generator->tmp_stack_offset;
        generator->tmp_stack_offset += type->size_in_bytes;
        bytecode_generator_add_instruction(
            generator,
            instruction_make_3(
                Instruction_Type::READ_MEMORY,
                result_reg_offset,
                generator->register_stack_locations[access.register_index],
                type->size_in_bytes
            )
        );
        return result_reg_offset;
    }
}

int bytecode_generator_add_access_instruction(Bytecode_Generator* generator, Data_Access destination, Bytecode_Instruction instr, int function_index)
{
    int result_reg_offset;
    Type_Signature* type;
    if (destination.type == Data_Access_Type::MEMORY_ACCESS) {
        type = generator->im_generator->functions[function_index].registers[destination.register_index].type_signature->child_type;
        generator->tmp_stack_offset = align_offset_next_multiple(generator->tmp_stack_offset, type->alignment_in_bytes);
        result_reg_offset = generator->tmp_stack_offset;
        generator->tmp_stack_offset = result_reg_offset + type->size_in_bytes;
    }
    else {
        type = generator->im_generator->functions[function_index].registers[destination.register_index].type_signature;
        result_reg_offset = generator->register_stack_locations[destination.register_index];
    }

    instr.op1 = result_reg_offset;
    int instruction_index = bytecode_generator_add_instruction(generator, instr);
    if (destination.type == Data_Access_Type::MEMORY_ACCESS) {
        bytecode_generator_add_instruction(
            generator,
            instruction_make_3(
                Instruction_Type::WRITE_MEMORY,
                generator->register_stack_locations[destination.register_index],
                result_reg_offset,
                type->size_in_bytes
            )
        );
    }
    return instruction_index;
}

void bytecode_generator_generate_function_instruction_slice(
    Bytecode_Generator* generator, int function_index, int instruction_start_index, int instruction_end_index_exclusive
)
{
    Intermediate_Function* function = &generator->im_generator->functions[function_index];
    for (int instruction_index = instruction_start_index;
        instruction_index < function->instructions.size && instruction_index < instruction_end_index_exclusive;
        instruction_index++)
    {
        Intermediate_Instruction* instr = &function->instructions[instruction_index];

        // Binary operations
        if ((int)instr->type >= (int)Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8 &&
            (int)instr->type <= (int)Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR)
        {
            Instruction_Type::ENUM result_instr_type = (Instruction_Type::ENUM) (
                (int)Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32 +
                ((int)instr->type - (int)Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32)
                );

            int operand_1_reg_offset = bytecode_generator_read_access_stack_offset(generator, instr->source1, function_index);
            int operand_2_reg_offset = bytecode_generator_read_access_stack_offset(generator, instr->source2, function_index);
            Bytecode_Instruction result_instr = instruction_make_3(result_instr_type, 0, operand_1_reg_offset, operand_2_reg_offset);
            bytecode_generator_add_access_instruction(generator, instr->destination, result_instr, function_index);
            continue;
        }

        // Unary operations
        if ((int)instr->type >= (int)Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I8 &&
            (int)instr->type <= (int)Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT)
        {
            Instruction_Type::ENUM result_instr_type = (Instruction_Type::ENUM) (
                (int)Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32 +
                ((int)instr->type - (int)Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32)
                );
            int operand_1_reg_offset = bytecode_generator_read_access_stack_offset(generator, instr->source1, function_index);
            Bytecode_Instruction result_instr = instruction_make_2(result_instr_type, 0, operand_1_reg_offset);
            bytecode_generator_add_access_instruction(generator, instr->destination, result_instr, function_index);
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
        case Intermediate_Instruction_Type::LOAD_CONSTANT_BOOL: {
            bytecode_generator_generate_load_constant_instruction(generator, function_index, instruction_index);
            break;
        }
        case Intermediate_Instruction_Type::IF_BLOCK:
        {
            bytecode_generator_generate_function_instruction_slice(
                generator, function_index, instr->condition_calculation_instruction_start, instr->condition_calculation_instruction_end_exclusive
            );
            int condition_stack_offset = bytecode_generator_read_access_stack_offset(generator, instr->source1, function_index);
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
                bytecode_generator_generate_function_instruction_slice(
                    generator, function_index, instr->false_branch_instruction_start, instr->false_branch_instruction_end_exclusive
                );
                generator->instructions[jmp_over_else_instruction_index].op1 = generator->instructions.size;
            }
            generator->instructions[jmp_instruction_index].op1 = generator->instructions.size;

            break;
        }
        case Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION:
        case Intermediate_Instruction_Type::CALL_FUNCTION:
        {
            // Move registers to the right place, then generate call instruction
            int argument_stack_offset = align_offset_next_multiple(generator->tmp_stack_offset, 16); // I think 16 is the hightest i have
            Type_Signature* function_sig;
            if (instr->type == Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION) {
                function_sig = generator->im_generator->analyser->hardcoded_functions[(int)instr->hardcoded_function_type].function_type;
            }
            else {
                function_sig = generator->im_generator->functions[instr->intermediate_function_index].function_type;
            }
            for (int i = 0; i < function_sig->parameter_types.size; i++)
            {
                Type_Signature* parameter_sig = function_sig->parameter_types[i];
                argument_stack_offset = align_offset_next_multiple(argument_stack_offset, parameter_sig->alignment_in_bytes);
                Instruction_Type::ENUM instr_type;
                Data_Access* arg = &instr->arguments[i];
                if (arg->type == Data_Access_Type::REGISTER_ACCESS) {
                    instr_type = Instruction_Type::MOVE_REGISTERS;
                }
                else {
                    instr_type = Instruction_Type::READ_MEMORY;
                }
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_3(
                        instr_type,
                        argument_stack_offset,
                        generator->register_stack_locations[arg->register_index],
                        parameter_sig->size_in_bytes
                    )
                );
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
            else {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::CALL, 0, argument_stack_offset)
                );
                Function_Call_Location call_loc;
                call_loc.call_instruction_location = generator->instructions.size - 1;
                call_loc.function_index = instr->intermediate_function_index;
                dynamic_array_push_back(&generator->function_calls, call_loc);
            }

            // Load return value to destination
            Type_Signature* return_type;
            if (instr->type == Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION) {
                Type_Signature* function_type = generator->im_generator->analyser->hardcoded_functions[(u32)instr->hardcoded_function_type].function_type;
                return_type = function_type->return_type;
            }
            else {
                Type_Signature* function_type = generator->im_generator->functions[instr->intermediate_function_index].function_type;
                return_type = function_type->return_type;
            }

            if (return_type != generator->im_generator->analyser->type_system.void_type)
            {
                Bytecode_Instruction ret_val_instr = instruction_make_2(Instruction_Type::LOAD_RETURN_VALUE, 0, return_type->size_in_bytes);
                bytecode_generator_add_access_instruction(generator, instr->destination, ret_val_instr, function_index);
            }
            break;
        }
        case Intermediate_Instruction_Type::RETURN:
        case Intermediate_Instruction_Type::EXIT:
        {
            Type_Signature* return_sig = generator->im_generator->functions[function_index].function_type->return_type;
            int return_data_stack_offset = 0;
            if (instr->return_has_value) {
                return_data_stack_offset = bytecode_generator_read_access_stack_offset(generator, instr->source1, function_index);
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
            int condition_stack_offset = bytecode_generator_read_access_stack_offset(generator, instr->source1, function_index);
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
        case Intermediate_Instruction_Type::CAST_PRIMITIVE_TYPES:
        {
            Instruction_Type::ENUM cast_type;
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

            bytecode_generator_add_access_instruction(generator, instr->destination,
                instruction_make_4(
                    cast_type, 0,
                    bytecode_generator_read_access_stack_offset(generator, instr->source1, function_index),
                    (int)instr->cast_to->primitive_type, (int)instr->cast_from->primitive_type
                ), 
                function_index
            );
            break;
        }
        case Intermediate_Instruction_Type::ADDRESS_OF:
        {
            int result_offset;
            if (instr->destination.type == Data_Access_Type::REGISTER_ACCESS) {
                result_offset = generator->register_stack_locations[instr->destination.register_index];
            }
            else {
                result_offset = align_offset_next_multiple(generator->stack_offset_end_of_variables, 8);
            }
            bytecode_generator_add_instruction(
                generator,
                instruction_make_2(
                    Instruction_Type::LOAD_REGISTER_ADDRESS,
                    result_offset,
                    generator->register_stack_locations[instr->source1.register_index]
                )
            );
            if (instr->destination.type == Data_Access_Type::MEMORY_ACCESS) {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_3(
                        Instruction_Type::WRITE_MEMORY,
                        generator->register_stack_locations[instr->destination.register_index],
                        result_offset,
                        8 // TODO: Maybe check this for 32bit systems, otherwise pointers are always 8 byte aligned
                    )
                );
            }
            break;
        }
        case Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER:
        {
            int tmp_register_offset = align_offset_next_multiple(generator->stack_offset_end_of_variables, 8);
            int register_address_reg;
            if (instr->source1.type == Data_Access_Type::REGISTER_ACCESS)
            {
                register_address_reg = tmp_register_offset;
                tmp_register_offset += 8;
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(
                        Instruction_Type::LOAD_REGISTER_ADDRESS,
                        register_address_reg,
                        generator->register_stack_locations[instr->source1.register_index]
                    )
                );
            }
            else {
                register_address_reg = generator->register_stack_locations[instr->source1.register_index];
            }

            int result_offset;
            if (instr->destination.type == Data_Access_Type::MEMORY_ACCESS) {
                result_offset = align_offset_next_multiple(generator->stack_offset_end_of_variables, 8);
            }
            else {
                result_offset = generator->register_stack_locations[instr->destination.register_index];
            }
            bytecode_generator_add_instruction(
                generator,
                instruction_make_3(
                    Instruction_Type::U64_ADD_CONSTANT_I32,
                    result_offset,
                    register_address_reg,
                    instr->constant_i32_value
                )
            );
            if (instr->destination.type == Data_Access_Type::MEMORY_ACCESS) {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_3(
                        Instruction_Type::WRITE_MEMORY,
                        generator->register_stack_locations[instr->destination.register_index],
                        result_offset,
                        8 // TODO: Check if we need 32 bit support, this would be such a case, maybe just use size of result type
                    )
                );
            }
            break;
        }
        case Intermediate_Instruction_Type::CALCULATE_ARRAY_ACCESS_POINTER:
        {
            int tmp_register_offset = align_offset_next_multiple(generator->stack_offset_end_of_variables, 8);
            int register_address_reg;
            if (instr->source1.type == Data_Access_Type::MEMORY_ACCESS)
            {
                register_address_reg = tmp_register_offset;
                tmp_register_offset += 8;
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_3(
                        Instruction_Type::READ_MEMORY,
                        register_address_reg,
                        generator->register_stack_locations[instr->source1.register_index], 8
                    )
                );
            }
            else {
                register_address_reg = generator->register_stack_locations[instr->source1.register_index];
            }

            int register_index_reg;
            if (instr->source2.type == Data_Access_Type::MEMORY_ACCESS)
            {
                register_index_reg = tmp_register_offset;
                tmp_register_offset += 8;
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_3(
                        Instruction_Type::READ_MEMORY,
                        register_index_reg,
                        generator->register_stack_locations[instr->source2.register_index],
                        4 // Has to be an u32 TODO: Check if other int types are valid
                    )
                );
            }
            else {
                register_index_reg = generator->register_stack_locations[instr->source2.register_index];
            }

            int result_offset;
            if (instr->destination.type == Data_Access_Type::MEMORY_ACCESS) {
                result_offset = align_offset_next_multiple(generator->stack_offset_end_of_variables, 8);
            }
            else {
                result_offset = generator->register_stack_locations[instr->destination.register_index];
            }
            bytecode_generator_add_instruction(
                generator,
                instruction_make_4(
                    Instruction_Type::U64_MULTIPLY_ADD_I32,
                    result_offset,
                    register_address_reg,
                    register_index_reg,
                    instr->constant_i32_value
                )
            );
            if (instr->destination.type == Data_Access_Type::MEMORY_ACCESS) {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_3(
                        Instruction_Type::WRITE_MEMORY,
                        generator->register_stack_locations[instr->destination.register_index],
                        result_offset,
                        8 // TODO: 32bit, just use result type instead of hard-coding this
                    )
                );
            }
            break;
        }
        }
    }
}

void bytecode_generator_calculate_function_register_locations(Bytecode_Generator* generator, int function_index)
{
    Intermediate_Function* function = &generator->im_generator->functions[function_index];
    // Set parameter stack locations
    {
        int stack_size_of_parameters = 0;
        Type_Signature* function_signature = generator->im_generator->functions[function_index].function_type;

        for (int i = 0; i < function_signature->parameter_types.size; i++)
        {
            Type_Signature* param_sig = function_signature->parameter_types[i];
            stack_size_of_parameters = align_offset_next_multiple(stack_size_of_parameters, param_sig->alignment_in_bytes);
            stack_size_of_parameters += param_sig->size_in_bytes;
        }
        // Adjust for pointer alignment of return address
        stack_size_of_parameters = align_offset_next_multiple(stack_size_of_parameters, 8);

        // Now actually set the things
        int stack_offset = -stack_size_of_parameters;
        for (int i = 0; i < function_signature->parameter_types.size; i++)
        {
            int reg_index = -1;
            for (int j = 0; j < function->registers.size; j++) {
                if (function->registers[j].type == Intermediate_Register_Type::PARAMETER &&
                    function->registers[j].parameter_index == i) {
                    reg_index = j;
                }
            }
            assert(reg_index != -1, "Should never happen");

            Type_Signature* param_sig = function_signature->parameter_types[i];
            stack_offset = align_offset_next_multiple(stack_offset, param_sig->alignment_in_bytes);
            generator->register_stack_locations[reg_index] = stack_offset;
            stack_offset += param_sig->size_in_bytes;
        }
    }

    // Set variable and intermediate results stack locations
    int stack_offset_end_of_variables = 0;
    {
        int stack_offset = 16; // return pointer + old stack pointer
        for (int i = 0; i < function->registers.size; i++)
        {
            Intermediate_Register* reg = &function->registers[i];
            if (reg->type == Intermediate_Register_Type::PARAMETER) continue; // Done in previous step
            Type_Signature* type_sig = reg->type_signature;
            stack_offset = align_offset_next_multiple(stack_offset, type_sig->alignment_in_bytes);
            generator->register_stack_locations[i] = stack_offset;
            stack_offset += type_sig->size_in_bytes;
        }
        stack_offset_end_of_variables = stack_offset;
    }

    generator->stack_offset_end_of_variables = stack_offset_end_of_variables;
    generator->tmp_stack_offset = generator->stack_offset_end_of_variables;
}

void bytecode_generator_generate_function_code(Bytecode_Generator* generator, int function_index)
{
    Intermediate_Function* function = &generator->im_generator->functions[function_index];
    generator->function_locations[function_index] = generator->instructions.size;

    bytecode_generator_calculate_function_register_locations(generator, function_index);
    bytecode_generator_generate_function_instruction_slice(generator, function_index, 0, function->instructions.size);
}

void bytecode_generator_generate(Bytecode_Generator* generator, Intermediate_Generator* im_generator)
{
    generator->im_generator = im_generator;
    dynamic_array_reset(&generator->instructions);
    dynamic_array_reset(&generator->break_instructions_to_fill_out);
    dynamic_array_reset(&generator->continue_instructions_to_fill_out);
    dynamic_array_reset(&generator->function_calls);
    dynamic_array_reset(&generator->function_locations);
    dynamic_array_reset(&generator->register_stack_locations);

    // Find function with highest register_count
    int max_register_count = 0;
    generator->maximum_function_stack_depth = 0;
    for (int i = 0; i < im_generator->functions.size; i++)
    {
        Intermediate_Function* function = &im_generator->functions[i];
        int max_function_stack_depth = 256;
        for (int j = 0; j < function->registers.size; j++) {
            Type_Signature* sig = function->registers[j].type_signature;
            max_function_stack_depth += sig->size_in_bytes + sig->alignment_in_bytes;
        }
        if (max_function_stack_depth > generator->maximum_function_stack_depth) {
            generator->maximum_function_stack_depth = max_function_stack_depth;
        }
        if (function->registers.size > max_register_count) {
            max_register_count = function->registers.size;
        }
    }
    dynamic_array_reserve(&generator->register_stack_locations, max_register_count);
    while (generator->register_stack_locations.size < max_register_count) {
        dynamic_array_push_back(&generator->register_stack_locations, 0);
    }
    dynamic_array_reserve(&generator->function_locations, generator->im_generator->functions.size);
    while (generator->function_locations.size < generator->im_generator->functions.size) {
        dynamic_array_push_back(&generator->function_locations, 0);
    }

    // Generate code for all functions
    for (int i = 0; i < im_generator->functions.size; i++) {
        bytecode_generator_generate_function_code(generator, i);
    }

    // Fill out all function calls
    for (int i = 0; i < generator->function_calls.size; i++) {
        Function_Call_Location& call_loc = generator->function_calls[i];
        generator->instructions[call_loc.call_instruction_location].op1 = generator->function_locations[call_loc.function_index];
    }

    generator->entry_point_index = generator->function_locations[generator->im_generator->main_function_index];
}

void bytecode_instruction_append_to_string(String* string, Bytecode_Instruction instruction)
{
    Intermediate_Instruction_Type intermediate_type = (Intermediate_Instruction_Type)(
            (int)instruction.instruction_type - (int) Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32
            + (int) Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32
        );
    if (intermediate_instruction_type_is_binary_operation(intermediate_type)) {
        intermediate_instruction_binop_append_to_string(string, intermediate_type);
        string_append_formated(string, "\t dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
    }
    else if (intermediate_instruction_type_is_unary_operation(intermediate_type)) {
        intermediate_instruction_unary_operation_append_to_string(string, intermediate_type);
        string_append_formated(string, "\t dst=%d, src1=%d\n", instruction.op1, instruction.op2);
    }
    else
    {
        switch (instruction.instruction_type)
        {
        case Instruction_Type::LOAD_CONSTANT_BOOLEAN:
            string_append_formated(string, "LOAD_CONSTANT_BOOLEAN             dest=%d, val=%s\n", instruction.op1, instruction.op2 ? "TRUE" : "FALSE");
            break;
        case Instruction_Type::LOAD_CONSTANT_F32:
            string_append_formated(string, "LOAD_CONSTANT_F32                 dest=%d, val=%3.2f\n", instruction.op1, *((float*)&instruction.op2));
            break;
        case Instruction_Type::LOAD_CONSTANT_I32:
            string_append_formated(string, "LOAD_CONSTANT_I32                 dest=%d, val=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::MOVE_REGISTERS:
            string_append_formated(string, "MOVE_REGISTER                     dest=%d, src=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::READ_MEMORY:
            string_append_formated(string, "READ_MEMORY                       dest=%d, src_addr_reg=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::WRITE_MEMORY:
            string_append_formated(string, "WRITE_MEMORY                      dest_addr_reg=%d, src=%d, size=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::MEMORY_COPY:
            string_append_formated(string, "MEMORY_COPY                       dest_addr_reg=%d, src_addr_reg=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_REGISTER_ADDRESS:
            string_append_formated(string, "LOAD_REGISTER_ADDRESS             dest=%d, reg_id=%d\n", instruction.op1, instruction.op2);
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
        case Instruction_Type::RETURN:
            string_append_formated(string, "RETURN                            return_reg=%d, size=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_RETURN_VALUE:
            string_append_formated(string, "LOAD_RETURN_VALUE                 dst=%d, size=%d\n", instruction.op1, instruction.op2);
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
    string_append_formated(string, "Code: \n");

    for (int i = 0; i < generator->instructions.size; i++)
    {
        Bytecode_Instruction& instruction = generator->instructions[i];
        string_append_formated(string, "%4d: ", i);
        bytecode_instruction_append_to_string(string, generator->instructions[i]);
    }
}

