#include "constant_pool.hpp"

#include "compiler.hpp"
#include "semantic_analyser.hpp"

bool deduplication_info_is_equal(Deduplication_Info* a, Deduplication_Info* b) {
    double start = timer_current_time_in_seconds(compiler.timer);
    SCOPE_EXIT(compiler.constant_pool.time_in_comparison += timer_current_time_in_seconds(compiler.timer) - start;);

    if (!types_are_equal(a->type, b->type) || a->memory.size != b->memory.size) return false;
    return memory_compare(a->memory.data, b->memory.data, a->memory.size);
}

u64 hash_deduplication(Deduplication_Info* info) {
    double start = timer_current_time_in_seconds(compiler.timer);
    SCOPE_EXIT(compiler.constant_pool.time_in_comparison += timer_current_time_in_seconds(compiler.timer) - start;);

    u64 hash = hash_memory(info->memory);
    hash = hash_combine(hash, hash_i32(&info->memory.size));
    hash = hash_combine(hash, hash_pointer(info->type));
    return hash;
}

// Constant Pool
Constant_Pool constant_pool_create()
{
    Constant_Pool result;
    result.constant_memory = stack_allocator_create_empty(2048);
    result.constants = dynamic_array_create_empty<Upp_Constant>(2048);
    result.references = dynamic_array_create_empty<Upp_Constant_Reference>(128);
    result.function_references = dynamic_array_create_empty<Upp_Constant_Function_Reference>(32);
    result.saved_pointers = hashtable_create_pointer_empty<void*, Upp_Constant>(32);
    result.deduplication_table = hashtable_create_empty<Deduplication_Info, Upp_Constant>(16, hash_deduplication, deduplication_info_is_equal);
    return result;
}

void constant_pool_destroy(Constant_Pool* pool) 
{
    stack_allocator_destroy(&pool->constant_memory);
    dynamic_array_destroy(&pool->constants);
    dynamic_array_destroy(&pool->references);
    dynamic_array_destroy(&pool->function_references);
    hashtable_destroy(&pool->saved_pointers);
    hashtable_destroy(&pool->deduplication_table);
}

bool upp_constant_is_equal(Upp_Constant a, Upp_Constant b) {
    return a.constant_index == b.constant_index;
}



Constant_Pool_Result constant_pool_result_make_success(Upp_Constant constant) {
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

struct Pointer_Info
{
    void** pointer_address;
    void* pointer_value;
    Datatype* points_to_type;
    int array_size; // For normal pointers 1, for slices >= 1
    Upp_Constant added_internal_constant;
};

Pointer_Info pointer_info_make(void** pointer_address, Datatype* points_to_type, int array_size) {
    Pointer_Info result;
    result.pointer_address = pointer_address;
    result.pointer_value = *pointer_address;
    result.points_to_type = points_to_type;
    result.array_size = array_size;
    result.added_internal_constant.constant_index = -1;
    return result;
}

Upp_Constant_Function_Reference function_reference_make(int offset, ModTree_Function* function)
{
    Upp_Constant_Function_Reference result;
    result.offset_from_constant_start = offset;
    result.points_to = function;
    return result;
}

// Doesn't record null-pointers, as those aren't necessary, returns false if a void-pointer isn't set null
// Checks slice size and checks any type
// Also checks if function pointers are present
bool record_pointers_and_set_padding_bytes_zero_recursive(
    Datatype* signature, int array_size, Array<byte> bytes, int start_offset, int offset_per_element, 
    Dynamic_Array<Pointer_Info>& pointer_infos, Dynamic_Array<Upp_Constant_Function_Reference>& function_references)
{
    assert(signature->memory_info.available, "Otherwise how could the bytes have been generated without knowing size of type?");
    auto& memory_info = signature->memory_info.value;
    
    // Early exit if there's nothing to do
    if (!memory_info.contains_padding_bytes && !memory_info.contains_reference && !memory_info.contains_function_pointer) {
        return true;
    }

    switch (signature->type)
    {
    case Datatype_Type::TYPE_HANDLE:
    case Datatype_Type::ERROR_TYPE:
    case Datatype_Type::ENUM:
    case Datatype_Type::TEMPLATE_PARAMETER:
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
    case Datatype_Type::PRIMITIVE: {
        return true;
    }
    case Datatype_Type::FUNCTION: {
        auto& functions = compiler.semantic_analyser->program->functions;
        for (int i = 0; i < array_size; i++) {
            int function_pointer_offset = start_offset + i * offset_per_element;
            i64 function_index = *(i64*)(bytes.data + function_pointer_offset) - 1;
            if (function_index == -1) {
                continue; // Note: Function indices are stored + 1 so that 0 equals nullpointer
            }
            if (function_index < 0 || function_index >= functions.size) {
                return false;
            }
            dynamic_array_push_back(&function_references, function_reference_make(function_pointer_offset, functions[function_index]));
        }
        return true;
    }
    case Datatype_Type::POINTER:
    {
        Datatype* points_to = downcast<Datatype_Pointer>(signature)->points_to_type;
        for (int i = 0; i < array_size; i++) {
            void** pointer_address = (void**)(bytes.data + start_offset + offset_per_element * i);
            if (*pointer_address != nullptr) { // Dont record null-pointers
                dynamic_array_push_back(&pointer_infos, pointer_info_make(pointer_address, points_to, 1));
            }
        }
        return true;
    }
    case Datatype_Type::SLICE:
    {
        Datatype* points_to = downcast<Datatype_Slice>(signature)->element_type;
        for (int i = 0; i < array_size; i++) {
            Upp_Slice_Base* slice = (Upp_Slice_Base*)(bytes.data + start_offset + offset_per_element * i);

            // Fill padding
            assert(sizeof(Upp_Slice_Base) == 16 && sizeof(slice->size) == 4, "Setting the padding to zero is only required when slices have i32 as size");
            memory_set_bytes(((byte*)slice) + 12, 4, 0);

            // Record pointer
            if (slice->size == 0) { // Sanitize slice
                slice->data = 0;
            }
            else if (slice->size < 0) { 
                return false;
            }
            if (slice->data == 0) {
                continue;
            }
            dynamic_array_push_back(&pointer_infos, pointer_info_make(&slice->data, points_to, slice->size)); // Note: Slice size check is done later
        }

        return true;
    }
    case Datatype_Type::VOID_POINTER: {
        for (int i = 0; i < array_size; i++) {
            void** pointer_address = (void**) (bytes.data + start_offset + offset_per_element * i);
            if (*pointer_address != nullptr) {
                return false;
            }
        }
        return true;
    }
    case Datatype_Type::ARRAY: {
        auto array = downcast<Datatype_Array>(signature);
        if (!array->count_known) {
            return true;
        }
        // Handle arrays of arrays efficiently
        if (array_size == 1 || offset_per_element == memory_info.size) {
            return record_pointers_and_set_padding_bytes_zero_recursive(
                array->element_type, array->element_count * array_size, bytes, start_offset, 
                array->element_type->memory_info.value.size, pointer_infos, function_references);
        }

        // Search all arrays seperately
        for (int i = 0; i < array_size; i++) {
            int element_offset = start_offset + offset_per_element * i;
            bool success = record_pointers_and_set_padding_bytes_zero_recursive(
                array->element_type, array->element_count, bytes, element_offset, 
                array->element_type->memory_info.value.size, pointer_infos, function_references);
            if (!success) {
                return false;
            }
        }
        return true;
    }
    case Datatype_Type::STRUCT:
    {
        auto& type_system = compiler.type_system;

        // Check if it's any type
        auto& any_type = type_system.predefined_types.any_type;
        if (types_are_equal(signature, upcast(any_type)))
        {
            for (int i = 0; i < array_size; i++) {
                Upp_Any* any = (Upp_Any*) (bytes.data + start_offset + offset_per_element * i);
                if (any->type.index >= (u32)type_system.types.size) {
                    return false;
                }

                // Fill padding to 0
                assert(sizeof(Upp_Any) == 16 && sizeof(any->type) == 4, "Setting the padding to zero is only required when slices have i32 as size");
                memory_set_bytes(((byte*)any) + 12, 4, 0);

                if (any->data == 0) {
                    continue;
                }
                Datatype* pointed_to_type = type_system.types[any->type.index];
                dynamic_array_push_back(&pointer_infos, pointer_info_make(&any->data, pointed_to_type, 1)); // Note: Slice size check is done later
            }
            return true;
        }

        // Loop over each member and call this function recursively
        auto structure = downcast<Datatype_Struct>(signature);
        auto& members = structure->members;
        switch (structure->struct_type)
        {
        case AST::Structure_Type::STRUCT:
        {
            for (int member_index = 0; member_index < members.size; member_index++) {
                Struct_Member* member = &members[member_index];

                // Set padding to zero
                int padding_after_member = 0;
                if (member_index == members.size - 1) {
                    padding_after_member = memory_info.size - (member->offset + member->type->memory_info.value.size);
                }
                else {
                    padding_after_member = members[member_index + 1].offset - (member->offset + member->type->memory_info.value.size);
                }
                if (padding_after_member != 0) {
                    for (int i = 0; i < array_size; i++) {
                        int member_offset = start_offset + offset_per_element * i + member->offset;
                        memory_set_bytes(bytes.data + member_offset + member->type->memory_info.value.size, padding_after_member, 0);
                    }
                }

                // Search members for references 
                bool success = record_pointers_and_set_padding_bytes_zero_recursive(
                    member->type, array_size, bytes, start_offset + member->offset, offset_per_element, pointer_infos, function_references);
                if (!success) {
                    return false;
                }
            }

            return true;
        }
        case AST::Structure_Type::UNION:
        {
            Datatype* tag_type = structure->tag_member.type;
            assert(tag_type->type == Datatype_Type::ENUM, "");
            Datatype_Enum* tag_enum = downcast<Datatype_Enum>(tag_type);
            auto& enum_members = tag_enum->members;

            for (int i = 0; i < array_size; i++) 
            {
                int union_offset = start_offset + offset_per_element * i;
                int enum_value = *(int*)(bytes.data + union_offset + structure->tag_member.offset);
                if (enum_value <= 0 || enum_value >= tag_enum->members.size + 1) { // 0 is currently not a valid tag value
                    return false;
                }

                // Figure out active member
                auto& active_member = structure->members[enum_value - 1];

                // Check for padding
                int padding = structure->tag_member.offset - (active_member.offset + active_member.type->memory_info.value.size);
                assert(padding >= 0, "Cannot have negative padding");
                if (padding != 0) {
                    memory_set_bytes(bytes.data + union_offset + active_member.type->memory_info.value.size, padding, 0);
                }

                // Search member for references
                bool success = record_pointers_and_set_padding_bytes_zero_recursive(
                    active_member.type, 1, bytes, union_offset, active_member.type->memory_info.value.size, pointer_infos, function_references);
                if (!success) {
                    return false;
                }
            }

            return true;
        }
        case AST::Structure_Type::C_UNION: {
            return false; // In theory C-unions without references could be serialized, but the padding may not be 0, so just disallow it for now
        }
        default: panic("");
        }
        break;
    }
    default: panic("");
    }

    panic("");
    return true;
}

Constant_Pool_Result constant_pool_add_constant_internal(Datatype* signature, int array_size, Array<byte> bytes)
{
    Constant_Pool* pool = &compiler.constant_pool;
    pool->added_internal_constants += 1;
    assert(signature->memory_info.available, "Otherwise how could the bytes have been generated without knowing size of type?");
    auto& memory_info = signature->memory_info.value;
    assert(memory_info.size * array_size == bytes.size, "Array/data must fit into buffer!");

    // Check if memory is readable
    if (!memory_is_readable(bytes.data, bytes.size)) {
        return constant_pool_result_make_error("Constant data contains invalid pointer that isn't null");
    }

    // Handle cyclic references (Stop adding to pool once same pointer was found)
    {
        Upp_Constant* already_saved_index = hashtable_find_element(&pool->saved_pointers, (void*)bytes.data);
        if (already_saved_index != 0) {
            return constant_pool_result_make_success(*already_saved_index);
        }
    }

    // Record all pointers (Which are in 'shallow' memory of this constant)
    Dynamic_Array<Pointer_Info> pointer_infos;
    Dynamic_Array<Upp_Constant_Function_Reference> function_references;
    SCOPE_EXIT(
        if (pointer_infos.data != 0) { dynamic_array_destroy(&pointer_infos); }
        if (function_references.data != 0) { dynamic_array_destroy(&function_references); }
    );
    {
        // Create pointers array if necessary
        if (memory_info.contains_reference) {
            pointer_infos = dynamic_array_create_empty<Pointer_Info>(1);
        }
        else {
            pointer_infos.data = 0;
            pointer_infos.size = 0;
            pointer_infos.capacity = 0;
        }
        if (memory_info.contains_function_pointer) {
            function_references = dynamic_array_create_empty<Upp_Constant_Function_Reference>(1);
        }
        else {
            function_references.data = 0;
            function_references.size = 0;
            function_references.capacity = 0;
        }

        bool success = record_pointers_and_set_padding_bytes_zero_recursive(
            signature, array_size, bytes, 0, memory_info.size, pointer_infos, function_references);
        if (!success) {
            return constant_pool_result_make_error(
                "Constant serialization failed because either non-null void pointers, c-unions, invalid any-type or invalid union tag");
        }
    }

    // Create rewind checkpoint
    auto checkpoint = stack_checkpoint_make(&pool->constant_memory);
    int rewind_constant_count = pool->constants.size;
    int rewind_reference_count = pool->references.size;
    int rewind_function_reference_count = pool->function_references.size;
    bool finished_successfully = false;
    SCOPE_EXIT({
        if (!finished_successfully) {
            stack_checkpoint_rewind(checkpoint);
            dynamic_array_rollback_to_size(&pool->constants, rewind_constant_count);
            dynamic_array_rollback_to_size(&pool->references, rewind_reference_count);
            dynamic_array_rollback_to_size(&pool->function_references, rewind_function_reference_count);
        }
    });

    // For all pointers, add another upp_constant internally, and change the pointer to the internal memory
    for (int i = 0; i < pointer_infos.size; i++) {
        auto& pointer_info = pointer_infos[i];
        assert(pointer_info.pointer_value != nullptr, "Should have been checked beforehand");

        // Create new constant
        Constant_Pool_Result referenced_constant = constant_pool_add_constant_internal(
            pointer_info.points_to_type, pointer_info.array_size, 
            array_create_static_as_bytes((byte*)pointer_info.pointer_value, pointer_info.points_to_type->memory_info.value.size * pointer_info.array_size)
        );
        if (!referenced_constant.success) {
            return referenced_constant;
        }
        pointer_info.added_internal_constant = referenced_constant.constant;

        // Update pointer
        *pointer_info.pointer_address = referenced_constant.constant.memory;
    }
    SCOPE_EXIT( // Restore original pointer values
        for (int i = 0; i < pointer_infos.size; i++) {
            auto& pointer_info = pointer_infos[i];
            *pointer_info.pointer_address = pointer_info.pointer_value;
        }
    );

    // Start creating constant
    Upp_Constant constant;
    constant.constant_index = pool->constants.size;
    constant.array_size = array_size;
    constant.type = signature;
    constant.memory = 0;

    // Check if deduplication possible (Memory hash + memory equals of shallow copy with updated pointers)
    {
        pool->duplication_checks += 1;
        Deduplication_Info deduplication_info;
        deduplication_info.memory = bytes;
        deduplication_info.type = signature;
        Upp_Constant* deduplicated = hashtable_find_element(&pool->deduplication_table, deduplication_info);
        // Note: Currently only the memory is hashed, so we have to make an extra type check here...
        if (deduplicated != 0) {
            return constant_pool_result_make_success(*deduplicated);
        }
        else {
            constant.memory = (byte*)stack_allocator_allocate_size(&pool->constant_memory, bytes.size, memory_info.alignment);
            memory_copy(constant.memory, bytes.data, bytes.size);
            deduplication_info.memory = array_create_static(constant.memory, bytes.size);
            hashtable_insert_element(&pool->deduplication_table, deduplication_info, constant);
        }
    }

    // Add constant to table
    dynamic_array_push_back(&pool->constants, constant);
    hashtable_insert_element(&pool->saved_pointers, (void*)bytes.data, constant);

    // Store references
    for (int i = 0; i < pointer_infos.size; i++)
    {
        auto& pointer_info = pointer_infos[i];

        Upp_Constant_Reference reference;
        reference.constant = constant;
        reference.pointer_member_byte_offset = (byte*)pointer_info.pointer_address - (byte*)bytes.data;
        assert(reference.pointer_member_byte_offset >= 0 && reference.pointer_member_byte_offset <= bytes.size, "");
        reference.points_to = pointer_info.added_internal_constant;
        dynamic_array_push_back(&pool->references, reference);
    }
    // Store function pointers
    for (int i = 0; i < function_references.size; i++) {
        auto& function_ref = function_references[i];
        function_ref.constant = constant;
        dynamic_array_push_back(&pool->function_references, function_ref);
    }

    // Return success
    finished_successfully = true;
    return constant_pool_result_make_success(constant);
}

/*
    Things that currently aren't correctly handled by constant pool:
     - Graphs     
     - Pointers into arrays (Also recursive structures
    These will be serialized, but can possible consume more memory (pointers inside array will be duplicated),
    and they won't be de-duplicated, meaning that instancing of polymorphic structs/functions may create more unnecessary instances
*/
Constant_Pool_Result constant_pool_add_constant(Datatype* signature, Array<byte> bytes)
{
    Constant_Pool* pool = &compiler.constant_pool;
    pool->added_internal_constants = 0;
    pool->duplication_checks = 0;
    pool->time_contains_reference = 0;
    pool->deepcopy_counts = 0;
    pool->time_in_comparison = 0;
    pool->time_in_hash = 0;
    hashtable_reset(&pool->saved_pointers);
    return constant_pool_add_constant_internal(signature, 1, bytes);
}



