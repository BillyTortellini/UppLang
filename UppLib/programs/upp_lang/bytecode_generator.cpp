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
    result.fill_out_function_ptr_loads = dynamic_array_create_empty<Function_Reference>(64);
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
    dynamic_array_destroy(&generator->fill_out_function_ptr_loads);
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
        load_instruction.op2 = generator->ir_program->constant_pool.constants[access.index].offset;
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
        load_instruction.op2 = generator->global_data_offsets[access.index];
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
        int* stack_offset_index = hashtable_find_element(&generator->function_parameter_stack_offset_index, access.option.function);
        Dynamic_Array<int>* parameter_offsets = &generator->stack_offsets[*stack_offset_index];
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

void bytecode_generator_write_stack_offset_to_destination(Bytecode_Generator* generator, int stack_offset, Type_Signature* type, IR_Data_Access destination)
{
    if (destination.is_memory_access)
    {
        destination.is_memory_access = false;
        int pointer_offset = bytecode_generator_data_access_to_stack_offset(generator, destination);
        bytecode_generator_add_instruction(generator, instruction_make_3(Instruction_Type::WRITE_MEMORY, pointer_offset, stack_offset, type->size_in_bytes));
        return;
    }
    else
    {
        switch (destination.type)
        {
        case IR_Data_Access_Type::CONSTANT:
            panic("No writes to constant value");
            break;
        case IR_Data_Access_Type::GLOBAL_DATA: {
            bytecode_generator_add_instruction(
                generator,
                instruction_make_3(
                    Instruction_Type::WRITE_GLOBAL,
                    generator->global_data_offsets[destination.index],
                    stack_offset,
                    type->size_in_bytes
                )
            );
            return;
        }
        case IR_Data_Access_Type::PARAMETER:
        case IR_Data_Access_Type::REGISTER: {
            bytecode_generator_add_instruction(
                generator,
                instruction_make_3(
                    Instruction_Type::MOVE_STACK_DATA,
                    bytecode_generator_data_access_to_stack_offset(generator, destination),
                    stack_offset,
                    type->size_in_bytes
                )
            );
            return;
        }
        }

    }
}

int bytecode_generator_add_instruction_and_set_destination(Bytecode_Generator* generator, IR_Data_Access destination, Bytecode_Instruction instr)
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
    panic("hey");
    return -1;
}

void bytecode_generator_move_accesses(Bytecode_Generator* generator, IR_Data_Access destination, IR_Data_Access source)
{
    int move_byte_size;
    Type_Signature* move_type = ir_data_access_get_type(&destination);
    move_byte_size = move_type->size_in_bytes;
    /*
    if (destination.is_memory_access) {
        //move_byte_size = move_type->child_type->size_in_bytes;
        move_byte_size = 8;
    }
    else {
        move_byte_size = move_type->size_in_bytes;
    }
    */

    int source_offset = bytecode_generator_data_access_to_stack_offset(generator, source);
    Bytecode_Instruction instr = instruction_make_3(Instruction_Type::MOVE_STACK_DATA, 0, source_offset, move_byte_size);
    bytecode_generator_add_instruction_and_set_destination(generator, destination, instr);
}

int bytecode_generator_get_pointer_to_access(Bytecode_Generator* generator, IR_Data_Access access)
{
    if (access.is_memory_access)
    {
        IR_Data_Access new_access = access;
        new_access.is_memory_access = false;
        return bytecode_generator_data_access_to_stack_offset(generator, new_access);
    }

    Bytecode_Instruction load_instr;
    int offset = 0;
    switch (access.type)
    {
    case IR_Data_Access_Type::REGISTER: {
        load_instr.instruction_type = Instruction_Type::LOAD_REGISTER_ADDRESS;
        Dynamic_Array<int>* reg_offsets = &generator->stack_offsets[*hashtable_find_element(
            &generator->code_block_register_stack_offset_index, access.option.definition_block
        )];
        offset = reg_offsets->data[access.index];
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        load_instr.instruction_type = Instruction_Type::LOAD_REGISTER_ADDRESS;
        Dynamic_Array<int>* reg_offsets = &generator->stack_offsets[*hashtable_find_element(
            &generator->function_parameter_stack_offset_index, access.option.function
        )];
        offset = reg_offsets->data[access.index];
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA:
        load_instr.instruction_type = Instruction_Type::LOAD_GLOBAL_ADDRESS;
        offset = generator->global_data_offsets[access.index];
        break;
    case IR_Data_Access_Type::CONSTANT:
        panic("Cannot access address of constant, I think");
        break;
    }
    load_instr.op1 = bytecode_generator_create_temporary_stack_offset(generator, generator->compiler->type_system.void_ptr_type);
    load_instr.op2 = offset;
    bytecode_generator_add_instruction(generator, load_instr);
    return load_instr.op1;
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

    const int PLACEHOLDER = 0;
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
                function_sig = ir_data_access_get_type(&call->options.pointer_access)->child_type;
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
                    load_instruction.op2 = generator->ir_program->constant_pool.constants[argument_access->index].offset;
                    break;
                }
                case IR_Data_Access_Type::GLOBAL_DATA: {
                    load_instruction.instruction_type = Instruction_Type::READ_GLOBAL;
                    load_instruction.op2 = generator->global_data_offsets[argument_access->index];
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
            if (function_sig->return_type != generator->compiler->type_system.void_type) {
                bytecode_generator_add_instruction_and_set_destination(
                    generator, call->destination,
                    instruction_make_2(Instruction_Type::LOAD_RETURN_VALUE, PLACEHOLDER, function_sig->return_type->size_in_bytes)
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
        {
            IR_Instruction_Cast* cast = &instr->options.cast;
            switch (cast->type)
            {
            case IR_Instruction_Cast_Type::POINTERS:
            case IR_Instruction_Cast_Type::POINTER_TO_U64:
            case IR_Instruction_Cast_Type::U64_TO_POINTER: {
                bytecode_generator_move_accesses(generator, cast->destination, cast->source);
                break;
            }
            case IR_Instruction_Cast_Type::PRIMITIVE_TYPES:
            {
                Type_Signature* cast_source = ir_data_access_get_type(&cast->source);
                Type_Signature* cast_destination = ir_data_access_get_type(&cast->destination);
                assert(cast_source->type == Signature_Type::PRIMITIVE && cast_destination->type == Signature_Type::PRIMITIVE, "Wrong types");

                Instruction_Type cast_type;
                if (primitive_type_is_integer(cast_source->primitive_type) && primitive_type_is_integer(cast_destination->primitive_type)) {
                    cast_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE;
                }
                else if (primitive_type_is_float(cast_source->primitive_type) && primitive_type_is_float(cast_destination->primitive_type)) {
                    cast_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE;
                }
                else if (primitive_type_is_float(cast_source->primitive_type) && primitive_type_is_integer(cast_destination->primitive_type)) {
                    cast_type = Instruction_Type::CAST_FLOAT_INTEGER;
                }
                else if (primitive_type_is_integer(cast_source->primitive_type) && primitive_type_is_float(cast_destination->primitive_type)) {
                    cast_type = Instruction_Type::CAST_INTEGER_FLOAT;
                }
                else panic("Should not happen!");

                bytecode_generator_add_instruction_and_set_destination(
                    generator,
                    cast->destination,
                    instruction_make_4(
                        cast_type, PLACEHOLDER,
                        bytecode_generator_data_access_to_stack_offset(generator, cast->source),
                        (int)cast_destination->primitive_type, (int)cast_source->primitive_type
                    )
                );
                break;
            }
            case IR_Instruction_Cast_Type::ARRAY_SIZED_TO_UNSIZED: 
            {
                int sized_ptr_offset = bytecode_generator_get_pointer_to_access(generator, cast->source);
                int unsized_ptr_offset = bytecode_generator_get_pointer_to_access(generator, cast->destination);
                bytecode_generator_add_instruction(generator, instruction_make_3(Instruction_Type::WRITE_MEMORY, unsized_ptr_offset, sized_ptr_offset, 8));

                int unsized_size_ptr_offset = bytecode_generator_create_temporary_stack_offset(generator, generator->compiler->type_system.void_ptr_type);
                bytecode_generator_add_instruction(generator, instruction_make_3(
                    Instruction_Type::U64_ADD_CONSTANT_I32, unsized_size_ptr_offset, unsized_ptr_offset, 8
                ));

                Type_Signature* array_sized_type = ir_data_access_get_type(&cast->source);
                int offset = generator->ir_program->constant_pool.constant_memory.size;
                i32* size = &array_sized_type->array_element_count;
                byte* data_ptr = (byte*)size;
                for (int i = 0; i < 4; i++) {
                    dynamic_array_push_back(&generator->ir_program->constant_pool.constant_memory, data_ptr[i]);
                }
                int const_val_offset = bytecode_generator_create_temporary_stack_offset(generator, generator->compiler->type_system.i32_type);
                bytecode_generator_add_instruction(generator,
                    instruction_make_3(Instruction_Type::READ_CONSTANT, const_val_offset, offset, 4));
                bytecode_generator_add_instruction(generator,
                    instruction_make_3(Instruction_Type::WRITE_MEMORY, unsized_size_ptr_offset, const_val_offset, 4));
                break;
            }
            }
            break;
        }
        case IR_Instruction_Type::ADDRESS_OF:
        {
            IR_Instruction_Address_Of* address_of = &instr->options.address_of;
            switch (address_of->type)
            {
            case IR_Instruction_Address_Of_Type::DATA: {
                int pointer_offset = bytecode_generator_get_pointer_to_access(generator, address_of->source);
                bytecode_generator_write_stack_offset_to_destination(
                    generator, pointer_offset, generator->compiler->type_system.void_ptr_type, address_of->destination
                );
                break;
            }
            case IR_Instruction_Address_Of_Type::FUNCTION: {
                Function_Reference ref;
                ref.function = address_of->options.function;
                ref.instruction_index = bytecode_generator_add_instruction_and_set_destination(generator,
                    address_of->destination, instruction_make_2(Instruction_Type::LOAD_FUNCTION_LOCATION, PLACEHOLDER, PLACEHOLDER)
                );
                dynamic_array_push_back(&generator->fill_out_function_ptr_loads, ref);
                break;
            }
            case IR_Instruction_Address_Of_Type::STRUCT_MEMBER: {
                int struct_pointer_offset = bytecode_generator_get_pointer_to_access(generator, address_of->source);
                bytecode_generator_add_instruction_and_set_destination(generator, address_of->destination,
                    instruction_make_3(
                        Instruction_Type::U64_ADD_CONSTANT_I32,
                        PLACEHOLDER,
                        struct_pointer_offset,
                        address_of->options.member.offset
                    )
                );
                break;
            }
            case IR_Instruction_Address_Of_Type::ARRAY_ELEMENT:
            {
                Type_Signature* array_type = ir_data_access_get_type(&address_of->source);
                int base_pointer_offset;
                if (array_type->type == Signature_Type::ARRAY_SIZED) {
                    base_pointer_offset = bytecode_generator_get_pointer_to_access(generator, address_of->source);
                }
                else if (array_type->type == Signature_Type::ARRAY_UNSIZED) {
                    base_pointer_offset = bytecode_generator_data_access_to_stack_offset(generator, address_of->source);
                }
                else {
                    panic("Hey, should not happen, since this is illegal");
                }

                int index_offset = bytecode_generator_data_access_to_stack_offset(generator, address_of->options.index_access);
                bytecode_generator_add_instruction_and_set_destination(generator, address_of->destination,
                    instruction_make_4(
                        Instruction_Type::U64_MULTIPLY_ADD_I32,
                        PLACEHOLDER,
                        base_pointer_offset,
                        index_offset,
                        math_round_next_multiple(array_type->child_type->size_in_bytes, array_type->child_type->alignment_in_bytes)
                    )
                );
                break;
            }
            }
            break;
        }
        case IR_Instruction_Type::BINARY_OP:
        {
            IR_Instruction_Binary_OP* binary_op = &instr->options.binary_op;
            Bytecode_Instruction instr;
            switch (binary_op->type)
            {
            case IR_Instruction_Binary_OP_Type::ADDITION:
                instr.instruction_type = Instruction_Type::BINARY_OP_ADDITION;
                break;
            case IR_Instruction_Binary_OP_Type::AND:
                instr.instruction_type = Instruction_Type::BINARY_OP_AND;
                break;
            case IR_Instruction_Binary_OP_Type::DIVISION:
                instr.instruction_type = Instruction_Type::BINARY_OP_DIVISION;
                break;
            case IR_Instruction_Binary_OP_Type::EQUAL:
                instr.instruction_type = Instruction_Type::BINARY_OP_EQUAL;
                break;
            case IR_Instruction_Binary_OP_Type::GREATER_EQUAL:
                instr.instruction_type = Instruction_Type::BINARY_OP_GREATER_EQUAL;
                break;
            case IR_Instruction_Binary_OP_Type::GREATER_THAN:
                instr.instruction_type = Instruction_Type::BINARY_OP_GREATER_THAN;
                break;
            case IR_Instruction_Binary_OP_Type::LESS_EQUAL:
                instr.instruction_type = Instruction_Type::BINARY_OP_LESS_EQUAL;
                break;
            case IR_Instruction_Binary_OP_Type::LESS_THAN:
                instr.instruction_type = Instruction_Type::BINARY_OP_LESS_THAN;
                break;
            case IR_Instruction_Binary_OP_Type::MODULO:
                instr.instruction_type = Instruction_Type::BINARY_OP_MODULO;
                break;
            case IR_Instruction_Binary_OP_Type::MULTIPLICATION:
                instr.instruction_type = Instruction_Type::BINARY_OP_MULTIPLICATION;
                break;
            case IR_Instruction_Binary_OP_Type::NOT_EQUAL:
                instr.instruction_type = Instruction_Type::BINARY_OP_NOT_EQUAL;
                break;
            case IR_Instruction_Binary_OP_Type::OR:
                instr.instruction_type = Instruction_Type::BINARY_OP_OR;
                break;
            case IR_Instruction_Binary_OP_Type::SUBTRACTION:
                instr.instruction_type = Instruction_Type::BINARY_OP_SUBTRACTION;
                break;
            }

            Type_Signature* operand_types = ir_data_access_get_type(&binary_op->operand_left);
            if (operand_types->type == Signature_Type::POINTER) {
                instr.op4 = (int)Primitive_Type::UNSIGNED_INT_64;
            }
            else {
                assert(operand_types->type == Signature_Type::PRIMITIVE, "Should not happen");
                instr.op4 = (int)operand_types->primitive_type;
            }
            instr.op2 = bytecode_generator_data_access_to_stack_offset(generator, binary_op->operand_left);
            instr.op3 = bytecode_generator_data_access_to_stack_offset(generator, binary_op->operand_right);
            bytecode_generator_add_instruction_and_set_destination(generator, binary_op->destination, instr);
            break;
        }
        case IR_Instruction_Type::UNARY_OP:
        {
            IR_Instruction_Unary_OP* unary_op = &instr->options.unary_op;
            Bytecode_Instruction instr;
            switch (unary_op->type)
            {
            case IR_Instruction_Unary_OP_Type::NEGATE:
                instr.instruction_type = Instruction_Type::UNARY_OP_NEGATE;
                break;
            case IR_Instruction_Unary_OP_Type::NOT:
                instr.instruction_type = Instruction_Type::UNARY_OP_NOT;
                break;
            }

            Type_Signature* operand_type = ir_data_access_get_type(&unary_op->source);
            assert(operand_type->type == Signature_Type::PRIMITIVE, "Should not happen");
            instr.op3 = (int)operand_type->primitive_type;
            instr.op2 = bytecode_generator_data_access_to_stack_offset(generator, unary_op->source);
            bytecode_generator_add_instruction_and_set_destination(generator, unary_op->destination, instr);
            break;
        }
        }
    }
}

void bytecode_generator_generate_function_code(Bytecode_Generator* generator, IR_Function* function)
{
    // Generate parameter offsets
    {
        int stack_offset_index = *hashtable_find_element(&generator->function_parameter_stack_offset_index, function);
        Dynamic_Array<int>* parameter_offsets = &generator->stack_offsets[stack_offset_index];
        int parameter_stack_size = stack_offsets_calculate(&function->function_type->parameter_types, parameter_offsets, 0);
        // Adjust stack_offsets since parameter offsets are negative
        parameter_stack_size = align_offset_next_multiple(parameter_stack_size, 8); // Adjust for pointer alignment of return address
        for (int i = 0; i < parameter_offsets->size; i++) {
            parameter_offsets->data[i] -= parameter_stack_size;
        }
        generator->current_stack_offset = 16;
    }

    // Register function
    hashtable_insert_element(&generator->function_locations, function, generator->instructions.size);

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
        hashtable_reset(&generator->function_locations);
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
        dynamic_array_reset(&generator->fill_out_function_ptr_loads);
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
        Dynamic_Array<int> parameter_stack_offsets = dynamic_array_create_empty<int>(16);
        dynamic_array_push_back(&generator->stack_offsets, parameter_stack_offsets);
        hashtable_insert_element(&generator->function_parameter_stack_offset_index,
            generator->ir_program->functions[i], generator->stack_offsets.size - 1
        );
    }
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

    // Fill out all function pointer loads
    for (int i = 0; i < generator->fill_out_function_ptr_loads.size; i++) {
        Function_Reference& call_loc = generator->fill_out_function_ptr_loads[i];
        int* location = hashtable_find_element(&generator->function_locations, call_loc.function);
        assert(location != 0, "Should not happen");
        generator->instructions[call_loc.instruction_index].op2 = *location;
    }

    generator->entry_point_index = *hashtable_find_element(&generator->function_locations, generator->ir_program->entry_function);
}



void bytecode_instruction_append_to_string(String* string, Bytecode_Instruction instruction)
{
    Bytecode_Instruction& i = instruction;
    switch (instruction.instruction_type)
    {
    case Instruction_Type::MOVE_STACK_DATA:
        string_append_formated(string, "MOVE_STACK_DATA              dst: %d, src: %d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::WRITE_MEMORY:
        string_append_formated(string, "WRITE_MEMORY                 addr_reg: %d, value_reg: %d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::READ_MEMORY:
        string_append_formated(string, "READ_MEMORY                  dst: %d, addr_reg: %d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::MEMORY_COPY:
        string_append_formated(string, "MEMORY_COPY                  dst_addr_reg: %d, src_addr_reg:%d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::READ_GLOBAL:
        string_append_formated(string, "READ_GLOBAL                  dst: %d, global_offset: %d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::WRITE_GLOBAL:
        string_append_formated(string, "WRITE_GLOBAL                 global_offset: %d, src: %d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::READ_CONSTANT:
        string_append_formated(string, "READ_CONSTANT                dst: %d, const_offset: %d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::U64_ADD_CONSTANT_I32:
        string_append_formated(string, "U64_ADD_CONSTANT_I32         dst: %d, base: %d, offset: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::U64_MULTIPLY_ADD_I32:
        string_append_formated(string, "U64_MULTIPLY_ADD_I32         dst: %d, base: %d, index_reg: %d, size: %d", i.op1, i.op2, i.op3, i.op4);
        break;
    case Instruction_Type::JUMP:
        string_append_formated(string, "JUMP                         instr-nr: %d", i.op1);
        break;
    case Instruction_Type::JUMP_ON_TRUE:
        string_append_formated(string, "JUMP_ON_TRUE                 instr-nr: %d, cond_reg: %d", i.op1, i.op2);
        break;
    case Instruction_Type::JUMP_ON_FALSE:
        string_append_formated(string, "JUMP_ON_FALSE                instr-nr: %d, cond_reg: %d", i.op1, i.op2);
        break;
    case Instruction_Type::CALL_FUNCTION:
        string_append_formated(string, "CALL_FUNCTION                function-start-instr: %d, arg-start-offset: %d", i.op1, i.op2);
        break;
    case Instruction_Type::CALL_FUNCTION_POINTER:
        string_append_formated(string, "CALL_FUNCTION_POINTER        pointer-reg: %d, arg-start-offset: %d", i.op1, i.op2);
        break;
    case Instruction_Type::CALL_HARDCODED_FUNCTION:
        string_append_formated(string, "CALL_HARDCODED_FUNCTION      hardcoded_func_type:");
        ir_hardcoded_function_type_append_to_string(string, (IR_Hardcoded_Function_Type)i.op1);
        string_append_formated(string, ", arg-start-offset: %d", i.op2);
        break;
    case Instruction_Type::RETURN:
        string_append_formated(string, "RETURN                       value-reg: %d, return-size: %d", i.op1, i.op2);
        break;
    case Instruction_Type::EXIT:
        string_append_formated(string, "EXIT                         exit-code: ");
        ir_exit_code_append_to_string(string, (IR_Exit_Code)i.op1);
        break;
    case Instruction_Type::LOAD_RETURN_VALUE:
        string_append_formated(string, "LOAD_RETURN_VALUE            dst: %d, size: %d", i.op1, i.op2);
        break;
    case Instruction_Type::LOAD_REGISTER_ADDRESS:
        string_append_formated(string, "LOAD_REGISTER_ADDRESS        dst: %d, reg-to-load: %d", i.op1, i.op2);
        break;
    case Instruction_Type::LOAD_GLOBAL_ADDRESS:
        string_append_formated(string, "LOAD_GLOBAL_ADDRESS          dst: %d, global-offset: %d", i.op1, i.op2);
        break;
    case Instruction_Type::LOAD_FUNCTION_LOCATION:
        string_append_formated(string, "LOAD_FUNCTION_LOCATION       dst: %d, func-start-instr: %d", i.op1, i.op2);
        break;
    case Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE:
        string_append_formated(string, "CAST_INTEGER_DIFFERENT_SIZE  dst: %d, src: %d, dst-primitive-type: %s, src-primitive-type: %s",
            i.op1, i.op2, primitive_type_to_string((Primitive_Type)i.op3).characters, primitive_type_to_string((Primitive_Type)i.op4)
        );
        break;
    case Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE:
        string_append_formated(string, "CAST_FLOAT_DIFFERENT_SIZE    dst: %d, src: %d, dst-primitive-type: %s, src-primitive-type: %s",
            i.op1, i.op2, primitive_type_to_string((Primitive_Type)i.op3).characters, primitive_type_to_string((Primitive_Type)i.op4)
        );
        break;
    case Instruction_Type::CAST_FLOAT_INTEGER:
        string_append_formated(string, "CAST_FLOAT_INTEGER           dst: %d, src: %d, dst-primitive-type: %s, src-primitive-type: %s",
            i.op1, i.op2, primitive_type_to_string((Primitive_Type)i.op3).characters, primitive_type_to_string((Primitive_Type)i.op4)
        );
        break;
    case Instruction_Type::CAST_INTEGER_FLOAT:
        string_append_formated(string, "CAST_INTEGER_FLOAT           dst: %d, src: %d, dst-primitive-type: %s, src-primitive-type: %s",
            i.op1, i.op2, primitive_type_to_string((Primitive_Type)i.op3).characters, primitive_type_to_string((Primitive_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_ADDITION:
        string_append_formated(string, "BINARY_OP_ADDITION           dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_SUBTRACTION:
        string_append_formated(string, "BINARY_OP_SUBTRACTION        dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_MULTIPLICATION:
        string_append_formated(string, "BINARY_OP_MULTIPLICATION     dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_DIVISION:
        string_append_formated(string, "BINARY_OP_DIVISION           dst %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_EQUAL:
        string_append_formated(string, "BINARY_OP_EQUAL              dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_NOT_EQUAL:
        string_append_formated(string, "BINARY_OP_NOT_EQUAL          dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_GREATER_THAN:
        string_append_formated(string, "BINARY_OP_GREATER_THAN       dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_GREATER_EQUAL:
        string_append_formated(string, "BINARY_OP_GREATER_EQUAL      dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_LESS_THAN:
        string_append_formated(string, "BINARY_OP_LESS_THAN          dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_LESS_EQUAL:
        string_append_formated(string, "BINARY_OP_LESS_EQUAL         dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_MODULO:
        string_append_formated(string, "BINARY_OP_MODULO             dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_AND:
        string_append_formated(string, "BINARY_OP_AND                dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::BINARY_OP_OR:
        string_append_formated(string, "BINARY_OP_OR                 dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, primitive_type_to_string((Primitive_Type)i.op4).characters
        );
        break;
    case Instruction_Type::UNARY_OP_NEGATE:
        string_append_formated(string, "UNARY_OP_NEGATE              dst: %d, src: %d, type: %s",
            i.op1, i.op2, primitive_type_to_string((Primitive_Type)i.op3).characters
        );
        break;
    case Instruction_Type::UNARY_OP_NOT:
        string_append_formated(string, "UNARY_OP_NOT                 dst: %d, src: %d, type: %s",
            i.op1, i.op2, primitive_type_to_string((Primitive_Type)i.op3).characters
        );
        break;
    default:
        string_append_formated(string, "FUCKING HELL\n");
        break;
    }
}

void bytecode_generator_append_bytecode_to_string(Bytecode_Generator* generator, String* string)
{
    string_append_formated(string, "Function starts:\n");
    {
        Hashtable_Iterator<IR_Function*, int> function_iter = hashtable_iterator_create(&generator->function_locations);
        int i = 0;
        while (hashtable_iterator_has_next(&function_iter)) {
            string_append_formated(string, "\t%d: %d\n", i, *function_iter.value);
            i++;
            hashtable_iterator_next(&function_iter);
        }
    }
    string_append_formated(string, "Global size: %d\n\n", generator->global_data_size);
    string_append_formated(string, "Code: \n");
    for (int i = 0; i < generator->instructions.size; i++)
    {
        Bytecode_Instruction& instruction = generator->instructions[i];
        string_append_formated(string, "%4d: ", i);
        bytecode_instruction_append_to_string(string, generator->instructions[i]);
        string_append_formated(string, "\n");
    }
}

