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
    result.stack_offsets = dynamic_array_create<Array<int>>(32);

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
        array_destroy(&generator->stack_offsets[i]);
    }
    dynamic_array_destroy(&generator->stack_offsets);

    // Fill outs
    dynamic_array_destroy(&generator->fill_out_gotos);
    dynamic_array_destroy(&generator->label_locations);
    dynamic_array_destroy(&generator->fill_out_calls);
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
            array_destroy(&generator->stack_offsets[i]);
        }
        dynamic_array_reset(&generator->stack_offsets);
        // Reset fill outs
        dynamic_array_reset(&generator->label_locations);
        dynamic_array_reset(&generator->fill_out_gotos);
        dynamic_array_reset(&generator->fill_out_calls);
    }
}




// Instructions
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



// Data Accesses
int data_access_read_value(Bytecode_Generator* generator, IR_Data_Access* access, int read_to_offset = -1);
int data_access_get_pointer_to_value(Bytecode_Generator* generator, IR_Data_Access* access, int pointer_stack_offset = -1);

int bytecode_generator_create_temporary_stack_offset(Bytecode_Generator* generator, Datatype* type) 
{
    assert(type->memory_info.available, "");
    auto& memory_info = type->memory_info.value;
    generator->current_stack_offset = align_offset_next_multiple(generator->current_stack_offset, memory_info.alignment);
    int result = generator->current_stack_offset;
    generator->current_stack_offset += memory_info.size;
    return result;
}

int data_access_is_completely_on_stack(Bytecode_Generator* generator, IR_Data_Access* access)
{
    switch (access->type)
    {
    case IR_Data_Access_Type::REGISTER: 
    {
        Array<int>* register_offsets = &generator->stack_offsets[
            *hashtable_find_element(&generator->code_block_register_stack_offset_index, access->option.register_access.definition_block)
        ];
        return register_offsets->data[access->option.register_access.index];
    }
    case IR_Data_Access_Type::PARAMETER: 
    {
        int* stack_offset_index = hashtable_find_element(
            &generator->function_parameter_stack_offset_index, access->option.parameter.function
        );
        Array<int>* parameter_offsets = &generator->stack_offsets[*stack_offset_index];
        return parameter_offsets->data[access->option.parameter.index];
    }
    case IR_Data_Access_Type::MEMBER_ACCESS:
    {
        int stack_addr = data_access_is_completely_on_stack(generator, access->option.member_access.struct_access);
        if (stack_addr == -1) return -1;
        return stack_addr + access->option.member_access.member.offset;
    }
    case IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS:
    {
        // Check that we access an array
        if (access->option.array_access.array_access->datatype->type != Datatype_Type::ARRAY) {
            return -1;
        }
        Datatype_Array* array_type = downcast<Datatype_Array>(access->option.array_access.array_access->datatype);

        // Check that index is known
        auto index_access = access->option.array_access.index_access;
        if (index_access->type != IR_Data_Access_Type::CONSTANT) {
            return -1;
        }

        auto& index_constant = compiler.constant_pool.constants[index_access->option.constant_index];
        if (!types_are_equal(index_constant.type, upcast(compiler.type_system.predefined_types.i32_type))) {
            return -1;
        }
        int index = *(int*)index_constant.memory;

        // Check that array value has a stack address
        int array_stack_offset = data_access_is_completely_on_stack(generator, access->option.array_access.array_access);
        if (array_stack_offset == -1) {
            return -1;
        }

        assert(array_type->count_known && index >= 0 && index < array_type->element_count, "");
        return array_stack_offset + index * array_type->element_type->memory_info.value.size;
    }
    default: return -1;
    }

    return -1;
}

int data_access_get_pointer_to_value(Bytecode_Generator* generator, IR_Data_Access* access, int pointer_stack_offset)
{
    Datatype* access_type = access->datatype;
    assert(access_type->memory_info.available, "");
    auto& memory_info = access_type->memory_info.value;
    auto& types = compiler.type_system.predefined_types;

    // Check if it's on stack
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
        }
        int stack_offset = data_access_is_completely_on_stack(generator, access);
        if (stack_offset != -1) {
            bytecode_generator_add_instruction(
                generator, instruction_make_2(Instruction_Type::LOAD_REGISTER_ADDRESS, pointer_stack_offset, stack_offset)
            );
            return pointer_stack_offset;
        }
    }

    switch (access->type)
    {
    case IR_Data_Access_Type::REGISTER: 
    case IR_Data_Access_Type::PARAMETER: 
        panic("Handled in previous code path");
    case IR_Data_Access_Type::CONSTANT: 
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
        }
        bytecode_generator_add_instruction(
            generator, instruction_make_2(Instruction_Type::LOAD_CONSTANT_ADDRESS, pointer_stack_offset, access->option.constant_index)
        );
        return pointer_stack_offset;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: 
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
        }
        bytecode_generator_add_instruction(
            generator, instruction_make_2(Instruction_Type::LOAD_GLOBAL_ADDRESS, pointer_stack_offset, access->option.global_index)
        );
        return pointer_stack_offset;
    }
    case IR_Data_Access_Type::ADDRESS_OF_VALUE: 
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
        }

        // This is a little weird, as we seem to want a pointer to a pointer now...
        int intermediate_ptr = bytecode_generator_create_temporary_stack_offset(generator, access_type);
        data_access_get_pointer_to_value(generator, access->option.address_of_value, intermediate_ptr);
        bytecode_generator_add_instruction(
            generator, instruction_make_2(Instruction_Type::LOAD_REGISTER_ADDRESS, pointer_stack_offset, intermediate_ptr)
        );
        return pointer_stack_offset;
    }
    case IR_Data_Access_Type::POINTER_DEREFERENCE: {
        // Just don't dereference the pointer if we want to have the pointer value
        return data_access_read_value(generator, access->option.pointer_value, pointer_stack_offset);
    }
    case IR_Data_Access_Type::MEMBER_ACCESS: 
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
        }

        data_access_get_pointer_to_value(generator, access->option.member_access.struct_access, pointer_stack_offset);
        bytecode_generator_add_instruction(
            generator, instruction_make_3(Instruction_Type::U64_ADD_CONSTANT_I32, pointer_stack_offset, pointer_stack_offset, access->option.member_access.member.offset)
        );
        return pointer_stack_offset;
    }
    case IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS: 
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
        }

        auto& array_access = access->option.array_access;
        auto array_type = datatype_get_non_const_type(array_access.array_access->datatype);
        if (array_type->type == Datatype_Type::SLICE) 
        {
            Datatype_Slice* slice = downcast<Datatype_Slice>(array_type);
            int slice_offset = data_access_read_value(generator, array_access.array_access, -1);
            int index_access = data_access_read_value(generator, array_access.index_access, -1);
            bytecode_generator_add_instruction(
                generator, instruction_make_4(
                    Instruction_Type::U64_MULTIPLY_ADD_I32, pointer_stack_offset, 
                    slice_offset, index_access, slice->element_type->memory_info.value.size
                )
            );
            return pointer_stack_offset;
        }
        else
        {
            assert(array_type->type == Datatype_Type::ARRAY, "");
            Datatype* element_type = downcast<Datatype_Array>(array_type)->element_type;
            data_access_get_pointer_to_value(generator, array_access.array_access, pointer_stack_offset);
            int index_access = data_access_read_value(generator, array_access.index_access, -1);
            bytecode_generator_add_instruction(
                generator, instruction_make_4(
                    Instruction_Type::U64_MULTIPLY_ADD_I32, pointer_stack_offset, 
                    pointer_stack_offset, index_access, element_type->memory_info.value.size
                )
            );
            return pointer_stack_offset;
        }

        break;
    }
    default: panic("Should not happen");
    }

    panic("");
    return -1;
}

// If read_to_offset == -1, then a temporary value is generated (temporary stack offset), and this value is returned
// Otherwise the the read will overwrite the value at read_to_offset
int data_access_read_value(Bytecode_Generator* generator, IR_Data_Access* access, int read_to_offset)
{
    Datatype* access_type = access->datatype;
    assert(access_type->memory_info.available, "");
    auto& memory_info = access_type->memory_info.value;
    auto& types = compiler.type_system.predefined_types;

    // Check if it's on stack
    {
        int stack_offset = data_access_is_completely_on_stack(generator, access);
        if (stack_offset != -1) 
        {
            if (read_to_offset == -1) {
                return stack_offset;
            }
            bytecode_generator_add_instruction(
                generator, instruction_make_3(Instruction_Type::MOVE_STACK_DATA, read_to_offset, stack_offset, memory_info.size)
            );
            return read_to_offset;
        }
    }

    switch (access->type)
    {
    case IR_Data_Access_Type::REGISTER: 
    case IR_Data_Access_Type::PARAMETER: 
        panic("Handled in previous code path");
    case IR_Data_Access_Type::CONSTANT: 
    {
        int dst = read_to_offset;
        if (dst == -1) {
            dst = bytecode_generator_create_temporary_stack_offset(generator, access_type);
        }

        Bytecode_Instruction load_instruction;
        load_instruction.instruction_type = Instruction_Type::READ_CONSTANT;
        load_instruction.op1 = dst;
        load_instruction.op2 = access->option.constant_index;
        load_instruction.op3 = memory_info.size;
        bytecode_generator_add_instruction(generator, load_instruction);
        return load_instruction.op1;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: 
    {
        int dst = read_to_offset;
        if (dst == -1) {
            dst = bytecode_generator_create_temporary_stack_offset(generator, access_type);
        }

        Bytecode_Instruction load_instruction;
        load_instruction.instruction_type = Instruction_Type::READ_GLOBAL;
        load_instruction.op1 = dst;
        load_instruction.op2 = access->option.global_index;
        load_instruction.op3 = memory_info.size;
        bytecode_generator_add_instruction(generator, load_instruction);
        return load_instruction.op1;
    }
    case IR_Data_Access_Type::ADDRESS_OF_VALUE: 
    {
        int dst = read_to_offset;
        if (dst == -1) {
            dst = bytecode_generator_create_temporary_stack_offset(generator, access_type);
        }

        // Handle address of constant and global data
        switch (access->option.address_of_value->type)
        {
        case IR_Data_Access_Type::CONSTANT: 
        {
            Bytecode_Instruction load_instruction;
            load_instruction.instruction_type = Instruction_Type::LOAD_CONSTANT_ADDRESS;
            load_instruction.op1 = dst;
            load_instruction.op2 = access->option.address_of_value->option.constant_index;
            load_instruction.op3 = memory_info.size;
            bytecode_generator_add_instruction(generator, load_instruction);
            return load_instruction.op1;
        }
        case IR_Data_Access_Type::GLOBAL_DATA: {
            Bytecode_Instruction load_instruction;
            load_instruction.instruction_type = Instruction_Type::LOAD_GLOBAL_ADDRESS;
            load_instruction.op1 = dst;
            load_instruction.op2 = access->option.address_of_value->option.global_index;
            load_instruction.op3 = memory_info.size;
            bytecode_generator_add_instruction(generator, load_instruction);
            return load_instruction.op1;
        }
        default: break;
        }

        return data_access_get_pointer_to_value(generator, access->option.address_of_value, dst);
    }
    case IR_Data_Access_Type::POINTER_DEREFERENCE: 
    {
        int dst = read_to_offset;
        if (dst == -1) {
            dst = bytecode_generator_create_temporary_stack_offset(generator, access_type);
        }

        Bytecode_Instruction read_instr;
        read_instr.instruction_type = Instruction_Type::READ_MEMORY;
        read_instr.op1 = dst;
        read_instr.op2 = data_access_read_value(generator, access->option.pointer_value);
        read_instr.op3 = memory_info.size;
        bytecode_generator_add_instruction(generator, read_instr);
        return read_instr.op1;
    }
    case IR_Data_Access_Type::MEMBER_ACCESS: 
    case IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS: 
    {
        int dst = read_to_offset;
        if (dst == -1) {
            dst = bytecode_generator_create_temporary_stack_offset(generator, access_type);
        }

        int value_address = data_access_get_pointer_to_value(generator, access, -1);
        bytecode_generator_add_instruction(generator,
            instruction_make_3(Instruction_Type::READ_MEMORY, dst, value_address, memory_info.size)
        );
        return dst;
    }
    default: panic("Should not happen");
    }

    panic("");
    return -1;
}

void bytecode_generator_add_instruction_and_set_destination(Bytecode_Generator* generator, IR_Data_Access* destination, Bytecode_Instruction instr)
{
    Datatype* type = destination->datatype;
    assert(type->memory_info.available, "");
    auto& memory_info = type->memory_info.value;

    // Check if destination is on stack
    {
        int stack_offset = data_access_is_completely_on_stack(generator, destination);
        if (stack_offset != -1) 
        {
            instr.op1 = stack_offset;
            bytecode_generator_add_instruction(generator, instr);
            return;
        }
    }

    // Otherwise we need to write the result to the address of the destination
    int value_offset = bytecode_generator_create_temporary_stack_offset(generator, type);
    instr.op1 = value_offset;
    bytecode_generator_add_instruction(generator, instr);

    int pointer_offset = data_access_get_pointer_to_value(generator, destination, -1);
    bytecode_generator_add_instruction(generator, instruction_make_3(Instruction_Type::WRITE_MEMORY, pointer_offset, value_offset, memory_info.size));
    return;
}

void bytecode_generator_move_accesses(Bytecode_Generator* generator, IR_Data_Access* destination, IR_Data_Access* source)
{
    Datatype* move_type = destination->datatype;
    assert(move_type->memory_info.available, "");
    auto& memory_info = move_type->memory_info.value;

    int move_byte_size = memory_info.size;
    int source_offset = data_access_read_value(generator, source);
    Bytecode_Instruction instr = instruction_make_3(Instruction_Type::MOVE_STACK_DATA, 0, source_offset, move_byte_size);
    bytecode_generator_add_instruction_and_set_destination(generator, destination, instr);
}



// Code Generation
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

void push_exit_instruction(Bytecode_Generator* generator, Exit_Code exit_code)
{
    const char* msg = exit_code.error_msg;
    u64 address = (u64)msg;
    int lower_32bit = (int) (address & 0xFFFFFFFF);
    int high_32_bit = (int) ((address >> 32) & 0xFFFFFFFF);
    bytecode_generator_add_instruction(generator,
        instruction_make_3(Instruction_Type::EXIT, (int)exit_code.type, lower_32bit, high_32_bit)
    );
}

Exit_Code exit_code_from_exit_instruction(const Bytecode_Instruction& exit_instr)
{
    assert(exit_instr.instruction_type == Instruction_Type::EXIT, "");
    Exit_Code result;
    result.type = (Exit_Code_Type)exit_instr.op1;

    u64 address = ((u64)(u32)exit_instr.op2) | (((u64)(u32)exit_instr.op3) << 32);
    result.error_msg = (const char*)address;
    return result;
}

void bytecode_generator_generate_code_block(Bytecode_Generator* generator, IR_Code_Block* code_block)
{
    // Generate Stack offsets
    int rewind_stack_offset = generator->current_stack_offset;
    SCOPE_EXIT(generator->current_stack_offset = rewind_stack_offset);
    {
        // Calcuate offsets for each register
        Array<int> register_stack_offsets = array_create<int>(code_block->registers.size);
        int stack_offset = generator->current_stack_offset;
        for (int i = 0; i < code_block->registers.size; i++)
        {
            Datatype* signature = code_block->registers[i].type;
            assert(signature->memory_info.available, "");
            auto& memory_info = signature->memory_info.value;

            stack_offset = align_offset_next_multiple(stack_offset, memory_info.alignment);
            register_stack_offsets[i] = stack_offset;
            stack_offset += memory_info.size;
        }

        // Store offset info
        generator->current_stack_offset = stack_offset;
        dynamic_array_push_back(&generator->stack_offsets, register_stack_offsets);
        hashtable_insert_element(&generator->code_block_register_stack_offset_index, code_block, generator->stack_offsets.size - 1);
    }

    auto& types = compiler.type_system.predefined_types;
    const int PLACEHOLDER = 0;
    // Generate instructions
    for (int i = 0; i < code_block->instructions.size; i++)
    {
        IR_Instruction* instr = &code_block->instructions[i];

        // Not entirely sure if I can do this after every instruction, but I think so, 
        // as "communication" between instructions can only happen through data-accesses,
        // and not through temporary values
        int rewind_stack_offset = generator->current_stack_offset;
        SCOPE_EXIT(generator->current_stack_offset = rewind_stack_offset);

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
                function_sig = call->options.function->signature;
                break;
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
                function_sig = downcast<Datatype_Function>(datatype_get_non_const_type(call->options.pointer_access->datatype));
                function_pointer_stack_offset = data_access_read_value(generator, call->options.pointer_access);
                break;
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
                function_sig = hardcoded_type_to_signature(call->options.hardcoded);
                break;
            default: panic("Error");
            }

            // Prepare new stack frame
            generator->current_stack_offset = align_offset_next_multiple(generator->current_stack_offset, 16); // 16 byte should be the highest possible alignment
            int stack_frame_start_offset = bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer);
            bytecode_generator_create_temporary_stack_offset(generator, types.byte_pointer); // For previous stack address

            // Push all arguments as parameters into new stack-frame
            int next_argument_offset = generator->current_stack_offset;
            for (int i = 0; i < function_sig->parameters.size; i++)
            {
                Datatype* parameter_sig = function_sig->parameters[i].type;
                assert(parameter_sig->memory_info.available, "");
                auto& param_memory = parameter_sig->memory_info.value;

                next_argument_offset = align_offset_next_multiple(next_argument_offset, param_memory.alignment);
                generator->current_stack_offset = next_argument_offset; // I think I'm allowed to reset the temporaray values from previous param here
                data_access_read_value(generator, call->arguments[i], next_argument_offset);
                next_argument_offset += param_memory.size;
            }
            generator->current_stack_offset = next_argument_offset;

            // Generate call instruction
            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL: 
            {
                // Handle extern functions
                if (call->options.function->function_type == ModTree_Function_Type::EXTERN) {
                    push_exit_instruction(generator, exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot call extern functions in bytecode"));
                    break;
                }

                auto& slots = compiler.semantic_analyser->function_slots;
                auto ir_function = slots[call->options.function->function_slot_index].ir_function;
                assert(ir_function != nullptr, "");
                
                Function_Reference call_ref;
                call_ref.function = ir_function;
                call_ref.instruction_index = bytecode_generator_add_instruction(generator,
                    instruction_make_2(Instruction_Type::CALL_FUNCTION, 0, stack_frame_start_offset)
                );
                dynamic_array_push_back(&generator->fill_out_calls, call_ref);
                break;
            }
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL: {
                assert(function_pointer_stack_offset != -1, "");
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::CALL_FUNCTION_POINTER, function_pointer_stack_offset, stack_frame_start_offset)
                );
                break;
            }
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
                bytecode_generator_add_instruction(generator,
                    instruction_make_2(Instruction_Type::CALL_HARDCODED_FUNCTION, (i32)call->options.hardcoded, stack_frame_start_offset)
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
            int condition_stack_offset = data_access_read_value(generator, switch_instr->condition_access);

            Dynamic_Array<int> case_jump_indices = dynamic_array_create<int>(switch_instr->cases.size);
            Dynamic_Array<int> jmp_to_switch_end_indices = dynamic_array_create<int>(switch_instr->cases.size + 1);
            SCOPE_EXIT(dynamic_array_destroy(&case_jump_indices));
            SCOPE_EXIT(dynamic_array_destroy(&jmp_to_switch_end_indices));

            for (int i = 0; i < switch_instr->cases.size; i++)
            {
                IR_Switch_Case* switch_case = &switch_instr->cases[i];
                dynamic_array_push_back(&case_jump_indices, 
                    bytecode_generator_add_instruction(generator,
                        instruction_make_3(Instruction_Type::JUMP_ON_INT_EQUAL, PLACEHOLDER, condition_stack_offset, switch_case->value)
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
            int condition_stack_offset = data_access_read_value(generator, if_instr->condition);
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
            // Note: Even though generate_code_block resets the temporaray_stack_offset,
            // it should still be possible to read the condition value right afterwards, as no other instruction may overwrite it in the meantime
            int condition_stack_offset = data_access_read_value(generator, while_instr->condition_access);
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
                push_exit_instruction(generator, return_instr->options.exit_code);
                break;
            }
            case IR_Instruction_Return_Type::RETURN_DATA: {
                Datatype* return_sig = return_instr->options.return_value->datatype;
                assert(return_sig->memory_info.available, "");
                bytecode_generator_add_instruction(
                    generator,
                    instruction_make_2(Instruction_Type::RETURN,
                        data_access_read_value(generator, return_instr->options.return_value),
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
        case IR_Instruction_Type::VARIABLE_DEFINITION: {
            auto& var = instr->options.variable_definition;
            if (var.initial_value.available) {
                bytecode_generator_move_accesses(generator, var.variable_access, var.initial_value.value);
            }
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
                Datatype* cast_source = cast->source->datatype;
                Datatype* cast_destination = cast->destination->datatype;
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
                        data_access_read_value(generator, cast->source),
                        (int)type_base_to_bytecode_type(cast_destination), (int)type_base_to_bytecode_type(cast_source)
                    )
                );
                break;
            }
            default: panic("");
            }
            break;
        }
        case IR_Instruction_Type::FUNCTION_ADDRESS:
        {
            IR_Instruction_Function_Address* function_address = &instr->options.function_address;
            auto& slots = compiler.semantic_analyser->function_slots;
            auto& slot = slots[function_address->function_slot_index];

            if (slot.modtree_function != nullptr && slot.modtree_function->function_type == ModTree_Function_Type::EXTERN) {
                push_exit_instruction(generator, exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot take address of extern function"));
                break;
            }

            bytecode_generator_add_instruction_and_set_destination(generator,
                function_address->destination, instruction_make_2(
                    Instruction_Type::LOAD_FUNCTION_LOCATION, PLACEHOLDER, (int)function_address->function_slot_index)
            );
            break;
        }
        case IR_Instruction_Type::BINARY_OP:
        {
            IR_Instruction_Binary_OP* binary_op = &instr->options.binary_op;
            Bytecode_Instruction instr;
            using AST::Binop;
            switch (binary_op->type)
            {
            case IR_Binop::ADDITION: instr.instruction_type = Instruction_Type::BINARY_OP_ADDITION; break;
            case IR_Binop::SUBTRACTION: instr.instruction_type = Instruction_Type::BINARY_OP_SUBTRACTION; break;
            case IR_Binop::DIVISION: instr.instruction_type = Instruction_Type::BINARY_OP_DIVISION; break;
            case IR_Binop::MULTIPLICATION: instr.instruction_type = Instruction_Type::BINARY_OP_MULTIPLICATION; break;
            case IR_Binop::MODULO: instr.instruction_type = Instruction_Type::BINARY_OP_MODULO; break;
            case IR_Binop::AND: instr.instruction_type = Instruction_Type::BINARY_OP_AND; break;
            case IR_Binop::OR: instr.instruction_type = Instruction_Type::BINARY_OP_OR; break;
            case IR_Binop::BITWISE_AND: instr.instruction_type = Instruction_Type::BINARY_OP_BITWISE_AND; break;
            case IR_Binop::BITWISE_OR: instr.instruction_type = Instruction_Type::BINARY_OP_BITWISE_OR; break;
            case IR_Binop::BITWISE_XOR: instr.instruction_type = Instruction_Type::BINARY_OP_BITWISE_XOR; break;
            case IR_Binop::BITWISE_SHIFT_LEFT: instr.instruction_type = Instruction_Type::BINARY_OP_BITWISE_SHIFT_LEFT; break;
            case IR_Binop::BITWISE_SHIFT_RIGHT: instr.instruction_type = Instruction_Type::BINARY_OP_BITWISE_SHIFT_RIGHT; break;
            case IR_Binop::EQUAL: instr.instruction_type = Instruction_Type::BINARY_OP_EQUAL; break;
            case IR_Binop::NOT_EQUAL: instr.instruction_type = Instruction_Type::BINARY_OP_NOT_EQUAL; break;
            case IR_Binop::LESS: instr.instruction_type = Instruction_Type::BINARY_OP_LESS_THAN; break;
            case IR_Binop::LESS_OR_EQUAL: instr.instruction_type = Instruction_Type::BINARY_OP_LESS_EQUAL; break;
            case IR_Binop::GREATER: instr.instruction_type = Instruction_Type::BINARY_OP_GREATER_THAN; break;
            case IR_Binop::GREATER_OR_EQUAL: instr.instruction_type = Instruction_Type::BINARY_OP_GREATER_EQUAL; break;
            default: panic("");
            }

            Datatype* operand_types = datatype_get_non_const_type(binary_op->operand_left->datatype);
            if (datatype_is_pointer(operand_types)) {
                instr.op4 = (int)Bytecode_Type::INT64;
            }
            else {
                instr.op4 = (int)type_base_to_bytecode_type(operand_types);
            }
            instr.op2 = data_access_read_value(generator, binary_op->operand_left);
            instr.op3 = data_access_read_value(generator, binary_op->operand_right);
            bytecode_generator_add_instruction_and_set_destination(generator, binary_op->destination, instr);
            break;
        }
        case IR_Instruction_Type::UNARY_OP:
        {
            IR_Instruction_Unary_OP* unary_op = &instr->options.unary_op;
            Bytecode_Instruction instr;
            switch (unary_op->type)
            {
            case IR_Unop::NEGATE:
                instr.instruction_type = Instruction_Type::UNARY_OP_NEGATE;
                break;
            case IR_Unop::NOT:
                instr.instruction_type = Instruction_Type::UNARY_OP_NOT;
                break;
            case IR_Unop::BITWISE_NOT:
                instr.instruction_type = Instruction_Type::UNARY_OP_BITWISE_NOT;
                break;
            default: panic("");
            }

            Datatype* operand_type = datatype_get_non_const_type(unary_op->source->datatype);
            assert(operand_type->type == Datatype_Type::PRIMITIVE, "Should not happen");
            instr.op3 = (int)type_base_to_bytecode_type(operand_type);
            instr.op2 = data_access_read_value(generator, unary_op->source);
            bytecode_generator_add_instruction_and_set_destination(generator, unary_op->destination, instr);
            break;
        }
        }
    }
}

void bytecode_generator_compile_function(Bytecode_Generator* generator, IR_Function* function)
{
    assert(hashtable_find_element(&generator->function_locations, function) == nullptr, "Function must not be compiled yet");

    // Generate parameter offsets
    {
        auto& function_parameters = function->function_type->parameters;
        Array<int> parameter_offsets = array_create<int>(function_parameters.size);

        int stack_offset = 16; // Stack starts with [Return_Address] [Old_Stack_Pointer], then parameters
        for (int i = 0; i < function_parameters.size; i++)
        {
            Datatype* signature = function_parameters[i].type;
            assert(signature->memory_info.available, "");
            auto& memory_info = signature->memory_info.value;

            stack_offset = align_offset_next_multiple(stack_offset, memory_info.alignment);
            parameter_offsets[i] = stack_offset;
            stack_offset += memory_info.size;
        }
        generator->current_stack_offset = stack_offset;

        dynamic_array_push_back(&generator->stack_offsets, parameter_offsets);
        hashtable_insert_element(&generator->function_parameter_stack_offset_index, function, generator->stack_offsets.size - 1);
    }

    // Register function
    hashtable_insert_element(&generator->function_locations, function, generator->instructions.size);

    // Generate code
    bytecode_generator_generate_code_block(generator, function->code);
    if (generator->current_stack_offset > generator->maximum_function_stack_depth) {
        generator->maximum_function_stack_depth = generator->current_stack_offset;
    }
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
    case Instruction_Type::JUMP_ON_INT_EQUAL:
        string_append_formated(string, "JUMP_ON_INT_EQUAL            instr-nr: %d, cond_reg: %d, equal_value: %d", i.op1, i.op2, i.op3);
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
    case Instruction_Type::EXIT: {
        string_append_formated(string, "EXIT                         Code: ");
        Exit_Code code = exit_code_from_exit_instruction(instruction);
        exit_code_append_to_string(string, code);
        break;
    }
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

    case Instruction_Type::BINARY_OP_BITWISE_AND:
        string_append_formated(string, "BINARY_BITWISE_AND           dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_BITWISE_OR:
        string_append_formated(string, "BINARY_OP_BITWISE_OR         dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_BITWISE_XOR:
        string_append_formated(string, "BINARY_OP_BITWISE_XOR        dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_BITWISE_SHIFT_LEFT:
        string_append_formated(string, "BINARY_OP_BITWISE_SHIFT_LEFT dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;
    case Instruction_Type::BINARY_OP_BITWISE_SHIFT_RIGHT:
        string_append_formated(string, "BINARY_OP_BITWISE_SHIFT_RIGHT dst: %d, left: %d, right: %d, type: %s",
            i.op1, i.op2, i.op3, bytecode_type_as_string((Bytecode_Type)i.op4)
        );
        break;

    case Instruction_Type::UNARY_OP_BITWISE_NOT:
        string_append_formated(string, "UNARY_OP_BITWISE_NOT         dst: %d, src: %d, type: %s",
            i.op1, i.op2, bytecode_type_as_string((Bytecode_Type)i.op3)
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

