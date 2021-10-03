#include "type_system.hpp"

#include "compiler.hpp"

void type_signature_destroy(Type_Signature* sig) 
{
    if (sig->type == Signature_Type::FUNCTION)
        dynamic_array_destroy(&sig->options.function.parameter_types);
    if (sig->type == Signature_Type::STRUCT)
        dynamic_array_destroy(&sig->options.structure.members);
}

Type_Signature type_signature_make_primitive(Primitive_Type type, int size, bool is_signed)
{
    Type_Signature result;
    result.type = Signature_Type::PRIMITIVE;
    result.options.primitive.is_signed = is_signed;
    result.options.primitive.type = type;
    result.size = size;
    result.alignment = size;
    return result;
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
    system->bool_type = new Type_Signature();
    system->i8_type = new Type_Signature();
    system->i16_type = new Type_Signature();
    system->i32_type = new Type_Signature();
    system->i64_type = new Type_Signature();
    system->u8_type = new Type_Signature();
    system->u16_type = new Type_Signature();
    system->u32_type = new Type_Signature();
    system->u64_type = new Type_Signature();
    system->f32_type = new Type_Signature();
    system->f64_type = new Type_Signature();
    system->error_type = new Type_Signature();
    system->void_type = new Type_Signature();
    system->void_ptr_type = new Type_Signature();

    *system->bool_type = type_signature_make_primitive(Primitive_Type::BOOLEAN, 1, false);
    *system->i8_type = type_signature_make_primitive(Primitive_Type::INTEGER, 1, true);
    *system->i16_type = type_signature_make_primitive(Primitive_Type::INTEGER, 2, true);
    *system->i32_type = type_signature_make_primitive(Primitive_Type::INTEGER, 4, true);
    *system->i64_type = type_signature_make_primitive(Primitive_Type::INTEGER, 8, true);
    *system->u8_type = type_signature_make_primitive(Primitive_Type::INTEGER, 1, false);
    *system->u16_type = type_signature_make_primitive(Primitive_Type::INTEGER, 2, false);
    *system->u32_type = type_signature_make_primitive(Primitive_Type::INTEGER, 4, false);
    *system->u64_type = type_signature_make_primitive(Primitive_Type::INTEGER, 8, false);
    *system->f32_type = type_signature_make_primitive(Primitive_Type::FLOAT, 4, true);
    *system->f64_type = type_signature_make_primitive(Primitive_Type::FLOAT, 8, true);
    {
        system->error_type->type = Signature_Type::ERROR_TYPE;
        system->error_type->size = 0;
        system->error_type->alignment = 1;
    }
    system->void_type->type = Signature_Type::VOID_TYPE;
    system->void_type->size = 0;
    system->void_type->alignment = 1;

    system->void_ptr_type->type = Signature_Type::POINTER;
    system->void_ptr_type->size = 8;
    system->void_ptr_type->alignment = 8;
    system->void_ptr_type->options.pointer_child = system->void_type;

    dynamic_array_push_back(&system->types, system->bool_type);
    dynamic_array_push_back(&system->types, system->i8_type);
    dynamic_array_push_back(&system->types, system->i16_type);
    dynamic_array_push_back(&system->types, system->i32_type);
    dynamic_array_push_back(&system->types, system->i64_type);
    dynamic_array_push_back(&system->types, system->u8_type);
    dynamic_array_push_back(&system->types, system->u16_type);
    dynamic_array_push_back(&system->types, system->u32_type);
    dynamic_array_push_back(&system->types, system->u64_type);
    dynamic_array_push_back(&system->types, system->f32_type);
    dynamic_array_push_back(&system->types, system->f64_type);
    dynamic_array_push_back(&system->types, system->error_type);
    dynamic_array_push_back(&system->types, system->void_type);
    dynamic_array_push_back(&system->types, system->void_ptr_type);

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

        system->string_type = new Type_Signature();
        system->string_type->type = Signature_Type::STRUCT;
        system->string_type->alignment = 8;
        system->string_type->size = 20;
        system->string_type->options.structure.members = string_members;
        system->string_type->options.structure.id = identifier_pool_add(pool, string_create_static("String"));
        dynamic_array_push_back(&system->types, system->string_type);
    }

    system->id_data = identifier_pool_add(pool, string_create_static("data"));
    system->id_size = identifier_pool_add(pool, string_create_static("size"));
}

Type_System type_system_create()
{
    Type_System result;
    result.types = dynamic_array_create_empty<Type_Signature*>(256);
    return result;
}

void type_system_destroy(Type_System* system) {
    dynamic_array_destroy(&system->types);
}

void type_system_reset(Type_System* system) {
    for (int i = 0; i < system->types.size; i++) {
        type_signature_destroy(system->types[i]);
        delete system->types[i];
    }
    dynamic_array_reset(&system->types);
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
                case Signature_Type::VOID_TYPE: are_equal = true; break;
                case Signature_Type::ERROR_TYPE: are_equal = true; break;
                case Signature_Type::PRIMITIVE: are_equal = sig1->options.primitive.type == sig2->options.primitive.type && 
                    sig1->options.primitive.is_signed == sig2->options.primitive.is_signed && sig1->size == sig2->size; break;
                case Signature_Type::POINTER: are_equal = sig1->options.pointer_child == sig2->options.pointer_child; break;
                case Signature_Type::STRUCT: are_equal = false; break;
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

    Type_Signature* new_sig = new Type_Signature();
    *new_sig = signature;
    dynamic_array_push_back(&system->types, new_sig);
    return new_sig;
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


