#include "compiler_misc.hpp"

#include "type_system.hpp"
#include "compiler.hpp"

const char* timing_task_to_string(Timing_Task task)
{
    switch (task)
    {
    case Timing_Task::LEXING: return "LEXING";
    case Timing_Task::PARSING: return "PARSING";
    case Timing_Task::ANALYSIS: return "ANALYSIS";
    case Timing_Task::CODE_GEN: return "CODE_GEN";
    case Timing_Task::CODE_EXEC: return "CODE_EXEC";
    case Timing_Task::RESET: return "RESET";
    case Timing_Task::OUTPUT: return "OUTPUT";
    case Timing_Task::FINISH: return "FINISH";
    default: panic("");
    }
    return "";
}

void hardcoded_type_append_to_string(String* string, Hardcoded_Type hardcoded)
{
    switch (hardcoded)
    {
    case Hardcoded_Type::ASSERT_FN:
        string_append_formated(string, "ASSERT");
        break;
    case Hardcoded_Type::TYPE_INFO:
        string_append_formated(string, "TYPE_INFO");
        break;
    case Hardcoded_Type::TYPE_OF:
        string_append_formated(string, "TYPE_OF");
        break;
    case Hardcoded_Type::PRINT_I32:
        string_append_formated(string, "PRINT_F32");
        break;
    case Hardcoded_Type::PRINT_F32:
        string_append_formated(string, "PRINT_F32");
        break;
    case Hardcoded_Type::PRINT_BOOL:
        string_append_formated(string, "PRINT_BOOL");
        break;
    case Hardcoded_Type::PRINT_LINE:
        string_append_formated(string, "PRINT_LINE");
        break;
    case Hardcoded_Type::PRINT_STRING:
        string_append_formated(string, "PRINT_STRING");
        break;
    case Hardcoded_Type::READ_I32:
        string_append_formated(string, "READ_I32");
        break;
    case Hardcoded_Type::READ_F32:
        string_append_formated(string, "READ_F32");
        break;
    case Hardcoded_Type::READ_BOOL:
        string_append_formated(string, "READ_BOOL");
        break;
    case Hardcoded_Type::RANDOM_I32:
        string_append_formated(string, "RANDOM_I32");
        break;
    case Hardcoded_Type::MALLOC_SIZE_I32:
        string_append_formated(string, "MALLOC_SIZE_I32");
        break;
    case Hardcoded_Type::FREE_POINTER:
        string_append_formated(string, "FREE_POINTER");
        break;
    default: panic("Should not happen");
    }
}

bool exit_code_is_valid(int value)
{
    return value >= (int)Exit_Code::SUCCESS && value <= (int)Exit_Code::INVALID_SWITCH_CASE;
}

void exit_code_append_to_string(String* string, Exit_Code code)
{
    switch (code)
    {
    case Exit_Code::ASSERTION_FAILED:
        string_append_formated(string, "ASSERTION_FAILED");
        break;
    case Exit_Code::OUT_OF_BOUNDS:
        string_append_formated(string, "OUT_OF_BOUNDS");
        break;
    case Exit_Code::RETURN_VALUE_OVERFLOW:
        string_append_formated(string, "RETURN_VALUE_OVERFLOW");
        break;
    case Exit_Code::STACK_OVERFLOW:
        string_append_formated(string, "STACK_OVERFLOW");
        break;
    case Exit_Code::SUCCESS:
        string_append_formated(string, "SUCCESS");
        break;
    case Exit_Code::COMPILATION_FAILED:
        string_append_formated(string, "COMPILATION_FAILED");
        break;
    case Exit_Code::EXTERN_FUNCTION_CALL_NOT_IMPLEMENTED:
        string_append_formated(string, "EXTERN_FUNCTION_CALL_NOT_IMPLEMENTED");
        break;
    case Exit_Code::ANY_CAST_INVALID:
        string_append_formated(string, "ANY_CAST_INVALID");
        break;
    case Exit_Code::INSTRUCTION_LIMIT_REACHED:
        string_append_formated(string, "INSTRUCTION_LIMIT_REACHED");
        break;
    case Exit_Code::INVALID_SWITCH_CASE:
        string_append_formated(string, "INVALID_SWITCH_CASE");
        break;
    case Exit_Code::CODE_ERROR_OCCURED:
        string_append_formated(string, "CODE_ERROR_OCCURED");
        break;
    default: panic("Hey");
    }
}

bool compare_array_memory(Constant_Deduplication* a, Constant_Deduplication* b) {
    if (a->data_size_in_byte != b->data_size_in_byte) {
        return false;
    }
    if (a->pool != b->pool) {
        return false;
    }

    void* a_ptr = a->data;
    if (a->is_pool_data) {
        a_ptr = &a->pool->buffer[a->offset];
    }
    void* b_ptr = b->data;
    if (b->is_pool_data) {
        b_ptr = &b->pool->buffer[b->offset];
    }
     
    return memory_compare(a_ptr, b_ptr, a->data_size_in_byte);
}

u64 hash_deduplication(Constant_Deduplication* a) {
    void* a_ptr = a->data;
    if (a->is_pool_data) {
        a_ptr = &a->pool->buffer[a->offset];
    }
    if (a->data_size_in_byte == 0 || a_ptr == 0) {
        return 0;
    }

    Array<byte> memory;
    memory.data = (byte*)a_ptr;
    memory.size = a->data_size_in_byte;
    return hash_memory(memory);
}

// Constant Pool
Constant_Pool constant_pool_create(Type_System* type_system)
{
    Constant_Pool result;
    result.buffer = dynamic_array_create_empty<byte>(2048);
    result.constants = dynamic_array_create_empty<Upp_Constant>(2048);
    result.references = dynamic_array_create_empty<Upp_Constant_Reference>(128);
    result.saved_pointers = hashtable_create_pointer_empty<void*, int>(32);
    result.deduplication_table = hashtable_create_empty<Constant_Deduplication, int>(16, hash_deduplication, compare_array_memory);
    result.type_system = type_system;
    result.max_buffer_size = 1024 * 1024; // 1 MB of constant buffer is allowed per default
    return result;
}

void constant_pool_destroy(Constant_Pool* pool) 
{
    dynamic_array_destroy(&pool->buffer);
    dynamic_array_destroy(&pool->constants);
    dynamic_array_destroy(&pool->references);
    hashtable_destroy(&pool->saved_pointers);
    hashtable_destroy(&pool->deduplication_table);
}

struct Offset_Result
{
    bool success;
    const char* error_message;
    int offset;
};

Offset_Result offset_result_make_success(int offset) {
    Offset_Result result;
    result.success = true;
    result.offset = offset;
    result.error_message = "";
    return result;
}

Offset_Result offset_result_make_error(const char* error) {
    Offset_Result result;
    result.success = false;
    result.error_message = error;
    result.offset = -1;
    return result;
}

Offset_Result constant_pool_add_constant_internal(Constant_Pool* pool, Type_Base* signature, Array<byte> bytes);

void* upp_constant_get_pointer(Constant_Pool* pool, Upp_Constant constant) {
    return (void*)&pool->buffer[constant.offset];
}

bool type_signature_contains_references(Type_Base* signature)
{
    switch (signature->type)
    {
    case Type_Type::PRIMITIVE: return false;
    case Type_Type::VOID_POINTER: return true;
    case Type_Type::POINTER: return true;
    case Type_Type::FUNCTION: return true;
    case Type_Type::STRUCT: 
    {
        auto& members = downcast<Type_Struct>(signature)->members;
        for (int i = 0; i < members.size; i++) {
            Struct_Member* member = &members[i];
            if (type_signature_contains_references(member->type)) {
                return true;
            }
        }
        return false;
    }
    case Type_Type::ENUM: return false;
    case Type_Type::ARRAY: return type_signature_contains_references(downcast<Type_Array>(signature)->element_type);
    case Type_Type::SLICE: return true;
    case Type_Type::TYPE_HANDLE: return false;
    case Type_Type::ERROR_TYPE: return false;
    default: panic("");
    }

    return false;
}

bool constant_pool_compare_constants(Constant_Pool* pool, Upp_Constant a, Upp_Constant b)
{
    /*
        It has to be assured that Struct-Constant Memory is initialized to zero,
        otherwise memory_compare cannot be used here, since the padding bytes may be uninitialized and therefore random

        Also, I could implement a deep comparison, but the use cases for that seem unclear

        Note: If we would have deduplication of constant values with references in them, then this could be a simple index check
    */

    if (a.type != b.type) return false;
    if (a.constant_index == b.constant_index || a.offset == b.offset) return true;

    byte* pool_data = (byte*) pool->buffer.data;
    byte* raw_data_a = &pool_data[a.offset];
    byte* raw_data_b = &pool_data[b.offset];
    Type_Base* signature = a.type;
    return memory_compare(raw_data_a, raw_data_b, signature->size);
}

// Return the error message
Optional<const char*> constant_pool_record_references(Constant_Pool* pool, int data_offset, Type_Base* signature)
{
    void* raw_data = &pool->buffer[data_offset];
    switch (signature->type)
    {
    case Type_Type::PRIMITIVE:
        break;
    case Type_Type::VOID_POINTER: {
        void* ptr_value = *(void**)raw_data;
        if (ptr_value != nullptr) {
            return optional_make_success("Contains void pointer that isn't null");
        }
        break;
    }
    case Type_Type::POINTER:
    {
        void* ptr_value = *(void**)raw_data;
        if (ptr_value == nullptr) {
            break;
        }
        Type_Base* points_to = downcast<Type_Pointer>(signature)->points_to_type;
        if (memory_is_readable(ptr_value, points_to->size)) 
        {
            Upp_Constant_Reference reference;
            reference.ptr_offset = data_offset;
            Offset_Result ptr_result = constant_pool_add_constant_internal(
                pool, points_to, array_create_static_as_bytes((byte*)ptr_value, points_to->size)
            );
            if (!ptr_result.success) return optional_make_success(ptr_result.error_message);
            reference.buffer_destination_offset = ptr_result.offset;
            dynamic_array_push_back(&pool->references, reference);
        }
        else {
            return optional_make_success("Constant data contains invalid pointer that isn't null");
        }
        break;
    }
    case Type_Type::FUNCTION: {
        return optional_make_success("Cannot save function pointers as constants currently!");
    }
    case Type_Type::STRUCT:
    {
        auto& type_system = compiler.type_system;
        auto& any_type = type_system.predefined_types.any_type;
        if (types_are_equal(signature, upcast(any_type))) 
        {
            Upp_Any* any = (Upp_Any*)raw_data;
            if (any->type.index >= (u32)type_system.types.size) {
                return optional_make_success("Any contained invalid type index");
            }
            Type_Base* pointed_to_type = type_system.types[any->type.index];

            // Check pointer
            if (any->data == 0) {
                break; // If nullptr, then we don't have a reference, so serialization is fine at this point
            }
            else if (memory_is_readable(any->data, pointed_to_type->size)) 
            {
                Upp_Constant_Reference reference;
                reference.ptr_offset = data_offset;
                Offset_Result data_result = constant_pool_add_constant_internal(pool, pointed_to_type, 
                    array_create_static_as_bytes((byte*)any->data, pointed_to_type->size)
                );
                if (!data_result.success) return optional_make_success(data_result.error_message);
                reference.buffer_destination_offset = data_result.offset;
                dynamic_array_push_back(&pool->references, reference);
            }
            else {
                return optional_make_success("Constant data contained slice with invalid data-pointer");
            }
            break; // Don't further handle any
        }

        // Loop over each member and call this function
        auto structure = downcast<Type_Struct>(signature);
        auto& members = structure->members;
        switch (structure->struct_type)
        {
        case AST::Structure_Type::STRUCT: 
        {
            for (int i = 0; i < members.size; i++) {
                Struct_Member* member = &members[i];
                auto member_error_opt = constant_pool_record_references(pool, data_offset + member->offset, member->type);
                if (member_error_opt.available) return member_error_opt;
            }
            break;
        }
        case AST::Structure_Type::C_UNION: {
            if (type_signature_contains_references(signature)) {
                return optional_make_success("Constant is/contains a c-union containing references, which cannot be serizalized");
            }
            break;
        }
        case AST::Structure_Type::UNION: 
        {
            Type_Base* tag_type = structure->tag_member.type;
            assert(tag_type->type == Type_Type::ENUM, "");
            Type_Enum* tag_enum = downcast<Type_Enum>(tag_type);
            auto& enum_members = tag_enum->members;

            int tag_value = *(int*)((byte*)raw_data + structure->tag_member.offset);
            int found_member_index = -1;
            for (int i = 0; i < enum_members.size; i++) 
            {
                auto member = &enum_members[i];
                if (member->value == tag_value) {
                    found_member_index = tag_value - 1;
                }
            }
            if (found_member_index != -1) {
                Struct_Member* member = &members[found_member_index];
                auto member_status = constant_pool_record_references(pool, data_offset + member->offset, member->type);
                if (member_status.available) return member_status;
            }
            else {
                return optional_make_success("Constant data contained union with invalid tag");
            }
            break;
        }
        default: panic("");
        }
        break;
    }
    case Type_Type::ENUM: {
        break;
    }
    case Type_Type::ARRAY: {
        auto array = downcast<Type_Array>(signature);
        if (!array->count_known) {
            return optional_make_success("Array size not known!");
        }
        if (type_signature_contains_references(array->element_type)) {
            for (int i = 0; i < array->element_count; i++) {
                int element_offset = i * array->element_type->size;
                auto element_status = constant_pool_record_references(pool, data_offset + element_offset, array->element_type);
                if (element_status.available) return element_status;
            }
        }
        break;
    }
    case Type_Type::SLICE: 
    {
        // Check if pointer is valid, if true, save slice data
        auto slice_type = downcast<Type_Slice>(signature);
        Upp_Slice_Base slice = *(Upp_Slice_Base*)raw_data;
        if (slice.data == nullptr || slice.size == 0) {
            break;
        }
        if (slice.size <= 0) {
            return optional_make_success("Constant data contained slice with negative size");
        }
        if (memory_is_readable(slice.data, slice_type->element_type->size * slice.size)) 
        {
            Upp_Constant_Reference reference;
            reference.ptr_offset = data_offset;
            Offset_Result data_result = constant_pool_add_constant_internal(
                pool, upcast(type_system_make_array(slice_type->element_type, true, slice.size)), 
                array_create_static_as_bytes((byte*)slice.data, slice_type->element_type->size * slice.size)
            );
            if (!data_result.success) return optional_make_success(data_result.error_message);
            reference.buffer_destination_offset = data_result.offset;
            dynamic_array_push_back(&pool->references, reference);
        }
        else {
            return optional_make_success("Constant data contained slice with invalid data-pointer");
        }
    }
    case Type_Type::TYPE_HANDLE:
    case Type_Type::ERROR_TYPE:
        break;
    default: panic("");
    }

    return optional_make_failure<const char*>();
}

Offset_Result constant_pool_add_constant_internal(Constant_Pool* pool, Type_Base* signature, Array<byte> bytes)
{
    // Check if deduplication is possible (Currently for values without references, maybe change later...)
    bool type_contains_references = type_signature_contains_references(signature);
    if (!type_contains_references) {
        Constant_Deduplication deduplication;
        deduplication.pool = pool;
        deduplication.data_size_in_byte = signature->size;
        deduplication.is_pool_data = false;
        deduplication.offset = -1;
        deduplication.data = bytes.data;
        int* offset_opt = hashtable_find_element(&pool->deduplication_table, deduplication);
        if (offset_opt != 0) {
            return offset_result_make_success(*offset_opt);
        }
    }

    // Handle cyclic references (Stop adding to pool once same pointer was found)
    {
        int* found_offset = hashtable_find_element(&pool->saved_pointers, (void*)bytes.data);
        if (found_offset != 0) {
            return offset_result_make_success(*found_offset);
        }
    }


    if (pool->buffer.size + signature->alignment + signature->size > pool->max_buffer_size) {
        return offset_result_make_error("Constant pool reached maximum buffer size");
    }

    // Reserve enough memory in pool
    dynamic_array_reserve(&pool->buffer, pool->buffer.size + signature->alignment + signature->size);

    // Align pool to type alignment
    while (pool->buffer.size % signature->alignment != 0) {
        dynamic_array_push_back(&pool->buffer, (byte)0);
    }

    // Copy data to pool
    int start_offset = pool->buffer.size;
    for (int i = 0; i < bytes.size; i++) {
        dynamic_array_push_back(&pool->buffer, bytes[i]);
    }
    hashtable_insert_element(&pool->saved_pointers, (void*)bytes.data, start_offset);

    // Handle references inside constant data
    auto error_message = constant_pool_record_references(pool, start_offset, signature);
    if (error_message.available) {
        dynamic_array_rollback_to_size(&pool->buffer, start_offset);
        return offset_result_make_error(error_message.value);
    }

    // Add to deduplication list
    if (!type_contains_references) {
        Constant_Deduplication deduplication;
        deduplication.pool = pool;
        deduplication.data_size_in_byte = signature->size;
        deduplication.is_pool_data = true;
        deduplication.offset = start_offset;
        deduplication.data = 0;
        hashtable_insert_element(&pool->deduplication_table, deduplication, start_offset);
    }

    return offset_result_make_success(start_offset);
}

Constant_Pool_Result constant_pool_add_constant(Constant_Pool* pool, Type_Base* signature, Array<byte> bytes)
{
    int rewind_index = pool->buffer.size;
    hashtable_reset(&pool->saved_pointers);
    Offset_Result offset_result = constant_pool_add_constant_internal(pool, signature, bytes);
    if (!offset_result.success) {
        Constant_Pool_Result result;
        result.success = false;
        result.error_message = offset_result.error_message;
        result.constant.constant_index = -1;
        result.constant.offset = -1;
        result.constant.type = 0;
        return result;
    }

    Upp_Constant constant;
    constant.type = signature;
    constant.offset = offset_result.offset;
    constant.constant_index = pool->constants.size;
    dynamic_array_push_back(&pool->constants, constant);

    Constant_Pool_Result result;
    result.success = true;
    result.error_message = "";
    result.constant = constant;
    return result;
}



// Extern sources
Extern_Sources extern_sources_create()
{
    Extern_Sources result;
    result.extern_functions = dynamic_array_create_empty<Extern_Function_Identifier>(8);
    result.headers_to_include = dynamic_array_create_empty<String*>(8);
    result.source_files_to_compile = dynamic_array_create_empty<String*>(8);
    result.lib_files = dynamic_array_create_empty<String*>(8);
    result.extern_type_signatures = hashtable_create_pointer_empty<Type_Base*, String*>(8);
    return result;
}

void extern_sources_destroy(Extern_Sources* sources)
{
    dynamic_array_destroy(&sources->extern_functions);
    dynamic_array_destroy(&sources->headers_to_include);
    dynamic_array_destroy(&sources->source_files_to_compile);
    dynamic_array_destroy(&sources->lib_files);
    hashtable_destroy(&sources->extern_type_signatures);
}



// Identifier Pool
Identifier_Pool identifier_pool_create()
{
    Identifier_Pool result;
    result.identifier_lookup_table = hashtable_create_empty<String, String*>(128, hash_string, string_equals);
    return result;
}

void identifier_pool_destroy(Identifier_Pool* pool)
{
    auto iter = hashtable_iterator_create(&pool->identifier_lookup_table);
    while (hashtable_iterator_has_next(&iter)) {
        String* str = *iter.value;
        string_destroy(str);
        delete str;
        hashtable_iterator_next(&iter);
    }
    hashtable_destroy(&pool->identifier_lookup_table);
}

String* identifier_pool_add(Identifier_Pool* pool, String identifier)
{
    String** found = hashtable_find_element(&pool->identifier_lookup_table, identifier);
    if (found != 0) {
        return *found;
    }
    else {
        String* copy = new String;
        *copy = string_create_empty(identifier.size);
        string_append_string(copy, &identifier);
        hashtable_insert_element(&pool->identifier_lookup_table, *copy, copy);
        return copy;
    }
}

void identifier_pool_print(Identifier_Pool* pool)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Identifiers: ");

    auto iter = hashtable_iterator_create(&pool->identifier_lookup_table);
    int i = 0;
    while (hashtable_iterator_has_next(&iter)) {
        String* str = *iter.value;
        string_append_formated(&msg, "\n\t%d: %s", i, str->characters);
        hashtable_iterator_next(&iter);
        i++;
    }
    string_append_formated(&msg, "\n");
    logg("%s", msg.characters);
}



// FIBER POOL
struct Fiber_Startup_Info // Just a helper struct for setting up initial fibers in pool
{
    Fiber_Pool* pool;
    int index_in_pool;
};

struct Fiber_Info
{
    Fiber_Handle handle;
    bool has_task_to_run; // If the fiber is currently executing a task or if its waiting for a new one
    fiber_entry_fn next_entry;
    void* next_userdata;
};

struct Fiber_Pool
{
    Fiber_Handle main_fiber; // Fiber that created the fiber pool
    Dynamic_Array<Fiber_Info> allocated_fibers;
    Dynamic_Array<int> next_free_index;
};

Fiber_Pool* fiber_pool_create() {
    Fiber_Pool* pool = new Fiber_Pool;
    pool->allocated_fibers = dynamic_array_create_empty<Fiber_Info>(1);
    pool->next_free_index = dynamic_array_create_empty<int>(1);
    if (!fiber_initialize()) {
        panic("Couldn't create fiber_pool, fiber initialization failed!\n");
    }
    pool->main_fiber = fiber_get_current();
    return pool;
}

void fiber_pool_destroy(Fiber_Pool* pool) {
    for (int i = 0; i < pool->allocated_fibers.size; i++) {
        fiber_delete(pool->allocated_fibers[i].handle);
    }
    dynamic_array_destroy(&pool->allocated_fibers);
    dynamic_array_destroy(&pool->next_free_index);
    delete pool;
}

void fiber_pool_instance_entry(void* userdata) {
    // Copy startup info to local stack
    Fiber_Startup_Info startup = *((Fiber_Startup_Info*)userdata);
    auto pool = startup.pool;
    // Switch to main fiber after copy and wait for commands
    fiber_switch_to(pool->main_fiber);

    while (true)
    {
        Fiber_Info info = pool->allocated_fibers[startup.index_in_pool];
        assert(info.has_task_to_run, "Trying to run a fiber without a given task!");
        if (info.next_entry == 0) {
            panic("Fiber pool instance started without properly setting the entryfn, should never happen!");
            break;
        }
        info.next_entry(info.next_userdata);
        pool->allocated_fibers[startup.index_in_pool].has_task_to_run = false;

        // When the fiber's job finishes, we add it back to the pool, and switch to the main fiber
        dynamic_array_push_back(&pool->next_free_index, startup.index_in_pool);
        pool->allocated_fibers[startup.index_in_pool].next_entry = 0; // Just for error detection
        pool->allocated_fibers[startup.index_in_pool].next_userdata = 0;
        fiber_switch_to(pool->main_fiber);
    }
}

Fiber_Pool_Handle fiber_pool_get_handle(Fiber_Pool* pool, fiber_entry_fn entry_fn, void* userdata) {
    // Setup new fiber if no free one is in pool
    if (pool->next_free_index.size == 0)
    {
        Fiber_Startup_Info startup;
        Fiber_Info info;
        info.handle = fiber_create(fiber_pool_instance_entry, &startup); // Note: startup is not filled out yet, but the pointer is still valid!
        info.next_entry = 0;
        info.next_userdata = 0;
        info.has_task_to_run = false;
        dynamic_array_push_back(&pool->allocated_fibers, info);
        dynamic_array_push_back(&pool->next_free_index, pool->allocated_fibers.size - 1);

        // Switch to fiber so it can grab its startup info, see 'fiber_pool_instance_entry'
        startup.pool = pool;
        startup.index_in_pool = pool->allocated_fibers.size - 1;
        fiber_switch_to(info.handle);
    }

    // Grab fiber from pool and setup to run the given function
    Fiber_Pool_Handle handle;
    handle.pool = pool;
    handle.pool_index = pool->next_free_index[pool->next_free_index.size - 1];
    pool->next_free_index.size = pool->next_free_index.size - 1; // Pop

    Fiber_Info* info = &pool->allocated_fibers[handle.pool_index];
    assert(!info->has_task_to_run, "We seem to be grabbing a function thats currently running!");
    info->next_entry = entry_fn;
    info->next_userdata = userdata;
    info->has_task_to_run = true;

    return handle;
}

bool fiber_pool_switch_to_handel(Fiber_Pool_Handle handle)
{
    Fiber_Info* info = &handle.pool->allocated_fibers[handle.pool_index];
    assert(info->has_task_to_run, "Fiber_Pool_Handle seems to be invalid, e.g. the task was already finished\n");
    fiber_switch_to(info->handle);
    // After return, return if task has completed
    info = &handle.pool->allocated_fibers[handle.pool_index]; // Refresh info pointer, just in case 
    return !info->has_task_to_run;
}

void fiber_pool_check_all_handles_completed(Fiber_Pool* pool) {
    for (int i = 0; i < pool->allocated_fibers.size; i++) {
        auto& info = pool->allocated_fibers[i];
        assert(!info.has_task_to_run, "Task must be completed!\n");
        info.next_entry = 0;
        info.next_userdata = 0;

        // Check that fiber is in next_free_entry list
        bool found = false;
        for (int j = 0; j < pool->next_free_index.size; j++) {
            if (pool->next_free_index[j] == i) {
                found = true;
                break;
            }
        }
        assert(found, "Finished fiber must be in next_free_entry list!\n");
    }

    // Reset next_free entry list
    assert(pool->next_free_index.size == pool->allocated_fibers.size, "Must be the same, since all fibers should be completed!\n");
    dynamic_array_reset(&pool->next_free_index);
    for (int i = 0; i < pool->allocated_fibers.size; i++) {
        dynamic_array_push_back(&pool->next_free_index, i);
    }
}

void fiber_pool_switch_to_main_fiber(Fiber_Pool* pool)
{
    fiber_switch_to(pool->main_fiber);
}



// Fiber Pool test
void test_print_int_task(void* userdata)
{
    int value = *((int*)userdata);
    logg("Fiber with userdata #%d working\n", value);
}

void test_pause_3_task(void* userdata)
{
    Fiber_Pool* pool = (Fiber_Pool*)userdata;
    logg("Wait 1\n");
    fiber_pool_switch_to_main_fiber(pool);
    logg("Wait 2\n");
    fiber_pool_switch_to_main_fiber(pool);
    logg("Wait 3\n");
    fiber_pool_switch_to_main_fiber(pool);
    logg("Finish\n");
}

void fiber_pool_test()
{
    Fiber_Pool* pool = fiber_pool_create();
    SCOPE_EXIT(fiber_pool_destroy(pool));

    int a = 1;
    int b = 2;
    int c = 3;
    Fiber_Pool_Handle handle1 = fiber_pool_get_handle(pool, test_print_int_task, &a);
    Fiber_Pool_Handle handle2 = fiber_pool_get_handle(pool, test_print_int_task, &b);
    bool finished = fiber_pool_switch_to_handel(handle1);
    assert(finished, "Must be finished now\n");
    finished = fiber_pool_switch_to_handel(handle2);
    assert(finished, "Must be finished now\n");

    Fiber_Pool_Handle pausing = fiber_pool_get_handle(pool, test_pause_3_task, pool);
    finished = false;
    while (!finished) {
        logg("switch to pausing\n");
        finished = fiber_pool_switch_to_handel(pausing);
    }
    logg("Returned from pausing!\n");
    
    assert(pool->allocated_fibers.size == 2, "Must not have allocated 3, since only max of 2 fibers at a time were active\n");
}




