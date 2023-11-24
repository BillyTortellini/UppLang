#include "type_system.hpp"
#include "compiler.hpp"
#include "symbol_table.hpp"

using AST::Structure_Type;

void type_base_destroy(Type_Base* base) 
{
    if (base->type == Type_Type::FUNCTION) {
        auto fn = downcast<Type_Function>(base);
        auto& params = fn->parameters;
        if (params.data != 0 && params.capacity != 0) {
            dynamic_array_destroy(&params);
        }
    }
    else if (base->type == Type_Type::STRUCT) {
        auto st = downcast<Type_Struct>(base);
        dynamic_array_destroy(&st->members);
    }
    else if (base->type == Type_Type::ENUM) {
        auto enum_type = downcast<Type_Enum>(base);
        dynamic_array_destroy(&enum_type->members);
    }
}

void type_append_to_string_with_children(String* string, Type_Base* signature, bool print_child)
{
    switch (signature->type)
    {
    case Type_Type::VOID_POINTER:
        string_append_formated(string, "void_pointer");
        break;
    case Type_Type::ARRAY: {
        auto array_type = downcast<Type_Array>(signature);
        if (array_type->count_known) {
            string_append_formated(string, "[%d]", array_type->element_count);
        }
        else {
            string_append_formated(string, "[Unknown]");
        }
        type_append_to_string_with_children(string, array_type->element_type, print_child);
        break;
    }
    case Type_Type::SLICE: {
        auto slice_type = downcast<Type_Slice>(signature);
        string_append_formated(string, "[]");
        type_append_to_string_with_children(string, slice_type->element_type, print_child);
        break;
    }
    case Type_Type::ERROR_TYPE:
        string_append_formated(string, "Error-Type");
        break;
    case Type_Type::TYPE_HANDLE:
        string_append_formated(string, "Type_Handle");
        break;
    case Type_Type::POINTER: {
        auto pointer_type = downcast<Type_Pointer>(signature);
        string_append_formated(string, "*");
        type_append_to_string_with_children(string, pointer_type->points_to_type, print_child);
        break;
    }
    case Type_Type::PRIMITIVE: {
        auto primitive = downcast<Type_Primitive>(signature);
        switch (primitive->primitive_type)
        {
        case Primitive_Type::BOOLEAN: string_append_formated(string, "bool"); break;
        case Primitive_Type::INTEGER: string_append_formated(string, "%s%d", primitive->is_signed ? "int" : "uint", signature->size * 8); break;
        case Primitive_Type::FLOAT: string_append_formated(string, "float%d", signature->size * 8); break;
        default: panic("Heyo");
        }
        break;
    }
    case Type_Type::ENUM:
    {
        auto enum_type = downcast<Type_Enum>(signature);
        if (enum_type->name != 0) {
            string_append_formated(string, enum_type->name->characters);
        }
        if (print_child)
        {
            string_append_formated(string, "{");
            for (int i = 0; i < enum_type->members.size; i++) {
                string_append_formated(string, "%s(%d)", enum_type->members[i].name->characters, enum_type->members[i].value);
                if (i != enum_type->members.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
            string_append_formated(string, "}");
        }
        break;
    }
    case Type_Type::STRUCT:
    {
        auto struct_type = downcast<Type_Struct>(signature);
        auto& members = struct_type->members;
        if (struct_type->name.available) {
            string_append_formated(string, struct_type->name.value->characters);
        }
        else {
            string_append_formated(string, "Struct");
        }
        if (print_child)
        {
            string_append_formated(string, "{");
            for (int i = 0; i < members.size && print_child; i++) {
                type_append_to_string_with_children(string, members[i].type, false);
                if (i != members.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
            string_append_formated(string, "}");
        }
        break;
    }
    case Type_Type::FUNCTION: {
        auto function_type = downcast<Type_Function>(signature);
        auto& parameters = function_type->parameters;
        string_append_formated(string, "(");
        for (int i = 0; i < function_type->parameters.size; i++) {
            auto& param = function_type->parameters[i];
            if (param.name.available) {
                string_append_formated(string, "%s: ", param.name.value->characters);
            }
            type_append_to_string_with_children(string, parameters[i].type, print_child);
            if (param.has_default_value) {
                string_append_formated(string, " = ...");
            }
            if (i != parameters.size - 1) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, ") -> ");
        if (function_type->return_type.available) {
            type_append_to_string_with_children(string, function_type->return_type.value, print_child);
        }
        else {
            string_append_formated(string, "void");
        }
        break;
    }
    default: panic("Fugg");
    }
}

void type_append_value_to_string(Type_Base* type, byte* value_ptr, String* string)
{
    if (!memory_is_readable(value_ptr, type->size)) {
        string_append_formated(string, "Memory not readable");
    }

    switch (type->type)
    {
    case Type_Type::FUNCTION:
        break;
    case Type_Type::ERROR_TYPE:
        break;
    case Type_Type::TYPE_HANDLE: {
        u64 value = *(u64*)value_ptr;
        string_append_formated(string, "Type_Handle, type_handle: %d", value);
        break;
    }
    case Type_Type::ARRAY:
    case Type_Type::SLICE:
    {
        int element_count = 0;
        Type_Base* element_type = 0;
        byte* array_data = 0;
        if (type->type == Type_Type::ARRAY) {
            auto array_type = downcast<Type_Array>(type);
            element_type = array_type->element_type;
            element_count = array_type->element_count;
            array_data = value_ptr;
        }
        else {
            auto slice_type = downcast<Type_Slice>(type);
            element_type = slice_type->element_type;
            array_data = *((byte**)value_ptr);
            element_count = *((int*)(value_ptr + 8));
        }

        string_append_formated(string, "[#%d ", element_count);
        if (!memory_is_readable(array_data, element_count * element_type->size)) {
            string_append_formated(string, "Memory not readable");
        }
        else {
            for (int i = 0; i < element_count && i < 3; i++) {
                byte* element_ptr = value_ptr + (i * element_type->alignment);
                type_append_value_to_string(element_type, element_ptr, string);
                if (i != element_count - 1) {
                    string_append_formated(string, ", ");
                }
            }
            if (element_count > 3) {
                string_append_formated(string, " ...");
            }
        }
        string_append_formated(string, "]");
        break;
    }
    case Type_Type::VOID_POINTER:
    case Type_Type::POINTER:
    {
        byte* data_ptr = *((byte**)value_ptr);
        if (data_ptr == 0) {
            string_append_formated(string, "nullptr");
            return;
        }
        string_append_formated(string, "Ptr %p", data_ptr);
        break;
    }
    case Type_Type::STRUCT:
    {
        auto struct_type = downcast<Type_Struct>(type);
        auto& members = struct_type->members;
        if (struct_type->name.available) {
            string_append_formated(string, "%s{", struct_type->name.value->characters);
        }
        else {
            string_append_formated(string, "%Struct{", struct_type->name.value->characters);
        }
        switch (struct_type->struct_type)
        {
        case Structure_Type::C_UNION: break;
        case Structure_Type::UNION: {
            int tag = *(i32*)(value_ptr + struct_type->tag_member.offset);
            if (tag > 0 && tag < members.size) {
                Struct_Member* member = &members[tag - 1];
                string_append_formated(string, "%s = ", member->id->characters);
                type_append_value_to_string(member->type, value_ptr + member->offset, string);
            }
            break;
        }
        case Structure_Type::STRUCT: {
            for (int i = 0; i < members.size; i++)
            {
                Struct_Member* mem = &members[i];
                byte* mem_ptr = value_ptr + mem->offset;
                type_append_value_to_string(mem->type, mem_ptr, string);
                if (i != members.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
            break;
        }
        }
        string_append_formated(string, "}");
        break;
    }
    case Type_Type::ENUM:
    {
        auto enum_type = downcast<Type_Enum>(type);
        auto& members = enum_type->members;
        if (enum_type->name != 0) {
            string_append_formated(string, "%s{", enum_type->name->characters);
        }
        else {
            string_append_formated(string, "Enum{");
        }
        int value = *(i32*)value_ptr;
        Enum_Item* found = 0;
        for (int i = 0; i < members.size; i++) {
            Enum_Item* mem = &members[i];
            if (value == mem->value) {
                found = mem;
                break;
            }
        }
        if (found == 0) {
            string_append_formated(string, "INVALID_VALUE");
        }
        else {
            string_append_formated(string, found->name->characters);
        }
        string_append_formated(string, "}");
        break;
    }
    case Type_Type::PRIMITIVE:
    {
        auto primitive = downcast<Type_Primitive>(type);
        switch (primitive->primitive_type)
        {
        case Primitive_Type::BOOLEAN: {
            bool val = *(bool*)value_ptr;
            string_append_formated(string, "%s", val ? "TRUE" : "FALSE");
            break;
        }
        case Primitive_Type::INTEGER: {
            int value = 0;
            if (primitive->is_signed)
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

void type_append_to_string(String* string, Type_Base* signature) {
    type_append_to_string_with_children(string, signature, false);
}



/*
    TYPE_SYSTEM
*/
Type_System type_system_create(Timer* timer)
{
    Type_System result;
    result.types = dynamic_array_create_empty<Type_Base*>(256);
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
void test_type_similarity(Type_Base* signature) {
    assert(signature->size == sizeof(T) && signature->alignment == alignof(T), "");
}

void type_system_reset(Type_System* system)
{
    system->register_time = 0;
    for (int i = 0; i < system->types.size; i++) {
        type_base_destroy(system->types[i]);
        delete system->types[i];
    }
    dynamic_array_reset(&system->types);
    for (int i = 0; i < system->internal_type_infos.size; i++)
    {
        Internal_Type_Information* info = &system->internal_type_infos[i];
        switch (info->options.tag)
        {
        case Type_Type::ENUM: {
            if (info->options.enumeration.members.data_ptr != 0) {
                delete[]info->options.enumeration.members.data_ptr;
                info->options.enumeration.members.data_ptr = 0;
            }
            break;
        }
        case Type_Type::FUNCTION:
            if (info->options.function.parameters.data_ptr != 0) {
                delete[]info->options.function.parameters.data_ptr;
                info->options.function.parameters.data_ptr = 0;
            }
            break;
        case Type_Type::STRUCT: {
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


Type_Base* type_system_deduplicate_and_create_internal_info_for_type(Type_Base* base_type)
{
    auto& type_system = compiler.type_system;
    double reg_start_time = timer_current_time_in_seconds(type_system.timer);

    if (base_type->type != Type_Type::STRUCT || base_type->type == Type_Type::ENUM) // Structs aren't deduplicated
    {
        // Check if type already exists
        for (int i = 0; i < type_system.types.size; i++)
        {
            bool are_equal = false;
            Type_Base* sig1 = base_type;
            Type_Base* sig2 = type_system.types[i];
            if (sig1->type != sig2->type || sig1->size != sig2->size && sig1->alignment != sig2->alignment) {
                continue;
            }

            switch (sig1->type)
            {
            case Type_Type::TYPE_HANDLE:
            case Type_Type::VOID_POINTER:
            case Type_Type::ERROR_TYPE: are_equal = true; break;
            case Type_Type::STRUCT:
            case Type_Type::ENUM: are_equal = false; break;
            case Type_Type::PRIMITIVE: { 
                auto p1 = downcast<Type_Primitive>(sig1);
                auto p2 = downcast<Type_Primitive>(sig2);
                are_equal = p1->primitive_type == p2->primitive_type && p1->is_signed == p2->is_signed;
                break;
            }
            case Type_Type::POINTER: {
                auto p1 = downcast<Type_Pointer>(sig1);
                auto p2 = downcast<Type_Pointer>(sig2);
                are_equal = types_are_equal(p1->points_to_type, p2->points_to_type); break;
                break;
            }
            case Type_Type::ARRAY: {
                auto p1 = downcast<Type_Array>(sig1);
                auto p2 = downcast<Type_Array>(sig2);
                are_equal = types_are_equal(p1->element_type, p2->element_type) && p1->count_known == p2->count_known;
                if (are_equal && p1->count_known) {
                    are_equal = p1->element_count == p2->element_count;
                }
                break;
            }
            case Type_Type::SLICE: {
                auto p1 = downcast<Type_Slice>(sig1);
                auto p2 = downcast<Type_Slice>(sig2);
                are_equal = types_are_equal(p1->element_type, p2->element_type);
                break;
            }
            case Type_Type::FUNCTION:
            {
                auto p1 = downcast<Type_Function>(sig1);
                auto p2 = downcast<Type_Function>(sig2);

                // Polymorphic functions currently aren't deduplicated
                if (p1->is_polymorphic || p2->is_polymorphic) {
                    are_equal = false;
                    break;
                }

                // Check return types
                if (p1->return_type.available != p2->return_type.available) {
                    are_equal = false;
                    break;
                }
                if (p1->return_type.available) {
                    if (!types_are_equal(p1->return_type.value, p2->return_type.value)) {
                        are_equal = false;
                        break;
                    }
                }

                // Check parameters
                if (p1->parameters.size != p2->parameters.size) {
                    are_equal = false;
                    break;
                }
                are_equal = true;
                for (int i = 0; i < p1->parameters.size; i++) 
                {
                    auto& param1 = p1->parameters[i];
                    auto& param2 = p2->parameters[i];
                    if (!types_are_equal(param1.type, param2.type) || param1.name.available != param2.name.available ||
                        param1.is_polymorphic != param2.is_polymorphic || param1.has_default_value != param2.has_default_value) {
                        are_equal = false;
                        break;
                    }
                    if (param1.name.available && param1.name.value != param2.name.value) {
                        are_equal = false;
                        break;
                    }
                    // If default values exist, we don't deduplicate the function, because comparison may be wrong currently
                    if (param1.has_default_value || param2.has_default_value) {
                        are_equal = false;
                        break;
                    }
                }
                break;
            }
            }

            if (are_equal) {
                type_base_destroy(base_type);
                return sig2;
            }
        }
    }

    // Create Interal type-info and internal index
    {
        base_type->type_handle.index = type_system.internal_type_infos.size;
        Internal_Type_Information info;
        memory_set_bytes(&info, sizeof(info), 0);
        info.type_handle.index = base_type->type_handle.index;
        info.size = base_type->size;
        info.alignment = base_type->alignment;
        info.options.tag = base_type->type;
        switch (base_type->type)
        {
        case Type_Type::STRUCT:
        case Type_Type::ENUM:
            // Are both handled in finish type
        case Type_Type::ERROR_TYPE:
        case Type_Type::TYPE_HANDLE:
        case Type_Type::VOID_POINTER:
            break; // There are no options for these types

        case Type_Type::FUNCTION:
        {
            auto function_type = downcast<Type_Function>(base_type);
            auto& parameters = function_type->parameters;
            info.options.function.parameters.data_ptr = new Type_Handle[parameters.size];
            info.options.function.parameters.size = parameters.size;
            for (int i = 0; i < parameters.size; i++) {
                Type_Base* param = parameters[i].type;
                Type_Handle* info_param = &info.options.function.parameters.data_ptr[i];
                *info_param = param->type_handle;
            }
            info.options.function.has_return_type = function_type->return_type.available;
            if (function_type->return_type.available) {
                info.options.function.return_type = function_type->return_type.value->type_handle;
            }
            else {
                info.options.function.return_type.index = -1;
            }
            break;
        }
        case Type_Type::POINTER: {
            info.options.pointer = downcast<Type_Pointer>(base_type)->points_to_type->type_handle;
            break;
        }
        case Type_Type::PRIMITIVE:
        {
            auto primitive = downcast<Type_Primitive>(base_type);
            info.options.primitive.tag = primitive->primitive_type;
            info.options.primitive.is_signed = primitive->is_signed;
            break;
        }
        case Type_Type::ARRAY: {
            auto array = downcast<Type_Array>(base_type);
            info.options.array.element_type = array->element_type->type_handle;
            if (array->count_known) {
                info.options.array.size = array->element_count;
            }
            else {
                info.options.array.size = 1;
            }
            break;
        }
        case Type_Type::SLICE: {
            auto slice = downcast<Type_Slice>(base_type);
            info.options.slice.element_type = slice->element_type->type_handle;
            break;
        }
        default: panic("");
        }
        dynamic_array_push_back(&type_system.internal_type_infos, info);
    }

    double reg_end_time = timer_current_time_in_seconds(type_system.timer);
    type_system.register_time += reg_end_time - reg_start_time;

    return 0;
}

template<typename T>
T* type_system_register_type(T value) 
{
    Type_Base* deduplicated = type_system_deduplicate_and_create_internal_info_for_type(upcast(&value));
    if (deduplicated != 0) {
        return downcast<T>(deduplicated);
    }
    
    T* new_type = new T;
    *new_type = value;
    dynamic_array_push_back(&compiler.type_system.types, upcast(new_type));
    return new_type;
}

Type_Primitive* type_system_make_primitive(Primitive_Type type, int size, bool is_signed)
{
    Type_Primitive result;
    result.base.type = Type_Type::PRIMITIVE;
    result.base.size = size;
    result.base.alignment = size;
    result.base.type_handle.index = -1;
    result.is_signed = is_signed;
    result.primitive_type = type;
    return type_system_register_type(result);
}

Type_Pointer* type_system_make_pointer(Type_Base* child_type)
{
    Type_Pointer result;
    result.base.type = Type_Type::POINTER;
    result.base.size = 8;
    result.base.alignment = 8;
    result.points_to_type = child_type;
    return type_system_register_type(result);
}

Type_Array* type_system_make_array(Type_Base* element_type, bool count_known, int element_count)
{
    assert(!(count_known && element_count < 0), "Hey");
    Type_Array result;
    result.base.type = Type_Type::ARRAY;
    result.base.alignment = element_type->alignment;
    result.element_type = element_type;
    result.count_known = count_known;
    if (count_known) {
        result.element_count = element_count;
    }
    else {
        result.element_count = 1;
    }
    result.base.size = element_type->size * result.element_count;
    return type_system_register_type(result);
}

Type_Slice* type_system_make_slice(Type_Base* element_type)
{
    Type_Slice result;
    result.base.type = Type_Type::SLICE;
    result.base.alignment = 8;
    result.base.size = 16;
    result.element_type = element_type;
    result.data_member.id = compiler.id_data;
    result.data_member.type = upcast(type_system_make_pointer(element_type));
    result.data_member.offset = 0;
    result.size_member.id = compiler.id_size;
    result.size_member.type = upcast(compiler.type_system.predefined_types.i32_type);
    result.size_member.offset = 8;
    return type_system_register_type(result);
}

Type_Function type_system_make_function_empty()
{
    Type_Function result;
    result.base.type = Type_Type::FUNCTION;
    result.base.type_handle.index = -1;
    result.base.alignment = 8;
    result.base.size = 8;
    result.parameters = dynamic_array_create_empty<Function_Parameter>(1);
    result.return_type.available = false;
    return result;
}

void type_system_add_parameter_to_empty_function(Type_Function& function, Type_Base* param_type, Optional<String*> param_name) {
    Function_Parameter parameter;
    parameter.type = param_type;
    parameter.has_default_value = false;
    parameter.default_value.available = false;
    parameter.is_polymorphic = false;
    parameter.name = param_name;
    dynamic_array_push_back(&function.parameters, parameter);
}

Type_Function* type_system_finish_function(Type_Function function, Type_Base* return_type) {
    function.is_polymorphic = false; // Change at soem point!
    if (return_type == 0) {
        function.return_type = optional_make_failure<Type_Base*>();
    }
    else {
        function.return_type = optional_make_success(return_type);
    }
    return type_system_register_type(function);
}

Type_Function* type_system_make_function(Dynamic_Array<Function_Parameter> parameters, Type_Base* return_type = 0)
{
    Type_Function result;
    result.base.type = Type_Type::FUNCTION;
    result.base.alignment = 8;
    result.base.size = 8;
    result.parameters = parameters;
    if (return_type == 0) {
        result.return_type = optional_make_failure<Type_Base*>();
    }
    else {
        result.return_type = optional_make_success(return_type);
    }
    return type_system_register_type(result);
}

Type_Function* type_system_make_function(std::initializer_list<Function_Parameter> parameter_types, Type_Base* return_type)
{
    Dynamic_Array<Function_Parameter> params = dynamic_array_create_empty<Function_Parameter>(1);
    for (auto& param : parameter_types) {
        dynamic_array_push_back(&params, param);
    }
    return type_system_make_function(params, return_type);
}

Type_Struct* type_system_make_struct_empty(AST::Structure_Type struct_type, String* name, Struct_Progress* progress)
{
    Type_Struct result;
    result.base.type = Type_Type::STRUCT;
    result.base.size = 0;
    result.base.alignment = 0;
    if (name != 0) {
        result.name = optional_make_success(name);
    }
    else {
        result.name.available = false;
    }
    result.progress = progress;
    result.tag_member.id = 0;
    result.tag_member.offset = 0;
    result.tag_member.type = 0;
    result.struct_type = struct_type;
    result.members = dynamic_array_create_empty<Struct_Member>(2);
    return type_system_register_type(result);
}

void struct_add_member(Type_Struct* structure, String* id, Type_Base* member_type)
{
    Struct_Member member;
    member.id = id;
    member.offset = 0;
    member.type = member_type;
    dynamic_array_push_back(&structure->members, member);
}

Type_Enum* type_system_make_enum_empty(String* name)
{
    Type_Enum result;
    result.base.type = Type_Type::ENUM;
    result.base.size = 0;
    result.base.alignment = 0;
    result.name = name;
    result.members = dynamic_array_create_empty<Enum_Item>(3);
    return type_system_register_type(result);
}

void type_system_finish_struct(Type_Struct* structure)
{
    auto& type_system = compiler.type_system;
    auto& members = structure->members;
    auto& base = structure->base;
    assert(base.type_handle.index < (u32)type_system.internal_type_infos.size, "");
    assert(type_system.internal_type_infos.size == type_system.types.size, "");
    assert(base.size == 0 && base.alignment == 0, "");

    // Calculate member offset/alignment + size
    int offset = 0;
    int alignment = 1;
    for (int i = 0; i < members.size; i++)
    {
        Struct_Member* member = &members[i];
        assert(!(member->type->size == 0 && member->type->alignment == 0), "");
        if (structure->struct_type != Structure_Type::STRUCT) {
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
    if (structure->struct_type == Structure_Type::UNION)
    {
        Type_Enum* tag_type = type_system_make_enum_empty(nullptr);
        for (int i = 0; i < members.size; i++) {
            Struct_Member* struct_member = &members[i];
            Enum_Item tag_member;
            tag_member.name = struct_member->id;
            tag_member.value = i + 1; // Note: Enum member values start at 1 by default
            dynamic_array_push_back(&tag_type->members, tag_member);
        }
        type_system_finish_enum(tag_type);

        auto& types = type_system.predefined_types;

        Struct_Member union_tag;
        union_tag.id = compiler.id_tag;
        offset = math_round_next_multiple(offset, types.i32_type->base.alignment);
        union_tag.offset = offset;
        union_tag.type = upcast(tag_type);
        dynamic_array_push_back(&members, union_tag);
        structure->tag_member = members[members.size - 1];

        offset += types.i32_type->base.size;
        alignment = math_maximum(alignment, types.i32_type->base.alignment);
    }

    // Finish type
    structure->base.size = math_maximum(1, math_round_next_multiple(offset, alignment));
    structure->base.alignment = alignment;



    // Update internal info to mirror struct info
    Internal_Type_Information* internal_info = &type_system.internal_type_infos[base.type_handle.index];
    internal_info->size = base.size;
    internal_info->alignment = base.alignment;
    internal_info->type_handle = base.type_handle;
    internal_info->options.tag = base.type;

    if (!structure->name.available) {
        internal_info->options.structure.name.character_buffer.size = 0;
        internal_info->options.structure.name.character_buffer.data = 0;
        internal_info->options.structure.name.size = 0;
    }
    else {
        internal_info->options.structure.name.character_buffer.size = structure->name.value->capacity;
        internal_info->options.structure.name.character_buffer.data = structure->name.value->characters;
        internal_info->options.structure.name.size = structure->name.value->size;
    }
    int member_count = members.size;
    internal_info->options.structure.members.size = member_count;
    internal_info->options.structure.members.data_ptr = new Internal_Type_Struct_Member[member_count];
    for (int i = 0; i < member_count; i++)
    {
        Internal_Type_Struct_Member* internal_member = &internal_info->options.structure.members.data_ptr[i];
        Struct_Member* member = &members[i];
        internal_member->name.size = member->id->size;
        internal_member->name.character_buffer.size = member->id->capacity;
        internal_member->name.character_buffer.data = member->id->characters;
        internal_member->offset = member->offset;
        internal_member->type = member->type->type_handle;
    }
    internal_info->options.structure.type.tag = structure->struct_type;
    if ((int)internal_info->options.structure.type.tag == 0) {
        logg("WHAT");
    }
    if (structure->struct_type == Structure_Type::UNION) {
        internal_info->options.structure.type.tag_member_index = members.size - 1;
    }
}

void type_system_finish_enum(Type_Enum* enum_type)
{
    auto& type_system = compiler.type_system;
    auto& base = enum_type->base;
    auto& members = enum_type->members;
    assert(base.type_handle.index < (u32) type_system.internal_type_infos.size, "");
    assert(type_system.internal_type_infos.size == type_system.types.size, "");
    assert(base.size == 0 && base.alignment == 0, "");

    // Finish enum
    base.size = 4;
    base.alignment = 4;

    // Update internal info to mirror struct info
    Internal_Type_Information* internal_info = &type_system.internal_type_infos[base.type_handle.index];
    internal_info->size = base.size;
    internal_info->alignment = base.alignment;
    internal_info->type_handle = base.type_handle;
    internal_info->options.tag = base.type;

    // Make mirroring internal info
    if (enum_type->name == 0) {
        internal_info->options.enumeration.name.character_buffer.size = 0;
        internal_info->options.enumeration.name.character_buffer.data = 0;
        internal_info->options.enumeration.name.size = 0;
    }
    else {
        internal_info->options.enumeration.name.character_buffer.size = enum_type->name->capacity;
        internal_info->options.enumeration.name.character_buffer.data = enum_type->name->characters;
        internal_info->options.enumeration.name.size =                  enum_type->name->size;
    }
    int member_count = members.size;
    internal_info->options.enumeration.members.size = member_count;
    internal_info->options.enumeration.members.data_ptr = new Internal_Type_Enum_Member[member_count];
    for (int i = 0; i < member_count; i++)
    {
        Enum_Item* member = &members[i];
        Internal_Type_Enum_Member* internal_member = &internal_info->options.enumeration.members.data_ptr[i];
        internal_member->name.size = member->name->size;
        internal_member->name.character_buffer.size = 0;
        internal_member->name.character_buffer.data = member->name->characters;
        internal_member->value = member->value;
    }
}

void type_system_finish_array(Type_Array* array)
{
    auto& type_system = compiler.type_system;
    auto& base = array->base;
    assert(base.type_handle.index < (u32) type_system.internal_type_infos.size, "");
    assert(type_system.internal_type_infos.size == type_system.types.size, "");
    assert(base.size == 0 && base.alignment == 0, "");
    assert(array->base.type == Type_Type::ARRAY, "");

    // Finish array
    base.size = array->element_type->size * array->element_count;
    base.alignment = array->element_type->alignment;

    // Update internal info to mirror struct info
    Internal_Type_Information* internal_info = &type_system.internal_type_infos[base.type_handle.index];
    internal_info->size = base.size;
    internal_info->alignment = base.alignment;
    internal_info->type_handle = base.type_handle;
    internal_info->options.tag = base.type;
}



void type_system_add_predefined_types(Type_System* system)
{
    Predefined_Types* types = &system->predefined_types;

    types->bool_type = type_system_make_primitive(Primitive_Type::BOOLEAN, 1, false);
    types->i8_type = type_system_make_primitive(Primitive_Type::INTEGER, 1, true);
    types->i16_type = type_system_make_primitive(Primitive_Type::INTEGER, 2, true);
    types->i32_type = type_system_make_primitive(Primitive_Type::INTEGER, 4, true);
    types->i64_type = type_system_make_primitive(Primitive_Type::INTEGER, 8, true);
    types->u8_type = type_system_make_primitive(Primitive_Type::INTEGER, 1, false);
    types->u16_type = type_system_make_primitive(Primitive_Type::INTEGER, 2, false);
    types->u32_type = type_system_make_primitive(Primitive_Type::INTEGER, 4, false);
    types->u64_type = type_system_make_primitive(Primitive_Type::INTEGER, 8, false);
    types->f32_type = type_system_make_primitive(Primitive_Type::FLOAT, 4, true);
    types->f64_type = type_system_make_primitive(Primitive_Type::FLOAT, 8, true);
    {
        Type_Base error_type;
        error_type.size = 1;
        error_type.alignment = 1;
        error_type.type = Type_Type::ERROR_TYPE;
        types->error_type = type_system_register_type(error_type);

        Type_Base void_pointer;
        void_pointer.type = Type_Type::VOID_POINTER;
        void_pointer.size = 8;
        void_pointer.alignment = 8;
        types->void_pointer_type = type_system_register_type(void_pointer);

        Type_Base type_handle;
        type_handle.type = Type_Type::TYPE_HANDLE;
        type_handle.size = 4;
        type_handle.alignment = 4;
        types->type_handle = type_system_register_type(type_handle);
    }

    using AST::Structure_Type;
    // Empty structure
    auto make_id = [&](const char* name) -> String* { return identifier_pool_add(&compiler.identifier_pool, string_create_static(name)); };
    {
        types->empty_struct_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Empty_Struct"), 0);
        type_system_finish_struct(types->empty_struct_type);
    }

    auto add_member_cstr = [&](Type_Struct* struct_type, const char* member_name, Type_Base* member_type) {
        String* id = identifier_pool_add(&compiler.identifier_pool, string_create_static(member_name));
        struct_add_member(struct_type, id, member_type);
    };

    // String
    {
        types->string_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("String"), 0);
        add_member_cstr(types->string_type, "character_buffer", upcast(type_system_make_slice(upcast(types->u8_type))));
        add_member_cstr(types->string_type, "size", upcast(types->i32_type));
        type_system_finish_struct(types->string_type);
        test_type_similarity<Upp_String>(upcast(types->string_type));
    }

    // Any
    {
        types->any_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Any"), 0);
        add_member_cstr(types->any_type, "data", types->void_pointer_type);
        add_member_cstr(types->any_type, "type", types->type_handle);
        type_system_finish_struct(types->any_type);
        test_type_similarity<Upp_Any>(upcast(types->any_type));
    }

    // Type Information
    {
        Type_Struct* option_type = type_system_make_struct_empty(Structure_Type::UNION, make_id("Type_Info_Option"), 0);
        add_member_cstr(option_type, "void_pointer", upcast(types->empty_struct_type));
        add_member_cstr(option_type, "type_handle", upcast(types->empty_struct_type));
        add_member_cstr(option_type, "error_type", upcast(types->empty_struct_type));
        // Primitive
        {
            Type_Struct* primitive_type = type_system_make_struct_empty(Structure_Type::UNION, make_id("Primitive_Info"), 0);
            {
                Type_Struct* integer_info_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Integer_Info"), 0);
                add_member_cstr(integer_info_type, "is_signed", upcast(types->bool_type));
                type_system_finish_struct(integer_info_type);
                add_member_cstr(primitive_type, "integer", upcast(integer_info_type));
            }
            add_member_cstr(primitive_type, "floating_point", upcast(types->empty_struct_type));
            add_member_cstr(primitive_type, "boolean", upcast(types->empty_struct_type));
            type_system_finish_struct(primitive_type);
            test_type_similarity<Internal_Type_Primitive>(upcast(primitive_type));
            add_member_cstr(option_type, "primitive", upcast(primitive_type));
        }
        // Pointer
        add_member_cstr(option_type, "pointer", types->type_handle);
        // Function
        {
            Type_Struct* function_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Function_Info"), 0);
            add_member_cstr(function_type, "parameter_types", upcast(type_system_make_slice(types->type_handle)));
            add_member_cstr(function_type, "return_type", types->type_handle);
            add_member_cstr(function_type, "has_return_type", upcast(types->bool_type));
            type_system_finish_struct(function_type);
            test_type_similarity<Internal_Type_Function>(upcast(function_type));
            add_member_cstr(option_type, "function", upcast(function_type));
        }
        // Struct
        {
            Type_Struct* struct_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Struct_Info"), 0);
            {
                Type_Struct* struct_member_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Member_Info"), 0);
                add_member_cstr(struct_member_type, "name", upcast(types->string_type));
                add_member_cstr(struct_member_type, "type", types->type_handle);
                add_member_cstr(struct_member_type, "offset", upcast(types->i32_type));
                type_system_finish_struct(struct_member_type);
                add_member_cstr(struct_type, "members", upcast(type_system_make_slice(upcast(struct_member_type))));
                test_type_similarity<Internal_Type_Struct_Member>(upcast(struct_member_type));
            }
            add_member_cstr(struct_type, "name", upcast(types->string_type));
            {
                Type_Struct* structure_type_type = type_system_make_struct_empty(Structure_Type::UNION, make_id("Struct_Type_Info"), 0);
                add_member_cstr(structure_type_type, "structure", upcast(types->empty_struct_type));
                add_member_cstr(structure_type_type, "union_untagged", upcast(types->empty_struct_type));
                {
                    Type_Struct* union_tagged_struct = type_system_make_struct_empty(Structure_Type::STRUCT, 0, 0);
                    add_member_cstr(union_tagged_struct, "tag_member_index", upcast(types->i32_type));
                    type_system_finish_struct(union_tagged_struct);
                    add_member_cstr(structure_type_type, "union_tagged", upcast(union_tagged_struct));
                }
                type_system_finish_struct(structure_type_type);
                add_member_cstr(struct_type, "type", upcast(structure_type_type));
                test_type_similarity<Internal_Structure_Type>(upcast(structure_type_type));
            }
            type_system_finish_struct(struct_type);
            add_member_cstr(option_type, "structure", upcast(struct_type));
            test_type_similarity<Internal_Type_Struct>(upcast(struct_type));
        }
        // ENUM
        {
            Type_Struct* enum_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Enum_Info"), 0);
            {
                Type_Struct* enum_member_type = type_system_make_struct_empty(Structure_Type::STRUCT, 0, 0);
                add_member_cstr(enum_member_type, "name", upcast(types->string_type));
                add_member_cstr(enum_member_type, "value", upcast(types->i32_type));
                type_system_finish_struct(enum_member_type);
                add_member_cstr(enum_type, "members", upcast(type_system_make_slice(upcast(enum_member_type))));
                test_type_similarity<Internal_Type_Enum_Member>(upcast(enum_member_type));
            }
            add_member_cstr(enum_type, "name", upcast(types->string_type));
            type_system_finish_struct(enum_type);
            add_member_cstr(option_type, "enumeration", upcast(enum_type));
            test_type_similarity<Internal_Type_Enum>(upcast(enum_type));
        }
        // Array
        {
            Type_Struct* array_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Array_Info"), 0);
            add_member_cstr(array_type, "element_type", types->type_handle);
            add_member_cstr(array_type, "size", upcast(types->i32_type));
            type_system_finish_struct(array_type);
            test_type_similarity<Internal_Type_Array>(upcast(array_type));
            add_member_cstr(option_type, "array", upcast(array_type));
        }
        // Slice
        {
            Type_Struct* slice_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Slice_Info"), 0);
            add_member_cstr(slice_type, "element_type", types->type_handle);
            type_system_finish_struct(slice_type);
            test_type_similarity<Internal_Type_Slice>(upcast(slice_type));
            add_member_cstr(option_type, "slice", upcast(slice_type));
        }
        type_system_finish_struct(option_type);
        test_type_similarity<Internal_Type_Info_Options>(upcast(option_type));

        // Type Information
        {
            Type_Struct* type_info_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Type_Info"), 0);
            add_member_cstr(type_info_type, "type", types->type_handle);
            add_member_cstr(type_info_type, "size", upcast(types->i32_type));
            add_member_cstr(type_info_type, "alignment", upcast(types->i32_type));
            add_member_cstr(type_info_type, "options", upcast(option_type));
            type_system_finish_struct(type_info_type);
            types->type_information_type = type_info_type;
            test_type_similarity<Internal_Type_Information>(upcast(type_info_type));
        }
    }

    // Hardcoded Functions
    {
        auto& ts = *system;
        auto make_param = [&](Type_Base* signature, const char* name) -> Function_Parameter {
            Function_Parameter param;
            param.type = signature;
            param.name = optional_make_success(identifier_pool_add(&compiler.identifier_pool, string_create_static(name)));
            param.has_default_value = false;
            param.default_value.available = false;
            param.is_polymorphic = false;
            return param;
        };
        types->type_assert =       type_system_make_function({ make_param(upcast(types->bool_type), "condition") });
        types->type_free =         type_system_make_function({ make_param(types->void_pointer_type, "pointer") });
        types->type_malloc =       type_system_make_function({ make_param(upcast(types->i32_type), "size") }, upcast(types->void_pointer_type));
        types->type_type_info =    type_system_make_function({ make_param(upcast(types->type_handle), "type_handle") }, upcast(types->type_information_type));
        types->type_type_of =      type_system_make_function({ make_param(upcast(types->empty_struct_type), "value") }, upcast(types->type_handle));
        types->type_print_bool =   type_system_make_function({ make_param(upcast(types->bool_type), "value") });
        types->type_print_i32 =    type_system_make_function({ make_param(upcast(types->i32_type), "value") });
        types->type_print_f32 =    type_system_make_function({ make_param(upcast(types->f32_type), "value") });
        types->type_print_line =   type_system_make_function({});
        types->type_print_string = type_system_make_function({ make_param(upcast(types->string_type), "value") });
        types->type_read_i32 =     type_system_make_function({});
        types->type_read_f32 =     type_system_make_function({});
        types->type_read_bool =    type_system_make_function({});
        types->type_random_i32 =   type_system_make_function({}, upcast(types->i32_type));
    }
}



void type_system_print(Type_System * system)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Type_System: ");
    for (int i = 0; i < system->types.size; i++)
    {
        Type_Base* type = system->types[i];
        string_append_formated(&msg, "\n\t%d: ", i);
        type_append_to_string(&msg, type);
        string_append_formated(&msg, " size: %d, alignment: %d", type->size, type->alignment);
    }
    string_append_formated(&msg, "\n");
    logg("%s", msg.characters);
}

Optional<Enum_Item> enum_type_find_member_by_value(Type_Enum* enum_type, int value)
{
    auto& members = enum_type->members;
    for (int i = 0; i < members.size; i++) {
        Enum_Item member = members[i];
        if (member.value == value) return optional_make_success(member);
    }
    return optional_make_failure<Enum_Item>();
}

Optional<Struct_Member> type_signature_find_member_by_id(Type_Base* type, String* id)
{
    switch (type->type)
    {
    case Type_Type::STRUCT:
    {
        auto structure = downcast<Type_Struct>(type);
        auto& members = structure->members;
        for (int i = 0; i < members.size; i++) {
            auto& member = members[i];
            if (member.id == id) return optional_make_success(member);
        }
        if (structure->struct_type == AST::Structure_Type::UNION && id == compiler.id_tag) {
            return optional_make_success(structure->tag_member);
        }
        break;
    }
    case Type_Type::SLICE:
    {
        auto slice = downcast<Type_Slice>(type);
        if (id == compiler.id_data) {
            return optional_make_success(slice->data_member);
        }
        else if (id == compiler.id_size) {
            return optional_make_success(slice->size_member);
        }
        break;
    }
    }
    return optional_make_failure<Struct_Member>();
}

bool types_are_equal(Type_Base* a, Type_Base* b) {
    return a == b;
}

