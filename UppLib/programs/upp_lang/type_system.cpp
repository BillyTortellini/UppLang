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
        assert(signature->options.structure.id != 0, "HEY");
        string_append_formated(string, signature->options.structure.id->characters);
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
        error_type.alignment = 0;
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

    // String
    {
        Struct_Member character_buffer_member;
        character_buffer_member.id = identifier_pool_add(pool, string_create_static("character_buffer"));
        character_buffer_member.offset = 0;
        character_buffer_member.type = type_system_make_slice(system, system->u8_type);

        Struct_Member size_member;
        size_member.id = identifier_pool_add(pool, string_create_static("size"));
        size_member.offset = 16;
        size_member.type = system->i32_type;

        Dynamic_Array<Struct_Member> string_members = dynamic_array_create_empty<Struct_Member>(2);
        dynamic_array_push_back(&string_members, character_buffer_member);
        dynamic_array_push_back(&string_members, size_member);

        Type_Signature string_type;
        string_type.type = Signature_Type::STRUCT;
        string_type.alignment = 8;
        string_type.size = 20;
        string_type.options.structure.members = string_members;
        string_type.options.structure.struct_type = Structure_Type::STRUCT;
        string_type.options.structure.id = identifier_pool_add(pool, string_create_static("String"));
        system->string_type = type_system_register_type(system, string_type);
    }

    // Primitive Type enum
    {
        system->type_primitive_type_enum = type_system_make_enum_empty_from_node(system, identifier_pool_add(pool, string_create_static("Primitive_Type")));
        Enum_Member m0;
        m0.definition_node = 0;
        m0.id = identifier_pool_add(pool, string_create_static("INTEGER"));
        m0.value = 1;
        dynamic_array_push_back(&system->type_primitive_type_enum->options.enum_type.members, m0);
        Enum_Member m1;
        m1.definition_node = 0;
        m1.id = identifier_pool_add(pool, string_create_static("FLOAT"));
        m1.value = 2;
        dynamic_array_push_back(&system->type_primitive_type_enum->options.enum_type.members, m1);
        Enum_Member m2;
        m2.definition_node = 0;
        m2.id = identifier_pool_add(pool, string_create_static("BOOL"));
        m2.value = 3;
        dynamic_array_push_back(&system->type_primitive_type_enum->options.enum_type.members, m2);
        type_system_finish_type(system, system->type_primitive_type_enum);
    }

    // Type-Information
    {
        system->type_information_type = type_system_make_struct_empty_full(
            system, identifier_pool_add(pool, string_create_static("Type_Information")), Structure_Type::STRUCT
        );
        Struct_Member type_member;
        type_member.id = identifier_pool_add(pool, string_create_static("type"));
        type_member.offset = 0;
        type_member.type = system->type_type;
        dynamic_array_push_back(&system->type_information_type->options.structure.members, type_member);

        Struct_Member size_member;
        size_member.id = identifier_pool_add(pool, string_create_static("size"));
        size_member.offset = 0;
        size_member.type = system->i32_type;
        dynamic_array_push_back(&system->type_information_type->options.structure.members, size_member);

        Struct_Member alignment_member;
        alignment_member.id = identifier_pool_add(pool, string_create_static("alignment"));
        alignment_member.offset = 0;
        alignment_member.type = system->i32_type;
        dynamic_array_push_back(&system->type_information_type->options.structure.members, alignment_member);

        Struct_Member is_primitive_member;
        is_primitive_member.id = identifier_pool_add(pool, string_create_static("is_primitive"));
        is_primitive_member.offset = 0;
        is_primitive_member.type = system->bool_type;
        dynamic_array_push_back(&system->type_information_type->options.structure.members, is_primitive_member);

        Struct_Member primitive_type_member;
        primitive_type_member.id = identifier_pool_add(pool, string_create_static("primitive_type"));
        primitive_type_member.offset = 0;
        primitive_type_member.type = system->type_primitive_type_enum;
        dynamic_array_push_back(&system->type_information_type->options.structure.members, primitive_type_member);

        type_system_finish_type(system, system->type_information_type);
    }

    // Empty structure
    {
        Type_Signature empty_struct_type;
        empty_struct_type.alignment = 1;
        empty_struct_type.size = 0;
        empty_struct_type.type = Signature_Type::STRUCT;
        empty_struct_type.options.structure.struct_type = Structure_Type::STRUCT;
        empty_struct_type.options.structure.id = 0;
        empty_struct_type.options.structure.members = dynamic_array_create_empty<Struct_Member>(1);
        system->empty_struct_type = type_system_register_type(system, empty_struct_type);
    }
}

Type_System type_system_create()
{
    Type_System result;
    result.types = dynamic_array_create_empty<Type_Signature*>(256);
    result.internal_type_infos = dynamic_array_create_empty<Internal_Type_Information>(256);
    return result;
}

void type_system_destroy(Type_System* system) {
    dynamic_array_destroy(&system->types);
    dynamic_array_destroy(&system->internal_type_infos);
}

void type_system_reset(Type_System* system) {
    for (int i = 0; i < system->types.size; i++) {
        type_signature_destroy(system->types[i]);
        delete system->types[i];
    }
    dynamic_array_reset(&system->types);
    dynamic_array_reset(&system->internal_type_infos);
}

Type_Signature* type_system_register_type(Type_System* system, Type_Signature signature)
{
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
        info.type = signature.internal_index;
        info.size = signature.size;
        info.alignment = signature.alignment;
        info.is_primitive_type = signature.type == Signature_Type::PRIMITIVE;
        if (info.is_primitive_type) {
            info.primitive_type = (i32)signature.options.primitive.type;
        }
        dynamic_array_push_back(&system->internal_type_infos, info);
    }
    Type_Signature* new_sig = new Type_Signature();
    *new_sig = signature;
    dynamic_array_push_back(&system->types, new_sig);
    return new_sig;
}

void type_system_finish_type(Type_System* system, Type_Signature* type)
{
    assert(type->internal_index < system->internal_type_infos.size, "");
    Internal_Type_Information* internal_info = &system->internal_type_infos[type->internal_index];
    switch (type->type)
    {
    case Signature_Type::ENUM:
        // FUTURE: Update interal info membaz
        type->size = system->i32_type->size;
        type->alignment = system->i32_type->alignment;
        break;
    case Signature_Type::STRUCT:
    {
        // FUTURE: Update interal info membaz
        assert(type->size == 0 && type->alignment == 0, "");
        bool overlap_members = !(type->options.structure.struct_type == Structure_Type::STRUCT);
        int offset = 0;
        int alignment = 0;
        for (int i = 0; i < type->options.structure.members.size; i++)
        {
            Struct_Member* member = &type->options.structure.members[i];
            offset = align_offset_next_multiple(offset, member->type->size);
            member->offset = offset;
            alignment = math_maximum(member->type->alignment, alignment);
            if (!overlap_members) {
                offset += member->type->size;
            }
        }
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

            type->options.structure.tag_member.id = system->id_tag;
            type->options.structure.tag_member.type = tag_type;
            offset = math_round_next_multiple(offset, tag_type->alignment);
            type->options.structure.tag_member.offset = offset;
            offset += tag_type->size;
            alignment = math_maximum(alignment, tag_type->alignment);
            dynamic_array_push_back(&type->options.structure.members, type->options.structure.tag_member);
        }

        type->size = math_round_next_multiple(offset, alignment);
        type->alignment = alignment;
        break;
    }
    case Signature_Type::ARRAY: {
        type->size = type->options.array.element_type->size * type->options.array.element_count;
        type->alignment = type->options.array.element_type->alignment;
        break;
    }
    default: panic("");
    }

    internal_info->size = type->size;
    internal_info->alignment = type->alignment;
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
