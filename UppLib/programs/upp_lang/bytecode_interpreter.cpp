#include "bytecode_interpreter.hpp"

#include <iostream>
#include "../../utility/random.hpp"

Bytecode_Interpreter bytecode_intepreter_create()
{
    Bytecode_Interpreter result;
    result.stack = array_create_empty<byte>(8192);
    return result;
}

void bytecode_interpreter_destroy(Bytecode_Interpreter* interpreter) {
    array_destroy(&interpreter->stack);
}

// Returns true if we need to stop execution, e.g. on exit instruction
bool bytecode_interpreter_execute_current_instruction(Bytecode_Interpreter* interpreter)
{
    Bytecode_Instruction* i = interpreter->instruction_pointer;
    switch (i->instruction_type)
    {
    case Instruction_Type::MOVE_REGISTERS:
        memory_copy(interpreter->stack_pointer + i->op1, interpreter->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::WRITE_MEMORY:
        memory_copy(*(void**)(interpreter->stack_pointer + i->op1), interpreter->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_MEMORY: {
        void* result = *(void**)(interpreter->stack_pointer + i->op2);
        memory_copy(interpreter->stack_pointer + i->op1, *((void**)(interpreter->stack_pointer + i->op2)), i->op3);
        break;
    }
    case Instruction_Type::MEMORY_COPY:
        memory_copy(*(void**)(interpreter->stack_pointer + i->op1), *(void**)(interpreter->stack_pointer + i->op2), i->op3);
        break;
    case Instruction_Type::U64_ADD_CONSTANT_I32:
        *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) + (i->op3);
        break;
    case Instruction_Type::U64_MULTIPLY_ADD_I32: {
        u64 offset = (u64)((*(u32*)(interpreter->stack_pointer + i->op3)) * (u64)i->op4);
        if ((i32)offset < 0) {
            interpreter->exit_code = Exit_Code::OUT_OF_BOUNDS;
            return true;
        }
        *(u64**)(interpreter->stack_pointer + i->op1) = (u64*)(*(byte**)(interpreter->stack_pointer + i->op2) + offset);
        break;
    }
    case Instruction_Type::JUMP:
        interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];
        return false;
    case Instruction_Type::JUMP_ON_TRUE:
        if (*(interpreter->stack_pointer + i->op2) != 0) {
            interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];
            return false;
        }
        break;
    case Instruction_Type::JUMP_ON_FALSE:
        if (*(interpreter->stack_pointer + i->op2) == 0) {
            interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];
            return false;
        }
        break;
    case Instruction_Type::CALL: {
        if (&interpreter->stack[interpreter->stack.size-1] - interpreter->stack_pointer < interpreter->generator->maximum_function_stack_depth) {
            interpreter->exit_code = Exit_Code::STACK_OVERFLOW;
            return true;
        }
        byte* base_pointer = interpreter->stack_pointer;
        Bytecode_Instruction* next = interpreter->instruction_pointer + 1;
        interpreter->stack_pointer = interpreter->stack_pointer + i->op2;
        *((Bytecode_Instruction**)interpreter->stack_pointer) = next;
        *(byte**)(interpreter->stack_pointer + 8) = base_pointer;
        interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];
        return false;
    }
    case Instruction_Type::RETURN: {
        if (i->op2 > 256) {
            interpreter->exit_code = Exit_Code::RETURN_VALUE_OVERFLOW;
            return true;
        }
        memory_copy(interpreter->return_register, interpreter->stack_pointer + i->op1, i->op2);
        Bytecode_Instruction* return_address = *(Bytecode_Instruction**)interpreter->stack_pointer;
        byte* stack_old_base = *(byte**)(interpreter->stack_pointer + 8);
        interpreter->instruction_pointer = return_address;
        interpreter->stack_pointer = stack_old_base;
        return false;
    }
    case Instruction_Type::EXIT: {
        interpreter->exit_code = (Exit_Code) i->op3;
        if (interpreter->exit_code == Exit_Code::SUCCESS) {
            memory_copy(&interpreter->return_register[0], interpreter->stack_pointer + i->op1, i->op2);
        }
        return true;
    }
    case Instruction_Type::CALL_HARDCODED_FUNCTION: 
    {
        Hardcoded_Function_Type type = (Hardcoded_Function_Type)i->op1;
        byte* argument_start = interpreter->stack_pointer + i->op2 - 8; // Check if this is correct
        memory_set_bytes(&interpreter->return_register[0], 256, 0);
        switch (type)
        {
        case Hardcoded_Function_Type::MALLOC_SIZE_I32: {
            i32 size = *(i32*) argument_start;
            void* alloc_data = malloc(size);
            memory_copy(interpreter->return_register, &alloc_data, 8);
            break;
        }
        case Hardcoded_Function_Type::FREE_POINTER: {
            void* free_data = *(void**) argument_start;
            free(free_data);
            break;
        }
        case Hardcoded_Function_Type::PRINT_I32: {
            logg("%d", *(i32*)(argument_start)); break;
        }
        case Hardcoded_Function_Type::PRINT_F32: {
            logg("%3.2f", *(f32*)(argument_start)); break;
        }
        case Hardcoded_Function_Type::PRINT_BOOL: {
            logg("%s", *(argument_start) == 0 ? "FALSE" : "TRUE"); break;
        }
        case Hardcoded_Function_Type::PRINT_LINE: {
            logg("\n"); break;
        }
        case Hardcoded_Function_Type::READ_I32: {
            logg("Please input an i32: ");
            i32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            memory_copy(interpreter->return_register, &num, 4);
            break;
        }
        case Hardcoded_Function_Type::READ_F32: {
            logg("Please input an f32: ");
            f32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            memory_copy(interpreter->return_register, &num, 4);
            break;
        }
        case Hardcoded_Function_Type::READ_BOOL: {
            logg("Please input an bool (As int): ");
            i32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            if (num == 0) {
                interpreter->return_register[0] = 0;
            }
            else {
                interpreter->return_register[0] = 1;
            }
            break;
        }
        case Hardcoded_Function_Type::RANDOM_I32: {
            i32 result = random_next_int();
            memory_copy(interpreter->return_register, &result, 4);
            break;
        }
        default: {panic("What"); }
        }
        break;
    }
    case Instruction_Type::LOAD_RETURN_VALUE:
        memory_copy(interpreter->stack_pointer + i->op1, &interpreter->return_register[0], i->op2);
        break;
    case Instruction_Type::LOAD_REGISTER_ADDRESS:
        *(void**)(interpreter->stack_pointer + i->op1) = (void*)(interpreter->stack_pointer + i->op2);
        break;
    case Instruction_Type::LOAD_CONSTANT_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *((f32*)&i->op2);
        break;
    case Instruction_Type::LOAD_CONSTANT_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = i->op2;
        break;
    case Instruction_Type::LOAD_CONSTANT_BOOLEAN:
        *(interpreter->stack_pointer + i->op1) = (byte)i->op2;
        break;

    case Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) + *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) - *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) * *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) / *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) % *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) == *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) != *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) > * (i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) >= *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) < *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) <= *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = -(*(i32*)(interpreter->stack_pointer + i->op2));
        break;

    case Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) + *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) - *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) * *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) / *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) == *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) != *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) > * (f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) >= *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) < *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) <= *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = -(*(f32*)(interpreter->stack_pointer + i->op2));
        break;

    case Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL:
        *(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) == *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL:
        *(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) != *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_BOOLEAN_AND:
        *(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) && *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_BOOLEAN_OR:
        *(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) || *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::UNARY_OP_BOOLEAN_NOT:
        *(interpreter->stack_pointer + i->op1) = !(*(bool*)(interpreter->stack_pointer + i->op2));
        break;

    default: {
        panic("Should not happen!\n");
        return true;
    }
    }

    // Debug output
    if (false)
    {
        int start = (int)Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32;
        int end = (int)Instruction_Type::UNARY_OP_BOOLEAN_NOT;
        int index = (int)i->instruction_type;
        if (index >= start && index <= end)
        {
            int counter = interpreter->instruction_pointer - interpreter->generator->instructions.data;
            bool unary = i->instruction_type == Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32 ||
                i->instruction_type == Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32 ||
                i->instruction_type == Instruction_Type::UNARY_OP_BOOLEAN_NOT;
            logg("%d Stack index: %d\n", counter, (int)(interpreter->stack_pointer - interpreter->stack.data));
            logg("%d dest = %6d / %3.2f / %s\n",
                counter,
                *(int*)(interpreter->stack_pointer + i->op1),
                *(float*)(interpreter->stack_pointer + i->op1),
                (*(bool*)(interpreter->stack_pointer + i->op1)) ? "True" : "False");
            logg("%d src1 = %6d / %3.2f / %s\n",
                counter,
                *(int*)(interpreter->stack_pointer + i->op2),
                *(float*)(interpreter->stack_pointer + i->op2),
                (*(bool*)(interpreter->stack_pointer + i->op2)) ? "True" : "False");
            if (!unary) {
                logg("%d src2 = %6d / %3.2f / %s\n",
                    counter,
                    *(int*)(interpreter->stack_pointer + i->op3),
                    *(float*)(interpreter->stack_pointer + i->op3),
                    (*(bool*)(interpreter->stack_pointer + i->op3)) ? "True" : "False");
            }
        }
    }

    interpreter->instruction_pointer = &interpreter->instruction_pointer[1];
    return false;
}

void type_signature_print_value(Type_Signature* type, byte* value_ptr)
{
    switch (type->type)
    {
    case Signature_Type::FUNCTION:
    case Signature_Type::VOID_TYPE:
    case Signature_Type::ERROR_TYPE:
    case Signature_Type::ARRAY_SIZED: 
    {
        logg("[%d]: ", type->array_element_count);
        if (type->array_element_count > 4) {
            logg("...");
            return;
        }
        for (int i = 0; i < type->array_element_count; i++) {
            byte* element_ptr = value_ptr + (i * type->child_type->size_in_bytes);
            type_signature_print_value(type->child_type, element_ptr);
            logg(", ");
        }
        break;
    }
    case Signature_Type::ARRAY_UNSIZED: 
    {
        byte* data_ptr = *((byte**)value_ptr);
        int element_count = *((int*)(value_ptr + 8));
        logg("ptr: %p  [%d]: ", data_ptr, element_count);
        if (element_count > 4) {
            logg("...");
            return;
        }
        for (int i = 0; i < element_count; i++) {
            byte* element_ptr = data_ptr + (i * type->child_type->size_in_bytes);
            type_signature_print_value(type->child_type, element_ptr);
            logg(", ");
        }
        break;
    }
    case Signature_Type::POINTER: 
    {
        byte* data_ptr = *((byte**)value_ptr);
        if (data_ptr == 0) {
            logg("nullpointer");
            return;
        }
        logg("Ptr %p", data_ptr);
        break;
    }
    case Signature_Type::STRUCT: 
    {
        logg("Struct: {");
        for (int i = 0; i < type->member_types.size; i++) {
            Struct_Member* mem = &type->member_types[i];
            byte* mem_ptr = value_ptr + mem->offset;
            type_signature_print_value(mem->type, mem_ptr);
            logg(", ");
        }
        logg("}");
        break;
    }
    case Signature_Type::PRIMITIVE: 
    {
        switch (type->primitive_type)
        {
        case Primitive_Type::BOOLEAN: {
            bool val = *(bool*)value_ptr;
            logg("%s", val ? "TRUE" : "FALSE");
            break;
        }
        case Primitive_Type::SIGNED_INT_8: {
            int val = (i32)*(i8*)value_ptr;
            logg("%d", val);
            break;
        }
        case Primitive_Type::SIGNED_INT_16: {
            int val = (i32)*(i16*)value_ptr;
            logg("%d", val);
            break;
        }
        case Primitive_Type::SIGNED_INT_32: {
            int val = (i32)*(i32*)value_ptr;
            logg("%d", val);
            break;
        }
        case Primitive_Type::SIGNED_INT_64: {
            int val = (i32)*(i64*)value_ptr;
            logg("%d", val);
            break;
        }
        case Primitive_Type::UNSIGNED_INT_8 : {
            int val = (i32)*(u8*)value_ptr;
            logg("%d", val);
            break;
        }
        case Primitive_Type::UNSIGNED_INT_16: {
            int val = (i32)*(u16*)value_ptr;
            logg("%d", val);
            break;
        }
        case Primitive_Type::UNSIGNED_INT_32: {
            int val = (i32)*(u32*)value_ptr;
            logg("%d", val);
            break;
        }
        case Primitive_Type::UNSIGNED_INT_64: {
            int val = (i32)*(u64*)value_ptr;
            logg("%d", val);
            break;
        }
        case Primitive_Type::FLOAT_32: {
            float val = *(float*)value_ptr;
            logg("%3.2f", val);
            break;
        }
        case Primitive_Type::FLOAT_64: {
            double val = *(double*)value_ptr;
            logg("%3.2f", val);
            break;
        }
        }
        break;
    }
    }

}

void bytecode_interpreter_print_state(Bytecode_Interpreter* interpreter)
{
    int current_instruction_index = interpreter->instruction_pointer - interpreter->generator->instructions.data;
    int current_function_index = -1;
    for (int i = 0; i < interpreter->generator->function_locations.size; i++)
    {
        int function_start_loc = interpreter->generator->function_locations[i];
        int function_end_loc_exclusive = interpreter->generator->instructions.size;
        if (i + 1 < interpreter->generator->function_locations.size) {
            function_end_loc_exclusive = interpreter->generator->function_locations[i + 1];
        }
        if (current_instruction_index >= function_start_loc && current_instruction_index < function_end_loc_exclusive) {
            current_function_index = i;
            break;
        }
    }
    if (current_function_index == -1) panic("Should not happen!\n");

    bytecode_generator_calculate_function_register_locations(interpreter->generator, current_function_index);
    Intermediate_Function* func = &interpreter->generator->im_generator->functions[current_function_index];
    logg("\n\n\n\n---------------------- CURRENT STATE ----------------------\n");
    logg("Current Function: %s\n", lexer_identifer_to_string(interpreter->generator->im_generator->analyser->parser->lexer, func->name_handle).characters);
    logg("Current Stack offset: %d\n", interpreter->stack.data - interpreter->stack_pointer);
    logg("Instruction Index: %d\n", current_instruction_index);
    {
        String tmp = string_create_empty(64);
        SCOPE_EXIT(string_destroy(&tmp));
        bytecode_instruction_append_to_string(&tmp, interpreter->generator->instructions[current_instruction_index]);
        logg("Instruction: %s\n", tmp.characters);
    }
    for (int i = 0; i < func->registers.size; i++)
    {
        Intermediate_Register* reg = &func->registers[i];
        int stack_offset = interpreter->generator->register_stack_locations[i];
        byte* reg_data_ptr = interpreter->stack_pointer + stack_offset;
        Type_Signature* reg_type = reg->type_signature;
        if (reg->type == Intermediate_Register_Type::PARAMETER) {
            logg("Parameter %d (Offset %d): ", reg->parameter_index, stack_offset);
        }
        else if (reg->type == Intermediate_Register_Type::VARIABLE) {
            logg("Variable %s (Offset %d): ",
                lexer_identifer_to_string(interpreter->generator->im_generator->analyser->parser->lexer, reg->name_id).characters,
                stack_offset
            );
        }
        else continue;
        type_signature_print_value(reg_type, reg_data_ptr);
        logg("\n");
    }
}

void bytecode_interpreter_execute_main(Bytecode_Interpreter* interpreter, Bytecode_Generator* generator)
{
    interpreter->generator = generator;
    memory_set_bytes(&interpreter->return_register, 256, 0);
    memory_set_bytes(interpreter->stack.data, 16, 0);
    interpreter->instruction_pointer = &generator->instructions[generator->entry_point_index];
    interpreter->stack_pointer = &interpreter->stack[0];
    int current_instruction_index = interpreter->instruction_pointer - interpreter->generator->instructions.data;

    while (true) {
        //bytecode_interpreter_print_state(interpreter);
        if (bytecode_interpreter_execute_current_instruction(interpreter)) { break; }
    }
}