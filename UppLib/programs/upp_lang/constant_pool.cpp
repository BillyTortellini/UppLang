#include "constant_pool.hpp"

#include "compiler.hpp"
#include "semantic_analyser.hpp"
#include "editor_analysis_info.hpp"

bool deduplication_info_is_equal(Deduplication_Info* a, Deduplication_Info* b) {
    if (!types_are_equal(a->type, b->type) || a->memory.size != b->memory.size) return false;
    return memory_compare(a->memory.data, b->memory.data, a->memory.size);
}

u64 hash_deduplication(Deduplication_Info* info) {
    u64 hash = hash_memory(info->memory);
    hash = hash_combine(hash, hash_i32(&info->memory.size));
    hash = hash_combine(hash, hash_pointer(info->type));
    return hash;
}

// Constant Pool
Constant_Pool constant_pool_create()
{
    Constant_Pool result;
    result.constant_memory = Arena::create(2048);
    result.constants = dynamic_array_create<Upp_Constant>(2048);
    result.deduplication_table = hashtable_create_empty<Deduplication_Info, Upp_Constant>(16, hash_deduplication, deduplication_info_is_equal);
    return result;
}

void constant_pool_destroy(Constant_Pool* pool) 
{
    pool->constant_memory.destroy();
    dynamic_array_destroy(&pool->constants);
    hashtable_destroy(&pool->deduplication_table);
}

bool upp_constant_is_equal(Upp_Constant a, Upp_Constant b) {
    return a.constant_index == b.constant_index;
}



Constant_Pool_Result constant_pool_result_make_success(Upp_Constant constant) {
    Constant_Pool_Result result;
    result.success = true;
    result.options.constant = constant;
    return result;
}

Constant_Pool_Result constant_pool_result_make_error(const char* error) {
    Constant_Pool_Result result;
    result.success = false;
    result.options.error_message = error;
    return result;
}

// prototype
void datatype_memory_check_correctness_and_set_padding_bytes_zero(Datatype* signature, byte* memory, Constant_Pool_Result& result);

void struct_memory_set_padding_to_zero_recursive(
    Datatype_Struct* structure, byte* struct_memory_start, int subtype_depth, Dynamic_Array<Datatype_Struct*>& expected_subtypes, 
    int offset_in_struct, int subtype_end_offset, Constant_Pool_Result& result)
{
    // Handle members
    int next_member_offset = offset_in_struct;
    for (int i = 0; i < structure->members.size; i++)
    {
        Struct_Member* member = &structure->members[i];
        // Fill out padding until member
        if (member->offset != next_member_offset) {
            assert(member->offset > next_member_offset, "");
            memory_set_bytes(struct_memory_start + next_member_offset, member->offset - next_member_offset, 0);
        }
        next_member_offset = member->offset + member->type->memory_info.value.size;

        // Handle member types
        datatype_memory_check_correctness_and_set_padding_bytes_zero(member->type, struct_memory_start + member->offset, result);
        if (!result.success) return;
    }

    // Early exit if no subtypes exist
    if (structure->subtypes.size == 0) 
    {
        // Fill empty bytes till subtype-end
        if (next_member_offset != subtype_end_offset) {
            assert(next_member_offset < subtype_end_offset, "");
            memory_set_bytes(struct_memory_start + next_member_offset, subtype_end_offset - next_member_offset, 0);
        }
    }
    else 
    {
        // Handle tag and subtypes 
        Datatype_Struct* subtype = nullptr;

        int sub_index = (*(int*)(struct_memory_start + structure->tag_member.offset)) - 1;
        if (sub_index < 0 || sub_index >= structure->subtypes.size) {
            result = constant_pool_result_make_error("Found struct subtype where tag value is invalid");
            return;
        }
        subtype = structure->subtypes[sub_index];

        if (subtype_depth < expected_subtypes.size) {
            if (subtype != expected_subtypes[subtype_depth]) {
                result = constant_pool_result_make_error("Found struct subtype where tag doesn't match expected subtype");
                return;
            }
        }

        struct_memory_set_padding_to_zero_recursive(
            subtype, struct_memory_start, subtype_depth + 1, expected_subtypes, next_member_offset, 
            structure->tag_member.offset + structure->tag_member.type->memory_info.value.size, result
        );
    }
    return;
}

void datatype_memory_check_correctness_and_set_padding_bytes_zero(Datatype* signature, byte* memory, Constant_Pool_Result& result)
{
    auto& types = compiler.analysis_data->type_system.predefined_types;
    assert(signature->memory_info.available, "Otherwise how could the bytes have been generated without knowing size of type?");
    auto& memory_info = signature->memory_info.value;

    signature = datatype_get_non_const_type(signature); // We don't care for constants here
    switch (signature->type)
    {
    case Datatype_Type::PATTERN_VARIABLE:
    case Datatype_Type::STRUCT_PATTERN:
    case Datatype_Type::UNKNOWN_TYPE: {
        panic("Shouldn't happen");
        return;
    }
    case Datatype_Type::ENUM: return;
    case Datatype_Type::PRIMITIVE: {
        auto primitive = downcast<Datatype_Primitive>(signature);
        if (primitive->primitive_type == Primitive_Type::ADDRESS) {
            void* pointer = *(void**)memory;
            if (pointer != nullptr) {
                result = constant_pool_result_make_error("Found address that isn't nullptr");
                return;
            }
        }
        return;
    }
    case Datatype_Type::CONSTANT: {
        panic("Shouldn't happen after previous call");
        return;
    }
    case Datatype_Type::FUNCTION_POINTER: {
        // Check if function index is correct
        auto& slots = compiler.analysis_data->function_slots;
        i64 function_index = (*(i64*)memory) - 1;
        if (function_index < -1 || function_index >= slots.size) { // Note: -1 would mean nullptr in this context
            result = constant_pool_result_make_error("Found function pointer with invalid value");
            return;
        }
        return;
    }
    case Datatype_Type::POINTER:
    {
        void* pointer = *(void**)memory;
        if (pointer != nullptr) {
            result = constant_pool_result_make_error("Found pointer that isn't nullptr");
            return;
        }
        return;
    }
    case Datatype_Type::SLICE:
    {
        Datatype* points_to = downcast<Datatype_Slice>(signature)->element_type;
        Upp_Slice_Base slice = *(Upp_Slice_Base*)memory;

        if (slice.data != nullptr || slice.size != 0) {
            result = constant_pool_result_make_error("Found non-empty slice");
            return;
        }

        memory_set_bytes(memory, sizeof(Upp_Slice_Base), 0);
        return;
    }
    case Datatype_Type::ARRAY:
    {
        auto array = downcast<Datatype_Array>(signature);
        if (!array->count_known) {
            result = constant_pool_result_make_error("Value contains array with unknown-count");
            return;
        }

        // Handle all elements
        for (int i = 0; i < array->element_count; i++) {
            byte* element_memory = memory + array->element_type->memory_info.value.size * i;
            datatype_memory_check_correctness_and_set_padding_bytes_zero(array->element_type, element_memory, result);
            if (!result.success) return;
        }
        return;
    }
    case Datatype_Type::OPTIONAL_TYPE: 
    {
        auto opt = downcast<Datatype_Optional>(signature);
        bool available = *(bool*)(memory + opt->is_available_member.offset);

        if (!available) {
            memory_set_bytes(memory, opt->base.memory_info.value.size, 0);
        }
        else {
            datatype_memory_check_correctness_and_set_padding_bytes_zero(opt->child_type, memory, result);
            int size = opt->base.memory_info.value.size;
            int end = opt->is_available_member.offset + 1;
            if (end < size) {
                memory_set_bytes(memory + end, size - end, 0);
            }
        }
        return;
    }
    case Datatype_Type::STRUCT:
    {
        // Check if it's Any-type or c_string
        if (types_are_equal(signature, upcast(types.any_type)))
        {
            Upp_Any any = *(Upp_Any*)memory;
            if (any.type.index >= (u32)compiler.analysis_data->type_system.types.size) {
                result = constant_pool_result_make_error("Found any type with invalid type-handle index");
                return;
            }
            result = constant_pool_result_make_error("Value contains any-type, which is the same as a pointer");
            return;
        }
        else if (types_are_equal(signature, types.c_string))
        {
            Upp_C_String string = *(Upp_C_String*)memory;

            // Check if memory is readable
            if (!memory_is_readable((void*)string.bytes.data, string.bytes.size)) {
                result = constant_pool_result_make_error("Value contains c_string with unreadable memory");
                return;
            }

            auto id = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static((const char*)string.bytes.data));
            // Create c_string value pointing to identifier pool (Always null terminated)
            memory_set_bytes(&string, sizeof(Upp_C_String), 0);
            *(Upp_C_String*)memory = upp_c_string_from_id(id);;

            return;
        }
        // TODO: type-handles should probably also always be correct values, would require less checks elsewhere

        Datatype_Struct* structure = downcast<Datatype_Struct>(datatype_get_non_const_type(signature));
        if (structure->is_union) {
            result = constant_pool_result_make_error("Found Union");
            return;
        }

        Dynamic_Array<Datatype_Struct*> expected_subtypes = dynamic_array_create<Datatype_Struct*>();
        SCOPE_EXIT(dynamic_array_destroy(&expected_subtypes));
        while (structure->parent_struct != nullptr) {
            dynamic_array_push_back(&expected_subtypes, structure);
            structure = structure->parent_struct;
        }
        dynamic_array_reverse_order(&expected_subtypes);

        struct_memory_set_padding_to_zero_recursive(
            structure, memory, 0, expected_subtypes, 0, memory_info.size, result
        );
        return;
    }
    default: panic("");
    }

    panic("");
    return;
}

Constant_Pool_Result constant_pool_add_constant(Datatype* signature, Array<byte> bytes)
{
    Constant_Pool& pool = compiler.analysis_data->constant_pool;
    signature = type_system_make_constant(signature); // All types in constant pool are constant? Not sure if this is working as intended!
    assert(signature->memory_info.available, "Otherwise how could the bytes have been generated without knowing size of type?");
    auto& memory_info = signature->memory_info.value;
    assert(memory_info.size == bytes.size, "Array/data must fit into buffer!");

    // Check if memory is readable
    if (!memory_is_readable(bytes.data, bytes.size)) {
        return constant_pool_result_make_error("Constant data contains invalid pointer");
    }

    // Set padding to zero
    Constant_Pool_Result result;
    result.success = true;
    datatype_memory_check_correctness_and_set_padding_bytes_zero(signature, bytes.data, result);
    if (!result.success) {
        return result;
    }

    // Check for deduplication
    Deduplication_Info deduplication_info;
    deduplication_info.memory = bytes;
    deduplication_info.type = signature;
    Upp_Constant* deduplicated = hashtable_find_element(&pool.deduplication_table, deduplication_info);
    if (deduplicated != 0) {
        return constant_pool_result_make_success(*deduplicated);
    }

    // Create new constant
    Upp_Constant constant;
    constant.constant_index = pool.constants.size;
    constant.type = signature;
    constant.memory = (byte*)pool.constant_memory.allocate_raw(bytes.size, memory_info.alignment);
    memory_copy(constant.memory, bytes.data, bytes.size);

    // Add constant to table
    dynamic_array_push_back(&pool.constants, constant);
    deduplication_info.memory.data = constant.memory;
    hashtable_insert_element(&pool.deduplication_table, deduplication_info, constant);

    return constant_pool_result_make_success(constant);
}



