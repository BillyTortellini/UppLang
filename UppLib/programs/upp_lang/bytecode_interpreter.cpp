#include "bytecode_interpreter.hpp"

#include <iostream>
#include "../../utility/random.hpp"
#include "compilation_data.hpp"
#include <Windows.h>
#include "ir_code.hpp"
#include "compilation_data.hpp"

struct Bytecode_Thread
{
    Compilation_Data* compilation_data;
    Arena* arena; // Where all heap allocations go
    int max_instruction_executions;
    int max_heap_consumption;
    bool allow_global_access;

    // Run-Information
    int instruction_index;
    byte* stack_pointer;
    Array<byte> stack;
    int heap_memory_consumption;
    int executed_instruction_count;

    // Result infos
    Exit_Code exit_code;
};


Bytecode_Thread* bytecode_thread_create(
	Compilation_Data* compilation_data, Arena* arena, int max_instruction_executions, int max_heap_consumption, int stack_size, bool allow_global_access
)
{
    Bytecode_Thread* result = arena->allocate<Bytecode_Thread>();
    memory_zero(result);
    result->arena = arena;
    result->compilation_data = compilation_data;

    result->max_instruction_executions = max_instruction_executions;
    result->max_heap_consumption = max_heap_consumption;
    result->allow_global_access = allow_global_access;
    result->stack.size = stack_size;
    result->stack.data = (byte*) arena->allocate_raw(stack_size, 16); // Allocate raw instead of allocate_array so we can have 16 byte alignment

    return result;
}

void interpreter_safe_memcopy(Bytecode_Thread* thread, void* dst, void* src, int size)
{
    if (memory_is_readable(dst, size) && memory_is_readable(src, size)) {
        memory_copy(dst, src, size);
    }
    else {
        thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Invalid memory access");
    }
}

// Returns true if we need to stop execution, e.g. on exit instruction
void bytecode_thread_execute_current_instruction(Bytecode_Thread* thread)
{
    auto compilation_data = thread->compilation_data;
    auto& globals = compilation_data->globals;
    auto& constant_pool = compilation_data->constant_pool;
    auto& instructions = compilation_data->bytecode;

    Bytecode_Instruction* i = &instructions[thread->instruction_index];
    switch (i->instruction_type)
    {
    case Instruction_Type::MOVE_STACK_DATA:
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, thread->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_GLOBAL: 
    {
        if (!thread->allow_global_access) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot read extern global");
            return;
        }

        auto global = globals[i->op2];
        if (global->is_extern) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot read extern global");
            return;
        }
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, globals[i->op2]->memory, i->op3);
        break;
    }
    case Instruction_Type::WRITE_GLOBAL: {
        auto global = globals[i->op2];
        if (global->is_extern) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot write to extern global");
            return;
        }
        interpreter_safe_memcopy(thread, globals[i->op1]->memory, thread->stack_pointer + i->op2, i->op3);
        break;
    }
    case Instruction_Type::WRITE_MEMORY:
        interpreter_safe_memcopy(thread, *(void**)(thread->stack_pointer + i->op1), thread->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_MEMORY: {
        void* result = *(void**)(thread->stack_pointer + i->op2);
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, *((void**)(thread->stack_pointer + i->op2)), i->op3);
        break;
    }
    case Instruction_Type::MEMORY_COPY:
        interpreter_safe_memcopy(thread, *(void**)(thread->stack_pointer + i->op1), *(void**)(thread->stack_pointer + i->op2), i->op3);
        break;
    case Instruction_Type::READ_CONSTANT:
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, constant_pool->constants[i->op2].memory, i->op3);
        break;
    case Instruction_Type::U64_ADD_CONSTANT_I32:
        *(u64*)(thread->stack_pointer + i->op1) = *(u64*)(thread->stack_pointer + i->op2) + (i->op3);
        break;
    case Instruction_Type::U64_MULTIPLY_ADD_I32: {
        u64 offset = (u64)((*(u32*)(thread->stack_pointer + i->op3)) * (u64)i->op4);
        if ((i32)offset < 0) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Multiply-Add (Used in array index calculations) went out of bounds");
            return;
        }
        *(u64**)(thread->stack_pointer + i->op1) = (u64*)(*(byte**)(thread->stack_pointer + i->op2) + offset);
        break;
    }
    case Instruction_Type::JUMP:
        thread->instruction_index = i->op1;
        return; // Return so we don't increment instruction-index
    case Instruction_Type::JUMP_ON_TRUE:
        if (*(thread->stack_pointer + i->op2) != 0) {
            thread->instruction_index = i->op1;
            return;
        }
        break;
    case Instruction_Type::JUMP_ON_FALSE:
        if (*(thread->stack_pointer + i->op2) == 0) {
            thread->instruction_index = i->op1;
            return;
        }
        break;
    case Instruction_Type::JUMP_ON_INT_EQUAL: {
        int value = *(int*)(thread->stack_pointer + i->op2);
        if (value == i->op3) {
            thread->instruction_index = i->op1;
            return;
        }
        break;
    }
    case Instruction_Type::CALL_FUNCTION_POINTER: 
    case Instruction_Type::CALL_FUNCTION: 
    {
        int function_index = 0;
        {
            if (i->instruction_type == Instruction_Type::CALL_FUNCTION) {
                function_index = i->op1 - 1;
            }
            else {
                function_index = (int)(*(i64*)(thread->stack_pointer + i->op1)) - 1;
            }
            if (function_index < 0 || function_index >= compilation_data->functions.size) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Function call failed call-instruction has invalid function index");
                return;
            }
        }

        Upp_Function* function = nullptr;
        {
            function = compilation_data->functions[function_index];
            if (function->is_extern) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Call to extern function not possible in bytecode");
                return;
            }
            else if (function->contains_errors) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Call to function with errors");
                return;
            }
            else if (function->poly_type == Poly_Type::BASE || function->poly_type == Poly_Type::PARTIAL) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Call to poly-base");
                return;
            }
            else if (function->bytecode_start_instruction == -1) {
                thread->exit_code = exit_code_make(Exit_Code_Type::CALL_TO_UNFINISHED_FUNCTION);
                thread->exit_code.options.waiting_for_function = function;
                return;
            }
        }

        // Check for stack-overflow
        int remaining_stack_size = &thread->stack[thread->stack.size - 1] - thread->stack_pointer;
        if (remaining_stack_size <= function->bytecode_maximum_stack_offset) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Stack overflow on normal function call");
            return;
        }

        byte* base_pointer = thread->stack_pointer;
        thread->stack_pointer = thread->stack_pointer + i->op2; 
        *((int*)thread->stack_pointer) = thread->instruction_index + 1; // Push return address
        *(byte**)(thread->stack_pointer + 8) = base_pointer; // Push current stack_pointer
        thread->instruction_index = function->bytecode_start_instruction; // Jump to function

        return;
    }
    case Instruction_Type::RETURN: 
    {
        // Check if we finished execution
        if (thread->stack_pointer == &thread->stack[0]) {
            thread->exit_code = exit_code_make(Exit_Code_Type::SUCCESS);
            return;
        }

        // Restore stack and instruction pointer
        int return_address = *(int*)thread->stack_pointer;
        byte* stack_old_base = *(byte**)(thread->stack_pointer + 8);
        thread->instruction_index = return_address;
        thread->stack_pointer = stack_old_base;
        return;
    }
    case Instruction_Type::EXIT: {
        thread->exit_code = exit_code_from_exit_instruction(*i);
        return;
    }
    case Instruction_Type::CALL_BUILTIN_FUNCTION:
    {
        IR_Builtin_Function builtin_type = (IR_Builtin_Function)i->op1;
        assert((u32)builtin_type < (int)IR_Builtin_Function::MAX_ENUM_VALUE, "");

        byte* return_buffer = thread->stack_pointer + i->op2 + 16; // Return buffer is after [return_instruction] [prev_stack_frame] [return_buffer] [params]...
        switch (builtin_type)
        {
        case IR_Builtin_Function::SYSTEM_ALLOC: 
        {
            // System-alloc (u64 size) => address
            byte* argument_start = return_buffer + 8;
            u64 size = *(u64*)argument_start;
            if (size == 0) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Called malloc with size 0");
                return;
            }
            if (thread->heap_memory_consumption + size > thread->max_heap_consumption) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Reached maximum heap allocations");
                return;
            }

            void* alloc_data = thread->arena->allocate_raw(size, 16);
            thread->heap_memory_consumption += size;

            // logg("Allocated memory size: %5d, pointer: %p\n", size, alloc_data);
            memory_copy(return_buffer, &alloc_data, sizeof(void*));
            break;
        }
        case IR_Builtin_Function::SYSTEM_FREE: 
        {
            // System-free fn (ptr: address)
            byte* argument_start = return_buffer;
            void* free_data = *(void**)argument_start;

            if (free_data == nullptr) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Free called on nullptr");
                return;
            }

            // logg("Interpreter Free pointer: %p\n", free_data);
            break;
        }
        case IR_Builtin_Function::MEMORY_COPY: 
        case IR_Builtin_Function::MEMORY_COPY_NO_OVERLAP: 
        {
            // fn (dst: address, src: address, size: usize)
            byte* argument_start = return_buffer;
            void* destination = *(void**)argument_start;
            void* source = *(void**)(argument_start + 8);
            u64 size = *(u64*)(argument_start + 16);
            interpreter_safe_memcopy(thread, destination, source, size);
            break;
        }
        case IR_Builtin_Function::MEMORY_COMPARE: 
        {
            // fn (a: address, b: address, size: usize) => bool
            byte* argument_start = return_buffer + 8; // 1 byte for return-buffer, 7 byte alignment
            void* destination = *(void**)argument_start;
            void* source = *(void**)(argument_start + 8);
            u64 size = *(u64*)(argument_start + 16);
            if (!memory_is_readable(destination, size) || !memory_is_readable(source, size)) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Memory compare called with invalid pointers/size");
                return;
            }
            int cmp = memcmp(destination, source, size);
            *return_buffer = cmp == 0;
            break;
        }
        case IR_Builtin_Function::MEMORY_ZERO: 
        {
            // fn (a: address, size: usize)
            byte* argument_start = return_buffer;
            void* destination = *(void**)argument_start;
            u64 size = *(u64*)(argument_start + 8);
            if (!memory_is_readable(destination, size)) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Memory zero called with invalid pointers/size");
                return;
            }
            memset(destination, 0, size);
            break;
        }
        case IR_Builtin_Function::PRINT_I32: {
            byte* argument_start = return_buffer;
            i32 value = *(i32*)(argument_start);
            logg("%d", value); break;
        }
        case IR_Builtin_Function::PRINT_F32: {
            byte* argument_start = return_buffer;
            logg("%3.2f", *(f32*)(argument_start)); break;
        }
        case IR_Builtin_Function::PRINT_BOOL: {
            byte* argument_start = return_buffer;
            logg("%s", *(argument_start) == 0 ? "FALSE" : "TRUE"); break;
        }
        case IR_Builtin_Function::PRINT_STRING: 
        {
            byte* argument_start = return_buffer;
            Upp_String string = *(Upp_String*)argument_start;

            // Check if c_string size is correct
            if (string.size == 0) {break;}
            if (string.size >= 10000) {
                thread->exit_code = exit_code_make(
                    Exit_Code_Type::CODE_ERROR, 
                    "Print_string failed, slice size was too large, > 10000");
                return;
            }
            // Check if pointer data is correct
            if (!memory_is_readable((void*)string.data, string.size)) {
                thread->exit_code = exit_code_make(
                    Exit_Code_Type::CODE_ERROR, 
                    "Print c_string failed, memory of c_string was not readable");
                return;
            }

            logg("%.*s", string.size, (const char*) string.data);
            break;
        }
        case IR_Builtin_Function::PRINT_LINE: {
            logg("\n"); break;
        }
        case IR_Builtin_Function::READ_I32: {
            logg("Please input an i32: ");
            i32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            interpreter_safe_memcopy(thread, return_buffer, &num, 4);
            break;
        }
        case IR_Builtin_Function::READ_F32: {
            logg("Please input an f32: ");
            f32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            interpreter_safe_memcopy(thread, return_buffer, &num, 4);
            break;
        }
        case IR_Builtin_Function::READ_BOOL: {
            logg("Please input an bool (As int): ");
            i32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            if (num == 0) {
                *return_buffer = false;
            }
            else {
                *return_buffer = true;
            }
            break;
        }
        case IR_Builtin_Function::TYPE_INFO: 
        {
            auto& type_system = thread->compilation_data->type_system;

            byte* argument_start = thread->stack_pointer + i->op2 + 16;
            int type_index = *(int*)(argument_start);
            if (type_index > type_system->types.size || type_index < 0) {
                thread->exit_code = exit_code_make(
                    Exit_Code_Type::CODE_ERROR, 
                    "type_info failed, type-handle was invalid value");
                return;
            }
            if (type_size_is_unfinished(type_system->types[type_index])) {
                thread->exit_code = exit_code_make(Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED);
                thread->exit_code.options.waiting_for_type_finish_type = type_system->types[type_index];
                return;
            }
            *((Internal_Type_Information**)return_buffer) = type_system->types[type_index]->internal_info;
            break;
        }
        default: {panic("What"); }
        }
        break;
    }
    case Instruction_Type::LOAD_REGISTER_ADDRESS:
        *(void**)(thread->stack_pointer + i->op1) = (void*)(thread->stack_pointer + i->op2);
        break;
    case Instruction_Type::LOAD_GLOBAL_ADDRESS: 
    {
        if (!thread->allow_global_access) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Trying to load global variable address");
            return;
        }
        auto global = globals[i->op2];
        if (global->is_extern) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot load extern global address");
            return;
        }
        *(void**)(thread->stack_pointer + i->op1) = (void*)(globals[i->op2]->memory);
        break;
    }
    case Instruction_Type::LOAD_CONSTANT_ADDRESS:
        *(void**)(thread->stack_pointer + i->op1) = (void*)(constant_pool->constants[i->op2].memory);
        break;
    case Instruction_Type::LOAD_FUNCTION_LOCATION:
        *(i64*)(thread->stack_pointer + i->op1) = (i64)(i->op2 + 1); // Note: Function pointers are encoded as function indices in interpreter
        break;
    case Instruction_Type::IR_OPERATION: 
    {
        void* dst = thread->stack_pointer + i->op1;
        void* src1 = thread->stack_pointer + i->op2;
        void* src2 = thread->stack_pointer + i->op3;

        Bytecode_Type dst_type, left_type, right_type;
        Primitive_Operation ir_op;
        bytecode_unpack_operation_and_types_from_int(i->op4, ir_op, dst_type, left_type, right_type);
        bool success = bytecode_execute_ir_operation(ir_op, dst, src1, src2, dst_type, left_type, right_type);
        if (!success) {
            thread->exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Division or modulo by 0");
        }
        break;
    }
    default: {
        panic("Should not happen!\n");
    }
    }

    if (thread->exit_code.type != Exit_Code_Type::RUNNING) {
        return;
    }
    thread->instruction_index += 1;
    thread->executed_instruction_count += 1;
}

void bytecode_thread_print_state(Bytecode_Thread* interpreter)
{
    /*
    int current_instruction_index = thread->instruction_index - thread->generator->instructions.data;
    int current_function_index = -1;
    for (int i = 0; i < thread->generator->function_locations.size; i++)
    {
        int function_start_loc = thread->generator->function_locations[i];
        int function_end_loc_exclusive = thread->generator->instructions.size;
        if (i + 1 < thread->generator->function_locations.size) {
            function_end_loc_exclusive = thread->generator->function_locations[i + 1];
        }
        if (current_instruction_index >= function_start_loc && current_instruction_index < function_end_loc_exclusive) {
            current_function_index = i;
            break;
        }
    }
    if (current_function_index == -1) panic("Should not happen!\n");

    logg("\n\n\n\n---------------------- CURRENT STATE ----------------------\n");
    logg("Current Function: %s\n", identifier_pool_index_to_string(&thread->compiler->code_source, func->name_handle).characters);
    logg("Current Stack offset: %d\n", thread->stack.data - thread->stack_pointer);
    logg("Instruction Index: %d\n", current_instruction_index);
    {
        String tmp = string_create(64);
        SCOPE_EXIT(string_destroy(&tmp));
        bytecode_instruction_append_to_string(&tmp, thread->generator->instructions[current_instruction_index]);
        logg("Instruction: %s\n", tmp.characters);
    }
    */
    /*
    for (int i = 0; i < func->registers.size; i++)
    {
        Intermediate_Register* reg = &func->registers[i];
        int stack_offset = thread->generator->variable_stack_offsets[i];
        byte* reg_data_ptr = thread->stack_pointer + stack_offset;
        Datatype* reg_type = reg->type_signature;
        if (reg->type == Intermediate_Register_Type::PARAMETER) {
            logg("Parameter %d (Offset %d): ", reg->defined_in_parameter_index, stack_offset);
        }
        else if (reg->type == Intermediate_Register_Type::VARIABLE) {
            logg("Variable %s (Offset %d): ",
                identifier_pool_index_to_string(thread->generator->im_generator->analyser->parser->code_source, reg->id).characters,
                stack_offset
            );
        }
        else if (reg->type == Intermediate_Register_Type::EXPRESSION_RESULT) {
            logg("Expression %d (Offset %d): ", i, stack_offset);
        }

        type_signature_print_value(reg_type, reg_data_ptr);
        logg("\n");
    }
    */
}

void bytecode_thread_set_initial_state(Bytecode_Thread* thread, Upp_Function* entry_function)
{
    // Note parameters would be possible, but then we would need to initialze the stack correctly first
    assert(entry_function->signature->param_count() == 0, "Entry function cannot have parameters currently");
    assert(entry_function->bytecode_start_instruction != -1, "Function should be compiled at this point");

    thread->stack_pointer = &thread->stack[0];
    thread->instruction_index = entry_function->bytecode_start_instruction;
    thread->executed_instruction_count = 0;
    thread->heap_memory_consumption = 0;
}

Exit_Code bytecode_thread_execute(Bytecode_Thread* thread)
{
    Timing_Task before_task = thread->compilation_data->task_current;
    compilation_data_switch_timing_task(thread->compilation_data, Timing_Task::CODE_EXEC);

    thread->exit_code = exit_code_make(Exit_Code_Type::RUNNING);
    __try
    {
        while (true) 
        {
            //bytecode_thread_print_state(thread);
            bytecode_thread_execute_current_instruction(thread);
            if (thread->exit_code.type != Exit_Code_Type::RUNNING) {
                break;
            }
            if (thread->max_instruction_executions > 0 && thread->executed_instruction_count >= thread->max_instruction_executions) {
                thread->exit_code = exit_code_make(Exit_Code_Type::INSTRUCTION_LIMIT_REACHED);
                break;
            }
        }
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
        GetExceptionCode() == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ||
        GetExceptionCode() == EXCEPTION_DATATYPE_MISALIGNMENT ||
        GetExceptionCode() == EXCEPTION_GUARD_PAGE ||
        GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
        GetExceptionCode() == EXCEPTION_INT_DIVIDE_BY_ZERO ||
        GetExceptionCode() == EXCEPTION_INVALID_HANDLE ||
        GetExceptionCode() == EXCEPTION_PRIV_INSTRUCTION ||
        GetExceptionCode() == EXCEPTION_STACK_OVERFLOW) 
    {
        thread->exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Internal exception occured (Division by 0, invalid memory access, ...)");
    }

    compilation_data_switch_timing_task(thread->compilation_data, before_task);
    return thread->exit_code;
}

void* bytecode_thread_get_return_value_ptr(Bytecode_Thread* thread) {
    return &thread->stack[16]; // Return value starts at offset 16 in stack frame
}



// IR-Operation execute
template<typename T>
void handle_ir_operation_arithmetic(Primitive_Operation operation, T* dst, T* src1, T* src2)
{
    switch (operation)
    {
    case Primitive_Operation::ADDITION:         *dst = (*src1 + *src2); break;
    case Primitive_Operation::SUBTRACTION:      *dst = (*src1 - *src2); break;
    case Primitive_Operation::MULTIPLICATION:   *dst = (*src1 * *src2); break;
    case Primitive_Operation::NEGATE:           *dst = -(*src1); break;
    default: panic("");
    }
}

template<typename T>
void handle_ir_operation_order_comparison(Primitive_Operation operation, bool* dst, T* src1, T* src2)
{
    switch (operation)
    {
    case Primitive_Operation::LESS:             *dst = (*src1 < *src2); break;
    case Primitive_Operation::LESS_OR_EQUAL:    *dst = (*src1 <= *src2); break;
    case Primitive_Operation::GREATER:          *dst = (*src1 > *src2); break;
    case Primitive_Operation::GREATER_OR_EQUAL: *dst = (*src1 >= *src2); break;
    default: panic("");
    }
}

template<typename T>
void handle_ir_operation_equals(Primitive_Operation operation, bool* dst, T* src1, T* src2)
{
    switch (operation)
    {
    case Primitive_Operation::EQUAL:     *dst = (*src1 == *src2); break;
    case Primitive_Operation::NOT_EQUAL: *dst = (*src1 != *src2); break;
    default: panic("");
    }
}

// Note: right-type/dst_type get ignored by a lot of operations, which assume that all types are the same
bool bytecode_execute_ir_operation(
    Primitive_Operation operation, void* dst, void* src1, void* src2, Bytecode_Type dst_type, Bytecode_Type left_type, Bytecode_Type right_type)
{
    Bytecode_Type src_type = left_type;
    auto argument_count = ir_operation_parameter_count(operation);
    switch (operation)
    {
    case Primitive_Operation::PRIMITIVE_CAST: 
    {
        assert(dst_type != Bytecode_Type::BOOL && src_type != Bytecode_Type::BOOL, "");

        auto is_int = [](Bytecode_Type type) {
            return type != Bytecode_Type::FLOAT32 && type != Bytecode_Type::FLOAT64;
        };

        if (is_int(src_type) && is_int(dst_type)) // Int to int
        {
            u64 source_unsigned = 0;
            i64 source_signed = 0;
            bool source_is_signed = false;
            switch (src_type) {
            case Bytecode_Type::INT8: source_is_signed = true; source_signed = *(i8*)(src1); break;
            case Bytecode_Type::INT16: source_is_signed = true; source_signed = *(i16*)(src1); break;
            case Bytecode_Type::INT32: source_is_signed = true; source_signed = *(i32*)(src1); break;
            case Bytecode_Type::INT64: source_is_signed = true; source_signed = *(i64*)(src1); break;
            case Bytecode_Type::UINT8: source_is_signed = false; source_unsigned = *(u8*)(src1); break;
            case Bytecode_Type::UINT16: source_is_signed = false; source_unsigned = *(u16*)(src1); break;
            case Bytecode_Type::UINT32: source_is_signed = false; source_unsigned = *(u32*)(src1); break;
            case Bytecode_Type::UINT64: source_is_signed = false; source_unsigned = *(u64*)(src1); break;
            default: panic("what the frigg\n");
            }

            switch (dst_type) {
            case Bytecode_Type::INT8:   *(i8*)(dst)  = (i8) (source_is_signed ? source_signed : source_unsigned); break;
            case Bytecode_Type::INT16:  *(i16*)(dst) = (i16)(source_is_signed ? source_signed : source_unsigned); break;
            case Bytecode_Type::INT32:  *(i32*)(dst) = (i32)(source_is_signed ? source_signed : source_unsigned); break;
            case Bytecode_Type::INT64:  *(i64*)(dst) = (i64)(source_is_signed ? source_signed : source_unsigned); break;
            case Bytecode_Type::UINT8:  *(u8*)(dst)  = (u8) (source_is_signed ? source_signed : source_unsigned); break;
            case Bytecode_Type::UINT16: *(u16*)(dst) = (u16)(source_is_signed ? source_signed : source_unsigned); break;
            case Bytecode_Type::UINT32: *(u32*)(dst) = (u32)(source_is_signed ? source_signed : source_unsigned); break;
            case Bytecode_Type::UINT64: *(u64*)(dst) = (u64)(source_is_signed ? source_signed : source_unsigned); break;
            default: panic("what the frigg\n");
            }
        }
        else if (is_int(src_type)) // Int to float
        {
            u64 source_unsigned;
            i64 source_signed;
            bool source_is_signed = false;
            switch (src_type) {
            case Bytecode_Type::INT8:   source_is_signed = true; source_signed = *(i8*)(src1); break;
            case Bytecode_Type::INT16:  source_is_signed = true; source_signed = *(i16*)(src1); break;
            case Bytecode_Type::INT32:  source_is_signed = true; source_signed = *(i32*)(src1); break;
            case Bytecode_Type::INT64:  source_is_signed = true; source_signed = *(i64*)(src1); break;
            case Bytecode_Type::UINT8:  source_is_signed = false; source_unsigned = *(u8*)(src1); break;
            case Bytecode_Type::UINT16: source_is_signed = false; source_unsigned = *(u16*)(src1); break;
            case Bytecode_Type::UINT32: source_is_signed = false; source_unsigned = *(u32*)(src1); break;
            case Bytecode_Type::UINT64: source_is_signed = false; source_unsigned = *(u64*)(src1); break;
            default: panic("what the frigg\n");
            }

            switch (dst_type) {
            case Bytecode_Type::FLOAT32: *(f32*)(dst) = (f32)(source_is_signed ? source_signed : source_unsigned); break;
            case Bytecode_Type::FLOAT64: *(f64*)(dst) = (f64)(source_is_signed ? source_signed : source_unsigned); break;
            default: panic("what the frigg\n");
            }
        }
        else if (is_int(dst_type)) // Float to int
        {
            double source = 0.0;
            switch (src_type) {
            case Bytecode_Type::FLOAT32: source = *(f32*)(src1); break;
            case Bytecode_Type::FLOAT64: source = *(f64*)(src1); break;
            default: panic("what the frigg\n");
            }

            switch (dst_type) {
            case Bytecode_Type::INT8:   *(i8*)(dst) = (i8)source; break;
            case Bytecode_Type::INT16:  *(i16*)(dst) = (i16)source; break;
            case Bytecode_Type::INT32:  *(i32*)(dst) = (i32)source; break;
            case Bytecode_Type::INT64:  *(i64*)(dst) = (i64)source; break;
            case Bytecode_Type::UINT8:  *(u8*)(dst) = (u8)source; break;
            case Bytecode_Type::UINT16: *(u16*)(dst) = (u16)source; break;
            case Bytecode_Type::UINT32: *(u32*)(dst) = (u32)source; break;
            case Bytecode_Type::UINT64: *(u64*)(dst) = (u64)source; break;
            default: panic("what the frigg\n");
            }
        }
        else // Float to float
        {
            f64 source = 0.0;
            switch (src_type) {
            case Bytecode_Type::FLOAT32: source = *(f32*)(src1); break;
            case Bytecode_Type::FLOAT64: source = *(f64*)(src1); break;
            default: panic("what the frigg\n");
            }

            switch (dst_type) {
            case Bytecode_Type::FLOAT32: *(f32*)(dst) = (f32)source; break;
            case Bytecode_Type::FLOAT64: *(f64*)(dst) = (f64)source; break;
            default: panic("what the frigg\n");
            }
        }

        break;
    }

    // Arithmetics
    case Primitive_Operation::ADDITION:
    case Primitive_Operation::SUBTRACTION:
    case Primitive_Operation::MULTIPLICATION:
    case Primitive_Operation::NEGATE:
    {
        switch (src_type)
        {
        case Bytecode_Type::INT8:    handle_ir_operation_arithmetic(operation, (i8*)dst, (i8*)src1, (i8*)src2); break;
        case Bytecode_Type::INT16:   handle_ir_operation_arithmetic(operation, (i16*)dst, (i16*)src1, (i16*)src2); break;
        case Bytecode_Type::INT32:   handle_ir_operation_arithmetic(operation, (i32*)dst, (i32*)src1, (i32*)src2); break;
        case Bytecode_Type::INT64:   handle_ir_operation_arithmetic(operation, (i64*)dst, (i64*)src1, (i64*)src2); break;
        case Bytecode_Type::UINT8:   handle_ir_operation_arithmetic(operation, (u8*)dst,  (u8*)src1,  (u8*)src2); break;
        case Bytecode_Type::UINT16:  handle_ir_operation_arithmetic(operation, (u16*)dst, (u16*)src1, (u16*)src2); break;
        case Bytecode_Type::UINT32:  handle_ir_operation_arithmetic(operation, (u32*)dst, (u32*)src1, (u32*)src2); break;
        case Bytecode_Type::UINT64:  handle_ir_operation_arithmetic(operation, (u64*)dst, (u64*)src1, (u64*)src2); break;
        case Bytecode_Type::FLOAT32: handle_ir_operation_arithmetic(operation, (f32*)dst, (f32*)src1, (f32*)src2); break;
        case Bytecode_Type::FLOAT64: handle_ir_operation_arithmetic(operation, (f64*)dst, (f64*)src1, (f64*)src2); break;
        default: panic("");
        }
        break;
    }

    case Primitive_Operation::LESS:
    case Primitive_Operation::LESS_OR_EQUAL:
    case Primitive_Operation::GREATER:
    case Primitive_Operation::GREATER_OR_EQUAL:
    {
        switch (src_type)
        {
        case Bytecode_Type::INT8:    handle_ir_operation_order_comparison(operation, (bool*)dst, (i8*)src1, (i8*)src2); break;
        case Bytecode_Type::INT16:   handle_ir_operation_order_comparison(operation, (bool*)dst, (i16*)src1, (i16*)src2); break;
        case Bytecode_Type::INT32:   handle_ir_operation_order_comparison(operation, (bool*)dst, (i32*)src1, (i32*)src2); break;
        case Bytecode_Type::INT64:   handle_ir_operation_order_comparison(operation, (bool*)dst, (i64*)src1, (i64*)src2); break;
        case Bytecode_Type::UINT8:   handle_ir_operation_order_comparison(operation, (bool*)dst, (u8*)src1, (u8*)src2); break;
        case Bytecode_Type::UINT16:  handle_ir_operation_order_comparison(operation, (bool*)dst, (u16*)src1, (u16*)src2); break;
        case Bytecode_Type::UINT32:  handle_ir_operation_order_comparison(operation, (bool*)dst, (u32*)src1, (u32*)src2); break;
        case Bytecode_Type::UINT64:  handle_ir_operation_order_comparison(operation, (bool*)dst, (u64*)src1, (u64*)src2); break;
        case Bytecode_Type::FLOAT32: handle_ir_operation_order_comparison(operation, (bool*)dst, (f32*)src1, (f32*)src2); break;
        case Bytecode_Type::FLOAT64: handle_ir_operation_order_comparison(operation, (bool*)dst, (f64*)src1, (f64*)src2); break;
        default: panic("");
        }
        break;
    }
    
    case Primitive_Operation::EQUAL:
    case Primitive_Operation::NOT_EQUAL:
    {
        switch (src_type)
        {
        case Bytecode_Type::INT8:    handle_ir_operation_equals(operation, (bool*)dst, (i8*)src1, (i8*)src2); break;
        case Bytecode_Type::INT16:   handle_ir_operation_equals(operation, (bool*)dst, (i16*)src1, (i16*)src2); break;
        case Bytecode_Type::INT32:   handle_ir_operation_equals(operation, (bool*)dst, (i32*)src1, (i32*)src2); break;
        case Bytecode_Type::INT64:   handle_ir_operation_equals(operation, (bool*)dst, (i64*)src1, (i64*)src2); break;
        case Bytecode_Type::UINT8:   handle_ir_operation_equals(operation, (bool*)dst, (u8*)src1, (u8*)src2); break;
        case Bytecode_Type::UINT16:  handle_ir_operation_equals(operation, (bool*)dst, (u16*)src1, (u16*)src2); break;
        case Bytecode_Type::UINT32:  handle_ir_operation_equals(operation, (bool*)dst, (u32*)src1, (u32*)src2); break;
        case Bytecode_Type::UINT64:  handle_ir_operation_equals(operation, (bool*)dst, (u64*)src1, (u64*)src2); break;
        case Bytecode_Type::FLOAT32: handle_ir_operation_equals(operation, (bool*)dst, (f32*)src1, (f32*)src2); break;
        case Bytecode_Type::FLOAT64: handle_ir_operation_equals(operation, (bool*)dst, (f64*)src1, (f64*)src2); break;
        default: panic("");
        }
        break;
    }

    case Primitive_Operation::AND: { *(bool*)dst = *(bool*)src1 && *(bool*)src2; break; }
    case Primitive_Operation::OR:  { *(bool*)dst = *(bool*)src1 || *(bool*)src2; break; }
    case Primitive_Operation::NOT: { *(bool*)dst = !(*(bool*)src1); break; }

    case Primitive_Operation::DIVISION:
    {
        switch (src_type)
        {
        case Bytecode_Type::INT8:   if ( *(i8*)src2 == 0) { return false; } *(i8*)dst  =  *(i8*)src1 / *(i8*)src2; break;
        case Bytecode_Type::INT16:  if (*(i16*)src2 == 0) { return false; } *(i16*)dst = *(i16*)src1 / *(i16*)src2; break;
        case Bytecode_Type::INT32:  if (*(i32*)src2 == 0) { return false; } *(i32*)dst = *(i32*)src1 / *(i32*)src2; break; 
        case Bytecode_Type::INT64:  if (*(i64*)src2 == 0) { return false; } *(i64*)dst = *(i64*)src1 / *(i64*)src2; break;
        case Bytecode_Type::UINT8:  if ( *(u8*)src2 == 0) { return false; } *(u8*)dst  =  *(u8*)src1 / *(u8*)src2; break;
        case Bytecode_Type::UINT16: if (*(u16*)src2 == 0) { return false; } *(u16*)dst = *(u16*)src1 / *(u16*)src2; break;
        case Bytecode_Type::UINT32: if (*(u32*)src2 == 0) { return false; } *(u32*)dst = *(u32*)src1 / *(u32*)src2; break;
        case Bytecode_Type::UINT64: if (*(u64*)src2 == 0) { return false; } *(u64*)dst = *(u64*)src1 / *(u64*)src2; break;
        case Bytecode_Type::FLOAT32:  *(f32*)dst = *(f32*)src1 / *(f32*)src2; break;
        case Bytecode_Type::FLOAT64:  *(f64*)dst = *(f64*)src1 / *(f64*)src2; break;
        default: panic("");
        }
        break;
    }
    case Primitive_Operation::MODULO:
    {
        switch (src_type)
        {
        case Bytecode_Type::INT8:   if ( *(i8*)src2 == 0) { return false; } *(i8*)dst  =  *(i8*)src1 % *(i8*)src2; break;
        case Bytecode_Type::INT16:  if (*(i16*)src2 == 0) { return false; } *(i16*)dst = *(i16*)src1 % *(i16*)src2; break;
        case Bytecode_Type::INT32:  if (*(i32*)src2 == 0) { return false; } *(i32*)dst = *(i32*)src1 % *(i32*)src2; break; 
        case Bytecode_Type::INT64:  if (*(i64*)src2 == 0) { return false; } *(i64*)dst = *(i64*)src1 % *(i64*)src2; break;
        case Bytecode_Type::UINT8:  if ( *(u8*)src2 == 0) { return false; } *(u8*)dst  =  *(u8*)src1 % *(u8*)src2; break;
        case Bytecode_Type::UINT16: if (*(u16*)src2 == 0) { return false; } *(u16*)dst = *(u16*)src1 % *(u16*)src2; break;
        case Bytecode_Type::UINT32: if (*(u32*)src2 == 0) { return false; } *(u32*)dst = *(u32*)src1 % *(u32*)src2; break;
        case Bytecode_Type::UINT64: if (*(u64*)src2 == 0) { return false; } *(u64*)dst = *(u64*)src1 % *(u64*)src2; break;
        default: panic("");
        }
        break;
    }

    case Primitive_Operation::BITWISE_NOT:
    case Primitive_Operation::BITWISE_AND:
    case Primitive_Operation::BITWISE_OR:
    case Primitive_Operation::BITWISE_XOR:
    case Primitive_Operation::BITWISE_SHIFT_LEFT:
    case Primitive_Operation::BITWISE_SHIFT_RIGHT:
	case Primitive_Operation::HIGHEST_SET_BIT:
	case Primitive_Operation::LOWEST_SET_BIT:
    {
        u64 value1 = 0;
        switch (src_type)
        {
        case Bytecode_Type::UINT8:  value1 = *(u8*)src1; break;
        case Bytecode_Type::UINT16: value1 = *(u16*)src1; break;
        case Bytecode_Type::UINT32: value1 = *(u32*)src1; break;
        case Bytecode_Type::UINT64: value1 = *(u64*)src1; break;
        default: panic("");
        }

        u64 value2 = 0;
        if (argument_count == 2) 
        {
            switch (right_type)
            {
            case Bytecode_Type::UINT8:  value2 = *(u8*)src2; break;
            case Bytecode_Type::UINT16: value2 = *(u16*)src2; break;
            case Bytecode_Type::UINT32: value2 = *(u32*)src2; break;
            case Bytecode_Type::UINT64: value2 = *(u64*)src2; break;
            default: panic("");
            }
        }

        u64 result = 0;
        switch (operation)
        {
        case Primitive_Operation::BITWISE_NOT:         result = ~value1; break;
        case Primitive_Operation::BITWISE_AND:         result = value1 & value2; break;
        case Primitive_Operation::BITWISE_OR:          result = value1 | value2; break;
        case Primitive_Operation::BITWISE_XOR:         result = value1 ^ value2; break;
        case Primitive_Operation::BITWISE_SHIFT_LEFT:  result = value1 << value2; break;
        case Primitive_Operation::BITWISE_SHIFT_RIGHT: result = value1 >> value2; break;
	    case Primitive_Operation::HIGHEST_SET_BIT:     result = integer_highest_set_bit_index(value1); break;
        case Primitive_Operation::LOWEST_SET_BIT:      result = integer_lowest_set_bit_index(value1); break;
        default: panic("");
        }

        switch (dst_type)
        {
        case Bytecode_Type::UINT8:  *(i8*)dst  = result; break;
        case Bytecode_Type::UINT16: *(i16*)dst = result; break;
        case Bytecode_Type::UINT32: *(i32*)dst = result; break;
        case Bytecode_Type::UINT64: *(i64*)dst = result; break;
        default: panic("");
        }

        break;
    }

    // Float binops
	case Primitive_Operation::FLOAT_MODULO:
	case Primitive_Operation::FLOAT_REMAINDER:
	case Primitive_Operation::ATAN2:
	case Primitive_Operation::POW:
    {
        f32(*binop_fn_f32)(f32 a, f32 b) = nullptr;
        f64(*binop_fn_f64)(f64 a, f64 b) = nullptr;

        switch (operation)
        {
        case Primitive_Operation::FLOAT_MODULO:    binop_fn_f64 = fmod; binop_fn_f32 = fmodf; break;
	    case Primitive_Operation::FLOAT_REMAINDER: binop_fn_f64 = remainder; binop_fn_f32 = remainderf; break;
	    case Primitive_Operation::ATAN2:           binop_fn_f64 = atan2; binop_fn_f32 = atan2f; break;
	    case Primitive_Operation::POW:             binop_fn_f64 = pow; binop_fn_f32 = powf; break;
        default: panic("");
        }

        switch (src_type)
        {
        case Bytecode_Type::FLOAT32: *(f32*)dst = binop_fn_f32(*(f32*)src1, *(f32*)src2); break;
        case Bytecode_Type::FLOAT64: *(f32*)dst = binop_fn_f64(*(f32*)src1, *(f32*)src2); break;
        default: panic("");
        }
        break;
    }

    case Primitive_Operation::FLOAT_ABS: 
	case Primitive_Operation::ROUND_UP:       
	case Primitive_Operation::ROUND_DOWN:        
	case Primitive_Operation::ROUND_TOWARDS_ZERO:
	case Primitive_Operation::ROUND_NEAREST:
	case Primitive_Operation::EXP:
	case Primitive_Operation::LN:
	case Primitive_Operation::LOG10:
	case Primitive_Operation::LOG2:
	case Primitive_Operation::SQUARE_ROOT:
	case Primitive_Operation::CUBE_ROOT:
	case Primitive_Operation::SIN:
	case Primitive_Operation::COS:
	case Primitive_Operation::TAN:
	case Primitive_Operation::ASIN:
	case Primitive_Operation::ACOS:
	case Primitive_Operation::ATAN:
	case Primitive_Operation::SINH:
	case Primitive_Operation::COSH:
	case Primitive_Operation::TANH:
	case Primitive_Operation::ASINH:
	case Primitive_Operation::ACOSH:
	case Primitive_Operation::ATANH:
    {
        f32(*unop_fn_f32)(f32 a) = nullptr;
        f64(*unop_fn_f64)(f64 a) = nullptr;

        switch (operation)
        {
        case Primitive_Operation::FLOAT_ABS:     unop_fn_f64 = fabs;  unop_fn_f32 = fabsf; break;
	    case Primitive_Operation::ROUND_UP:           unop_fn_f64 = ceil;  unop_fn_f32 = ceilf; break;
	    case Primitive_Operation::ROUND_DOWN:         unop_fn_f64 = floor; unop_fn_f32 = floorf; break;
	    case Primitive_Operation::ROUND_TOWARDS_ZERO: unop_fn_f64 = trunc; unop_fn_f32 = truncf; break;
	    case Primitive_Operation::ROUND_NEAREST:      unop_fn_f64 = round; unop_fn_f32 = roundf; break;
	    case Primitive_Operation::EXP:                unop_fn_f64 = exp;   unop_fn_f32 = expf; break;
	    case Primitive_Operation::LN:                 unop_fn_f64 = log;   unop_fn_f32 = logf; break;
	    case Primitive_Operation::LOG10:              unop_fn_f64 = log10; unop_fn_f32 = log10f; break;
	    case Primitive_Operation::LOG2:               unop_fn_f64 = log2;  unop_fn_f32 = log2f; break;
	    case Primitive_Operation::SQUARE_ROOT:        unop_fn_f64 = sqrt;  unop_fn_f32 = sqrtf; break;
	    case Primitive_Operation::CUBE_ROOT:          unop_fn_f64 = cbrt;  unop_fn_f32 = cbrtf; break;
	    case Primitive_Operation::SIN:                unop_fn_f64 = sin;   unop_fn_f32 = sinf; break;
	    case Primitive_Operation::COS:                unop_fn_f64 = cos;   unop_fn_f32 = cosf; break;
	    case Primitive_Operation::TAN:                unop_fn_f64 = tan;   unop_fn_f32 = tanf; break;
	    case Primitive_Operation::ASIN:               unop_fn_f64 = asin;  unop_fn_f32 = asinf; break;
	    case Primitive_Operation::ACOS:               unop_fn_f64 = acos;  unop_fn_f32 = acosf; break;
	    case Primitive_Operation::ATAN:               unop_fn_f64 = atan;  unop_fn_f32 = atanf; break;
	    case Primitive_Operation::SINH:               unop_fn_f64 = asinh; unop_fn_f32 = asinhf; break;
	    case Primitive_Operation::COSH:               unop_fn_f64 = acosh; unop_fn_f32 = acoshf; break;
	    case Primitive_Operation::TANH:               unop_fn_f64 = atanh; unop_fn_f32 = atanhf; break;
	    case Primitive_Operation::ASINH:              unop_fn_f64 = asinh; unop_fn_f32 = asinhf; break;
	    case Primitive_Operation::ACOSH:              unop_fn_f64 = acosh; unop_fn_f32 = acoshf; break;
	    case Primitive_Operation::ATANH:              unop_fn_f64 = atanh; unop_fn_f32 = atanhf; break;
        default: panic("");
        }

        switch (src_type)
        {
        case Bytecode_Type::FLOAT32: *(f32*)dst = unop_fn_f32(*(f32*)src1); break;
        case Bytecode_Type::FLOAT64: *(f32*)dst = unop_fn_f64(*(f32*)src1); break;
        default: panic("");
        }
        break;

        break;
    }
	case Primitive_Operation::IS_NAN:
    {
        switch (src_type)
        {
        case Bytecode_Type::FLOAT32: *(bool*)dst = isnan(*(f32*)src1); break;
        case Bytecode_Type::FLOAT64: *(bool*)dst = isnan(*(f64*)src1); break;
        default: panic("");
        }
        break;
    }
	case Primitive_Operation::IS_FINITE:
    {
        switch (src_type)
        {
        case Bytecode_Type::FLOAT32: *(bool*)dst = isfinite(*(f32*)src1); break;
        case Bytecode_Type::FLOAT64: *(bool*)dst = isfinite(*(f64*)src1); break;
        default: panic("");
        }
        break;
    }
	case Primitive_Operation::IS_INFINITE:
    {
        switch (src_type)
        {
        case Bytecode_Type::FLOAT32: *(bool*)dst = isinf(*(f32*)src1); break;
        case Bytecode_Type::FLOAT64: *(bool*)dst = isinf(*(f64*)src1); break;
        default: panic("");
        }
        break;
    }
    default: panic("");
    }

    return true;
}

