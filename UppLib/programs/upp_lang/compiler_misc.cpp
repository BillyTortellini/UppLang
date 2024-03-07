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
    return value >= (int)Exit_Code::SUCCESS && value <= (int)Exit_Code::TYPE_INFO_WAITING_FOR_TYPE_FINISHED;
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
    case Exit_Code::TYPE_INFO_WAITING_FOR_TYPE_FINISHED:
        string_append_formated(string, "TYPE_INFO_WAITING_FOR_TYPE_FINISHED");
        break;
    default: panic("Hey");
    }
}

// CORRECTNESS: Structs may have padding, and padding memory may be uninitialized, so just a mem_compare may be insufficient
//              To be fair, currently this only leads to constants not being deduplicated, so it's not a big deal,
//              but if other code uses this deduplication to check if two constants are the same, it could lead to problems
bool compare_array_memory(Constant_Deduplication* a, Constant_Deduplication* b) {
    double start = timer_current_time_in_seconds(compiler.timer);
    SCOPE_EXIT(compiler.constant_pool.time_in_comparison += timer_current_time_in_seconds(compiler.timer) - start;);

    if (!types_are_equal(a->type, b->type)) {
        return false;
    }
    if (a->data_size_in_byte != b->data_size_in_byte) {
        return false;
    }
    if (a->pool != b->pool) {
        return false;
    }
    return memory_compare(a->data, b->data, a->data_size_in_byte);
}

u64 hash_deduplication(Constant_Deduplication* a) {
    double start = timer_current_time_in_seconds(compiler.timer);
    SCOPE_EXIT(compiler.constant_pool.time_in_comparison += timer_current_time_in_seconds(compiler.timer) - start;);
    if (a->data_size_in_byte == 0 || a->data == 0) {
        return 0;
    }

    Array<byte> memory;
    memory.data = (byte*)a->data;
    memory.size = a->data_size_in_byte;
    u64 hash = hash_memory(memory);
    hash = hash_combine(hash, hash_i32(&a->data_size_in_byte));
    hash = hash_combine(hash, hash_pointer(a->type));
    return hash;
}

// Constant Pool
Constant_Pool constant_pool_create(Type_System* type_system)
{
    Constant_Pool result;
    result.constant_memory = stack_allocator_create_empty(2048);
    result.constants = dynamic_array_create_empty<Upp_Constant>(2048);
    result.references = dynamic_array_create_empty<Upp_Constant_Reference>(128);
    result.saved_pointers = hashtable_create_pointer_empty<void*, Upp_Constant>(32);
    result.deduplication_table = hashtable_create_empty<Constant_Deduplication, Upp_Constant>(16, hash_deduplication, compare_array_memory);
    result.type_system = type_system;
    return result;
}

void constant_pool_destroy(Constant_Pool* pool) 
{
    stack_allocator_destroy(&pool->constant_memory);
    dynamic_array_destroy(&pool->constants);
    dynamic_array_destroy(&pool->references);
    hashtable_destroy(&pool->saved_pointers);
    hashtable_destroy(&pool->deduplication_table);
}

Constant_Pool_Result constant_pool_result_make_success(Constant_Pool* pool, Upp_Constant constant) {
    Constant_Pool_Result result;
    result.success = true;
    result.error_message = "";
    result.constant = constant;
    return result;
}

Constant_Pool_Result constant_pool_result_make_error(const char* error) {
    Constant_Pool_Result result;
    result.success = false;
    result.error_message = error;
    return result;
}

bool type_signature_contains_references_rec(Type_Base* signature)
{
    switch (signature->type)
    {
    case Type_Type::PRIMITIVE: return false;
    case Type_Type::VOID_POINTER: return true;
    case Type_Type::POLYMORPHIC: return false;
    case Type_Type::POINTER: return true;
    case Type_Type::FUNCTION: return true;
    case Type_Type::STRUCT: 
    {
        auto& members = downcast<Type_Struct>(signature)->members;
        for (int i = 0; i < members.size; i++) {
            Struct_Member* member = &members[i];
            if (type_signature_contains_references_rec(member->type)) {
                return true;
            }
        }
        return false;
    }
    case Type_Type::ENUM: return false;
    case Type_Type::ARRAY: return type_signature_contains_references_rec(downcast<Type_Array>(signature)->element_type);
    case Type_Type::SLICE: return true;
    case Type_Type::TYPE_HANDLE: return false;
    case Type_Type::ERROR_TYPE: return false;
    default: panic("");
    }

    return false;
}

bool type_signature_contains_references(Type_Base* signature) {
    double start = timer_current_time_in_seconds(compiler.timer);
    auto result = type_signature_contains_references_rec(signature);
    double end = timer_current_time_in_seconds(compiler.timer);
    compiler.constant_pool.time_contains_reference += end - start;
    return result;
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
    if (a.constant_index == b.constant_index || a.memory == b.memory) return true;

    Type_Base* signature = a.type;
    return memory_compare(a.memory, b.memory, signature->size);
}

Constant_Pool_Result constant_pool_add_constant_internal(Constant_Pool* pool, Type_Base* signature, Array<byte> bytes);

// Returns an error_message on failure
// Also, this message gets invoced on e.g. all struct members of a struct (But with a non-zero offset from base)
Optional<const char*> constant_pool_deepcopy_references_recursive(Constant_Pool* pool, Upp_Constant constant, Type_Base* signature, int offset_from_base)
{
    pool->deepcopy_counts += 1;

    auto record_pointer_reference = [&](void** pointer_ptr, Type_Base* type) -> Optional<const char*>
    {
        void* pointer = *pointer_ptr;
        // Check if pointer is null
        if (pointer == nullptr) {
            return optional_make_failure<const char*>(); // Nullptrs can be stored as nullpointer, so this is fine
        }
        if (!memory_is_readable(pointer, type->size)) {
            return optional_make_success("Constant data contains invalid pointer that isn't null");
        }

        Constant_Pool_Result referenced_constant = constant_pool_add_constant_internal(
            pool, type, array_create_static_as_bytes((byte*)pointer, type->size)
        );
        if (!referenced_constant.success) return optional_make_success(referenced_constant.error_message);

        // Update pointer
        *pointer_ptr = referenced_constant.constant.memory;

        // Store reference
        Upp_Constant_Reference reference;
        reference.constant = constant;
        reference.pointer_member_byte_offset = (byte*)pointer_ptr - constant.memory;
        assert(reference.pointer_member_byte_offset >= 0 && reference.pointer_member_byte_offset <= constant.type->size, "");
        reference.points_to = referenced_constant.constant;
        dynamic_array_push_back(&pool->references, reference);

        return optional_make_failure<const char*>();
    };

    byte* raw_data = (byte*)constant.memory + offset_from_base;
    switch (signature->type)
    {
    case Type_Type::POLYMORPHIC:
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
        Type_Base* points_to = downcast<Type_Pointer>(signature)->points_to_type;
        return record_pointer_reference((void**)raw_data, points_to);
    }
    case Type_Type::FUNCTION: {
        return optional_make_success("Cannot save function pointers as constants currently!");
    }
    case Type_Type::STRUCT:
    {
        auto& type_system = compiler.type_system;

        // Check if it's any type
        auto& any_type = type_system.predefined_types.any_type;
        if (types_are_equal(signature, upcast(any_type)))
        {
            Upp_Any* any = (Upp_Any*)raw_data;
            if (any->type.index >= (u32)type_system.types.size) {
                return optional_make_success("Any contained invalid type index");
            }
            Type_Base* pointed_to_type = type_system.types[any->type.index];
            return record_pointer_reference((void**)raw_data, pointed_to_type);
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
                auto member_error_opt = constant_pool_deepcopy_references_recursive(pool, constant, member->type, offset_from_base + member->offset);
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
                auto member_status = constant_pool_deepcopy_references_recursive(pool, constant, member->type, offset_from_base + member->offset);
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
                auto element_status = constant_pool_deepcopy_references_recursive(pool, constant, array->element_type, offset_from_base + element_offset);
                if (element_status.available) return element_status;
            }
        }
        break;
    }
    case Type_Type::SLICE:
    {
        // Check if pointer is valid, if true, save slice data
        auto slice_type = downcast<Type_Slice>(signature);
        Upp_Slice_Base* slice = (Upp_Slice_Base*)raw_data;
        if (slice->data == nullptr || slice->size == 0) {
            break;
        }
        if (slice->size <= 0) {
            return optional_make_success("Constant data contained slice with negative size");
        }

        return record_pointer_reference((void**)&slice->data, upcast(type_system_make_array(slice_type->element_type, true, slice->size)));
    }
    case Type_Type::TYPE_HANDLE:
    case Type_Type::ERROR_TYPE:
        break;
    default: panic("");
    }

    return optional_make_failure<const char*>();
}

Constant_Pool_Result constant_pool_add_constant_internal(Constant_Pool* pool, Type_Base* signature, Array<byte> bytes)
{
    pool->added_internal_constants += 1;

    auto checkpoint = stack_checkpoint_make(&pool->constant_memory);
    int rewind_constant_count = pool->constants.size;
    int rewind_reference_count = pool->references.size;

    // Handle cyclic references (Stop adding to pool once same pointer was found)
    {
        Upp_Constant* already_saved_index = hashtable_find_element(&pool->saved_pointers, (void*)bytes.data);
        if (already_saved_index != 0) {
            return constant_pool_result_make_success(pool, *already_saved_index);
        }
    }

    // Check if deduplication is possible (Currently for values without references, maybe change later...)
    bool type_contains_references = type_signature_contains_references(signature);
    if (!type_contains_references) {
        pool->duplication_checks += 1;
        Constant_Deduplication deduplication;
        deduplication.pool = pool;
        deduplication.data_size_in_byte = signature->size;
        deduplication.data = bytes.data;
        deduplication.type = signature;
        Upp_Constant* deduplicated = hashtable_find_element(&pool->deduplication_table, deduplication);
        if (deduplicated != 0) {
            return constant_pool_result_make_success(pool, *deduplicated);
        }
    }

    // Create shallow copy in pool memory
    Upp_Constant constant;
    constant.constant_index = pool->constants.size;
    constant.type = signature;
    constant.memory = (byte*)stack_allocator_allocate_size(&pool->constant_memory, signature->size, signature->alignment);
    dynamic_array_push_back(&pool->constants, constant);
    memory_copy(constant.memory, bytes.data, signature->size);
    hashtable_insert_element(&pool->saved_pointers, (void*)bytes.data, constant);

    // Convert shallow copy to deepcopy by copying all references
    auto error_message = constant_pool_deepcopy_references_recursive(pool, constant, signature, 0);
    if (error_message.available) {
        stack_checkpoint_rewind(checkpoint);
        dynamic_array_rollback_to_size(&pool->constants, rewind_constant_count);
        dynamic_array_rollback_to_size(&pool->references, rewind_reference_count);
        return constant_pool_result_make_error(error_message.value);
    }

    // Add to deduplication list
    if (!type_contains_references) {
        Constant_Deduplication deduplication;
        deduplication.pool = pool;
        deduplication.type = constant.type;
        deduplication.data_size_in_byte = signature->size;
        deduplication.data = constant.memory;
        hashtable_insert_element(&pool->deduplication_table, deduplication, constant);
    }

    return constant_pool_result_make_success(pool, constant);
}

Constant_Pool_Result constant_pool_add_constant(Constant_Pool* pool, Type_Base* signature, Array<byte> bytes)
{
    pool->added_internal_constants = 0;
    pool->duplication_checks = 0;
    pool->time_contains_reference = 0;
    pool->deepcopy_counts = 0;
    pool->time_in_comparison = 0;
    pool->time_in_hash = 0;
    hashtable_reset(&pool->saved_pointers);
    return constant_pool_add_constant_internal(pool, signature, bytes);
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




