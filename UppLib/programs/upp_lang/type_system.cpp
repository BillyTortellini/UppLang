#include "type_system.hpp"
#include "compiler.hpp"
#include "dependency_analyser.hpp"

using AST::Structure_Type;

Type_Signature* type_system_register_type(Type_System* system, Type_Signature signature);

void type_signature_destroy(Type_Signature* sig) 
{
    if (sig->type == Signature_Type::FUNCTION) {
        auto& params = sig->options.function.parameters;
        if (params.data != 0 && params.capacity != 0) {
            dynamic_array_destroy(&params);
        }
    }
    if (sig->type == Signature_Type::STRUCT)
        dynamic_array_destroy(&sig->options.structure.members);
    if (sig->type == Signature_Type::ENUM)
        dynamic_array_destroy(&sig->options.enum_type.members);
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
        if (signature->options.array.count_known) {
            string_append_formated(string, "[%d]", signature->options.array.element_count);
        }
        else {
            string_append_formated(string, "[Unknown]", signature->options.array.element_count);
        }
        type_signature_append_to_string_with_children(string, signature->options.array.element_type, print_child);
        break;
    case Signature_Type::SLICE:
        string_append_formated(string, "[]");
        type_signature_append_to_string_with_children(string, signature->options.array.element_type, print_child);
        break;
    case Signature_Type::UNKNOWN_TYPE:
        string_append_formated(string, "Unknown-Type");
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
        if (signature->options.structure.symbol != 0) {
            string_append_formated(string, signature->options.structure.symbol->id->characters);
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
        for (int i = 0; i < signature->options.function.parameters.size; i++) {
            type_signature_append_to_string_with_children(string, signature->options.function.parameters[i].type, print_child);
            if (i != signature->options.function.parameters.size - 1) {
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
    case Signature_Type::UNKNOWN_TYPE:
        break;
    case Signature_Type::TYPE_TYPE: {
        u64 value = *(u64*)value_ptr;
        string_append_formated(string, "Type_Type, index: %d", value);
        break;
    }
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
        if (type->options.structure.symbol != 0) {
            string_append_formated(string, "%s{", type->options.structure.symbol->id->characters);
        }
        else {
            string_append_formated(string, "Struct{");
        }
        switch (type->options.structure.struct_type)
        {
        case Structure_Type::C_UNION: break;
        case Structure_Type::UNION: {
            int tag = *(i32*)(value_ptr + type->options.structure.tag_member.offset);
            if (tag > 0 && tag < type->options.structure.members.size) {
                Struct_Member* member = &type->options.structure.members[tag - 1];
                string_append_formated(string, "%s = ", member->id->characters);
                type_signature_append_value_to_string(member->type, value_ptr + member->offset, string);
            }
        }
        case Structure_Type::STRUCT:
            for (int i = 0; i < type->options.structure.members.size; i++) 
            {
                Struct_Member* mem = &type->options.structure.members[i];
                byte* mem_ptr = value_ptr + mem->offset;
                type_signature_append_value_to_string(mem->type, mem_ptr, string);
                if (i != type->options.structure.members.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
        }
        string_append_formated(string, "}");
        break;
    }
    case Signature_Type::ENUM:
    {
        if (type->options.enum_type.id != 0) {
            string_append_formated(string, "%s{", type->options.enum_type.id->characters);
        }
        else {
            string_append_formated(string, "Enum{");
        }
        int value = *(i32*)value_ptr;
        Enum_Item* found = 0;
        for (int i = 0; i < type->options.enum_type.members.size; i++) {
            Enum_Item* mem = &type->options.enum_type.members[i];
            if (value == mem->value) {
                found = mem;
                break;
            }
        }
        if (found == 0) {
            string_append_formated(string, "INVALID_VALUE");
        }
        else {
            string_append_formated(string, found->id->characters);
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

template<typename T>
void test_type_similarity(Type_Signature* signature) {
    assert(signature->size == sizeof(T) && signature->alignment == alignof(T), "");
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

void type_system_add_predefined_types(Type_System* system)
{
    Predefined_Types* types = &system->predefined_types;

    types->bool_type = type_system_make_primitive(system, Primitive_Type::BOOLEAN, 1, false);
    types->i8_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 1, true);
    types->i16_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 2, true);
    types->i32_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 4, true);
    types->i64_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 8, true);
    types->u8_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 1, false);
    types->u16_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 2, false);
    types->u32_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 4, false);
    types->u64_type = type_system_make_primitive(system, Primitive_Type::INTEGER, 8, false);
    types->f32_type = type_system_make_primitive(system, Primitive_Type::FLOAT, 4, true);
    types->f64_type = type_system_make_primitive(system, Primitive_Type::FLOAT, 8, true);
    {
        Type_Signature error_type;
        error_type.size = 1;
        error_type.alignment = 1;
        error_type.type = Signature_Type::UNKNOWN_TYPE;
        types->unknown_type = type_system_register_type(system, error_type);

        Type_Signature void_type;
        void_type.type = Signature_Type::VOID_TYPE;
        void_type.size = 0;
        void_type.alignment = 1;
        types->void_type = type_system_register_type(system, void_type);

        Type_Signature void_ptr_type;
        void_ptr_type.type = Signature_Type::POINTER;
        void_ptr_type.size = 8;
        void_ptr_type.alignment = 8;
        void_ptr_type.options.pointer_child = types->void_type;
        types->void_ptr_type = type_system_register_type(system, void_ptr_type);

        Type_Signature type_type;
        type_type.type = Signature_Type::TYPE_TYPE;
        type_type.size = 8;
        type_type.alignment = 8;
        types->type_type = type_system_register_type(system, type_type);
    }

    using AST::Structure_Type;
    // Empty structure
    {
        types->empty_struct_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
        type_system_finish_type(system, types->empty_struct_type);
    }

    auto add_member_cstr = [&](Type_Signature* struct_type, const char* member_name, Type_Signature* member_type) {
        String* id = identifier_pool_add(&compiler.identifier_pool, string_create_static(member_name));
        struct_add_member(struct_type, id, member_type);
    };

    // String
    {
        types->string_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
        add_member_cstr(types->string_type, "character_buffer", type_system_make_slice(system, types->u8_type));
        add_member_cstr(types->string_type, "size", types->i32_type);
        type_system_finish_type(system, types->string_type);
        test_type_similarity<Upp_String>(types->string_type);
    }

    // Any
    {
        types->any_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
        add_member_cstr(types->any_type, "data", types->void_ptr_type);
        add_member_cstr(types->any_type, "type", types->type_type);
        type_system_finish_type(system, types->any_type);
        test_type_similarity<Upp_Any>(types->any_type);
    }

    // Type Information
    {
        Type_Signature* option_type = type_system_make_struct_empty(
            system, 0, Structure_Type::UNION
        );
        add_member_cstr(option_type, "void_type", types->empty_struct_type);
        add_member_cstr(option_type, "type", types->empty_struct_type);
        add_member_cstr(option_type, "pointer", types->type_type);
        // Array
        {
            Type_Signature* array_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
            add_member_cstr(array_type, "element_type", types->type_type);
            add_member_cstr(array_type, "size", types->i32_type);
            type_system_finish_type(system, array_type);
            add_member_cstr(option_type, "array", array_type);
            test_type_similarity<Internal_Type_Array>(array_type);
        }
        // Slice
        {
            Type_Signature* slice_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
            add_member_cstr(slice_type, "element_type", types->type_type);
            type_system_finish_type(system, slice_type);
            add_member_cstr(option_type, "slice", slice_type);
            test_type_similarity<Internal_Type_Slice>(slice_type);
        }
        // Primitive
        {
            Type_Signature* primitive_type = type_system_make_struct_empty(system, 0, Structure_Type::UNION);
            {
                Type_Signature* integer_info_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
                add_member_cstr(integer_info_type, "is_signed", types->bool_type);
                type_system_finish_type(system, integer_info_type);
                add_member_cstr(primitive_type, "integer", integer_info_type);
            }
            add_member_cstr(primitive_type, "floating_point", types->empty_struct_type);
            add_member_cstr(primitive_type, "boolean", types->empty_struct_type);
            type_system_finish_type(system, primitive_type);
            add_member_cstr(option_type, "primitive", primitive_type);
            test_type_similarity<Internal_Type_Primitive>(primitive_type);
        }
        // Function
        {
            Type_Signature* function_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
            add_member_cstr(function_type, "parameter_types", type_system_make_slice(system, types->type_type));
            add_member_cstr(function_type, "return_type", types->type_type);
            type_system_finish_type(system, function_type);
            add_member_cstr(option_type, "function", function_type);
            test_type_similarity<Internal_Type_Function>(function_type);
        }
        // Struct
        {
            Type_Signature* struct_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
            {
                Type_Signature* struct_member_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
                add_member_cstr(struct_member_type, "name", types->string_type);
                add_member_cstr(struct_member_type, "type", types->type_type);
                add_member_cstr(struct_member_type, "offset", types->i32_type);
                type_system_finish_type(system, struct_member_type);
                add_member_cstr(struct_type, "members", type_system_make_slice(system, struct_member_type));
                test_type_similarity<Internal_Type_Struct_Member>(struct_member_type);
            }
            add_member_cstr(struct_type, "name", types->string_type);
            {
                Type_Signature* structure_type_type = type_system_make_struct_empty(system, 0, Structure_Type::UNION);
                add_member_cstr(structure_type_type, "structure", types->empty_struct_type);
                add_member_cstr(structure_type_type, "union_untagged", types->empty_struct_type);
                {
                    Type_Signature* union_tagged_struct = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
                    add_member_cstr(union_tagged_struct, "tag_member_index", types->i32_type);
                    type_system_finish_type(system, union_tagged_struct);
                    add_member_cstr(structure_type_type, "union_tagged", union_tagged_struct);
                }
                type_system_finish_type(system, structure_type_type);
                add_member_cstr(struct_type, "type", structure_type_type);
                test_type_similarity<Internal_Structure_Type>(structure_type_type);
            }
            type_system_finish_type(system, struct_type);
            add_member_cstr(option_type, "structure", struct_type);
            test_type_similarity<Internal_Type_Struct>(struct_type);
        }
        // ENUM
        {
            Type_Signature* enum_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
            {
                Type_Signature* enum_member_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
                add_member_cstr(enum_member_type, "name", types->string_type);
                add_member_cstr(enum_member_type, "value", types->i32_type);
                type_system_finish_type(system, enum_member_type);
                add_member_cstr(enum_type, "members", type_system_make_slice(system, enum_member_type));
                test_type_similarity<Internal_Type_Enum_Member>(enum_member_type);
            }
            add_member_cstr(enum_type, "name", types->string_type);
            type_system_finish_type(system, enum_type);
            add_member_cstr(option_type, "enumeration", enum_type);
            test_type_similarity<Internal_Type_Enum>(enum_type);
        }
        type_system_finish_type(system, option_type);
        test_type_similarity<Internal_Type_Info_Options>(option_type);

        // Type Information
        {
            Type_Signature* type_info_type = type_system_make_struct_empty(system, 0, Structure_Type::STRUCT);
            add_member_cstr(type_info_type, "type", types->type_type);
            add_member_cstr(type_info_type, "size", types->i32_type);
            add_member_cstr(type_info_type, "alignment", types->i32_type);
            add_member_cstr(type_info_type, "options", option_type);
            type_system_finish_type(system, type_info_type);
            types->type_information_type = type_info_type;
            test_type_similarity<Internal_Type_Information>(type_info_type);
        }
    }

    // Hardcoded Functions
    {
        auto& ts = *system;
        auto make_param = [&](Type_Signature* signature, const char* name) -> Function_Parameter {
            Function_Parameter param;
            param.type = signature;
            param.name = optional_make_success(identifier_pool_add(&compiler.identifier_pool, string_create_static(name)));
            return param;
        };
        types->type_assert = type_system_make_function(&ts, { make_param(types->bool_type, "condition") }, types->void_type);
        types->type_free = type_system_make_function(&ts, { make_param(types->void_ptr_type, "pointer") }, types->void_type);
        types->type_malloc = type_system_make_function(&ts, { make_param(types->i32_type, "size") }, types->void_ptr_type);
        types->type_type_info = type_system_make_function(&ts, { make_param(types->type_type, "type_id") }, types->type_information_type);
        types->type_type_of = type_system_make_function(&ts, { make_param(types->empty_struct_type, "type") }, types->type_type); // I am not sure if this is valid...
        types->type_print_bool = type_system_make_function(&ts, { make_param(types->bool_type, "value") }, types->void_type);
        types->type_print_i32 = type_system_make_function(&ts, { make_param(types->i32_type, "value") }, types->void_type);
        types->type_print_f32 = type_system_make_function(&ts, { make_param(types->f32_type, "value") }, types->void_type);
        types->type_print_line = type_system_make_function(&ts, {}, types->void_type);
        types->type_print_string = type_system_make_function(&ts, { make_param(types->string_type, "value") }, types->void_type);
        types->type_read_i32 = type_system_make_function(&ts, {}, types->void_type);
        types->type_read_f32 = type_system_make_function(&ts, {}, types->void_type);
        types->type_read_bool = type_system_make_function(&ts, {}, types->void_type);
        types->type_random_i32 = type_system_make_function(&ts, {}, types->void_type);
    }
}



Internal_Type_Options_Tag signature_type_to_internal_type(Signature_Type type)
{
    switch (type)
    {
    case Signature_Type::VOID_TYPE: return Internal_Type_Options_Tag::VOID_TYPE;
    case Signature_Type::TEMPLATE_TYPE:return Internal_Type_Options_Tag::VOID_TYPE;
    case Signature_Type::UNKNOWN_TYPE:return Internal_Type_Options_Tag::VOID_TYPE;

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

Type_Signature* type_system_register_type(Type_System * system, Type_Signature signature)
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
                case Signature_Type::UNKNOWN_TYPE: are_equal = true; break;
                case Signature_Type::PRIMITIVE: are_equal = sig1->options.primitive.type == sig2->options.primitive.type &&
                    sig1->options.primitive.is_signed == sig2->options.primitive.is_signed && sig1->size == sig2->size; break;
                case Signature_Type::POINTER: are_equal = type_signature_equals(sig1->options.pointer_child, sig2->options.pointer_child); break;
                case Signature_Type::STRUCT: are_equal = false; break;
                case Signature_Type::ENUM: are_equal = false; break;
                case Signature_Type::TEMPLATE_TYPE: are_equal = false; break;
                case Signature_Type::ARRAY: are_equal = type_signature_equals(sig1->options.array.element_type, sig2->options.array.element_type) &&
                    sig1->options.array.element_count == sig2->options.array.element_count && sig1->options.array.count_known == sig2->options.array.count_known; break;
                case Signature_Type::SLICE: are_equal = type_signature_equals(sig1->options.array.element_type, sig2->options.array.element_type); break;
                case Signature_Type::FUNCTION:
                {
                    are_equal = true;
                    if (!type_signature_equals(sig1->options.function.return_type, sig2->options.function.return_type) ||
                        sig1->options.function.parameters.size != sig2->options.function.parameters.size) {
                        are_equal = false; break;
                    }
                    for (int i = 0; i < sig1->options.function.parameters.size; i++) {
                        auto& param1 = sig1->options.function.parameters[i];
                        auto& param2 = sig2->options.function.parameters[i];
                        if (!type_signature_equals(param1.type, param2.type) || !param1.name.available != param2.name.available) {
                            are_equal = false;
                            break;
                        }
                        if (param1.name.available && param1.name.value != param2.name.value) {
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
        case Signature_Type::TEMPLATE_TYPE:
        case Signature_Type::UNKNOWN_TYPE:
        case Signature_Type::TYPE_TYPE:
        case Signature_Type::VOID_TYPE:
            break; // There are no options for these types

        case Signature_Type::FUNCTION:
        {
            int param_count = signature.options.function.parameters.size;
            info.options.function.parameters.data_ptr = new u64[param_count];
            info.options.function.parameters.size = param_count;
            for (int i = 0; i < param_count; i++) {
                Type_Signature* param = signature.options.function.parameters[i].type;
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
        case Signature_Type::ARRAY: {
            info.options.array.element_type = signature.options.array.element_type->internal_index;
            info.options.array.size = signature.options.array.element_count;
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

void type_system_finish_type(Type_System * system, Type_Signature * type)
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
            Enum_Item* member = &type->options.enum_type.members[i];
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
            assert(!(member->type->size == 0 && member->type->alignment == 0), "");
            if (overlap_members) {
                offset = math_maximum(offset, member->type->size);
                member->offset = 0;
            }
            else {
                offset = math_round_next_multiple(offset, member->type->alignment);
                member->offset = offset;
                offset += member->type->size;
            }
            alignment = math_maximum(member->type->alignment, alignment);
        }

        // Add tag if Union
        if (type->options.structure.struct_type == Structure_Type::UNION)
        {
            Type_Signature* tag_type = type_system_make_enum_empty(system, 0);
            tag_type->size = 4;
            tag_type->alignment = 4;
            for (int i = 0; i < type->options.structure.members.size; i++) {
                Struct_Member* struct_member = &type->options.structure.members[i];
                Enum_Item tag_member;
                tag_member.id = struct_member->id;
                //tag_member.definition_node = 0;
                tag_member.value = i + 1;
                dynamic_array_push_back(&tag_type->options.enum_type.members, tag_member);
            }
            type_system_finish_type(system, tag_type);

            auto& types = system->predefined_types;

            Struct_Member union_tag;
            union_tag.id = compiler.id_tag;
            offset = math_round_next_multiple(offset, types.i32_type->alignment);
            union_tag.offset = offset;
            union_tag.type = tag_type;
            dynamic_array_push_back(&type->options.structure.members, union_tag);
            type->options.structure.tag_member = type->options.structure.members[type->options.structure.members.size - 1];

            offset += types.i32_type->size;
            alignment = math_maximum(alignment, types.i32_type->alignment);
        }

        // Finish type
        type->size = math_round_next_multiple(offset, alignment);
        type->alignment = alignment;



        // Make mirroring internal info
        if (type->options.structure.symbol == 0) {
            internal_info->options.structure.name.character_buffer_size = 0;
            internal_info->options.structure.name.character_buffer_data = 0;
            internal_info->options.structure.name.size = 0;
        }
        else {
            internal_info->options.structure.name.character_buffer_size = type->options.structure.symbol->id->capacity;
            internal_info->options.structure.name.character_buffer_data = type->options.structure.symbol->id->characters;
            internal_info->options.structure.name.size = type->options.structure.symbol->id->size;
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

Type_Signature* type_system_make_primitive(Type_System * system, Primitive_Type type, int size, bool is_signed)
{
    Type_Signature result;
    result.type = Signature_Type::PRIMITIVE;
    result.options.primitive.is_signed = is_signed;
    result.options.primitive.type = type;
    result.size = size;
    result.alignment = size;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_pointer(Type_System * system, Type_Signature * child_type)
{
    Type_Signature result;
    result.type = Signature_Type::POINTER;
    result.options.pointer_child = child_type;
    result.size = 8;
    result.alignment = 8;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_array(Type_System * system, Type_Signature * element_type, bool count_known, int element_count)
{
    assert(!(count_known && element_count < 0), "Hey");
    Type_Signature result;
    result.type = Signature_Type::ARRAY;
    result.alignment = element_type->alignment;
    result.options.array.element_type = element_type;
    result.options.array.count_known = count_known;
    if (count_known) {
        result.options.array.element_count = element_count;
    }
    else {
        result.options.array.element_count = 1;
    }
    result.size = element_type->size * result.options.array.element_count;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_slice(Type_System * system, Type_Signature * element_type)
{
    Type_Signature result;
    result.type = Signature_Type::SLICE;
    result.alignment = 8;
    result.size = 16;
    result.options.slice.element_type = element_type;
    result.options.slice.data_member.id = compiler.id_data;
    result.options.slice.data_member.type = type_system_make_pointer(system, element_type);
    result.options.slice.data_member.offset = 0;
    result.options.slice.size_member.id = compiler.id_size;
    result.options.slice.size_member.type = system->predefined_types.i32_type;
    result.options.slice.size_member.offset = 8;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_function(Type_System * system, Dynamic_Array<Function_Parameter> parameter_types, Type_Signature * return_type)
{
    Type_Signature result;
    result.type = Signature_Type::FUNCTION;
    result.alignment = 8;
    result.size = 8;
    result.options.function.parameters = parameter_types;
    result.options.function.return_type = return_type;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_function(Type_System * system, std::initializer_list<Function_Parameter> parameter_types, Type_Signature * return_type)
{
    Dynamic_Array<Function_Parameter> params = dynamic_array_create_empty<Function_Parameter>(1);
    for (auto& param : parameter_types) {
        dynamic_array_push_back(&params, param);
    }
    return type_system_make_function(system, params, return_type);
}

Type_Signature* type_system_make_template(Type_System * system, String * id)
{
    Type_Signature result;
    result.size = 1;
    result.alignment = 1;
    result.type = Signature_Type::TEMPLATE_TYPE;
    result.options.template_id = id;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_struct_empty(Type_System * system, Symbol * symbol, Structure_Type struct_type)
{
    Type_Signature result;
    result.type = Signature_Type::STRUCT;
    result.size = 0;
    result.alignment = 0;
    result.options.structure.symbol = symbol;
    result.options.structure.tag_member.id = 0;
    result.options.structure.tag_member.offset = 0;
    result.options.structure.tag_member.type = 0;
    result.options.structure.struct_type = struct_type;
    result.options.structure.members = dynamic_array_create_empty<Struct_Member>(2);
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_enum_empty(Type_System * system, String * id)
{
    Type_Signature result;
    result.type = Signature_Type::ENUM;
    result.size = 0;
    result.alignment = 0;
    result.options.enum_type.id = id;
    result.options.enum_type.members = dynamic_array_create_empty<Enum_Item>(3);
    return type_system_register_type(system, result);
}

void type_system_print(Type_System * system)
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

Optional<Enum_Item> enum_type_find_member_by_value(Type_Signature * enum_type, int value)
{
    assert(enum_type->type == Signature_Type::ENUM, "");
    for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
        Enum_Item member = enum_type->options.enum_type.members[i];
        if (member.value == value) return optional_make_success(member);
    }
    return optional_make_failure<Enum_Item>();
}

Optional<Struct_Member> type_signature_find_member_by_id(Type_Signature * type, String * id)
{
    switch (type->type)
    {
    case Signature_Type::STRUCT:
    {
        for (int i = 0; i < type->options.structure.members.size; i++) {
            auto& member = type->options.structure.members[i];
            if (member.id == id) return optional_make_success(member);
        }
        if (type->options.structure.struct_type == AST::Structure_Type::UNION && id == compiler.id_tag) {
            return optional_make_success(type->options.structure.tag_member);
        }
        break;
    }
    case Signature_Type::SLICE:
    {
        if (id == compiler.id_data) {
            return optional_make_success(type->options.slice.data_member);
        }
        else if (id == compiler.id_size) {
            return optional_make_success(type->options.slice.size_member);
        }
        break;
    }
    }
    return optional_make_failure<Struct_Member>();
}

bool type_signature_equals(Type_Signature * a, Type_Signature * b)
{
    return a == b;
}

void struct_add_member(Type_Signature * struct_sig, String * id, Type_Signature * member_type)
{
    Struct_Member member;
    member.id = id;
    member.offset = 0;
    member.type = member_type;
    dynamic_array_push_back(&struct_sig->options.structure.members, member);
}

