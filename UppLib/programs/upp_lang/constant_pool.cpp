#include "constant_pool.hpp"

#include "compilation_data.hpp"
#include "semantic_analyser.hpp"
#include "compilation_data.hpp"

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
Constant_Pool* constant_pool_create(Compilation_Data* compilation_data)
{
    Constant_Pool* result = new Constant_Pool;
    result->compilation_data = compilation_data;
    result->constant_memory = Arena::create(2048);
    result->constants = dynamic_array_create<Upp_Constant>(2048);
    result->deduplication_table = hashtable_create_empty<Deduplication_Info, Upp_Constant>(16, hash_deduplication, deduplication_info_is_equal);

    {
        auto& types = compilation_data->type_system->predefined_types;
        auto& predef = result->predefined;

        // Add bool values (Needs to be done here, as add_bool relies on these values)
        {
            bool value_bool = false;
            Constant_Pool_Result add_result = constant_pool_add_constant(
                result, upcast(types.bool_type), array_create_static_as_bytes<bool>(&value_bool, 1)
            );
            assert(add_result.success, "");
            predef.bool_true = add_result.options.constant;

            value_bool = false;
            add_result = constant_pool_add_constant(
                result, upcast(types.bool_type), array_create_static_as_bytes<bool>(&value_bool, 1)
            );
            assert(add_result.success, "");
            predef.bool_false = add_result.options.constant;
        }

        predef.empty_string = result->add_string_assume_valid(string_create_static(""));
        predef.i32_zero = result->add_i32(0);
        predef.i32_one = result->add_i32(1);
        predef.usize_zero = result->add_usize(0);
        predef.usize_one = result->add_usize(1);
        predef.u32_zero = result->add_u32(0);
        predef.u32_one = result->add_u32(1);

        void* ptr = nullptr;
        Constant_Pool_Result add_result = constant_pool_add_constant(
            result, upcast(types.rawptr), array_create_static_as_bytes<void*>(&ptr, 1)
        );
        assert(add_result.success, "");
        predef.nil = add_result.options.constant;
    }

    return result;
}

void constant_pool_destroy(Constant_Pool* pool) 
{
    pool->constant_memory.destroy();
    dynamic_array_destroy(&pool->constants);
    hashtable_destroy(&pool->deduplication_table);
    delete pool;
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
void datatype_memory_check_correctness_and_set_padding_bytes_zero(
    Datatype* signature, byte* memory, Constant_Pool_Result& result, Compilation_Data* compilation_data);

void struct_memory_set_padding_to_zero_recursive(
    Datatype_Struct* structure, byte* struct_memory_start, int subtype_depth, Dynamic_Array<Datatype_Struct*>& expected_subtypes, 
    int offset_in_struct, int subtype_end_offset, Constant_Pool_Result& result, Compilation_Data* compilation_data)
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
        next_member_offset = member->offset + member->datatype->memory_info.value.size;

        // Handle member types
        datatype_memory_check_correctness_and_set_padding_bytes_zero(member->datatype, struct_memory_start + member->offset, result, compilation_data);
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
            structure->tag_member.offset + structure->tag_member.datatype->memory_info.value.size, result, compilation_data
        );
    }
    return;
}

void datatype_memory_check_correctness_and_set_padding_bytes_zero(
    Datatype* datatype, byte* memory, Constant_Pool_Result& result, Compilation_Data* compilation_data)
{
    Type_System* type_system = compilation_data->type_system;
    auto& types = type_system->predefined_types;
    assert(datatype->memory_info.available, "Otherwise how could the bytes have been generated without knowing size of type?");
    auto& memory_info = datatype->memory_info.value;

    switch (datatype->type)
    {
    case Datatype_Type::PATTERN_VARIABLE:
    case Datatype_Type::UNKNOWN_TYPE: {
        panic("Shouldn't happen");
        return;
    }
    case Datatype_Type::ENUM: return;
    case Datatype_Type::BUILT_IN:
    {
        auto builtin= downcast<Datatype_Builtin>(datatype);
        switch (builtin->builtin_type)
        {
        case Builtin_Type::RAWPTR:
        {
            void* pointer = *(void**)memory;
            if (pointer != nullptr) {
                result = constant_pool_result_make_error("Found pointer that isn't nullptr");
                return;
            }
            return;
        }
        case Builtin_Type::TYPE_HANDLE:
        {
            Upp_Type_Handle handle;
            memory_copy(&handle, memory, sizeof(Upp_Type_Handle));
            if (handle.index >= (u32)type_system->types.size) {
                result = constant_pool_result_make_error("Invalid type handle found");
            }
            return;
        }
        case Builtin_Type::ANY:
        {
            Upp_Any any = *(Upp_Any*)memory;
            if (any.type.index >= (u32)type_system->types.size) {
                result = constant_pool_result_make_error("Found any type with invalid type-handle index");
                return;
            }
            result = constant_pool_result_make_error("Value contains any-type, which is the same as a pointer");
            return;
        }
        case Builtin_Type::STRING:
        {
            // Note: We don't store c-strings in constant-pool, only upp-strings
            Upp_String string = *(Upp_String*)memory;

            // Check if memory is readable
            if (!memory_is_readable((void*)string.data, string.size)) {
                result = constant_pool_result_make_error("Value contains string with unreadable memory");
                return;
            }

            // Create c_string value pointing to identifier pool (Always null terminated)
            auto id = identifier_pool_add(&compilation_data->identifier_pool, string_create_static((const char*)string.data));
            memory_set_bytes(&string, sizeof(Upp_String), 0);
            *(Upp_String*)memory = upp_string_from_id(id);;
            return;
        }
        case Builtin_Type::C_STRING: {
            result = constant_pool_result_make_error("C_String cannot be added to constant pool");
            return;
        }
        case Builtin_Type::C_CHAR:
        case Builtin_Type::USIZE:
        case Builtin_Type::ISIZE:
        case Builtin_Type::CODE_POINT:
            return;
        default: panic("");
        }
        break;
    }
    case Datatype_Type::PRIMITIVE: 
    {
        // Primitives never have padding
        return;
    }
    case Datatype_Type::FUNCTION_POINTER: {
        // Check if function index is correct
        i64 function_index = (*(i64*)memory) - 1;
        if (function_index < -1 || function_index >= compilation_data->functions.size) { // Note: -1 would mean nullptr in this context
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
        Datatype* points_to = downcast<Datatype_Slice>(datatype)->element_type;
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
        auto array = downcast<Datatype_Array>(datatype);
        if (!array->count_known) {
            result = constant_pool_result_make_error("Value contains array with unknown-count");
            return;
        }

        // Handle all elements
        for (int i = 0; i < array->element_count; i++) {
            byte* element_memory = memory + array->element_type->memory_info.value.size * i;
            datatype_memory_check_correctness_and_set_padding_bytes_zero(array->element_type, element_memory, result, compilation_data);
            if (!result.success) return;
        }
        return;
    }
    case Datatype_Type::STRUCT:
    {
        Datatype_Struct* structure = downcast<Datatype_Struct>(datatype);
        if (structure->upp_struct->is_union) {
            result = constant_pool_result_make_error("Found Union");
            return;
        }

        Dynamic_Array<Datatype_Struct*> expected_subtypes = dynamic_array_create<Datatype_Struct*>();
        SCOPE_EXIT(dynamic_array_destroy(&expected_subtypes));
        while (structure->parent != nullptr) {
            dynamic_array_push_back(&expected_subtypes, structure);
            structure = structure->parent;
        }
        dynamic_array_reverse_order(&expected_subtypes);

        struct_memory_set_padding_to_zero_recursive(
            structure, memory, 0, expected_subtypes, 0, memory_info.size, result, compilation_data
        );
        return;
    }
    default: panic("");
    }

    panic("");
    return;
}

Constant_Pool_Result constant_pool_add_constant(Constant_Pool* constant_pool, Datatype* signature, Array<byte> bytes)
{
    Constant_Pool& pool = *constant_pool;
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
    datatype_memory_check_correctness_and_set_padding_bytes_zero(signature, bytes.data, result, pool.compilation_data);
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

Upp_Constant Constant_Pool::add_i32(i32 value)
{
    auto& types = this->compilation_data->type_system->predefined_types;
    Constant_Pool_Result result = constant_pool_add_constant(
        this, upcast(types.i32_type), array_create_static_as_bytes<i32>(&value, 1)
    );
    assert(result.success, "");
    return result.options.constant;
}

Upp_Constant Constant_Pool::add_u32(u32 value)
{
    auto& types = this->compilation_data->type_system->predefined_types;
    Constant_Pool_Result result = constant_pool_add_constant(
        this, upcast(types.u32_type), array_create_static_as_bytes<u32>(&value, 1)
    );
    assert(result.success, "");
    return result.options.constant;
}

Upp_Constant Constant_Pool::add_usize(usize value)
{
    auto& types = this->compilation_data->type_system->predefined_types;
    Constant_Pool_Result result = constant_pool_add_constant(
        this, upcast(types.usize), array_create_static_as_bytes<usize>(&value, 1)
    );
    assert(result.success, "");
    return result.options.constant;
}

Upp_Constant Constant_Pool::add_f32(f32 value)
{
    auto& types = this->compilation_data->type_system->predefined_types;
    Constant_Pool_Result result = constant_pool_add_constant(
        this, upcast(types.f32_type), array_create_static_as_bytes<f32>(&value, 1)
    );
    assert(result.success, "");
    return result.options.constant;
}

Upp_Constant Constant_Pool::add_f64(f64 value)
{
    auto& types = this->compilation_data->type_system->predefined_types;
    Constant_Pool_Result result = constant_pool_add_constant(
        this, upcast(types.f64_type), array_create_static_as_bytes<f64>(&value, 1)
    );
    assert(result.success, "");
    return result.options.constant;
}

Upp_Constant Constant_Pool::add_upp_string_assume_valid(Upp_String string) 
{
    auto& types = this->compilation_data->type_system->predefined_types;
    Constant_Pool_Result result = constant_pool_add_constant(
        this, upcast(types.string), array_create_static_as_bytes<Upp_String>(&string, 1)
    );
    assert(result.success, "");
    return result.options.constant;
}

Upp_Constant Constant_Pool::add_string_assume_valid(String id)
{
    Upp_String string;
    string.data = id.characters;
    string.size = id.size;
    return add_upp_string_assume_valid(string);
}

Upp_Constant Constant_Pool::add_bool(bool value)
{
    return value ? predefined.bool_true : predefined.bool_false;
}

Upp_Constant Constant_Pool::add_enum_value_assume_valid(Datatype_Enum* enum_type, int value)
{
    assert(enum_type->base.memory_info.value.size == 4, "");
    Constant_Pool_Result result = constant_pool_add_constant(
        this, upcast(enum_type), array_create_static_as_bytes<int>(&value, 1)
    );
    assert(result.success, "");
    return result.options.constant;
}

Upp_Constant Constant_Pool::add_type_handle_assume_valid(Upp_Type_Handle type_handle)
{
    Constant_Pool_Result result = constant_pool_add_constant(
        this, compilation_data->type_system->predefined_types.type_handle->upcast(), 
        array_create_static_as_bytes<Upp_Type_Handle>(&type_handle, 1)
    );
    assert(result.success, "");
    return result.options.constant;
}



