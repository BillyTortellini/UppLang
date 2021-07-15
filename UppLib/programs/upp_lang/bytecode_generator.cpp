#include "bytecode_generator.hpp"

#include "compiler.hpp"
#include "../../utility/hash_functions.hpp"

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
    // Code
    result.instructions = dynamic_array_create_empty<Bytecode_Instruction>(64);

    // Code Information
    result.function_locations = hashtable_create_pointer_empty<IR_Function*, int>(64);
    result.function_parameter_stack_offset_index = hashtable_create_pointer_empty<IR_Function*, int>(64);
    result.code_block_register_stack_offset_index = hashtable_create_pointer_empty<IR_Code_Block*, int>(64);
    result.stack_offsets = dynamic_array_create_empty<Dynamic_Array<int>>(64);
    result.global_data_offsets = dynamic_array_create_empty<int>(256);

    // Fill outs
    result.fill_out_breaks = dynamic_array_create_empty<int>(64);
    result.fill_out_continues = dynamic_array_create_empty<int>(64);
    result.fill_out_calls = dynamic_array_create_empty<Function_Reference>(64);
    return result;
}

void bytecode_generator_destroy(Bytecode_Generator* generator)
{
    // Code
    dynamic_array_destroy(&generator->instructions);

    // Code Information
    hashtable_destroy(&generator->function_locations);
    hashtable_destroy(&generator->function_parameter_stack_offset_index);
    hashtable_destroy(&generator->code_block_register_stack_offset_index);
    dynamic_array_destroy(&generator->global_data_offsets);
    for (int i = 0; i < generator->stack_offsets.size; i++) {
        dynamic_array_destroy(&generator->stack_offsets[i]);
    }
    dynamic_array_destroy(&generator->stack_offsets);

    // Fill outs
    dynamic_array_destroy(&generator->fill_out_breaks);
    dynamic_array_destroy(&generator->fill_out_continues);
    dynamic_array_destroy(&generator->fill_out_calls);
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
    generator->current_stack_offset = align_offset_next_multiple(generator->current_stack_offset, type->alignment_in_bytes);
    int result = generator->current_stack_offset;
    generator->current_stack_offset += type->size_in_bytes;
    return result;
}

int bytecode_generator_data_access_to_stack_offset(Bytecode_Generator* generator, IR_Data_Access access)
{
    Type_Signature* access_type = ir_data_access_get_type(&access);

    int stack_offset = 0;
    switch (access.type)
    {
    case IR_Data_Access_Type::CONSTANT: {
        Bytecode_Instruction load_instruction;
        load_instruction.instruction_type = Instruction_Type::READ_CONSTANT;
        load_instruction.op2 = access.index;
        if (access.is_memory_access) {
            load_instruction.op1 = bytecode_generator_create_temporary_stack_offset(generator, generator->compiler->type_system.void_ptr_type);
            load_instruction.op3 = 8;
        }
        else {
            load_instruction.op1 = bytecode_generator_create_temporary_stack_offset(generator, access_type);
            load_instruction.op3 = access_type->size_in_bytes;
        }
        stack_offset = load_instruction.op1;
        bytecode_generator_add_instruction(generator, load_instruction);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: {
        Bytecode_Instruction load_instruction;
        load_instruction.instruction_type = Instruction_Type::READ_GLOBAL;
        load_instruction.op2 = access.index;
        if (access.is_memory_access) {
            load_instruction.op1 = bytecode_generator_create_temporary_stack_offset(generator, generator->compiler->type_system.void_ptr_type);
            load_instruction.op3 = 8;
        }
        else {
            load_instruction.op1 = bytecode_generator_create_temporary_stack_offset(generator, access_type);
            load_instruction.op3 = access_type->size_in_bytes;
        }
        stack_offset = load_instruction.op1;
        bytecode_generator_add_instruction(generator, load_instruction);
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        Dynamic_Array<int>* parameter_offsets = &generator->stack_offsets[
            *hashtable_find_element(&generator->function_parameter_stack_offset_index, access.option.function)
        ];
        stack_offset = parameter_offsets->data[access.index];
        break;
    }
    case IR_Data_Access_Type::REGISTER: {
        Dynamic_Array<int>* register_offsets = &generator->stack_offsets[
            *hashtable_find_element(&generator->code_block_register_stack_offset_index, access.option.definition_block)
        ];
        stack_offset = register_offsets->data[access.index];
        break;
    }
    default: panic("Should not happen");
    }

    if (access.is_memory_access)
    {
        int result_offset = bytecode_generator_create_temporary_stack_offset(generator, access_type);
        bytecode_generator_add_instruction(
            generator,
            instruction_make_3(
                Instruction_Type::READ_MEMORY,
                result_offset,
                stack_offset,
                access_type->size_in_bytes
            )
        );
        return result_offset;
    }
    else {
        return stack_offset;
    }
}

int bytecode_generator_add_instruction_with_destination_access(Bytecode_Generator* generator, IR_Data_Access destination, Bytecode_Instruction instr)
{
    Type_Signature* type = ir_data_access_get_type(&destination);
    if (destination.is_memory_access)
    {
        int source_reg_offset = bytecode_generator_create_temporary_stack_offset(generator, type);
        instr.op1 = source_reg_offset;
        int instruction_index = bytecode_generator_add_instruction(generator, instr);

        destination.is_memory_access = false;
        int pointer_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, destination);

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
        switch (destination.type)
        {
        case IR_Data_Access_Type::CONSTANT:
            panic("No writes to constant value");
            break;
        case IR_Data_Access_Type::GLOBAL_DATA: {
            int source_reg_offset = bytecode_generator_create_temporary_stack_offset(generator, type);
            instr.op1 = source_reg_offset;
            int instruction_index = bytecode_generator_add_instruction(generator, instr);
            bytecode_generator_add_instruction(
                generator,
                instruction_make_3(
                    Instruction_Type::WRITE_GLOBAL,
                    generator->global_data_offsets[destination.index],
                    source_reg_offset,
                    type->size_in_bytes
                )
            );
            return instruction_index;
        }
        case IR_Data_Access_Type::PARAMETER:
            instr.op1 = bytecode_generator_data_access_to_stack_offset(generator, destination);
            return bytecode_generator_add_instruction(generator, instr);
        case IR_Data_Access_Type::REGISTER:
            instr.op1 = bytecode_generator_data_access_to_stack_offset(generator, destination);
            return bytecode_generator_add_instruction(generator, instr);
        }
    }
}

void bytecode_generator_move_accesses(Bytecode_Generator* generator, IR_Data_Access destination, IR_Data_Access source)
{
    int move_byte_size;
    if (destination.is_memory_access) {
        move_byte_size = 8;
    }
    else {
        move_byte_size = ir_data_access_get_type(&destination)->size_in_bytes;
    }

    int source_offset = bytecode_generator_data_access_to_stack_offset(generator, source);
    Bytecode_Instruction instr = instruction_make_3(Instruction_Type::MOVE_STACK_DATA, 0, source_offset, move_byte_size);
    bytecode_generator_add_instruction_with_destination_access(generator, destination, instr);
}

void bytecode_generator_generate_code_block(Bytecode_Generator* generator, IR_Code_Block* code_block)
{
    // Generate Stack offsets
    int rewind_stack_offset = generator->current_stack_offset;
    SCOPE_EXIT(generator->current_stack_offset = rewind_stack_offset);
    {
        Dynamic_Array<int> register_stack_offsets = dynamic_array_create_empty<int>(code_block->registers.size);
        generator->current_stack_offset = stack_offsets_calculate(&code_block->registers, &register_stack_offsets, generator->current_stack_offset);
        dynamic_array_push_back(&generator->stack_offsets, register_stack_offsets);
        hashtable_insert_element(&generator->code_block_register_stack_offset_index, code_block, generator->stack_offsets.size - 1);
    }

    // Generate instructions
    for (int i = 0; i < code_block->instructions.size; i++)
    {
        IR_Instruction* instr = &code_block->instructions[i];

        switch (instr->type)
        {
        case IR_Instruction_Type::FUNCTION_CALL:
        {
            IR_Instruction_Call* call = &instr->options.call;
            Type_Signature* function_sig = 0;
            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL:
                function_sig = call->options.function->function_type;
                break;
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
                function_sig = ir_data_access_get_type(&call->options.pointer_access);
                break;
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
                function_sig = call->options.hardcoded->signature;
                break;
            default: panic("Error");
            }

            // Put arguments into the correct place on the stack
            int pointer_offset = bytecode_generator_create_temporary_stack_offset(generator, generator->compiler->type_system.void_ptr_type);
            int argument_stack_offset = align_offset_next_multiple(generator->current_stack_offset, 16); // I think 16 is the hightest i have
            for (int i = 0; i < function_sig->parameter_types.size; i++)
            {
                Type_Signature* parameter_sig = function_sig->parameter_types[i];
                argument_stack_offset = align_offset_next_multiple(argument_stack_offset, parameter_sig->alignment_in_bytes);
                IR_Data_Access* argument_access = &call->arguments[i];

                Bytecode_Instruction load_instruction;
                if (argument_access->is_memory_access) {
                    load_instruction.op1 = pointer_offset;
                    load_instruction.op3 = 8;
                }
                else {
                    load_instruction.op1 = argument_stack_offset;
                    load_instruction.op3 = parameter_sig->size_in_bytes;
                }
                switch (argument_access->type)
                {
                case IR_Data_Access_Type::CONSTANT: {
                    load_instruction.instruction_type = Instruction_Type::READ_CONSTANT;
                    load_instruction.op2 = argument_access->index;
                    break;
                }
                case IR_Data_Access_Type::GLOBAL_DATA: {
                    load_instruction.instruction_type = Instruction_Type::READ_GLOBAL;
                    load_instruction.op2 = argument_access->index;
                    break;
                }
                case IR_Data_Access_Type::PARAMETER: {
                    load_instruction.instruction_type = Instruction_Type::MOVE_STACK_DATA;
                    Dynamic_Array<int>* parameter_offsets = &generator->stack_offsets[
                        *hashtable_find_element(&generator->function_parameter_stack_offset_index, code_block->function)
                    ];
                    load_instruction.op2 = parameter_offsets->data[argument_access->index];
                    break;
                }
                case IR_Data_Access_Type::REGISTER: {
                    load_instruction.instruction_type = Instruction_Type::MOVE_STACK_DATA;
                    Dynamic_Array<int>* register_offsets = &generator->stack_offsets[
                        *hashtable_find_element(&generator->code_block_register_stack_offset_index, code_block)
                    ];
                    load_instruction.op2 = register_offsets->data[argument_access->index];
                    break;
                }
                default: panic("Should not happen");
                }
                bytecode_generator_add_instruction(generator, load_instruction);
                if (argument_access->is_memory_access) {
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

                argument_stack_offset += parameter_sig->size_in_bytes;
            }

            // Align argument_stack_offset for return pointer
            argument_stack_offset = align_offset_next_multiple(argument_stack_offset, 8);
            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL: {
                Function_Reference call_ref;
                call_ref.function = call->options.function;
                call_ref.instruction_index = bytecode_generator_add_instruction(generator,
                    instruction_make_2(Instruction_Type::CALL_FUNCTION, 0, argument_stack_offset)
                );
                dynamic_array_push_back(&generator->fill_out_calls, call_ref);
                break;
            }
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL: {
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(
                        Instruction_Type::CALL_FUNCTION_POINTER,
                        bytecode_generator_data_access_to_stack_offset(generator, call->options.pointer_access),
                        argument_stack_offset
                    )
                );
                break;
            }
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
                bytecode_generator_add_instruction(generator,
                    instruction_make_2(Instruction_Type::CALL_HARDCODED_FUNCTION, (i32)call->options.hardcoded->type, argument_stack_offset)
                );
                break;
            default: panic("Error");
            }

            // Load return value to destination
            if (function_sig->return_type != generator->compiler->type_system.void_type){
                bytecode_generator_add_instruction_with_destination_access(
                    generator, call->destination, instruction_make_2(Instruction_Type::LOAD_RETURN_VALUE, 0, function_sig->return_type->size_in_bytes)
                );
            }
            break;
        }
        case IR_Instruction_Type::IF:
        {
            const int PLACEHOLDER = 0;
            IR_Instruction_If* if_instr = &instr->options.if_instr;
            int condition_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, if_instr->condition);
            int jmp_to_else_instr_index = bytecode_generator_add_instruction(
                generator,
                instruction_make_2(Instruction_Type::JUMP_ON_FALSE, PLACEHOLDER, condition_stack_offset)
            );
            bytecode_generator_generate_code_block(generator, if_instr->true_branch);
            int jmp_over_else_instruction_index = bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, PLACEHOLDER)
            );
            generator->instructions[jmp_to_else_instr_index].op1 = generator->instructions.size;
            bytecode_generator_generate_code_block(generator, if_instr->false_branch);
            generator->instructions[jmp_over_else_instruction_index].op1 = generator->instructions.size;
            break;
        }
        case IR_Instruction_Type::WHILE:
        {
            const int PLACEHOLDER = 0;
            IR_Instruction_While* while_instr = &instr->options.while_instr;
            int condition_evaluation_start = generator->instructions.size;
            bytecode_generator_generate_code_block(generator, while_instr->condition_code);
            int condition_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, while_instr->condition_access);
            int jmp_to_end_instruction_index = bytecode_generator_add_instruction(
                generator,
                instruction_make_2(Instruction_Type::JUMP_ON_FALSE, PLACEHOLDER, condition_stack_offset)
            );
            bytecode_generator_generate_code_block(generator, while_instr->code);
            bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, condition_evaluation_start)
            );
            generator->instructions[jmp_to_end_instruction_index].op1 = generator->instructions.size;

            for (int i = 0; i < generator->fill_out_breaks.size; i++) {
                generator->instructions[generator->fill_out_breaks[i]].op1 = generator->instructions.size;
            }
            for (int i = 0; i < generator->fill_out_continues.size; i++) {
                generator->instructions[generator->fill_out_breaks[i]].op1 = condition_evaluation_start;
            }
            dynamic_array_reset(&generator->fill_out_breaks);
            dynamic_array_reset(&generator->fill_out_continues);
            break;
        }
        case IR_Instruction_Type::BLOCK:
            bytecode_generator_generate_code_block(generator, instr->options.block);
            break;
        case IR_Instruction_Type::BREAK: {
            int break_jump = bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, 0)
            );
            dynamic_array_push_back(&generator->fill_out_breaks, break_jump);
            break;
        }
        case IR_Instruction_Type::CONTINUE: {
            int continue_jump = bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, 0)
            );
            dynamic_array_push_back(&generator->fill_out_continues, continue_jump);
            break;
        }
        case IR_Instruction_Type::RETURN: 
        {
            IR_Instruction_Return* return_instr = &instr->options.return_instr;

            switch (return_instr->type)
            {
            case IR_Instruction_Return_Type::EXIT: {
                bytecode_generator_add_instruction(generator,
                    instruction_make_1(Instruction_Type::EXIT, (int)return_instr->options.exit_code)
                );
                break;
            }
            case IR_Instruction_Return_Type::RETURN_DATA: {
                Type_Signature* return_sig = ir_data_access_get_type(&return_instr->options.return_value);
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::RETURN,
                        bytecode_generator_data_access_to_stack_offset(generator, return_instr->options.return_value),
                        return_sig->size_in_bytes
                    )
                );
                break;
            }
            case IR_Instruction_Return_Type::RETURN_EMPTY:
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::RETURN, 0, 0)
                );
                break;
            }
            break;
        }
        case IR_Instruction_Type::MOVE: {
            bytecode_generator_move_accesses(generator, instr->options.move.destination, instr->options.move.source);
            break;
        }
        case IR_Instruction_Type::CAST:
        case IR_Instruction_Type::ADDRESS_OF:
        case IR_Instruction_Type::UNARY_OP:
        case IR_Instruction_Type::BINARY_OP:
        case Intermediate_Instruction_Type::LOAD_FUNCTION_POINTER: {
            bytecode_generator_add_instruction_with_destination_access(generator,
                instr->destination, instruction_make_2(Instruction_Type::LOAD_FUNCTION_LOCATION, 0, instr->intermediate_function_index),
                function_index
            );
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

    }
}

int stack_offsets_calculate(Dynamic_Array<Type_Signature*>* types, Dynamic_Array<int>* offsets, int start_byte_offset)
{
    int stack_offset = start_byte_offset;
    for (int i = 0; i < types->size; i++)
    {
        Type_Signature* signature = types->data[i];
        stack_offset = align_offset_next_multiple(stack_offset, signature->alignment_in_bytes);
        dynamic_array_push_back(offsets, stack_offset);
        stack_offset += signature->size_in_bytes;
    }

    return stack_offset;
}

void bytecode_generator_generate_function_code(Bytecode_Generator* generator, IR_Function* function)
{
    // Generate parameter offsets
    Dynamic_Array<int> parameter_offsets = dynamic_array_create_empty<int>(function->function_type->parameter_types.size);
    {
        int parameter_stack_size = stack_offsets_calculate(&function->function_type->parameter_types, &parameter_offsets, 0);
        // Adjust stack_offsets since parameter offsets are negative
        parameter_stack_size = align_offset_next_multiple(parameter_stack_size, 8); // Adjust for pointer alignment of return address
        for (int i = 0; i < parameter_offsets.size; i++) {
            parameter_offsets[i] -= parameter_stack_size;
        }
        generator->current_stack_offset = 16;
    }

    // Register function
    hashtable_insert_element(&generator->function_locations, function, generator->instructions.size);
    dynamic_array_push_back(&generator->stack_offsets, parameter_offsets);
    hashtable_insert_element(&generator->function_parameter_stack_offset_index, function, generator->stack_offsets.size - 1);

    // Generate code
    bytecode_generator_generate_code_block(generator, function->code);
    if (generator->current_stack_offset > generator->maximum_function_stack_depth) {
        generator->maximum_function_stack_depth = generator->current_stack_offset;
    }
}

void bytecode_generator_generate(Bytecode_Generator* generator, Compiler* compiler)
{
    generator->ir_program = compiler->analyser.program;
    generator->compiler = compiler;
    // Reset previous code
    {
        dynamic_array_reset(&generator->instructions);
        // Reset information
        hashtable_reset(&generator->code_block_register_stack_offset_index);
        hashtable_reset(&generator->function_parameter_stack_offset_index);
        dynamic_array_reset(&generator->global_data_offsets);
        for (int i = 0; i < generator->stack_offsets.size; i++) {
            dynamic_array_destroy(&generator->stack_offsets[i]);
        }
        dynamic_array_reset(&generator->stack_offsets);
        // Reset fill outs
        dynamic_array_reset(&generator->fill_out_breaks);
        dynamic_array_reset(&generator->fill_out_continues);
        dynamic_array_reset(&generator->fill_out_calls);
    }

    // Generate global data offsets
    generator->global_data_size = 0;
    dynamic_array_reserve(&generator->global_data_offsets, generator->ir_program->globals.size);
    for (int i = 0; i < generator->ir_program->globals.size; i++) {
        Type_Signature* signature = generator->ir_program->globals[i];
        generator->global_data_size = align_offset_next_multiple(generator->global_data_size, signature->alignment_in_bytes);
        dynamic_array_push_back(&generator->global_data_offsets, generator->global_data_size);
        generator->global_data_size += signature->size_in_bytes;
    }

    // Generate code for all functions
    for (int i = 0; i < generator->ir_program->functions.size; i++) {
        bytecode_generator_generate_function_code(generator, generator->ir_program->functions[i]);
    }

    // Fill out all function calls
    for (int i = 0; i < generator->fill_out_calls.size; i++) {
        Function_Reference& call_loc = generator->fill_out_calls[i];
        int* location = hashtable_find_element(&generator->function_locations, call_loc.function);
        assert(location != 0, "Should not happen");
        generator->instructions[call_loc.instruction_index].op1 = *location;
    }

    generator->entry_point_index = *hashtable_find_element(&generator->function_locations, generator->ir_program->entry_function);
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
        int type_index = (int)type - (int)Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_U8;
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
        int type_index = (int)type - (int)Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32;
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
        case Instruction_Type::CALL_FUNCTION:
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
    /*
    for (int i = 0; i < generator->function_locations.size; i++) {
        string_append_formated(string, "\t%d: %d\n",
            i, generator->function_locations[i]
        );
    }
    */
    string_append_formated(string, "Global size: %d\n\n", generator->global_data_size);
    string_append_formated(string, "Code: \n");

    for (int i = 0; i < generator->instructions.size; i++)
    {
        Bytecode_Instruction& instruction = generator->instructions[i];
        string_append_formated(string, "%4d: ", i);
        bytecode_instruction_append_to_string(string, generator->instructions[i]);
    }
}

