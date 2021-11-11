#include "type_system.hpp"
#include "compiler.hpp"

void type_signature_destroy(Type_Signature* sig) 
{
    if (sig->type == Signature_Type::FUNCTION)
        dynamic_array_destroy(&sig->options.function.parameter_types);
    if (sig->type == Signature_Type::STRUCT)
        dynamic_array_destroy(&sig->options.structure.members);
}

void type_signature_append_to_string_with_children(String* string, Type_Signature* signature, bool print_child)
{
    switch (signature->type)
    {
    case Signature_Type::TEMPLATE_TYPE:
        string_append_formated(string, "TEMPLATE_TYPE");
        break;
    case Signature_Type::VOID_TYPE:
        string_append_formated(string, "VOID");
        break;
    case Signature_Type::ARRAY:
        string_append_formated(string, "[%d]", signature->options.array.element_count);
        type_signature_append_to_string_with_children(string, signature->options.array.element_type, print_child);
        break;
    case Signature_Type::SLICE:
        string_append_formated(string, "[]");
        type_signature_append_to_string_with_children(string, signature->options.array.element_type, print_child);
        break;
    case Signature_Type::ERROR_TYPE:
        string_append_formated(string, "ERROR-Type");
        break;
    case Signature_Type::TYPE_TYPE:
        string_append_formated(string, "TYPE_TYPE");
        break;
    case Signature_Type::POINTER:
        string_append_formated(string, "*");
        type_signature_append_to_string_with_children(string, signature->options.pointer_child, print_child);
        break;
    case Signature_Type::PRIMITIVE:
        switch (signature->options.primitive.type) 
        {
        case Primitive_Type::BOOLEAN: string_append_formated(string, "BOOLEAN"); break;
        case Primitive_Type::INTEGER: string_append_formated(string, "%s%d", signature->options.primitive.is_signed ? "INT" : "UINT", signature->size * 8); break;
        case Primitive_Type::FLOAT: string_append_formated(string, "FLOAT%d", signature->size * 8); break;
        default: panic("Heyo");
        }
        break;
    case Signature_Type::ENUM:
    {
        if (signature->options.enum_type.id != 0) {
            string_append_formated(string, signature->options.enum_type.id->characters);
        }
        if (print_child)
        {
            string_append_formated(string, "{");
            for (int i = 0; i < signature->options.enum_type.members.size; i++) {
                string_append_formated(string, "%s(%d)", signature->options.enum_type.members[i].id->characters, signature->options.enum_type.members[i].value);
                if (i != signature->options.enum_type.members.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
            string_append_formated(string, "}");
        }
        break;
    }
    case Signature_Type::STRUCT:
    {
        if (signature->options.structure.id != 0) {
            string_append_formated(string, signature->options.structure.id->characters);
        }
        if (print_child)
        {
            string_append_formated(string, "{");
            for (int i = 0; i < signature->options.structure.members.size && print_child; i++) {
                type_signature_append_to_string_with_children(string, signature->options.structure.members[i].type, false);
                if (i != signature->options.structure.members.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
            string_append_formated(string, "}");
        }
        break;
    }
    case Signature_Type::FUNCTION:
        string_append_formated(string, "(");
        for (int i = 0; i < signature->options.function.parameter_types.size; i++) {
            type_signature_append_to_string_with_children(string, signature->options.function.parameter_types[i], print_child);
            if (i != signature->options.function.parameter_types.size - 1) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, ") -> ");
        type_signature_append_to_string_with_children(string, signature->options.function.return_type, print_child);
        break;
    default: panic("Fugg");
    }
}

void type_signature_append_value_to_string(Type_Signature* type, byte* value_ptr, String* string)
{
    if (!memory_is_readable(value_ptr, type->size)) {
        string_append_formated(string, "Memory not readable");
    }

    switch (type->type)
    {
    case Signature_Type::FUNCTION:
        break;
    case Signature_Type::VOID_TYPE:
        break;
    case Signature_Type::ERROR_TYPE:
        break;
    case Signature_Type::TEMPLATE_TYPE:
        break;
    case Signature_Type::ARRAY:
    {
        string_append_formated(string, "[#%d ", type->options.array.element_count);
        if (type->options.array.element_count > 4) {
            string_append_formated(string, " ...]");
            return;
        }
        for (int i = 0; i < type->options.array.element_count; i++) {
            byte* element_ptr = value_ptr + (i * type->options.array.element_type->size);
            type_signature_append_value_to_string(type->options.array.element_type, element_ptr, string);
            string_append_formated(string, ", ");
        }
        string_append_formated(string, "]");
        break;
    }
    case Signature_Type::SLICE:
    {
        byte* data_ptr = *((byte**)value_ptr);
        int element_count = *((int*)(value_ptr + 8));
        string_append_formated(string, "[#%d ", element_count);
        if (!memory_is_readable(data_ptr, element_count * type->options.array.element_type->size)) {
            string_append_formated(string, "Memory not readable");
        }
        else {
            if (element_count > 4) {
                string_append_formated(string, " ...]");
                return;
            }
            for (int i = 0; i < element_count; i++) {
                byte* element_ptr = data_ptr + (i * type->options.array.element_type->size);
                type_signature_append_value_to_string(type->options.array.element_type, element_ptr, string);
                string_append_formated(string, ", ");
            }
            string_append_formated(string, "]");
        }
        break;
    }
    case Signature_Type::POINTER:
    {
        byte* data_ptr = *((byte**)value_ptr);
        if (data_ptr == 0) {
            string_append_formated(string, "nullptr");
            return;
        }
        string_append_formated(string, "Ptr %p", data_ptr);
        if (!memory_is_readable(data_ptr, type->options.pointer_child->size)) {
            string_append_formated(string, "(UNREADABLE)");
        }
        break;
    }
    case Signature_Type::STRUCT:
    {
        string_append_formated(string, "Struct: {");
        for (int i = 0; i < type->options.structure.members.size; i++) {
            Struct_Member* mem = &type->options.structure.members[i];
            byte* mem_ptr = value_ptr + mem->offset;
            if (memory_is_readable(mem_ptr, mem->type->size)) {
                type_signature_append_value_to_string(mem->type, mem_ptr, string);
            }
            else {
                string_append_formated(string, "UNREADABLE");
            }
            string_append_formated(string, ", ");
        }
        string_append_formated(string, "}");
        break;
    }
    case Signature_Type::PRIMITIVE:
    {
        switch (type->options.primitive.type)
        {
        case Primitive_Type::BOOLEAN: {
            bool val = *(bool*)value_ptr;
            string_append_formated(string, "%s", val ? "TRUE" : "FALSE");
            break;
        }
        case Primitive_Type::INTEGER: {
            int value = 0;
            if (type->options.primitive.is_signed)
            {
                switch (type->size)
                {
                case 1: value = (i32) * (i8*)value_ptr; break;
                case 2: value = (i32) * (i16*)value_ptr; break;
                case 4: value = (i32) * (i32*)value_ptr; break;
                case 8: value = (i32) * (i64*)value_ptr; break;
                default: panic("HEY");
                }
            }
            else
            {
                switch (type->size)
                {
                case 1: value = (i32) * (u8*)value_ptr; break;
                case 2: value = (i32) * (u16*)value_ptr; break;
                case 4: value = (i32) * (u32*)value_ptr; break;
                case 8: value = (i32) * (u64*)value_ptr; break;
                default: panic("HEY");
                }
            }
            string_append_formated(string, "%d", value);
            break;
        }
        case Primitive_Type::FLOAT: {
            if (type->size == 4) {
                string_append_formated(string, "%3.2f", *(float*)value_ptr);
            }
            else if (type->size == 8) {
                string_append_formated(string, "%3.2f", *(float*)value_ptr);
            }
            else panic("HEY");
            break;
        }
        default: panic("HEY");
        }
        break;
    }
    default: panic("HEY");
    }
}

void type_signature_append_to_string(String* string, Type_Signature* signature) {
    type_signature_append_to_string_with_children(string, signature, false);
}



void struct_add_member(Type_Signature* struct_sig, Identifier_Pool* pool, const char* name, Type_Signature* member_type)
{
    Struct_Member member;
    member.id = identifier_pool_add(pool, string_create_static(name));
    member.offset = 0;
    member.type = member_type;
    dynamic_array_push_back(&struct_sig->options.structure.members, member);
}

template<typename T>
void assert_similarity(Type_Signature* signature) {
    assert(signature->size == sizeof(T) && signature->alignment == alignof(T), "");
}
/*
    TYPE_SYSTEM
*/

void type_system_add_primitives(Type_System* system, Identifier_Pool* pool)
{
    system->id_data = identifier_pool_add(pool, string_create_static("data"));
    system->id_size = identifier_pool_add(pool, string_create_static("size"));
    system->id_tag = identifier_pool_add(pool, string_create_static("tag"));

    system->bool_type = type_system_make_primitive(system, Primitive_Type::BOOLEAN, 1, false);
    system->i8_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 1, true);
    system->i16_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 2, true);
    system->i32_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 4, true);
    system->i64_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 8, true);
    system->u8_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 1, false);
    system->u16_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 2, false);
    system->u32_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 4, false);
    system->u64_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 8, false);
    system->f32_type = type_system_make_primitive(system, Primitive_Type::FLOAT, 4, true);
    system->f64_type = type_system_make_primitive(system, Primitive_Type::FLOAT, 8, true);
    {
        Type_Signature error_type;
        error_type.size = 0;
        error_type.alignment = 1;
        error_type.type = Signature_Type::ERROR_TYPE;
        system->error_type = type_system_register_type(system, error_type);

        Type_Signature void_type;
        void_type.type = Signature_Type::VOID_TYPE;
        void_type.size = 0;
        void_type.alignment = 1;
        system->void_type = type_system_register_type(system, void_type);

        Type_Signature void_ptr_type;
        void_ptr_type.type = Signature_Type::POINTER;
        void_ptr_type.size = 8;
        void_ptr_type.alignment = 8;
        void_ptr_type.options.pointer_child = system->void_type;
        system->void_ptr_type = type_system_register_type(system, void_ptr_type);

        Type_Signature type_type;
        type_type.type = Signature_Type::TYPE_TYPE;
        type_type.size = 8;
        type_type.alignment = 8;
        system->type_type = type_system_register_type(system, type_type);
    }

    // Empty structure
    {
        system->empty_struct_type = type_system_make_struct_empty_full(
            system, identifier_pool_add(pool, string_create_static("Empty_Type")), Structure_Type::STRUCT
        );
        type_system_finish_type(system, system->empty_struct_type);
    }

    // String
    {
        system->string_type = type_system_make_struct_empty_full(
            system, identifier_pool_add(pool, string_create_static("String")), Structure_Type::STRUCT
        );
        struct_add_member(system->string_type, pool, "character_buffer", type_system_make_slice(system, system->u8_type));
        struct_add_member(system->string_type, pool, "size", system->i32_type);
        type_system_finish_type(system, system->string_type);
        assert_similarity<Upp_String>(system->string_type);
    }

    {
        Type_Signature* option_type = type_system_make_struct_empty_full(
            system, identifier_pool_add(pool, string_create_static("Type_Information_Options")), Structure_Type::UNION
        );
        struct_add_member(option_type, pool, "void_type", system->empty_struct_type);
        struct_add_member(option_type, pool, "type", system->empty_struct_type);
        struct_add_member(option_type, pool, "pointer", system->type_type);
        // Array
        {
            Type_Signature* array_type = type_system_make_struct_empty_full(
                system, identifier_pool_add(pool, string_create_static("Type_Information_Option_Array")), Structure_Type::STRUCT
            );
            struct_add_member(array_type, pool, "element_type", system->type_type);
            struct_add_member(array_type, pool, "size", system->i32_type);
            type_system_finish_type(system, array_type);
            struct_add_member(option_type, pool, "array", array_type);
            assert_similarity<Internal_Type_Array>(array_type);
        }
        // Slice
        {
            Type_Signature* slice_type = type_system_make_struct_empty_full(
                system, identifier_pool_add(pool, string_create_static("Type_Information_Option_Slice")), Structure_Type::STRUCT
            );
            struct_add_member(slice_type, pool, "element_type", system->type_type);
            type_system_finish_type(system, slice_type);
            struct_add_member(option_type, pool, "slice", slice_type);
            assert_similarity<Internal_Type_Slice>(slice_type);
        }
        // Primitive
        {
            Type_Signature* primitive_type = type_system_make_struct_empty_full(
                system, identifier_pool_add(pool, string_create_static("Type_Information_Option_Primitive")), Structure_Type::UNION
            );
            {
                Type_Signature* integer_info_type = type_system_make_struct_empty_full(
                    system, identifier_pool_add(pool, string_create_static("Type_Information_Option_Primitive_Integer")), Structure_Type::STRUCT
                );
                struct_add_member(integer_info_type, pool, "is_signed", system->bool_type);
                type_system_finish_type(system, integer_info_type);
                struct_add_member(primitive_type, pool, "integer", integer_info_type);
            }
            struct_add_member(primitive_type, pool, "boolean", system->empty_struct_type);
            struct_add_member(primitive_type, pool, "floating_point", system->empty_struct_type);
            type_system_finish_type(system, primitive_type);
            struct_add_member(option_type, pool, "primitive", primitive_type);
            assert_similarity<Internal_Type_Primitive>(primitive_type);
        }
        // Function
        {
            Type_Signature* function_type = type_system_make_struct_empty_full(
                system, identifier_pool_add(pool, string_create_static("Type_Information_Option_Function")), Structure_Type::STRUCT
            );
            struct_add_member(function_type, pool, "parameter_types", type_system_make_slice(system, system->type_type));
            struct_add_member(function_type, pool, "return_type", system->type_type);
            type_system_finish_type(system, function_type);
            struct_add_member(option_type, pool, "function", function_type);
            assert_similarity<Internal_Type_Function>(function_type);
        }
        // Struct
        {
            Type_Signature* struct_type = type_system_make_struct_empty_full(
                system, identifier_pool_add(pool, string_create_static("Type_Information_Option_Struct")), Structure_Type::STRUCT
            );
            {
                Type_Signature* struct_member_type = type_system_make_struct_empty_full(
                    system, identifier_pool_add(pool, string_create_static("Type_Information_Struct_Member")), Structure_Type::STRUCT
                );
                struct_add_member(struct_member_type, pool, "name", system->string_type);
                struct_add_member(struct_member_type, pool, "type", system->type_type);
                struct_add_member(struct_member_type, pool, "offset", system->i32_type);
                type_system_finish_type(system, struct_member_type);
                struct_add_member(struct_type, pool, "members", type_system_make_slice(system, struct_member_type));
                assert_similarity<Internal_Type_Struct_Member>(struct_member_type);
            }
            struct_add_member(struct_type, pool, "name", system->string_type);
            {
                Type_Signature* structure_type_type = type_system_make_struct_empty_full(
                    system, identifier_pool_add(pool, string_create_static("Type_Information_Structure_Type")), Structure_Type::UNION
                );
                struct_add_member(structure_type_type, pool, "structure", system->empty_struct_type);
                struct_add_member(structure_type_type, pool, "union_untagged", system->empty_struct_type);
                {
                    Type_Signature* union_tagged_struct = type_system_make_struct_empty_full(
                        system, identifier_pool_add(pool, string_create_static("Type_Information_Union_Tagged_Struct")), Structure_Type::STRUCT
                    );
                    struct_add_member(union_tagged_struct, pool, "tag_member_index", system->i32_type);
                    type_system_finish_type(system, union_tagged_struct);
                    struct_add_member(structure_type_type, pool, "union_tagged", union_tagged_struct);
                }
                type_system_finish_type(system, structure_type_type);
                struct_add_member(struct_type, pool, "type", structure_type_type);
                assert_similarity<Internal_Structure_Type>(structure_type_type);
            }
            type_system_finish_type(system, struct_type);
            struct_add_member(option_type, pool, "structure", struct_type);
            assert_similarity<Internal_Type_Struct>(struct_type);
        }
        // ENUM
        {
            Type_Signature* enum_type = type_system_make_struct_empty_full(
                system, identifier_pool_add(pool, string_create_static("Type_Information_Option_Enum")), Structure_Type::STRUCT
            );
            {
                Type_Signature* enum_member_type = type_system_make_struct_empty_full(
                    system, identifier_pool_add(pool, string_create_static("Type_Information_Enum_Member")), Structure_Type::STRUCT
                );
                struct_add_member(enum_member_type, pool, "name", system->string_type);
                struct_add_member(enum_member_type, pool, "value", system->i32_type);
                type_system_finish_type(system, enum_member_type);
                struct_add_member(enum_type, pool, "members", type_system_make_slice(system, enum_member_type));
                assert_similarity<Internal_Type_Enum_Member>(enum_member_type);
            }
            struct_add_member(enum_type, pool, "name", system->string_type);
            type_system_finish_type(system, enum_type);
            struct_add_member(option_type, pool, "enumeration", enum_type);
            assert_similarity<Internal_Type_Enum>(enum_type);
        }
        type_system_finish_type(system, option_type);
        assert_similarity<Internal_Type_Info_Options>(option_type);

        // Type Information
        {
            Type_Signature* type_info_type = type_system_make_struct_empty_full(
                system, identifier_pool_add(pool, string_create_static("Type_Information")), Structure_Type::STRUCT
            );
            struct_add_member(type_info_type, pool, "type", system->type_type);
            struct_add_member(type_info_type, pool, "size", system->i32_type);
            struct_add_member(type_info_type, pool, "alignment", system->i32_type);
            struct_add_member(type_info_type, pool, "options", option_type);
            type_system_finish_type(system, type_info_type);
            system->type_information_type = type_info_type;
            assert_similarity<Internal_Type_Information>(type_info_type);
        }
    }
}

Type_System type_system_create(Timer* timer)
{
    Type_System result;
    result.types = dynamic_array_create_empty<Type_Signature*>(256);
    result.internal_type_infos = dynamic_array_create_empty<Internal_Type_Information>(256);
    result.timer = timer;
    result.register_time = 0;
    return result;
}

void type_system_destroy(Type_System* system) {
    type_system_reset(system);
    dynamic_array_destroy(&system->types);
    dynamic_array_destroy(&system->internal_type_infos);
}

void type_system_reset(Type_System* system)
{
    system->register_time = 0;
    for (int i = 0; i < system->types.size; i++) {
        type_signature_destroy(system->types[i]);
        delete system->types[i];
    }
    dynamic_array_reset(&system->types);
    for (int i = 0; i < system->internal_type_infos.size; i++)
    {
        Internal_Type_Information* info = &system->internal_type_infos[i];
        switch (info->options.tag)
        {
        case Internal_Type_Options_Tag::ENUMERATION: {
            if (info->options.enumeration.members.data_ptr != 0) {
                delete[]info->options.enumeration.members.data_ptr;
                info->options.enumeration.members.data_ptr = 0;
            }
            break;
        }
        case Internal_Type_Options_Tag::FUNCTION:
            if (info->options.function.parameters.data_ptr != 0) {
                delete[]info->options.function.parameters.data_ptr;
                info->options.function.parameters.data_ptr = 0;
            }
            break;
        case Internal_Type_Options_Tag::STRUCTURE: {
            if (info->options.structure.members.data_ptr != 0) {
                delete[]info->options.structure.members.data_ptr;
                info->options.structure.members.data_ptr = 0;
            }
            break;
        }
        default: break;
        }
    }
    dynamic_array_reset(&system->internal_type_infos);
}

Internal_Type_Options_Tag signature_type_to_internal_type(Signature_Type type)
{
    switch (type)
    {
    case Signature_Type::VOID_TYPE: return Internal_Type_Options_Tag::VOID_TYPE;
    case Signature_Type::TEMPLATE_TYPE:return Internal_Type_Options_Tag::VOID_TYPE;
    case Signature_Type::ERROR_TYPE:return Internal_Type_Options_Tag::VOID_TYPE;

    case Signature_Type::TYPE_TYPE:return Internal_Type_Options_Tag::TYPE_TYPE;
    case Signature_Type::PRIMITIVE: return Internal_Type_Options_Tag::PRIMITIVE;
    case Signature_Type::POINTER:return Internal_Type_Options_Tag::POINTER;
    case Signature_Type::FUNCTION: return Internal_Type_Options_Tag::FUNCTION;
    case Signature_Type::STRUCT:return Internal_Type_Options_Tag::STRUCTURE;
    case Signature_Type::ENUM:return Internal_Type_Options_Tag::ENUMERATION;
    case Signature_Type::ARRAY: return Internal_Type_Options_Tag::ARRAY;
    case Signature_Type::SLICE:return Internal_Type_Options_Tag::SLICE;
    default: panic("");
    }
    panic("");
    return Internal_Type_Options_Tag::ARRAY;
}

Type_Signature* type_system_register_type(Type_System* system, Type_Signature signature)
{
    double reg_start_time = timer_current_time_in_seconds(system->timer);
    if (signature.type != Signature_Type::STRUCT)
    {
        // Check if type already exists
        for (int i = 0; i < system->types.size; i++)
        {
            bool are_equal = false;
            Type_Signature* sig1 = &signature;
            Type_Signature* sig2 = system->types[i];
            if (sig1->type == sig2->type)
            {
                switch (sig1->type)
                {
                case Signature_Type::TYPE_TYPE: are_equal = true; break;
                case Signature_Type::VOID_TYPE: are_equal = true; break;
                case Signature_Type::ERROR_TYPE: are_equal = true; break;
                case Signature_Type::PRIMITIVE: are_equal = sig1->options.primitive.type == sig2->options.primitive.type &&
                    sig1->options.primitive.is_signed == sig2->options.primitive.is_signed && sig1->size == sig2->size; break;
                case Signature_Type::POINTER: are_equal = sig1->options.pointer_child == sig2->options.pointer_child; break;
                case Signature_Type::STRUCT: are_equal = false; break;
                case Signature_Type::ENUM: are_equal = false; break;
                case Signature_Type::TEMPLATE_TYPE: are_equal = false; break;
                case Signature_Type::ARRAY: are_equal = sig1->options.array.element_type == sig2->options.array.element_type &&
                    sig1->options.array.element_count == sig2->options.array.element_count; break;
                case Signature_Type::SLICE: are_equal = sig1->options.array.element_type == sig2->options.array.element_type; break;
                case Signature_Type::FUNCTION:
                {
                    are_equal = true;
                    if (sig1->options.function.return_type != sig2->options.function.return_type ||
                        sig1->options.function.parameter_types.size != sig2->options.function.parameter_types.size) {
                        are_equal = false; break;
                    }
                    for (int i = 0; i < sig1->options.function.parameter_types.size; i++) {
                        if (sig1->options.function.parameter_types[i] != sig2->options.function.parameter_types[i]) {
                            are_equal = false;
                            break;
                        }
                    }
                    break;
                }
                }
            }

            if (are_equal) {
                type_signature_destroy(&signature);
                return sig2;
            }
        }
    }

    // Create Interal type Info and internal index
    {
        signature.internal_index = system->internal_type_infos.size;
        Internal_Type_Information info;
        memory_set_bytes(&info, sizeof(info), 0);
        info.type = signature.internal_index;
        info.size = signature.size;
        info.alignment = signature.alignment;
        info.options.tag = signature_type_to_internal_type(signature.type);
        switch (signature.type)
        {
        case Signature_Type::STRUCT:
        case Signature_Type::ENUM:
            // Are both handled in finish type
        case Signature_Type::ARRAY:
        case Signature_Type::TEMPLATE_TYPE:
        case Signature_Type::ERROR_TYPE:
        case Signature_Type::TYPE_TYPE:
        case Signature_Type::VOID_TYPE:
            break; // There are no options for these types

        case Signature_Type::FUNCTION:
        {
            int param_count = signature.options.function.parameter_types.size;
            info.options.function.parameters.data_ptr = new u64[param_count];
            info.options.function.parameters.size = param_count;
            for (int i = 0; i < param_count; i++) {
                Type_Signature* param = signature.options.function.parameter_types[i];
                u64* info_param = &info.options.function.parameters.data_ptr[i];
                *info_param = param->internal_index;
            }
            info.options.function.return_type = signature.options.function.return_type->internal_index;
            break;
        }
        case Signature_Type::POINTER: {
            info.options.pointer = signature.options.pointer_child->internal_index;
            break;
        }
        case Signature_Type::PRIMITIVE:
        {
            info.options.primitive.tag = signature.options.primitive.type;
            info.options.primitive.is_signed = signature.options.primitive.is_signed;
            break;
        }
        case Signature_Type::SLICE: {
            info.options.slice.element_type = signature.options.slice.element_type->internal_index;
            break;
        }
        default: panic("");
        }
        dynamic_array_push_back(&system->internal_type_infos, info);
    }

    Type_Signature* new_sig = new Type_Signature(signature);
    dynamic_array_push_back(&system->types, new_sig);

    double reg_end_time = timer_current_time_in_seconds(system->timer);
    system->register_time += reg_end_time - reg_start_time;

    return new_sig;
}

void type_system_finish_type(Type_System* system, Type_Signature* type)
{
    assert(type->internal_index < system->internal_type_infos.size, "");
    assert(system->internal_type_infos.size == system->types.size, "");

    Internal_Type_Information* internal_info = &system->internal_type_infos[type->internal_index];
    switch (type->type)
    {
    case Signature_Type::ENUM:
    {
        // Finish enum
        type->size = 4;
        type->alignment = 4;

        // Make mirroring internal info
        if (type->options.enum_type.id == 0) {
            internal_info->options.enumeration.name.character_buffer_size = 0;
            internal_info->options.enumeration.name.character_buffer_data = 0;
            internal_info->options.enumeration.name.size = 0;
        }
        else {
            internal_info->options.enumeration.name.character_buffer_size = type->options.enum_type.id->capacity;
            internal_info->options.enumeration.name.character_buffer_data = type->options.enum_type.id->characters;
            internal_info->options.enumeration.name.size = type->options.enum_type.id->size;
        }
        int member_count = type->options.enum_type.members.size;
        internal_info->options.enumeration.members.size = member_count;
        internal_info->options.enumeration.members.data_ptr = new Internal_Type_Enum_Member[member_count];
        for (int i = 0; i < member_count; i++)
        {
            Enum_Member* member = &type->options.enum_type.members[i];
            Internal_Type_Enum_Member* internal_member = &internal_info->options.enumeration.members.data_ptr[i];
            internal_member->name.size = member->id->size;
            internal_member->name.character_buffer_size = 0;
            internal_member->name.character_buffer_data = member->id->characters;
            internal_member->value = member->value;
        }
        break;
    }
    case Signature_Type::STRUCT:
    {
        // Finish struct
        assert(type->size == 0 && type->alignment == 0, "");
        bool overlap_members = !(type->options.structure.struct_type == Structure_Type::STRUCT);

        // Calculate member offset/alignment + size
        int offset = 0;
        int alignment = 1;
        for (int i = 0; i < type->options.structure.members.size; i++)
        {
            Struct_Member* member = &type->options.structure.members[i];
            if (overlap_members) {
                offset = math_maximum(offset, member->type->size);
                member->offset = 0;
            }
            else {
                offset = align_offset_next_multiple(offset, member->type->alignment);
                member->offset = offset;
                offset += member->type->size;
            }
            alignment = math_maximum(member->type->alignment, alignment);
        }

        // Add tag if Union
        if (type->options.structure.struct_type == Structure_Type::UNION)
        {
            Type_Signature* tag_type = type_system_make_enum_empty_from_node(system, 0);
            tag_type->size = 4;
            tag_type->alignment = 4;
            for (int i = 0; i < type->options.structure.members.size; i++) {
                Struct_Member* struct_member = &type->options.structure.members[i];
                Enum_Member tag_member;
                tag_member.id = struct_member->id;
                tag_member.definition_node = 0;
                tag_member.value = i + 1;
                dynamic_array_push_back(&tag_type->options.enum_type.members, tag_member);
            }
            type_system_finish_type(system, tag_type);

            Struct_Member union_tag;
            union_tag.id = system->id_tag;
            offset = math_round_next_multiple(offset, system->i32_type->alignment);
            union_tag.offset = offset;
            union_tag.type = tag_type;
            dynamic_array_push_back(&type->options.structure.members, union_tag);
            type->options.structure.tag_member = type->options.structure.members[type->options.structure.members.size - 1];

            offset += system->i32_type->size;
            alignment = math_maximum(alignment, system->i32_type->alignment);
        }

        // Finish type
        type->size = math_round_next_multiple(offset, alignment);
        type->alignment = alignment;



        // Make mirroring internal info
        if (type->options.structure.id == 0) {
            internal_info->options.structure.name.character_buffer_size = 0;
            internal_info->options.structure.name.character_buffer_data = 0;
            internal_info->options.structure.name.size = 0;
        }
        else {
            internal_info->options.structure.name.character_buffer_size = type->options.structure.id->capacity;
            internal_info->options.structure.name.character_buffer_data = type->options.structure.id->characters;
            internal_info->options.structure.name.size = type->options.structure.id->size;
        }
        int member_count = type->options.structure.members.size;
        internal_info->options.structure.members.size = member_count;
        internal_info->options.structure.members.data_ptr = new Internal_Type_Struct_Member[member_count];
        for (int i = 0; i < member_count; i++) 
        {
            Internal_Type_Struct_Member* internal_member = &internal_info->options.structure.members.data_ptr[i];
            Struct_Member* member = &type->options.structure.members[i];
            internal_member->name.size = member->id->size;
            internal_member->name.character_buffer_size = member->id->capacity;
            internal_member->name.character_buffer_data = member->id->characters;
            internal_member->offset = member->offset;
            internal_member->type = member->type->internal_index;
        }
        internal_info->options.structure.type.tag = type->options.structure.struct_type;
        if ((int)internal_info->options.structure.type.tag == 0) {
            logg("WHAT");
        }
        if (type->options.structure.struct_type == Structure_Type::UNION) {
            internal_info->options.structure.type.tag_member_index = type->options.structure.members.size - 1;
        }
        break;
    }
    case Signature_Type::ARRAY: {
        type->size = type->options.array.element_type->size * type->options.array.element_count;
        type->alignment = type->options.array.element_type->alignment;

        internal_info->options.array.element_type = type->options.array.element_type->internal_index;
        internal_info->options.array.size = type->options.array.element_count;
        break;
    }
    default: panic("");
    }

    internal_info->size = type->size;
    internal_info->alignment = type->alignment;
    internal_info->type = type->internal_index;
    internal_info->options.tag = signature_type_to_internal_type(type->type);
}

Type_Signature* type_system_make_primitive(Type_System* system, Primitive_Type type, int size, bool is_signed)
{
    Type_Signature result;
    result.type = Signature_Type::PRIMITIVE;
    result.options.primitive.is_signed = is_signed;
    result.options.primitive.type = type;
    result.size = size;
    result.alignment = size;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_pointer(Type_System* system, Type_Signature* child_type)
{
    Type_Signature result;
    result.type = Signature_Type::POINTER;
    result.options.pointer_child = child_type;
    result.size = 8;
    result.alignment = 8;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_array_finished(Type_System* system, Type_Signature* element_type, int element_count)
{
    assert(!(element_type->size == 0 && element_type->alignment == 0), "Hey");
    Type_Signature result;
    result.type = Signature_Type::ARRAY;
    result.alignment = element_type->alignment;
    result.size = element_type->size * element_count;
    result.options.array.element_type = element_type;
    result.options.array.element_count = element_count;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_slice(Type_System* system, Type_Signature* element_type)
{
    Type_Signature result;
    result.type = Signature_Type::SLICE;
    result.alignment = 8;
    result.size = 16;
    result.options.slice.element_type = element_type;
    result.options.slice.data_member.id = system->id_data;
    result.options.slice.data_member.type = type_system_make_pointer(system, element_type);
    result.options.slice.data_member.offset = 0;
    result.options.slice.size_member.id = system->id_size;
    result.options.slice.size_member.type = system->i32_type;
    result.options.slice.size_member.offset = 8;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_function(Type_System* system, Dynamic_Array<Type_Signature*> parameter_types, Type_Signature* return_type)
{
    Type_Signature result;
    result.type = Signature_Type::FUNCTION;
    result.alignment = 1;
    result.size = 0;
    result.options.function.parameter_types = parameter_types;
    result.options.function.return_type = return_type;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_template(Type_System* system, String* id)
{
    Type_Signature result;
    result.size = 1;
    result.alignment = 1;
    result.type = Signature_Type::TEMPLATE_TYPE;
    result.options.template_id = id;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_struct_empty_full(Type_System* system, String* id, Structure_Type struct_type)
{
    Type_Signature result;
    result.type = Signature_Type::STRUCT;
    result.size = 0;
    result.alignment = 0;
    result.options.structure.id = id;
    result.options.structure.tag_member.id = 0;
    result.options.structure.tag_member.offset = 0;
    result.options.structure.tag_member.type = 0;
    result.options.structure.struct_type = struct_type;
    result.options.structure.members = dynamic_array_create_empty<Struct_Member>(2);
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_struct_empty(Type_System* system, AST_Node* struct_node)
{
    Structure_Type struct_type;
    switch (struct_node->type) {
    case AST_Node_Type::C_UNION: struct_type = Structure_Type::C_UNION; break;
    case AST_Node_Type::STRUCT: struct_type = Structure_Type::STRUCT; break;
    case AST_Node_Type::UNION: struct_type = Structure_Type::UNION; break;
    default: panic("");
    }
    return type_system_make_struct_empty_full(system, struct_node->id, struct_type);
}

Type_Signature* type_system_make_enum_empty_from_node(Type_System* system, String* id)
{
    Type_Signature result;
    result.type = Signature_Type::ENUM;
    result.size = 0;
    result.alignment = 0;
    result.options.enum_type.id = id;
    result.options.enum_type.members = dynamic_array_create_empty<Enum_Member>(3);
    return type_system_register_type(system, result);
}

void type_system_print(Type_System* system)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Type_System: ");
    for (int i = 0; i < system->types.size; i++)
    {
        Type_Signature* type = system->types[i];
        string_append_formated(&msg, "\n\t%d: ", i);
        type_signature_append_to_string(&msg, type);
        string_append_formated(&msg, " size: %d, alignment: %d", type->size, type->alignment);
    }
    string_append_formated(&msg, "\n");
    logg("%s", msg.characters);
}

Optional<Enum_Member> enum_type_find_member_by_value(Type_Signature* enum_type, int value)
{
    assert(enum_type->type == Signature_Type::ENUM, "");
    for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
        Enum_Member member = enum_type->options.enum_type.members[i];
        if (member.value == value) return optional_make_success(member);
    }
    return optional_make_failure<Enum_Member>();
}
