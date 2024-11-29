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
    result.constant_memory = stack_allocator_create_empty(2048);
    result.constants = dynamic_array_create<Upp_Constant>(2048);
    result.deduplication_table = hashtable_create_empty<Deduplication_Info, Upp_Constant>(16, hash_deduplication, deduplication_info_is_equal);
    return result;
}

void constant_pool_destroy(Constant_Pool* pool) 
{
    stack_allocator_destroy(&pool->constant_memory);
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
    Struct_Content* content, byte* struct_memory_start, Subtype_Index* subtype_index, 
    int index_level, int offset_in_struct, int subtype_end_offset, Constant_Pool_Result& result)
{
    // Handle members
    int next_member_offset = offset_in_struct;
    for (int i = 0; i < content->members.size; i++)
    {
        Struct_Member* member = &content->members[i];
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
    if (content->subtypes.size == 0) 
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
        Struct_Content* child_content;
        if (index_level < subtype_index->indices.size)
        {
            // Here we know the subtype
            int expected_index = subtype_index->indices[index_level].index;
            int sub_index = (*(int*)(struct_memory_start + content->tag_member.offset)) - 1;
            if (sub_index != expected_index) {
                result = constant_pool_result_make_error("Found struct subtype where tag doesn't match expected subtype");
                return;
            }
            child_content = content->subtypes[expected_index];
        }
        else
        {
            int sub_index = (*(int*)(struct_memory_start + content->tag_member.offset)) - 1;
            if (sub_index < 0 || sub_index >= content->subtypes.size) {
                result = constant_pool_result_make_error("Found struct subtype where tag value is invalid");
                return;
            }
            child_content = content->subtypes[sub_index];
        }

        struct_memory_set_padding_to_zero_recursive(
            child_content, struct_memory_start, subtype_index, index_level + 1, next_member_offset, content->tag_member.offset, result
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
    case Datatype_Type::TEMPLATE_TYPE:
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
    case Datatype_Type::UNKNOWN_TYPE: {
        panic("Shouldn't happen");
        return;
    }
    case Datatype_Type::TYPE_HANDLE:
    case Datatype_Type::ENUM:
    case Datatype_Type::PRIMITIVE: {
        return;
    }
    case Datatype_Type::CONSTANT: {
        panic("Shouldn't happen after previous call");
        return;
    }
    case Datatype_Type::FUNCTION: {
        // Check if function index is correct
        auto& slots = compiler.analysis_data->function_slots;
        i64 function_index = (*(i64*)memory) - 1;
        if (function_index < -1 || function_index >= slots.size) { // Note: -1 would mean nullptr in this context
            result = constant_pool_result_make_error("Found function pointer with invalid value");
            return;
        }
        return;
    }
    case Datatype_Type::BYTE_POINTER:
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
    case Datatype_Type::SUBTYPE:
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

            auto& id_pool = compiler.identifier_pool;
            auto id = identifier_pool_add(&id_pool, string_create_static((const char*)string.bytes.data));

            // Create c_string value pointing to identifier pool (Always null terminated)
            memory_set_bytes(&string, sizeof(Upp_C_String), 0);
            *(Upp_C_String*)memory = upp_c_string_from_id(id);;

            return;
        }

        if (signature->base_type->type != Datatype_Type::STRUCT) {
            // I guess this would only happen for struct instance template, which should be polymorphic and doesn't get to pool (e.g. fails at calculate comptime)
            panic("I dont think this should happen");
            return;
        }

        Datatype_Struct* structure = downcast<Datatype_Struct>(signature->base_type);
        if (structure->struct_type == AST::Structure_Type::UNION) {
            result = constant_pool_result_make_error("Found Union");
            return;
        }
        struct_memory_set_padding_to_zero_recursive(
            &structure->content, memory, signature->mods.subtype_index, 0, 0, memory_info.size, result
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
    constant.memory = (byte*)stack_allocator_allocate_size(&pool.constant_memory, bytes.size, memory_info.alignment);
    memory_copy(constant.memory, bytes.data, bytes.size);

    // Add constant to table
    dynamic_array_push_back(&pool.constants, constant);
    deduplication_info.memory.data = constant.memory;
    hashtable_insert_element(&pool.deduplication_table, deduplication_info, constant);

    return constant_pool_result_make_success(constant);
}



