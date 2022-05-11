#include "compiler_misc.hpp"

#include "type_system.hpp"

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

void hardcoded_function_type_append_to_string(String* string, Hardcoded_Function_Type hardcoded)
{
    switch (hardcoded)
    {
    case Hardcoded_Function_Type::PRINT_I32:
        string_append_formated(string, "PRINT_I32");
        break;
    case Hardcoded_Function_Type::PRINT_F32:
        string_append_formated(string, "PRINT_F32");
        break;
    case Hardcoded_Function_Type::PRINT_BOOL:
        string_append_formated(string, "PRINT_BOOL");
        break;
    case Hardcoded_Function_Type::PRINT_LINE:
        string_append_formated(string, "PRINT_LINE");
        break;
    case Hardcoded_Function_Type::PRINT_STRING:
        string_append_formated(string, "PRINT_STRING");
        break;
    case Hardcoded_Function_Type::READ_I32:
        string_append_formated(string, "READ_I32");
        break;
    case Hardcoded_Function_Type::READ_F32:
        string_append_formated(string, "READ_F32");
        break;
    case Hardcoded_Function_Type::READ_BOOL:
        string_append_formated(string, "READ_BOOL");
        break;
    case Hardcoded_Function_Type::RANDOM_I32:
        string_append_formated(string, "RANDOM_I32");
        break;
    case Hardcoded_Function_Type::MALLOC_SIZE_I32:
        string_append_formated(string, "MALLOC_SIZE_I32");
        break;
    case Hardcoded_Function_Type::FREE_POINTER:
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



// Constant Pool
Constant_Pool constant_pool_create(Type_System* type_system)
{
    Constant_Pool result;
    result.buffer = dynamic_array_create_empty<byte>(2048);
    result.constants = dynamic_array_create_empty<Upp_Constant>(2048);
    result.references = dynamic_array_create_empty<Upp_Constant_Reference>(128);
    result.saved_pointers = hashtable_create_pointer_empty<void*, int>(32);
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
}

struct Offset_Result
{
    Constant_Status status;
    int offset;
};

Offset_Result offset_result_make_success(int offset) {
    Offset_Result result;
    result.status = Constant_Status::SUCCESS;
    result.offset = offset;
    return result;
}

Offset_Result offset_result_make_error(Constant_Status error_status) {
    Offset_Result result;
    result.status = error_status;
    result.offset = -1;
    return result;
}

Offset_Result constant_pool_add_constant_internal(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes);
const char* constant_status_to_string(Constant_Status status)
{
    switch (status)
    {
    case Constant_Status::SUCCESS: return "SUCCESS";
    case Constant_Status::CONTAINS_VOID_TYPE: return "CONTAINS_VOID_TYPE";
    case Constant_Status::CONTAINS_INVALID_POINTER_NOT_NULL: return "CONTAINS_INVALID_POINTER_NOT_NULL";
    case Constant_Status::CANNOT_SAVE_FUNCTIONS_YET: return "CANNOT_SAVE_FUNCTIONS_YET";
    case Constant_Status::CANNOT_SAVE_C_UNIONS_CONTAINING_REFERENCES: return "CANNOT_SAVE_C_UNIONS_CONTAINING_REFERENCES";
    case Constant_Status::CONTAINS_INVALID_UNION_TAG: return "CONTAINS_INVALID_UNION_TAG";
    case Constant_Status::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case Constant_Status::INVALID_SLICE_SIZE: return "INVALID_SLICE_SIZE";
    default: panic("");
    }
    return 0;
}

bool type_signature_contains_references(Type_Signature* signature)
{
    switch (signature->type)
    {
    case Signature_Type::VOID_TYPE: return false;
    case Signature_Type::PRIMITIVE: return false;
    case Signature_Type::POINTER: return true;
    case Signature_Type::FUNCTION: return true;
    case Signature_Type::STRUCT: 
    {
        for (int i = 0; i < signature->options.structure.members.size; i++) {
            Struct_Member* member = &signature->options.structure.members[i];
            if (type_signature_contains_references(member->type)) {
                return true;
            }
        }
        return false;
    }
    case Signature_Type::ENUM: return false;
    case Signature_Type::ARRAY: return type_signature_contains_references(signature->options.array.element_type);
    case Signature_Type::SLICE: return true;
    case Signature_Type::TEMPLATE_TYPE: return false;
    case Signature_Type::TYPE_TYPE: return false;
    case Signature_Type::UNKNOWN_TYPE: return false;
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
    */

    if (a.type != b.type) return false;
    if (a.constant_index == b.constant_index || a.offset == b.offset) return true;

    byte* pool_data = (byte*) pool->buffer.data;
    byte* raw_data_a = &pool_data[a.offset];
    byte* raw_data_b = &pool_data[b.offset];
    Type_Signature* signature = a.type;
    return memory_compare(raw_data_a, raw_data_b, signature->size);
}

Constant_Status constant_pool_search_references(Constant_Pool* pool, int data_offset, Type_Signature* signature)
{
    void* raw_data = &pool->buffer[data_offset];
    switch (signature->type)
    {
    case Signature_Type::VOID_TYPE:
        return Constant_Status::CONTAINS_VOID_TYPE;
    case Signature_Type::PRIMITIVE:
        break;
    case Signature_Type::POINTER:
    {
        void* ptr_value = *(void**)raw_data;
        if (ptr_value == nullptr) {
            break;
        }
        if (memory_is_readable(ptr_value, signature->options.pointer_child->size)) 
        {
            Upp_Constant_Reference reference;
            reference.ptr_offset = data_offset;
            Offset_Result ptr_result = constant_pool_add_constant_internal(
                pool, signature->options.pointer_child, array_create_static_as_bytes((byte*)ptr_value, signature->options.pointer_child->size)
            );
            if (ptr_result.status != Constant_Status::SUCCESS) return ptr_result.status;
            reference.buffer_destination_offset = ptr_result.offset;
            dynamic_array_push_back(&pool->references, reference);
        }
        else {
            return Constant_Status::CONTAINS_INVALID_POINTER_NOT_NULL;
        }
        break;
    }
    case Signature_Type::FUNCTION: {
        return Constant_Status::CANNOT_SAVE_FUNCTIONS_YET;
    }
    case Signature_Type::STRUCT:
    {
        // Loop over each member and call this function
        switch (signature->options.structure.struct_type)
        {
        case Structure_Type::STRUCT: 
        {
            for (int i = 0; i < signature->options.structure.members.size; i++) {
                Struct_Member* member = &signature->options.structure.members[i];
                Constant_Status member_status = constant_pool_search_references(pool, data_offset + member->offset, member->type);
                if (member_status != Constant_Status::SUCCESS) return member_status;
            }
            break;
        }
        case Structure_Type::C_UNION: {
            if (type_signature_contains_references(signature)) {
                return Constant_Status::CANNOT_SAVE_C_UNIONS_CONTAINING_REFERENCES;
            }
            break;
        }
        case Structure_Type::UNION: 
        {
            Type_Signature* tag_type = signature->options.structure.tag_member.type;
            assert(tag_type->type == Signature_Type::ENUM, "");
            int tag_value = *(int*)((byte*)raw_data + signature->options.structure.tag_member.offset);
            int found_member_index = -1;
            for (int i = 0; i < tag_type->options.enum_type.members.size; i++) 
            {
                Enum_Member* member = &tag_type->options.enum_type.members[i];
                if (member->value == tag_value) {
                    found_member_index = tag_value - 1;
                }
            }
            if (found_member_index != -1) {
                Struct_Member* member = &signature->options.structure.members[found_member_index];
                Constant_Status member_status = constant_pool_search_references(pool, data_offset + member->offset, member->type);
                if (member_status != Constant_Status::SUCCESS) return member_status;
            }
            else {
                return Constant_Status::CONTAINS_INVALID_UNION_TAG;
                //panic("Could not find active member of given tag value, union seems not to be initialized");
            }
            break;
        }
        default: panic("");
        }
        break;
    }
    case Signature_Type::ENUM: {
        break;
    }
    case Signature_Type::ARRAY: {
        if (type_signature_contains_references(signature->options.array.element_type)) {
            for (int i = 0; i < signature->options.array.element_count; i++) {
                int element_offset = i * signature->options.array.element_type->size;
                Constant_Status element_status = constant_pool_search_references(pool, data_offset + element_offset, signature->options.array.element_type);
                if (element_status != Constant_Status::SUCCESS) return element_status;
            }
        }
        break;
    }
    case Signature_Type::SLICE: 
    {
        // Check if pointer is valid, if true, save slice data
        Upp_Slice_Base slice = *(Upp_Slice_Base*)raw_data;
        if (slice.data_ptr == nullptr || slice.size == 0) {
            break;
        }
        if (slice.size <= 0) {
            return Constant_Status::INVALID_SLICE_SIZE;
        }
        if (memory_is_readable(slice.data_ptr, signature->options.slice.element_type->size * slice.size)) 
        {
            Upp_Constant_Reference reference;
            reference.ptr_offset = data_offset;
            Offset_Result data_result = constant_pool_add_constant_internal(
                pool, type_system_make_array(pool->type_system, signature->options.slice.element_type, true, slice.size), 
                array_create_static_as_bytes((byte*)slice.data_ptr, signature->options.slice.element_type->size * slice.size)
            );
            if (data_result.status != Constant_Status::SUCCESS) return data_result.status;
            reference.buffer_destination_offset = data_result.offset;
            dynamic_array_push_back(&pool->references, reference);
        }
        else {
            return Constant_Status::CONTAINS_INVALID_POINTER_NOT_NULL;
        }
    }
    case Signature_Type::TEMPLATE_TYPE:
    case Signature_Type::TYPE_TYPE:
    case Signature_Type::UNKNOWN_TYPE:
        break;
    default: panic("");
    }
    return Constant_Status::SUCCESS;
}

Offset_Result constant_pool_add_constant_internal(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes)
{
    /*
        1. Reset Pointer Hashtable
        2. Push bytes into pool
        3. Add given pointer to hashtable
        4. Search through bytes for references
        5. If reference found, call add_constant with new reference, but don't reset hashtable
    */
    {
        int* found_offset = hashtable_find_element(&pool->saved_pointers, (void*)bytes.data);
        if (found_offset != 0) {
            return offset_result_make_success(*found_offset);
        }
    }
    if (pool->buffer.size + signature->alignment + signature->size > pool->max_buffer_size) {
        return offset_result_make_error(Constant_Status::OUT_OF_MEMORY);
    }

    // Reserve enough memory in pool
    dynamic_array_reserve(&pool->buffer, pool->buffer.size + signature->alignment + signature->size);

    // Align pool to type alignment
    while (pool->buffer.size % signature->alignment != 0) {
        dynamic_array_push_back(&pool->buffer, (byte)0);
    }

    int start_offset = pool->buffer.size;
    for (int i = 0; i < bytes.size; i++) {
        dynamic_array_push_back(&pool->buffer, bytes[i]);
    }
    hashtable_insert_element(&pool->saved_pointers, (void*)bytes.data, start_offset);
    Constant_Status status = constant_pool_search_references(pool, start_offset, signature);
    if (status != Constant_Status::SUCCESS) {
        return offset_result_make_error(status);
    }
    return offset_result_make_success(start_offset);
}

Constant_Result constant_pool_add_constant(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes)
{
    int rewind_index = pool->buffer.size;
    hashtable_reset(&pool->saved_pointers);
    Offset_Result offset_result = constant_pool_add_constant_internal(pool, signature, bytes);
    if (offset_result.status != Constant_Status::SUCCESS) {
        Constant_Result result;
        result.status = offset_result.status;
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

    Constant_Result result;
    result.status = Constant_Status::SUCCESS;
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
    result.extern_type_signatures = hashtable_create_pointer_empty<Type_Signature*, String*>(8);
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





