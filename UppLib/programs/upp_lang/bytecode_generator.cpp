#include "bytecode_generator.hpp"

#include "compilation_data.hpp"
#include "../../utility/hash_functions.hpp"
#include "ir_code.hpp"
#include "ast.hpp"

struct Goto_Label
{
    int jmp_instruction;
    int label_index;
};

// Either parameter or register
struct Stack_Location
{
    void* address; // Either Upp_Function* or IR_Register*
    int index;
};

Stack_Location stack_location_make_parameter(Upp_Function* function, int index) {
    Stack_Location result;
    result.address = (void*)function;
    result.index = index;
    return result;
}

Stack_Location stack_location_make_register(IR_Code_Block* ir_block, int index) {
    Stack_Location result;
    result.address = (void*)ir_block;
    result.index = index;
    return result;
}

u64 hash_stack_location(Stack_Location* loc) {
    return hash_combine(hash_pointer(loc->address), hash_i32(&loc->index));
}

bool equals_stack_location(Stack_Location* a, Stack_Location* b) {
    return a->address == b->address && a->index == b->index;
}

struct Bytecode_Generator
{
    Compilation_Data* compilation_data;

    Upp_Function* function;
    IR_Code_Block* current_block;
    int current_stack_offset;

    DynTable<Stack_Location, int> stack_locations; // For registers and parameters/return-value
    DynTable<IR_Code_Block*, int> continue_location;
    DynTable<IR_Code_Block*, int> break_location;
    DynTable<int, int> label_locations;
    DynArray<Goto_Label> fill_out_gotos;
};

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
    generator->compilation_data->bytecode.push_back(instruction);
    return generator->compilation_data->bytecode.size - 1;
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
    auto& types = generator->compilation_data->type_system->predefined_types;

    switch (access->type)
    {
    case IR_Data_Access_Type::REGISTER: 
    {
        return *generator->stack_locations.find(
            stack_location_make_register(access->option.register_access.definition_block, access->option.register_access.index)
        );
    }
    case IR_Data_Access_Type::PARAMETER: 
    {
        return *generator->stack_locations.find(
            stack_location_make_parameter(access->option.parameter.function, access->option.parameter.index)
        );
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

        auto& index_constant = generator->compilation_data->constant_pool->constants[index_access->option.constant_index];
        if (!types_are_equal(index_constant.type, upcast(types.i32_type))) {
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
    case IR_Data_Access_Type::NON_DESTRUCTIVE_CAST:
    {
        return data_access_is_completely_on_stack(generator, access->option.non_destructive_cast.value_access);
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
    auto& types = generator->compilation_data->type_system->predefined_types;

    // Check if it's on stack
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, upcast(types.rawptr));
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
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, upcast(types.rawptr));
        }
        bytecode_generator_add_instruction(
            generator, instruction_make_2(Instruction_Type::LOAD_CONSTANT_ADDRESS, pointer_stack_offset, access->option.constant_index)
        );
        return pointer_stack_offset;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: 
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, upcast(types.rawptr));
        }
        bytecode_generator_add_instruction(
            generator, instruction_make_2(Instruction_Type::LOAD_GLOBAL_ADDRESS, pointer_stack_offset, access->option.global_index)
        );
        return pointer_stack_offset;
    }
    case IR_Data_Access_Type::ADDRESS_OF_VALUE: 
    {
        if (pointer_stack_offset == -1) {
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, upcast(types.rawptr));
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
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, upcast(types.rawptr));
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
            pointer_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, upcast(types.rawptr));
        }

        auto& array_access = access->option.array_access;
        auto array_type = array_access.array_access->datatype;
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
    case IR_Data_Access_Type::NON_DESTRUCTIVE_CAST: 
    {
        return data_access_get_pointer_to_value(generator, access->option.non_destructive_cast.value_access, pointer_stack_offset);
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
    auto& types = generator->compilation_data->type_system->predefined_types;

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
    case IR_Data_Access_Type::NON_DESTRUCTIVE_CAST: 
    {
        return data_access_read_value(generator, access->option.non_destructive_cast.value_access, read_to_offset);
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
Bytecode_Type datatype_to_bytecode_type(Datatype* datatype)
{
    if (datatype->type == Datatype_Type::PRIMITIVE)
    {
        switch (downcast<Datatype_Primitive>(datatype)->primitive_type)
        {
        case Primitive_Type::I8:  return Bytecode_Type::INT8;
        case Primitive_Type::I16: return Bytecode_Type::INT16;
        case Primitive_Type::I32: return Bytecode_Type::INT32;
        case Primitive_Type::I64: return Bytecode_Type::INT64;
        case Primitive_Type::U8:  return Bytecode_Type::UINT8;
        case Primitive_Type::U16: return Bytecode_Type::UINT16;
        case Primitive_Type::U32: return Bytecode_Type::UINT32;
        case Primitive_Type::U64: return Bytecode_Type::UINT64;
        case Primitive_Type::F32: return Bytecode_Type::FLOAT32;
        case Primitive_Type::F64: return Bytecode_Type::FLOAT64;
        case Primitive_Type::BOOLEAN: return Bytecode_Type::BOOL;
        default: panic("");
        }
    }
    else if (datatype->type == Datatype_Type::ENUM)
    {
        return Bytecode_Type::INT32; // Not sure if uint or int should be preferred
    }
    else if (datatype->type == Datatype_Type::BUILT_IN)
    {
        switch (downcast<Datatype_Builtin>(datatype)->builtin_type)
        {
        case Builtin_Type::RAWPTR:      return Bytecode_Type::UINT64;
        case Builtin_Type::CODE_POINT:  return Bytecode_Type::UINT32;
        case Builtin_Type::C_CHAR:      return Bytecode_Type::UINT8;
        case Builtin_Type::ISIZE:       return Bytecode_Type::INT64;
        case Builtin_Type::USIZE:       return Bytecode_Type::UINT64;
        case Builtin_Type::TYPE_HANDLE: return Bytecode_Type::UINT32;
        default: break;
        }
    }
    else if (datatype_is_pointer(datatype, true, true, true)) {
        return Bytecode_Type::UINT64;
    }

    panic("HEY");
    return Bytecode_Type::INT32;
}

int bytecode_type_get_byte_size(Bytecode_Type type)
{
    switch (type)
    {
    case Bytecode_Type::INT8: return 1;
    case Bytecode_Type::INT16: return 2;
    case Bytecode_Type::INT32: return 4;
    case Bytecode_Type::INT64: return 8;
    case Bytecode_Type::UINT8: return 1;
    case Bytecode_Type::UINT16: return 2;
    case Bytecode_Type::UINT32: return 4;
    case Bytecode_Type::UINT64: return 8;
    case Bytecode_Type::FLOAT32: return 4;
    case Bytecode_Type::FLOAT64: return 8;
    case Bytecode_Type::BOOL: return 1;
    default: panic("");
    }
    return -1;
}

const char* bytecode_type_as_string(Bytecode_Type type) 
{
    switch (type)
    {
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
    case Bytecode_Type::BOOL:
        return "BOOL";
    default: panic("HEY");
    }
    return "ERROR";
}

void push_exit_instruction(Bytecode_Generator* generator, Exit_Code exit_code)
{
    assert(sizeof(Exit_Code) <= sizeof(int) * 4, "");
    Bytecode_Instruction instr = instruction_make_0(Instruction_Type::EXIT);
    memory_copy(&instr.op1, &exit_code, sizeof(Exit_Code));
    generator->compilation_data->bytecode.push_back(instr);
}

Exit_Code exit_code_from_exit_instruction(Bytecode_Instruction& exit_instr)
{
    assert(exit_instr.instruction_type == Instruction_Type::EXIT, "");
    Exit_Code result;
    memory_copy(&result, &exit_instr.op1, sizeof(Exit_Code));
    return result;
}

void bytecode_generator_generate_code_block(Bytecode_Generator* generator, IR_Code_Block* code_block)
{
    auto compilation_data = generator->compilation_data;
    auto& types = generator->compilation_data->type_system->predefined_types;
    auto& instructions = generator->compilation_data->bytecode;
    int& stack_offset = generator->current_stack_offset;

    int rewind_stack_offset = generator->current_stack_offset;
    SCOPE_EXIT(
        generator->function->bytecode_maximum_stack_offset = math_maximum(
            generator->function->bytecode_maximum_stack_offset, generator->current_stack_offset
        );
        generator->current_stack_offset = rewind_stack_offset;
    );

    // Generate Stack offsets for registers
    for (int i = 0; i < code_block->registers.size; i++)
    {
        Datatype* reg_datatype = code_block->registers[i].type;
        int reg_offset = bytecode_generator_create_temporary_stack_offset(generator, reg_datatype);
        generator->stack_locations.insert(stack_location_make_register(code_block, i), reg_offset);
    }

    const int PLACEHOLDER = 0;
    // Generate instructions
    for (int i = 0; i < code_block->instructions.size; i++)
    {
        IR_Instruction* instr = &code_block->instructions[i];

        // Not entirely sure if I can do this after every instruction, but I think so, 
        // as "communication" between instructions can only happen through data-accesses,
        // and not through temporary values
        int rewind_stack_offset = generator->current_stack_offset;
        SCOPE_EXIT(
            generator->function->bytecode_maximum_stack_offset = math_maximum(
                generator->function->bytecode_maximum_stack_offset, generator->current_stack_offset);
            generator->current_stack_offset = rewind_stack_offset;
        );

        switch (instr->type)
        {
        case IR_Instruction_Type::FUNCTION_CALL:
        {
            IR_Instruction_Call* call = &instr->options.call;

            int function_pointer_stack_offset = -1;
            Call_Signature* signature = 0;
            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL:
                signature = call->options.function->signature; // Note: this must be a normal function
                break;
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
                signature = downcast<Datatype_Function_Pointer>(call->options.pointer_access->datatype)->signature;
                function_pointer_stack_offset = data_access_read_value(generator, call->options.pointer_access);
                break;
            case IR_Instruction_Call_Type::BUILTIN_CALL:
            {
                signature = compilation_data->hardcoded_function_signatures[
                    (int)ir_builtin_fn_to_hardcoded_type(call->options.builtin_fn)
                ];
                break;
            }
            default: panic("Error");
            }

            Optional<Datatype*> return_type = signature->return_type();

            // Prepare new stack frame
            int& stack_offset = generator->current_stack_offset;
            stack_offset = align_offset_next_multiple(stack_offset, 16); // Just to be sure that all parameters will be aligned correctly
            int stack_frame_start_offset = bytecode_generator_create_temporary_stack_offset(generator, upcast(types.rawptr)); // Return instruction-index
            bytecode_generator_create_temporary_stack_offset(generator, upcast(types.rawptr)); // Previous stack-frame

            // Allocate buffer for return-value
            int return_value_stack_offset = stack_offset;
            if (return_type.available) {
                return_value_stack_offset = bytecode_generator_create_temporary_stack_offset(generator, return_type.value);
            }

            // Push all arguments as parameters into new stack-frame
            for (int i = 0; i < signature->parameters.size && i != signature->return_type_index; i++)
            {
                Datatype* parameter_type = signature->parameters[i].datatype;
                int param_value_offset = bytecode_generator_create_temporary_stack_offset(generator, parameter_type);
                int rewind_to_offset = stack_offset;
                data_access_read_value(generator, call->arguments[i], param_value_offset);
                stack_offset = rewind_to_offset; // data_access_read_value may generate temporary stack-variables, so we need to reset this back to get correct stack-frame
            }

            // Generate call instruction
            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL: 
            {
                bytecode_generator_add_instruction(generator,
                    instruction_make_2(Instruction_Type::CALL_FUNCTION, call->options.function->function_index + 1, stack_frame_start_offset)
                );
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
            case IR_Instruction_Call_Type::BUILTIN_CALL:
                bytecode_generator_add_instruction(generator,
                    instruction_make_2(Instruction_Type::CALL_BUILTIN_FUNCTION, (i32)call->options.builtin_fn, stack_frame_start_offset)
                );
                break;
            default: panic("Error");
            }

            // Load return value to destination
            if (return_type.available) 
            {
                assert(signature->return_type().value->memory_info.available, "");
                bytecode_generator_add_instruction_and_set_destination(
                    generator, call->destination,
                    instruction_make_3(
                        Instruction_Type::MOVE_STACK_DATA, 
                        PLACEHOLDER, // Set to call->destination
                        return_value_stack_offset,
                        return_type.value->memory_info.value.size // Size of return-type
                    )
                );
            }
            break;
        }
        case IR_Instruction_Type::MATCH:
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
                instructions[case_jump_indices[i]].op1 = instructions.size;
                bytecode_generator_generate_code_block(generator, switch_case->block);
                dynamic_array_push_back(&jmp_to_switch_end_indices,
                    bytecode_generator_add_instruction(generator, instruction_make_1(Instruction_Type::JUMP, PLACEHOLDER))
                );
            }

            // Set jumps to end of switch
            for (int i = 0; i < jmp_to_switch_end_indices.size; i++) {
                instructions[jmp_to_switch_end_indices[i]].op1 = instructions.size;
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
            instructions[jmp_to_else_instr_index].op1 = instructions.size;
            bytecode_generator_generate_code_block(generator, if_instr->false_branch);
            instructions[jmp_over_else_instruction_index].op1 = instructions.size;
            break;
        }
        case IR_Instruction_Type::WHILE:
        {
            IR_Instruction_While* while_instr = &instr->options.while_instr;
            int condition_evaluation_start = instructions.size;
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
            instructions[jmp_to_end_instruction_index].op1 = instructions.size;

            break;
        }
        case IR_Instruction_Type::BLOCK:
            bytecode_generator_generate_code_block(generator, instr->options.block);
            break;
        case IR_Instruction_Type::LABEL: {
            generator->label_locations.insert(instr->options.label_index, instructions.size);
            break;
        }
        case IR_Instruction_Type::GOTO: {
            bytecode_generator_add_instruction(
                generator,
                instruction_make_1(Instruction_Type::JUMP, PLACEHOLDER)
            );
            Goto_Label fill_out;
            fill_out.jmp_instruction = instructions.size - 1 ;
            fill_out.label_index = instr->options.label_index;
            generator->fill_out_gotos.push_back(fill_out);
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
            case IR_Instruction_Return_Type::RETURN_DATA: 
            {
                Datatype* return_sig = return_instr->options.return_value->datatype;
                assert(return_sig->memory_info.available, "");

                int return_value_stack_offset = 16; // Return value is always after [return-instr] [stack_pointer]
                data_access_read_value(generator, return_instr->options.return_value, return_value_stack_offset);
                bytecode_generator_add_instruction(generator, instruction_make_0(Instruction_Type::RETURN));
                break;
            }
            case IR_Instruction_Return_Type::RETURN_EMPTY:
                bytecode_generator_add_instruction(generator, instruction_make_0(Instruction_Type::RETURN));
                break;
            }
            break;
        }
        case IR_Instruction_Type::OPERATION:
        {
            auto& operation = instr->options.operation;
            int param_count = ir_operation_parameter_count(operation.type);
            bytecode_generator_add_instruction_and_set_destination
            (
                generator,
                operation.destination,
                instruction_make_4(
                    Instruction_Type::IR_OPERATION, 
                    PLACEHOLDER,
                    data_access_read_value(generator, operation.operand_1),
                    (param_count == 1 ? 0 : data_access_read_value(generator, operation.operand_2)),
                    bytecode_pack_operation_and_types_to_int(
                        operation.type, 
                        datatype_to_bytecode_type(operation.destination->datatype),
                        datatype_to_bytecode_type(operation.operand_1->datatype),
                        (param_count == 1 ? Bytecode_Type::UINT64 : datatype_to_bytecode_type(operation.operand_2->datatype))
                    )
                )
            );
            break;
        }
        case IR_Instruction_Type::VARIABLE_DEFINITION: {
            auto& var = instr->options.variable_definition;
            if (var.initial_value.available) {
                bytecode_generator_move_accesses(generator, var.variable_access, var.initial_value.value);
            }
            break;
        }
        case IR_Instruction_Type::FUNCTION_ADDRESS:
        {
            IR_Instruction_Function_Address* function_address = &instr->options.function_address;
            bytecode_generator_add_instruction_and_set_destination(generator,
                function_address->destination, instruction_make_2(
                    Instruction_Type::LOAD_FUNCTION_LOCATION, PLACEHOLDER, function_address->function->function_index)
            );
            break;
        }
        }
    }
}

void bytecode_generator_compile_function(Compilation_Data* compilation_data, Upp_Function* function)
{
    if (function->bytecode_start_instruction != -1) return; // Function already generated
    assert(function->ir_block != nullptr, "ir-block must exist");

    Arena* tmp_arena = &compilation_data->tmp_arena;
    auto checkpoint = tmp_arena->make_checkpoint();
    SCOPE_EXIT(checkpoint.rewind());

    Bytecode_Generator generator;
    generator.compilation_data = compilation_data;
    generator.function = function;
    generator.current_block = nullptr;
    generator.current_stack_offset = 0;
    generator.stack_locations   = DynTable<Stack_Location, int>::create(tmp_arena, hash_stack_location, equals_stack_location);
    generator.continue_location = DynTable<IR_Code_Block*, int>::create_pointer(tmp_arena);
    generator.break_location    = DynTable<IR_Code_Block*, int>::create_pointer(tmp_arena);
	generator.fill_out_gotos    = DynArray<Goto_Label>::create(tmp_arena);
    generator.label_locations   = DynTable<int, int>::create(tmp_arena, hash_i32, equals_i32);

    auto& stack_offset = generator.current_stack_offset;
    auto& instructions = compilation_data->bytecode;

    // Generate parameter offsets
    {
        auto& function_parameters = function->signature->parameters;
        stack_offset = 16; // Stack starts with [Return_Address] [Old_Stack_Pointer], then parameters
        auto return_type_opt = function->signature->return_type();
        if (return_type_opt.available) {
            // Note: we don't store return-type in stack-locations, because we shouldn't be able to access it
            bytecode_generator_create_temporary_stack_offset(&generator, return_type_opt.value);
        }
        for (int i = 0; i < function_parameters.size; i++)
        {
            if (function->signature->return_type_index == i) continue;
            int param_offset = bytecode_generator_create_temporary_stack_offset(&generator, function_parameters[i].datatype);
            generator.stack_locations.insert(stack_location_make_parameter(function, i), param_offset);
        }
    }

    // Store function start
    function->bytecode_start_instruction = instructions.size;

    // Generate code
    bytecode_generator_generate_code_block(&generator, function->ir_block);
    function->bytecode_maximum_stack_offset = math_maximum(function->bytecode_maximum_stack_offset, stack_offset);

    // Store function end
    function->bytecode_end_instruction = instructions.size;

    // Fill out all open gotos
    for (int i = 0; i < generator.fill_out_gotos.size; i++) {
        Goto_Label& fill_out = generator.fill_out_gotos[i];
        instructions[fill_out.jmp_instruction].op1 = *generator.label_locations.find(fill_out.label_index);
    }
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
        string_append_formated(string, "READ_GLOBAL                  dst: %d, global_index: %d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::WRITE_GLOBAL:
        string_append_formated(string, "WRITE_GLOBAL                 global_index: %d, src: %d, size: %d", i.op1, i.op2, i.op3);
        break;
    case Instruction_Type::READ_CONSTANT:
        string_append_formated(string, "READ_CONSTANT                dst: %d, const_index: %d, size: %d", i.op1, i.op2, i.op3);
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
        string_append_formated(string, "CALL_FUNCTION                function-start-instr: %d, new-frame-offset: %d", i.op1, i.op2);
        break;
    case Instruction_Type::CALL_FUNCTION_POINTER:
        string_append_formated(string, "CALL_FUNCTION_POINTER        pointer-reg: %d, new-frame-offset: %d", i.op1, i.op2);
        break;
    case Instruction_Type::CALL_BUILTIN_FUNCTION:
        string_append_formated(string, "CALL_BUILTIN_FUNCTION      hardcoded_func_type:");
        string->append(hardcoded_type_get_info((Hardcoded_Type)i.op1).symbol_name);
        string_append_formated(string, ", new-frame-offset: %d", i.op2);
        break;
    case Instruction_Type::RETURN:
        string_append_formated(string, "RETURN                       ");
        break;
    case Instruction_Type::EXIT: {
        string_append_formated(string, "EXIT                         Code: ");
        Exit_Code code = exit_code_from_exit_instruction(instruction);
        exit_code_append_to_string(string, code);
        break;
    }
    case Instruction_Type::LOAD_REGISTER_ADDRESS:
        string_append_formated(string, "LOAD_REGISTER_ADDRESS        dst: %d, reg-to-load: %d", i.op1, i.op2);
        break;
    case Instruction_Type::LOAD_GLOBAL_ADDRESS:
        string_append_formated(string, "LOAD_GLOBAL_ADDRESS          dst: %d, global-index: %d", i.op1, i.op2);
        break;
    case Instruction_Type::LOAD_CONSTANT_ADDRESS:
        string_append_formated(string, "LOAD_CONSTANT_ADDRESS        dst: %d, constant-index: %d", i.op1, i.op2);
        break;
    case Instruction_Type::LOAD_FUNCTION_LOCATION:
        string_append_formated(string, "LOAD_FUNCTION_LOCATION       dst: %d, function-index: %d", i.op1, i.op2);
        break;
    case Instruction_Type::IR_OPERATION:
    {
        IR_Operation ir_op;
        Bytecode_Type left_type;
        Bytecode_Type right_type;
        Bytecode_Type dst_type;
        bytecode_unpack_operation_and_types_from_int(i.op4, ir_op, left_type, right_type, dst_type);
        string_append_formated(string, "IR_OPERATION                 dst: %d, src1: %d, src2: %d", i.op1, i.op2, i.op3);
        string_append_formated(string, "  %s, left_type: %s", ir_operation_as_string(ir_op), bytecode_type_as_string(left_type));
        if (ir_operation_parameter_count(ir_op) == 2) {
            string_append_formated(string, " right_type: %s", bytecode_type_as_string(right_type));
        }
        string_append_formated(string, " dst_type: %s", bytecode_type_as_string(dst_type));
        break;

    }
    default:
        string_append_formated(string, "FUCKING HELL\n");
        break;
    }
}

void bytecode_generator_append_bytecode_to_string(Compilation_Data* compilation_data, String* string)
{
    for (int i = 0; i < compilation_data->functions.size; i++)
    {
        Upp_Function* function = compilation_data->functions[i];
        string_append_formated(string, "Function #%d: \"%s\" \n", i, function->name->characters);
        if (function->bytecode_start_instruction == -1) {
            string_append(string, "NO instructions generated!\n");
            continue;
        }

        for (int i = function->bytecode_start_instruction; i < function->bytecode_end_instruction; i++)
        {
            Bytecode_Instruction& instruction = compilation_data->bytecode[i];
            string_append_formated(string, "%4d: ", i);
            bytecode_instruction_append_to_string(string, instruction);
            string_append_formated(string, "\n");
        }
        string_append_formated(string, "\n");
    }
}

int bytecode_pack_operation_and_types_to_int(IR_Operation operation, Bytecode_Type dst_type, Bytecode_Type left_type, Bytecode_Type right_type)
{
    u32 packed = 0;
    packed = (packed << 8) | (u32)operation;
    packed = (packed << 8) | (u32)dst_type;
    packed = (packed << 8) | (u32)left_type;
    packed = (packed << 8) | (u32)right_type;
    return (int)packed;
}

void bytecode_unpack_operation_and_types_from_int(
    int value, IR_Operation& out_op, Bytecode_Type &out_dst, Bytecode_Type& out_left, Bytecode_Type& out_right)
{
    u32 packed = (u32)value;

    u32 op_value    = (packed >> 24) & 0xFF;
    u32 dst_value   = (packed >> 16) & 0xFF;
    u32 left_value  = (packed >> 8 ) & 0xFF;
    u32 right_value = (packed >> 0 ) & 0xFF;

    out_op = (IR_Operation)op_value;
    out_dst   = (Bytecode_Type)dst_value;
    out_left  = (Bytecode_Type)left_value;
    out_right = (Bytecode_Type)right_value;
}
