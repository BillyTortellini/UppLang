#include "bytecode_generator.hpp"

#include "compiler.hpp"
#include "../../utility/hash_functions.hpp"
#include "ir_code.hpp"
#include "ast.hpp"

int align_offset_next_multiple(int offset, int alignment) 
{
    if (alignment == 0) {
        panic("");
        return offset;
    }
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
    result.instructions = dynamic_array_create<Bytecode_Instruction>(64);

    // Code Information
    result.function_locations = hashtable_create_pointer_empty<IR_Function*, int>(64);
    result.function_parameter_stack_offset_index = hashtable_create_pointer_empty<IR_Function*, int>(64);
    result.code_block_register_stack_offset_index = hashtable_create_pointer_empty<IR_Code_Block*, int>(64);
    result.stack_offsets = dynamic_array_create<Dynamic_Array<int>>(64);

    // Fill outs
    result.fill_out_gotos = dynamic_array_create<Goto_Label>(64);
    result.label_locations = dynamic_array_create<int>(64);
    result.fill_out_calls = dynamic_array_create<Function_Reference>(64);
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
    for (int i = 0; i < generator->stack_offsets.size; i++) {
        dynamic_array_destroy(&generator->stack_offsets[i]);
    }
    dynamic_array_destroy(&generator->stack_offsets);

    // Fill outs
    dynamic_array_destroy(&generator->fill_out_gotos);
    dynamic_array_destroy(&generator->label_locations);
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

int bytecode_generator_create_temporary_stack_offset(Bytecode_Generator* generator, Datatype* type) 
{
    assert(type->memory_info.available, "");
    auto& memory_info = type->memory_info.value;
    generator->current_stack_offset = align_offset_next_multiple(generator->current_stack_offset, memory_info.alignment);
    int result = generator->current_stack_offset;
    generator->current_stack_offset += memory_info.size;
    return result;
}

int bytecode_generator_data_access_to_stack_offset(Bytecode_Generator* generator, IR_Data_Access access)
{
    Datatype* access_type = ir_data_access_get_type(&access);
    assert(access_type->memory_info.available, "");
    auto& memory_info = access_type->memory_info.value;
    auto& types = compiler.type_system.predefined_types;

    int stack_offset = 0;
    switch (access.type)
    {
    case IR_Data_Access_Type::CONSTANT: {
        Bytecode_Instruction load_instruction;
        load_instruction.instruction_type = Instruction_Type::READ_CONSTANT;
        load_instruction.op2 = access.index;
        if (access.is_memory_access) {
            load_instruction.op1 = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
            load_instruction.op3 = 8;
        }
        else {
            load_instruction.op1 = bytecode_generator_create_temporary_stack_offset(generator, access_type);
            load_instruction.op3 = memory_info.size;
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
            load_instruction.op1 = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
            load_instruction.op3 = 8;
        }
        else {
            load_instruction.op1 = bytecode_generator_create_temporary_stack_offset(generator, access_type);
            load_instruction.op3 = memory_info.size;
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
                memory_info.size
            )
        );
        return result_offset;
    }
    else {
        return stack_offset;
    }
}

void bytecode_generator_write_stack_offset_to_destination(Bytecode_Generator* generator, int stack_offset, Datatype* type, IR_Data_Access destination)
{
    assert(type->memory_info.available, "");
    auto& memory_info = type->memory_info.value;

    if (destination.is_memory_access)
    {
        destination.is_memory_access = false;
        int pointer_offset = bytecode_generator_data_access_to_stack_offset(generator, destination);
        bytecode_generator_add_instruction(generator, instruction_make_3(Instruction_Type::WRITE_MEMORY, pointer_offset, stack_offset, memory_info.size));
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
                    destination.index,
                    stack_offset,
                    memory_info.size
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
                    memory_info.size
                )
            );
            return;
        }
        }

    }
}

int bytecode_generator_add_instruction_and_set_destination(Bytecode_Generator* generator, IR_Data_Access destination, Bytecode_Instruction instr)
{
    Datatype* type = ir_data_access_get_type(&destination);
    assert(type->memory_info.available, "");
    auto& memory_info = type->memory_info.value;

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
                memory_info.size
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
                    destination.index,
                    source_reg_offset,
                    memory_info.size
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
    Datatype* move_type = ir_data_access_get_type(&destination);
    assert(move_type->memory_info.available, "");
    auto& memory_info = move_type->memory_info.value;

    int move_byte_size = memory_info.size;
    /*
    if (destination.is_memory_access) {
        //move_byte_size = move_type->child_type->size;
        move_byte_size = 8;
    }
    else {
        move_byte_size = move_type->size;
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
        offset = access.index;
        break;
    case IR_Data_Access_Type::CONSTANT:
        load_instr.instruction_type = Instruction_Type::LOAD_CONSTANT_ADDRESS;
        offset = access.index;
        break;
    default: panic("");
    }
    load_instr.op1 = bytecode_generator_create_temporary_stack_offset(generator, compiler.type_system.predefined_types.byte_pointer);
    load_instr.op2 = offset;
    bytecode_generator_add_instruction(generator, load_instr);
    return load_instr.op1;
}

int stack_offsets_calculate(Dynamic_Array<Datatype*>* types, Dynamic_Array<int>* offsets, int start_byte_offset)
{
    int stack_offset = start_byte_offset;
    for (int i = 0; i < types->size; i++)
    {
        Datatype* signature = types->data[i];
        assert(signature->memory_info.available, "");
        auto& memory_info = signature->memory_info.value;

        stack_offset = align_offset_next_multiple(stack_offset, memory_info.alignment);
        dynamic_array_push_back(offsets, stack_offset);
        stack_offset += memory_info.size;
    }

    return stack_offset;
}

Bytecode_Type type_base_to_bytecode_type(Datatype* type)
{
    type = datatype_get_non_const_type(type);
    assert(type->type == Datatype_Type::PRIMITIVE || type->type == Datatype_Type::ENUM || type->type == Datatype_Type::TYPE_HANDLE, "HEY");
    if (type->type == Datatype_Type::TYPE_HANDLE) {
        return Bytecode_Type::UINT32;
    }
    if (type->type == Datatype_Type::ENUM) {
        return Bytecode_Type::INT32;
    }

    auto primitive = downcast<Datatype_Primitive>(type);
    int type_size = type->memory_info.value.size;
    Bytecode_Type result;
    switch (primitive->primitive_type)
    {
    case Primitive_Type::INTEGER: {
        switch (type_size) {
        case 1: result = Bytecode_Type::INT8; break;
        case 2: result = Bytecode_Type::INT16; break;
        case 4: result = Bytecode_Type::INT32; break;
        case 8: result = Bytecode_Type::INT64; break;
        default: panic("HEY");
        }
        if (!primitive->is_signed) {
            result = (Bytecode_Type)((int)result + 4);
        }
        break;
    }
    case Primitive_Type::FLOAT: {
        if (type_size == 4) {
            result = Bytecode_Type::FLOAT32; break;
        }
        else if (type_size == 8) {
            result = Bytecode_Type::FLOAT64; break;
        }
        else panic("HEY");
        break;
    }
    case Primitive_Type::BOOLEAN: result = Bytecode_Type::BOOL; break;
    default: panic("HEY");
    }
    return result;
}

const char* bytecode_type_as_string(Bytecode_Type type) 
{
    switch (type)
    {
    case Bytecode_Type::BOOL:
        return "BOOL";
    case Bytecode_Type::FLOAT32:
        return "FLOAT32";
    case Bytecode_Type::FLOAT64:
        return "FLOAT64";
    case Bytecode_Type::INT8:
        return "INT8";
    case Bytecode_Type::INT16:
        return "INT16";
    case Bytecode_Type::INT32:
        return "INT32";
    case Bytecode_Type::INT64:
        return "INT64";
    case Bytecode_Type::UINT8:
        return "UINT8";
    case Bytecode_Type::UINT16:
        return "UINT16";
    case Bytecode_Type::UINT32:
        return "UINT32";
    case Bytecode_Type::UINT64:
        return "UINT64";
    default: panic("HEY");
    }
    return "ERROR";
}

void bytecode_generator_generate_code_block(Bytecode_Generator* generator, IR_Code_Block* code_block)
{
    // Generate Stack offsets
    int rewind_stack_offset = generator->current_stack_offset;
    SCOPE_EXIT(generator->current_stack_offset = rewind_stack_offset);
    {
        Dynamic_Array<int> register_stack_offsets = dynamic_array_create<int>(code_block->registers.size);
        generator->current_stack_offset = stack_offsets_calculate(&code_block->registers, &register_stack_offsets, generator->current_stack_offset);
        dynamic_array_push_back(&generator->stack_offsets, register_stack_offsets);
        hashtable_insert_element(&generator->code_block_register_stack_offset_index, code_block, generator->stack_offsets.size - 1);
    }

    auto& types = compiler.type_system.predefined_types;
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
            Datatype_Function* function_sig = 0;
            int function_pointer_stack_offset = -1;
            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL:
                function_sig = call->options.function->function_type;
                break;
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
                function_sig = downcast<Datatype_Function>(ir_data_access_get_type(&call->options.pointer_access));
                function_pointer_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, call->options.pointer_access);
                break;
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
                function_sig = call->options.hardcoded.signature;
                break;
            case IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL:
                function_sig = downcast<Datatype_Function>(call->options.extern_function.function_signature);
                break;
            default: panic("Error");
            }

            // Put arguments into the correct place on the stack
            int pointer_offset = bytecode_generator_create_temporary_stack_offset(generator, compiler.type_system.predefined_types.byte_pointer);
            int argument_stack_offset = align_offset_next_multiple(generator->current_stack_offset, 16); // I think 16 is the hightest i have
            for (int i = 0; i < function_sig->parameters.size; i++)
            {
                Datatype* parameter_sig = function_sig->parameters[i].type;
                assert(parameter_sig->memory_info.available, "");
                auto& param_memory = parameter_sig->memory_info.value;

                argument_stack_offset = align_offset_next_multiple(argument_stack_offset, param_memory.alignment);
                IR_Data_Access* argument_access = &call->arguments[i];

                Bytecode_Instruction load_instruction;
                if (argument_access->is_memory_access) {
                    load_instruction.op1 = pointer_offset;
                    load_instruction.op3 = 8;
                }
                else {
                    load_instruction.op1 = argument_stack_offset;
                    load_instruction.op3 = param_memory.size;
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
                        *hashtable_find_element(&generator->function_parameter_stack_offset_index, argument_access->option.function)
                    ];
                    load_instruction.op2 = parameter_offsets->data[argument_access->index];
                    break;
                }
                case IR_Data_Access_Type::REGISTER: {
                    load_instruction.instruction_type = Instruction_Type::MOVE_STACK_DATA;
                    Dynamic_Array<int>* register_offsets = &generator->stack_offsets[
                        *hashtable_find_element(&generator->code_block_register_stack_offset_index, argument_access->option.definition_block)
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
                            param_memory.size
                        )
                    );
                }

                argument_stack_offset += param_memory.size;
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
                assert(function_pointer_stack_offset != -1, "");
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::CALL_FUNCTION_POINTER, function_pointer_stack_offset, argument_stack_offset)
                );
                break;
            }
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
                bytecode_generator_add_instruction(generator,
                    instruction_make_2(Instruction_Type::CALL_HARDCODED_FUNCTION, (i32)call->options.hardcoded.type, argument_stack_offset)
                );
                break;
            case IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL:
                bytecode_generator_add_instruction(generator,
                    instruction_make_1(Instruction_Type::EXIT, (int)Exit_Code::EXTERN_FUNCTION_CALL_NOT_IMPLEMENTED)
                );
                break;
            default: panic("Error");
            }

            // Load return value to destination
            if (function_sig->return_type.available) {
                assert(function_sig->return_type.value->memory_info.available, "");
                bytecode_generator_add_instruction_and_set_destination(
                    generator, call->destination,
                    instruction_make_2(Instruction_Type::LOAD_RETURN_VALUE, PLACEHOLDER, function_sig->return_type.value->memory_info.value.size)
                );
            }
            break;
        }
        case IR_Instruction_Type::SWITCH:
        {
            /*
                Current switch implementation is just a linear search through the values.
                Better Implementations would be to use a combination of:
                    * Binary search
                    * Jump table
            */
            IR_Instruction_Switch* switch_instr = &instr->options.switch_instr;
            int condition_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, switch_instr->condition_access);
            int cmp_result_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, upcast(types.bool_type));

            Dynamic_Array<int> case_jump_indices = dynamic_array_create<int>(switch_instr->cases.size);
            Dynamic_Array<int> jmp_to_switch_end_indices = dynamic_array_create<int>(switch_instr->cases.size + 1);
            SCOPE_EXIT(dynamic_array_destroy(&case_jump_indices));
            SCOPE_EXIT(dynamic_array_destroy(&jmp_to_switch_end_indices));

            for (int i = 0; i < switch_instr->cases.size; i++)
            {
                IR_Switch_Case* switch_case = &switch_instr->cases[i];
                // TODO this is kinda bad, I should probably store the constant in IR_Switch_Case
                IR_Data_Access constant_access;
                constant_access.is_memory_access = false;
                constant_access.type = IR_Data_Access_Type::CONSTANT;
                constant_access.index = constant_pool_add_constant(
                    upcast(types.i32_type), array_create_static_as_bytes(&switch_case->value, 1)
                ).constant.constant_index;
                int constant_stack_offset = bytecode_generator_data_access_to_stack_offset(generator, constant_access);

                bytecode_generator_add_instruction(generator,
                    instruction_make_4(
                        Instruction_Type::BINARY_OP_EQUAL, 
                        cmp_result_stack_offset, condition_stack_offset, constant_stack_offset, (int)Bytecode_Type::INT32
                    )
                );
                dynamic_array_push_back(&case_jump_indices, 
                    bytecode_generator_add_instruction(generator,
                        instruction_make_2(Instruction_Type::JUMP_ON_TRUE, PLACEHOLDER, cmp_result_stack_offset)
                    )
                );
            }

            // Generate default block
            bytecode_generator_generate_code_block(generator, switch_instr->default_block);
            dynamic_array_push_back(&jmp_to_switch_end_indices, 
                bytecode_generator_add_instruction(generator, instruction_make_1(Instruction_Type::JUMP, PLACEHOLDER))
            );

            // Generate switch cases
            for (int i = 0; i < switch_instr->cases.size; i++)
            {
                IR_Switch_Case* switch_case = &switch_instr->cases[i];
                generator->instructions[case_jump_indices[i]].op1 = generator->instructions.size;
                bytecode_generator_generate_code_block(generator, switch_case->block);
                dynamic_array_push_back(&jmp_to_switch_end_indices,
                    bytecode_generator_add_instruction(generator, instruction_make_1(Instruction_Type::JUMP, PLACEHOLDER))
                );
            }

            // Set jumps to end of switch
            for (int i = 0; i < jmp_to_switch_end_indices.size; i++) {
                generator->instructions[jmp_to_switch_end_indices[i]].op1 = generator->instructions.size;
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

            break;
        }
        case IR_Instruction_Type::BLOCK:
            bytecode_generator_generate_code_block(generator, instr->options.block);
            break;
        case IR_Instruction_Type::LABEL: {
            while (generator->label_locations.size <= instr->options.label_index) {
                dynamic_array_push_back(&generator->label_locations, 0);
            }
            generator->label_locations[instr->options.label_index] = generator->instructions.size;
            break;
        }
        case IR_Instruction_Type::GOTO: {
            bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, PLACEHOLDER)
            );
            Goto_Label fill_out;
            fill_out.jmp_instruction = generator->instructions.size - 1 ;
            fill_out.label_index = instr->options.label_index;
            dynamic_array_push_back(&generator->fill_out_gotos, fill_out);
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
                Datatype* return_sig = ir_data_access_get_type(&return_instr->options.return_value);
                assert(return_sig->memory_info.available, "");
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::RETURN,
                        bytecode_generator_data_access_to_stack_offset(generator, return_instr->options.return_value),
                        return_sig->memory_info.value.size
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
            case IR_Cast_Type::POINTERS:
            case IR_Cast_Type::POINTER_TO_U64:
            case IR_Cast_Type::U64_TO_POINTER: {
                bytecode_generator_move_accesses(generator, cast->destination, cast->source);
                break;
            }
            case IR_Cast_Type::ENUM_TO_INT:
            case IR_Cast_Type::INT_TO_ENUM:
            case IR_Cast_Type::INTEGERS:
            case IR_Cast_Type::FLOATS:
            case IR_Cast_Type::FLOAT_TO_INT:
            case IR_Cast_Type::INT_TO_FLOAT:
            {
                Datatype* cast_source = ir_data_access_get_type(&cast->source);
                Datatype* cast_destination = ir_data_access_get_type(&cast->destination);
                Instruction_Type instr_type;
                switch (cast->type) {
                    case IR_Cast_Type::ENUM_TO_INT: 
                    case IR_Cast_Type::INT_TO_ENUM:
                    case IR_Cast_Type::INTEGERS:
                        instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
                    case IR_Cast_Type::FLOATS:
                        instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
                    case IR_Cast_Type::FLOAT_TO_INT:
                        instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
                    case IR_Cast_Type::INT_TO_FLOAT:
                        instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
                    default: panic("");
                }
                bytecode_generator_add_instruction_and_set_destination(
                    generator,
                    cast->destination,
                    instruction_make_4(
                        instr_type, PLACEHOLDER,
                        bytecode_generator_data_access_to_stack_offset(generator, cast->source),
                        (int)type_base_to_bytecode_type(cast_destination), (int)type_base_to_bytecode_type(cast_source)
                    )
                );
                break;
            }
            default: panic("");
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
                    generator, pointer_offset, types.byte_pointer, address_of->destination
                );
                break;
            }
            case IR_Instruction_Address_Of_Type::FUNCTION: {
                bytecode_generator_add_instruction_and_set_destination(generator,
                    address_of->destination, instruction_make_2(
                        Instruction_Type::LOAD_FUNCTION_LOCATION, PLACEHOLDER, (int)address_of->options.function->origin->function_index_plus_one)
                );
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
                Datatype* array_type = datatype_get_non_const_type(ir_data_access_get_type(&address_of->source));
                int base_pointer_offset;
                Datatype* element_type = 0;
                if (array_type->type == Datatype_Type::ARRAY) {
                    base_pointer_offset = bytecode_generator_get_pointer_to_access(generator, address_of->source);
                    element_type = downcast<Datatype_Array>(array_type)->element_type;
                }
                else if (array_type->type == Datatype_Type::SLICE) {
                    base_pointer_offset = bytecode_generator_data_access_to_stack_offset(generator, address_of->source);
                    element_type = downcast<Datatype_Slice>(array_type)->element_type;
                }
                else {
                    panic("Hey, should not happen, since this is illegal");
                }

                assert(element_type->memory_info.available, "");
                auto& element_memory = element_type->memory_info.value;

                int index_offset = bytecode_generator_data_access_to_stack_offset(generator, address_of->options.index_access);
                bytecode_generator_add_instruction_and_set_destination(generator, address_of->destination,
                    instruction_make_4(
                        Instruction_Type::U64_MULTIPLY_ADD_I32,
                        PLACEHOLDER,
                        base_pointer_offset,
                        index_offset,
                        math_round_next_multiple(element_memory.size, element_memory.alignment)
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
            using AST::Binop;
            // TODO: Replace this with integer logic
            switch (binary_op->type)
            {
            case Binop::ADDITION:
                instr.instruction_type = Instruction_Type::BINARY_OP_ADDITION;
                break;
            case Binop::AND:
                instr.instruction_type = Instruction_Type::BINARY_OP_AND;
                break;
            case Binop::DIVISION:
                instr.instruction_type = Instruction_Type::BINARY_OP_DIVISION;
                break;
            case Binop::EQUAL:
                instr.instruction_type = Instruction_Type::BINARY_OP_EQUAL;
                break;
            case Binop::GREATER:
                instr.instruction_type = Instruction_Type::BINARY_OP_GREATER_THAN;
                break;
            case Binop::GREATER_OR_EQUAL:
                instr.instruction_type = Instruction_Type::BINARY_OP_GREATER_EQUAL;
                break;
            case Binop::LESS:
                instr.instruction_type = Instruction_Type::BINARY_OP_LESS_THAN;
                break;
            case Binop::LESS_OR_EQUAL:
                instr.instruction_type = Instruction_Type::BINARY_OP_LESS_EQUAL;
                break;
            case Binop::MODULO:
                instr.instruction_type = Instruction_Type::BINARY_OP_MODULO;
                break;
            case Binop::MULTIPLICATION:
                instr.instruction_type = Instruction_Type::BINARY_OP_MULTIPLICATION;
                break;
            case Binop::NOT_EQUAL:
                instr.instruction_type = Instruction_Type::BINARY_OP_NOT_EQUAL;
                break;
            case Binop::OR:
                instr.instruction_type = Instruction_Type::BINARY_OP_OR;
                break;
            case Binop::SUBTRACTION:
                instr.instruction_type = Instruction_Type::BINARY_OP_SUBTRACTION;
                break;
            default: panic("");
            }

            Datatype* operand_types = ir_data_access_get_type(&binary_op->operand_left);
            if (operand_types->type == Datatype_Type::POINTER || operand_types->type == Datatype_Type::BYTE_POINTER) {
                instr.op4 = (int)Bytecode_Type::INT64;
            }
            else {
                instr.op4 = (int)type_base_to_bytecode_type(operand_types);
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

            Datatype* operand_type = datatype_get_non_const_type(ir_data_access_get_type(&unary_op->source));
            assert(operand_type->type == Datatype_Type::PRIMITIVE, "Should not happen");
            instr.op3 = (int)type_base_to_bytecode_type(operand_type);
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

        auto& function_parameters = function->function_type->parameters;
        Array<Datatype*> parameter_types = array_create<Datatype*>(function_parameters.size);
        SCOPE_EXIT(array_destroy(&parameter_types));
        for (int i = 0; i < function_parameters.size; i++) {
            parameter_types[i] = function_parameters[i].type;
        }
        auto dyn_param_types = array_to_dynamic_array(&parameter_types);

        int parameter_stack_size = stack_offsets_calculate(&dyn_param_types, parameter_offsets, 0);
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

void bytecode_generator_reset(Bytecode_Generator* generator, Compiler* compiler)
{
    generator->ir_program = compiler->ir_generator->program;
    generator->compiler = compiler;
    // Reset previous code
    {
        dynamic_array_reset(&generator->instructions);
        // Reset information
        hashtable_reset(&generator->function_locations);
        hashtable_reset(&generator->code_block_register_stack_offset_index);
        hashtable_reset(&generator->function_parameter_stack_offset_index);
        for (int i = 0; i < generator->stack_offsets.size; i++) {
            dynamic_array_destroy(&generator->stack_offsets[i]);
        }
        dynamic_array_reset(&generator->stack_offsets);
        // Reset fill outs
        dynamic_array_reset(&generator->label_locations);
        dynamic_array_reset(&generator->fill_out_gotos);
        dynamic_array_reset(&generator->fill_out_calls);
    }
}

void bytecode_generator_compile_function(Bytecode_Generator* generator, IR_Function* function)
{
    assert(hashtable_find_element(&generator->function_locations, function) == 0, "");
    Dynamic_Array<int> parameter_stack_offsets = dynamic_array_create<int>(16);
    dynamic_array_push_back(&generator->stack_offsets, parameter_stack_offsets);
    hashtable_insert_element(&generator->function_parameter_stack_offset_index, function, generator->stack_offsets.size - 1);
    bytecode_generator_generate_function_code(generator, function);
}

void bytecode_generator_update_references(Bytecode_Generator* generator)
{
    // Fill out all function calls
    for (int i = 0; i < generator->fill_out_calls.size; i++) {
        Function_Reference& call_loc = generator->fill_out_calls[i];
        int* location = hashtable_find_element(&generator->function_locations, call_loc.function);
        assert(location != 0, "Should not happen");
        generator->instructions[call_loc.instruction_index].op1 = *location;
    }
    dynamic_array_reset(&generator->fill_out_calls);

    // Fill out all open gotos
    for (int i = 0; i < generator->fill_out_gotos.size; i++) {
        Goto_Label fill_out = generator->fill_out_gotos[i];
        generator->instructions[fill_out.jmp_instruction].op1 = generator->label_locations[fill_out.label_index];
    }
    dynamic_array_reset(&generator->fill_out_gotos);
}

void bytecode_generator_set_entry_function(Bytecode_Generator* generator)
{
    int* entry_found = hashtable_find_element(&generator->function_locations, generator->ir_program->entry_function);
    assert(entry_found != 0, "");
    generator->entry_point_index = *entry_found;
    assert(generator->entry_point_index >= 0 && generator->entry_point_index < generator->instructions.size, "");
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
        hardcoded_type_append_to_string(string, (Hardcoded_Type)i.op1);
        string_append_formated(string, ", arg-start-offset: %d", i.op2);
        break;
    case Instruction_Type::RETURN:
        string_append_formated(string, "RETURN                       value-reg: %d, return-size: %d", i.op1, i.op2);
        break;
    case Instruction_Type::EXIT:
        string_append_formated(string, "EXIT                         exit-code: ");
        exit_code_append_to_string(string, (Exit_Code)i.op1);
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
    case Instruction_Type::LOAD_CONSTANT_ADDRESS:
        string_append_formated(string, "LOAD_CONSTANT_ADDRESS        dst: %d, constant-offset: %d", i.op1, i.op2);
        break;
    case Instruction_Type::LOAD_FUNCTION_LOCATION:
        string_append_formated(string, "LOAD_FUNCTION_LOCATION       dst: %d, func-start-instr: %d", i.op1, i.op2);
        break;
    case Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE:
        string_append_formated(string, "CAST_INTEGER_DIFFERENT_SIZE  dst: %d, src: %d, dst-primitive-type: %s, src-primitive-type: %s",
            i.op1, i.op2, bytecode_type_as_string((Bytecode_Type)i.op3), bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE:
        string_append_formated(string, "CAST_FLOAT_DIFFERENT_SIZE    dst: %d, src: %d, dst-primitive-type: %s, src-primitive-type: %s",
            i.op1, i.op2, bytecode_type_as_string((Bytecode_Type)i.op3), bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::CAST_FLOAT_INTEGER:
        string_append_formated(string, "CAST_FLOAT_INTEGER           dst: %d, src: %d, dst-primitive-type: %s, src-primitive-type: %s",
            i.op1, i.op2, bytecode_type_as_string((Bytecode_Type)i.op3), bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::CAST_INTEGER_FLOAT:
        string_append_formated(string, "CAST_INTEGER_FLOAT           dst: %d, src: %d, dst-primitive-type: %s, src-primitive-type: %s",
            i.op1, i.op2, bytecode_type_as_string((Bytecode_Type)i.op3), bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_ADDITION:
        string_append_formated(string, "BINARY_OP_ADDITION           dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_SUBTRACTION:
        string_append_formated(string, "BINARY_OP_SUBTRACTION        dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_MULTIPLICATION:
        string_append_formated(string, "BINARY_OP_MULTIPLICATION     dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_DIVISION:
        string_append_formated(string, "BINARY_OP_DIVISION           dst %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_EQUAL:
        string_append_formated(string, "BINARY_OP_EQUAL              dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_NOT_EQUAL:
        string_append_formated(string, "BINARY_OP_NOT_EQUAL          dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_GREATER_THAN:
        string_append_formated(string, "BINARY_OP_GREATER_THAN       dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_GREATER_EQUAL:
        string_append_formated(string, "BINARY_OP_GREATER_EQUAL      dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_LESS_THAN:
        string_append_formated(string, "BINARY_OP_LESS_THAN          dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_LESS_EQUAL:
        string_append_formated(string, "BINARY_OP_LESS_EQUAL         dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_MODULO:
        string_append_formated(string, "BINARY_OP_MODULO             dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_AND:
        string_append_formated(string, "BINARY_OP_AND                dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_OR:
        string_append_formated(string, "BINARY_OP_OR                 dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::UNARY_OP_NEGATE:
        string_append_formated(string, "UNARY_OP_NEGATE              dst: %d, src: %d, type: %s",
            i.op1, i.op2, bytecode_type_as_string((Bytecode_Type)i.op3)
        );
        break;
    case Instruction_Type::UNARY_OP_NOT:
        string_append_formated(string, "UNARY_OP_NOT                 dst: %d, src: %d, type: %s",
            i.op1, i.op2, bytecode_type_as_string((Bytecode_Type)i.op3)
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
    string_append_formated(string, "Code: \n");
    for (int i = 0; i < generator->instructions.size; i++)
    {
        Bytecode_Instruction& instruction = generator->instructions[i];
        string_append_formated(string, "%4d: ", i);
        bytecode_instruction_append_to_string(string, generator->instructions[i]);
        string_append_formated(string, "\n");
    }
}

