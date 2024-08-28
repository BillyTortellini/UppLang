#include "type_system.hpp"
#include "compiler.hpp"
#include "symbol_table.hpp"
#include "semantic_analyser.hpp"

using AST::Structure_Type;

Function_Parameter function_parameter_make_empty() {
    Function_Parameter result;
    result.name = compiler.predefined_ids.empty_string;
    result.type = compiler.type_system.predefined_types.unknown_type;
    result.default_value_exists = false;
    result.default_value_opt.available = false;
    return result;
}

void type_base_destroy(Datatype* base) 
{
    // Constants types are duplicates of the non-constant versions, and only keep references to arrays/data
    if (base->type == Datatype_Type::FUNCTION) {
        auto fn = downcast<Datatype_Function>(base);
        auto& params = fn->parameters;
        if (params.data != 0 && params.capacity != 0) {
            dynamic_array_destroy(&params);
        }
    }
    else if (base->type == Datatype_Type::STRUCT) {
        auto st = downcast<Datatype_Struct>(base);
        dynamic_array_destroy(&st->members);
        dynamic_array_destroy(&st->types_waiting_for_size_finish);
        dynamic_array_destroy(&st->subtypes);
    }
    else if (base->type == Datatype_Type::ENUM) {
        auto enum_type = downcast<Datatype_Enum>(base);
        dynamic_array_destroy(&enum_type->members);
    }
    else if (base->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE) {
        auto instance = downcast<Datatype_Struct_Instance_Template>(base);
        array_destroy(&instance->instance_values);
    }
}

void type_append_to_string_with_children(String* string, Datatype* signature, bool print_child)
{
    switch (signature->type)
    {
    case Datatype_Type::TEMPLATE_PARAMETER: {
        Datatype_Template_Parameter* polymorphic = downcast<Datatype_Template_Parameter>(signature);
        assert(polymorphic->symbol != 0, "");
        if (!polymorphic->is_reference) {
            string_append_formated(string, "$");
        }
        string_append_formated(string, "%s", polymorphic->symbol->id->characters);
        break;
    }
    case Datatype_Type::CONSTANT: {
        Datatype_Constant* constant = downcast<Datatype_Constant>(signature);
        string_append_formated(string, "const ");
        type_append_to_string_with_children(string, constant->element_type, print_child);
        break;
    }
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE: {
        string_append_formated(string, "Struct instance template");
        break;
    }
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE_SUBTYPE: {
        string_append_formated(string, "Struct instance template subtype (%s)", downcast<Datatype_Struct_Instance_Template_Subtype>(signature)->subtype_name->characters);
        break;
    }
    case Datatype_Type::ARRAY: {
        auto array_type = downcast<Datatype_Array>(signature);
        if (array_type->count_known) {
            string_append_formated(string, "[%d]", array_type->element_count);
        }
        else {
            string_append_formated(string, "[Unknown]");
        }
        type_append_to_string_with_children(string, array_type->element_type, print_child);
        break;
    }
    case Datatype_Type::SLICE: {
        auto slice_type = downcast<Datatype_Slice>(signature);
        string_append_formated(string, "[]");
        type_append_to_string_with_children(string, slice_type->element_type, print_child);
        break;
    }
    case Datatype_Type::UNKNOWN_TYPE:
        string_append_formated(string, "Unknown-Type");
        break;
    case Datatype_Type::POINTER: {
        auto pointer_type = downcast<Datatype_Pointer>(signature);
        string_append_formated(string, "*");
        type_append_to_string_with_children(string, pointer_type->element_type, print_child);
        break;
    }
    case Datatype_Type::BYTE_POINTER: {
        string_append_formated(string, "Byte_Pointer");
        break;
    }
    case Datatype_Type::TYPE_HANDLE: {
        string_append_formated(string, "Type_Handle");
        break;
    }
    case Datatype_Type::PRIMITIVE: {
        auto primitive = downcast<Datatype_Primitive>(signature);
        auto memory = primitive->base.memory_info.value;
        switch (primitive->primitive_type)
        {
        case Primitive_Type::BOOLEAN: string_append_formated(string, "bool"); break;
        case Primitive_Type::INTEGER: string_append_formated(string, "%s%d", primitive->is_signed ? "int" : "uint", memory.size * 8); break;
        case Primitive_Type::FLOAT: string_append_formated(string, "float%d", memory.size * 8); break;
        default: panic("Heyo");
        }
        break;
    }
    case Datatype_Type::ENUM:
    {
        auto enum_type = downcast<Datatype_Enum>(signature);
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
    case Datatype_Type::STRUCT_SUBTYPE: {
        auto subtype = downcast<Datatype_Struct_Subtype>(signature);
        string_append_formated(string, "Subtype \"%s\" ", subtype->subtype_name->characters);
        if (print_child) {
            type_append_to_string_with_children(string, upcast(subtype->structure), print_child);
        }
        break;
    }
    case Datatype_Type::STRUCT:
    {
        auto struct_type = downcast<Datatype_Struct>(signature);
        auto& members = struct_type->members;
        if (struct_type->name.available) {
            string_append_formated(string, struct_type->name.value->characters);
        }
        else {
            string_append_formated(string, "Struct");
        }

        // Append polymorphic instance values
        if (struct_type->workload != 0) {
            if (struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                string_append_formated(string, "(");
                SCOPE_EXIT(string_append_formated(string, ")"));
                auto& instance = struct_type->workload->polymorphic.instance;
                auto instance_values = instance.parent->info.instances[instance.instance_index].instance_parameter_values;
                for (int i = 0; i < instance_values.size; i++) {
                    auto& poly_value = instance_values[i];
                    assert(!poly_value.only_datatype_known, "True for instances");
                    auto& constant = poly_value.options.value;
                    datatype_append_value_to_string(constant.type, constant.memory, string);
                    if (i != instance_values.size - 1) {
                        string_append_formated(string, ", ");
                    }
                }
            }
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
    case Datatype_Type::FUNCTION: {
        auto function_type = downcast<Datatype_Function>(signature);
        auto& parameters = function_type->parameters;
        string_append_formated(string, "(");
        for (int i = 0; i < function_type->parameters.size; i++) {
            auto& param = function_type->parameters[i];
            string_append_formated(string, "%s: ", param.name->characters);
            type_append_to_string_with_children(string, parameters[i].type, print_child);
            if (param.default_value_exists) {
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

void datatype_append_value_to_string(Datatype* type, byte* value_ptr, String* string)
{
    if (!type->memory_info.available) {
        string_append_formated(string, "Type size not finished yet");
    }
    auto memory = type->memory_info.value;
    if (!memory_is_readable(value_ptr, memory.size)) {
        string_append_formated(string, "Memory not readable");
    }

    switch (type->type)
    {
    case Datatype_Type::FUNCTION:
        break;
    case Datatype_Type::UNKNOWN_TYPE:
        break;
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
        break;
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE_SUBTYPE:
        break;
    case Datatype_Type::CONSTANT: {
        auto constant = downcast<Datatype_Constant>(type);
        datatype_append_value_to_string(constant->element_type, value_ptr, string);
        break;
    }
    case Datatype_Type::ARRAY:
    case Datatype_Type::SLICE:
    {
        int element_count = 0;
        Datatype* element_type = 0;
        byte* array_data = 0;
        bool can_print = true;
        if (type->type == Datatype_Type::ARRAY) {
            auto array_type = downcast<Datatype_Array>(type);
            element_type = array_type->element_type;
            if (!array_type->count_known) {
                string_append_formated(string, "[Size_not_available]");
                return;
            }
            element_count = array_type->element_count;
            array_data = value_ptr;
        }
        else {
            auto slice_type = downcast<Datatype_Slice>(type);
            element_type = slice_type->element_type;
            array_data = *((byte**)value_ptr);
            element_count = *((int*)(value_ptr + 8));
        }

        if (!element_type->memory_info.available) {
            string_append_formated(string, "Element size not ready");
            return;
        }
        auto element_size = element_type->memory_info.value.size;
        if (!memory_is_readable(array_data, element_count * element_size)) {
            string_append_formated(string, "Memory not readable");
            return;
        }

        string_append_formated(string, "[#%d ", element_count);
        for (int i = 0; i < element_count && i < 3; i++) {
            byte* element_ptr = value_ptr + (i * element_type->memory_info.value.alignment);
            datatype_append_value_to_string(element_type, element_ptr, string);
            if (i != element_count - 1) {
                string_append_formated(string, ", ");
            }
        }
        if (element_count > 3) {
            string_append_formated(string, " ...");
        }
        string_append_formated(string, "]");
        break;
    }
    case Datatype_Type::TEMPLATE_PARAMETER: {
        string_append_formated(string, "Polymorphic?");
        break;
    }
    case Datatype_Type::POINTER:
    {
        byte* data_ptr = *((byte**)value_ptr);
        if (data_ptr == 0) {
            string_append_formated(string, "nullptr");
            return;
        }
        string_append_formated(string, "Ptr %p", data_ptr);
        break;
    }
    case Datatype_Type::BYTE_POINTER:
    {
        string_append_formated(string, "byte_pointer");
        byte* data_ptr = *((byte**)value_ptr);
        if (data_ptr == 0) {
            return;
        }
        string_append_formated(string, "Ptr %p", data_ptr);
        break;
    }
    case Datatype_Type::TYPE_HANDLE: 
    {
        Upp_Type_Handle handle = *((Upp_Type_Handle*)value_ptr);
        string_append_formated(string, "Type_Handle (#%d)", handle.index);
        break;
    }
    case Datatype_Type::STRUCT_SUBTYPE: 
    {
        auto subtype = downcast<Datatype_Struct_Subtype>(type);
        datatype_append_value_to_string(upcast(subtype->structure), value_ptr, string);
        break;
    }
    case Datatype_Type::STRUCT:
    {
        auto struct_type = downcast<Datatype_Struct>(type);
        auto& members = struct_type->members;
        if (struct_type->name.available) {
            string_append_formated(string, "%s{", struct_type->name.value->characters);
        }
        else {
            string_append_formated(string, "Struct{");
        }

        if (struct_type->struct_type == Structure_Type::UNION) {
            break;
        }

        for (int i = 0; i < members.size; i++)
        {
            Struct_Member* mem = &members[i];
            byte* mem_ptr = value_ptr + mem->offset;
            datatype_append_value_to_string(mem->type, mem_ptr, string);
            if (i != members.size - 1) {
                string_append_formated(string, ", ");
            }
        }

        if (struct_type->subtypes.size > 0)
        {
            int tag = *(i32*)(value_ptr + struct_type->tag_member.offset);
            if (tag > 0 && tag < members.size) {
                Subtype_Info* subtype = &struct_type->subtypes[tag - 1];
                string_append_formated(string, "%s = ", subtype->name->characters);
                datatype_append_value_to_string(upcast(subtype->type), value_ptr + subtype->offset, string);
            }
        }

        string_append_formated(string, "}");
        break;
    }
    case Datatype_Type::ENUM:
    {
        auto enum_type = downcast<Datatype_Enum>(type);
        auto& members = enum_type->members;
        if (enum_type->name != 0) {
            string_append_formated(string, "%s{", enum_type->name->characters);
        }
        else {
            string_append_formated(string, "Enum{");
        }
        int value = *(i32*)value_ptr;
        Enum_Member* found = 0;
        for (int i = 0; i < members.size; i++) {
            Enum_Member* mem = &members[i];
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
    case Datatype_Type::PRIMITIVE:
    {
        auto primitive = downcast<Datatype_Primitive>(type);
        int size = primitive->base.memory_info.value.size;
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
                switch (size)
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
                switch (size)
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
            if (size == 4) {
                string_append_formated(string, "%3.2f", *(float*)value_ptr);
            }
            else if (size == 8) {
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

void datatype_append_to_string(String* string, Datatype* signature) {
    type_append_to_string_with_children(string, signature, false);
}



/*
    TYPE_SYSTEM
*/

// Note: This tests structural equality, wheres the current types-are-equal test for the same pointer...
bool type_deduplication_is_equal(Type_Deduplication* a_ptr, Type_Deduplication* b_ptr)
{
    Type_Deduplication& a = *a_ptr;
    Type_Deduplication& b = *b_ptr;
    if (a.type != b.type) {
        return false;
    }

    switch (a.type)
    {
    case Type_Deduplication_Type::ARRAY: {
        return a.options.array_type.element_type == b.options.array_type.element_type &&
            a.options.array_type.element_count == b.options.array_type.element_count &&
            a.options.array_type.polymorphic_count_variable == b.options.array_type.polymorphic_count_variable &&
            a.options.array_type.size_known == b.options.array_type.size_known;
    }
    case Type_Deduplication_Type::CONSTANT: {
        return a.options.non_constant_type == b.options.non_constant_type;
    }
    case Type_Deduplication_Type::SUBTYPE: {
        return a.options.subtype.base_type == b.options.subtype.base_type && a.options.subtype.name == b.options.subtype.name;
    }
    case Type_Deduplication_Type::SLICE: {
        return a.options.pointer_element_type == b.options.pointer_element_type;
    }
    case Type_Deduplication_Type::POINTER: {
        return a.options.slice_element_type == b.options.slice_element_type;
    }
    case Type_Deduplication_Type::FUNCTION: 
    {
        // Check return types
        auto& fn_a = a.options.function;
        auto& fn_b = b.options.function;
        if (fn_a.return_type != fn_b.return_type) { // Note: In Type_Deduplication return type may be null
            return false;
        }

        // Check parameters
        if (fn_a.parameters.size != fn_b.parameters.size) {
            return false;
        }
        for (int i = 0; i < fn_a.parameters.size; i++)
        {
            auto& p_a = fn_a.parameters[i];
            auto& p_b = fn_b.parameters[i];
            if (p_a.type != p_b.type || p_a.name != p_b.name || p_a.default_value_exists != p_b.default_value_exists) {
                return false;
            }
            // If default values exist, we don't deduplicate the function, because comparison may be wrong currently
            if (p_a.default_value_exists) {
                return false;
            }
        }
        return true;
    }
    default: panic("");
    }

    panic("");
    return false;
}

u64 type_deduplication_hash(Type_Deduplication* dedup) 
{
    i32 type_type_value = (int)dedup->type;
    u64 hash = 2342342;
    hash = hash_combine(hash, hash_i32(&type_type_value));

    switch (dedup->type)
    {
    case Type_Deduplication_Type::POINTER: {
        hash = hash_combine(hash, hash_pointer(dedup->options.pointer_element_type));
        break;
    }
    case Type_Deduplication_Type::SLICE: {
        hash = hash_combine(hash, hash_pointer(dedup->options.slice_element_type));
        break;
    }
    case Type_Deduplication_Type::CONSTANT: {
        hash = hash_combine(hash, hash_pointer(dedup->options.non_constant_type));
        break;
    }
    case Type_Deduplication_Type::SUBTYPE: {
        hash = hash_combine(hash, hash_pointer(dedup->options.subtype.base_type));
        hash = hash_combine(hash, hash_pointer(dedup->options.subtype.name));
        break;
    }
    case Type_Deduplication_Type::ARRAY: {
        auto& infos = dedup->options.array_type;
        hash = hash_combine(hash, hash_pointer(infos.element_type));
        hash = hash_bool(hash, infos.size_known);
        hash = hash_combine(hash, hash_pointer(infos.polymorphic_count_variable));
        hash = hash_combine(hash, hash_i32(&infos.element_count));
        break;
    }
    case Type_Deduplication_Type::FUNCTION: {
        auto& infos = dedup->options.function;
        hash = hash_combine(hash, hash_pointer(infos.return_type));
        for (int i = 0; i < infos.parameters.size; i++) {
            auto& param = infos.parameters[i];
            hash = hash_combine(hash, hash_pointer(param.type));
            hash = hash_combine(hash, hash_pointer(param.name));
            hash = hash_bool(hash, param.default_value_exists);
        }
        break;
    }
    default: panic("");
    }

    return hash;
}

void internal_type_info_destroy(Internal_Type_Information* info)
{
    switch (info->tag)
    {
    case Datatype_Type::ENUM: {
        if (info->options.enumeration.members.data_ptr != 0) {
            delete[]info->options.enumeration.members.data_ptr;
            info->options.enumeration.members.data_ptr = 0;
        }
        break;
    }
    case Datatype_Type::FUNCTION:
        if (info->options.function.parameters.data_ptr != 0) {
            delete[]info->options.function.parameters.data_ptr;
            info->options.function.parameters.data_ptr = 0;
        }
        break;
    case Datatype_Type::STRUCT: {
        if (info->options.structure.members.data_ptr != 0) {
            delete[]info->options.structure.members.data_ptr;
            info->options.structure.members.data_ptr = 0;
        }
        if (info->options.structure.subtypes.data_ptr != 0) {
            delete[]info->options.structure.subtypes.data_ptr;
            info->options.structure.subtypes.data_ptr = 0;
        }
        break;
    }
    default: break;
    }
    delete info;
}

Type_System type_system_create(Timer* timer)
{
    Type_System result;
    result.deduplication_table = hashtable_create_empty<Type_Deduplication, Datatype*>(32, type_deduplication_hash, type_deduplication_is_equal);
    result.types = dynamic_array_create<Datatype*>(256);
    result.internal_type_infos = dynamic_array_create<Internal_Type_Information*>(256);
    result.timer = timer;
    result.register_time = 0;
    return result;
}

void type_system_destroy(Type_System* system) {
    type_system_reset(system);
    dynamic_array_destroy(&system->types);
    hashtable_destroy(&system->deduplication_table);

    for (int i = 0; i < system->internal_type_infos.size; i++) {
        internal_type_info_destroy(system->internal_type_infos[i]);
    }
    dynamic_array_destroy(&system->internal_type_infos);
}

void type_system_reset(Type_System* system)
{
    system->register_time = 0;
    for (int i = 0; i < system->types.size; i++) {
        type_base_destroy(system->types[i]);
        delete system->types[i];
    }
    dynamic_array_reset(&system->types);
    hashtable_reset(&system->deduplication_table);

    for (int i = 0; i < system->internal_type_infos.size; i++) {
        internal_type_info_destroy(system->internal_type_infos[i]);
    }
    dynamic_array_reset(&system->internal_type_infos);
}



// Type_System takes ownership of datatype pointer afterwards
Internal_Type_Information* type_system_register_type(Datatype* datatype)
{
    auto& type_system = compiler.type_system;

    // Finish type info
    datatype->type_handle.index = type_system.types.size;
    datatype->mods.constant_flags = 0;
    datatype->mods.pointer_level = 0;
    datatype->base_type = datatype;
    while (true)
    {
        if (datatype->base_type->type == Datatype_Type::POINTER) 
        {
            datatype->mods.pointer_level += 1;
            datatype->base_type = downcast<Datatype_Pointer>(datatype->base_type)->element_type;
            datatype->mods.constant_flags = datatype->mods.constant_flags << 1;
        }
        else if (datatype->base_type->type == Datatype_Type::CONSTANT)
        {
            datatype->mods.constant_flags = datatype->mods.constant_flags | 1;
            datatype->base_type = downcast<Datatype_Constant>(datatype->base_type)->element_type;
        }
        else {
            break;
        }
    }
    assert(datatype->mods.pointer_level < 32, "This is the max value on supported pointer constant flags");
    dynamic_array_push_back(&type_system.types, datatype);

    Internal_Type_Information* internal_info = new Internal_Type_Information;
    if (datatype->memory_info.available) {
        internal_info->size = datatype->memory_info.value.size;
        internal_info->alignment = datatype->memory_info.value.alignment;
    }
    else {
        internal_info->size = -1;
        internal_info->alignment = -1;
    }
    internal_info->type_handle = datatype->type_handle;
    internal_info->tag = datatype->type;


    dynamic_array_push_back(&type_system.internal_type_infos, internal_info);

    return internal_info;
}

Datatype datatype_make_simple_base(Datatype_Type type, int size, int alignment) {
    Datatype result;
    result.type_handle.index = -1; // This should be the case until the type is registered
    result.base_type = nullptr;
    result.mods.pointer_level = 0;
    result.mods.constant_flags = 0;
    result.type = type;
    result.contains_type_template = false;
    result.memory_info_workload = 0;
    result.memory_info.available = true;
    result.memory_info.value.size = size;
    result.memory_info.value.alignment = alignment;
    result.memory_info.value.contains_padding_bytes = false;
    result.memory_info.value.contains_function_pointer = false;
    result.memory_info.value.contains_reference = false;
    return result;
}

Datatype_Primitive* type_system_make_primitive(Primitive_Type type, int size, bool is_signed)
{
    Datatype_Primitive* result = new Datatype_Primitive;
    result->base = datatype_make_simple_base(Datatype_Type::PRIMITIVE, size, size);
    result->is_signed = is_signed;
    result->primitive_type = type;

    auto& internal_info = type_system_register_type(upcast(result))->options.primitive;
    internal_info.is_signed = is_signed;
    internal_info.tag = type;

    return result;
}

Datatype_Template_Parameter* type_system_make_template_parameter(Symbol* symbol, int value_access_index, int defined_in_parameter_index)
{
    Datatype_Template_Parameter* result = new Datatype_Template_Parameter;
    result->base = datatype_make_simple_base(Datatype_Type::TEMPLATE_PARAMETER, 1, 1);
    result->base.contains_type_template = true;

    result->symbol = symbol;
    result->is_reference = false;
    result->mirrored_type = 0;
    result->defined_in_parameter_index = defined_in_parameter_index;
    result->value_access_index = value_access_index;

    auto internal_info = type_system_register_type(upcast(result));
    internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

    // Create mirror type exactly the same way as normal type, but set mirror flag
    Datatype_Template_Parameter* mirror_type = new Datatype_Template_Parameter;
    *mirror_type = *result;
    mirror_type->mirrored_type = result;
    mirror_type->is_reference = true;
    result->mirrored_type = mirror_type;

    internal_info = type_system_register_type(upcast(mirror_type));
    internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

    return result;
}

Datatype_Struct_Instance_Template* type_system_make_struct_instance_template(
    Workload_Structure_Polymorphic* base, Array<Polymorphic_Value> instance_values)
{
    Datatype_Struct_Instance_Template* result = new Datatype_Struct_Instance_Template;
    result->base = datatype_make_simple_base(Datatype_Type::STRUCT_INSTANCE_TEMPLATE, 1, 1);
    result->base.contains_type_template = true;

    result->instance_values = instance_values;
    result->struct_base = base;
    auto internal_info = type_system_register_type(upcast(result));
    internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

    return result;
}

Datatype_Struct_Instance_Template_Subtype* type_system_make_struct_instance_template_subtype(Datatype_Struct_Instance_Template* instance_template, String* name)
{
    auto& type_system = compiler.type_system;

    Type_Deduplication dedup;
    dedup.type = Type_Deduplication_Type::SUBTYPE;
    dedup.options.subtype.base_type = upcast(instance_template);
    dedup.options.subtype.name = name;
    // Check if type was already created
    {
        auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
        if (type_opt != nullptr) {
            Datatype* type = *type_opt;
            assert(type->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE_SUBTYPE, "");
            return downcast<Datatype_Struct_Instance_Template_Subtype>(type);
        }
    }

    Datatype_Struct_Instance_Template_Subtype* result = new Datatype_Struct_Instance_Template_Subtype;
    result->struct_template = instance_template;
    result->subtype_name = name;
    result->base = datatype_make_simple_base(Datatype_Type::STRUCT_INSTANCE_TEMPLATE_SUBTYPE, 1, 1);
    result->base.contains_type_template = true;

    auto internal_info = type_system_register_type(upcast(result));
    internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

    hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));
    return result;
}

Datatype_Pointer* type_system_make_pointer(Datatype* child_type)
{
    auto& type_system = compiler.type_system;

    Type_Deduplication dedup;
    dedup.type = Type_Deduplication_Type::POINTER;
    dedup.options.pointer_element_type = child_type;

    // Check if type was already created
    {
        auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
        if (type_opt != nullptr) {
            Datatype* type = *type_opt;
            assert(type->type == Datatype_Type::POINTER, "");
            return downcast<Datatype_Pointer>(type);
        }
    }

    Datatype_Pointer* result = new Datatype_Pointer;
    result->base = datatype_make_simple_base(Datatype_Type::POINTER, 8, 8);
    result->base.memory_info.value.contains_reference = true;
    result->base.contains_type_template = child_type->contains_type_template;
    result->element_type = child_type;

    auto& internal_info = type_system_register_type(upcast(result))->options.pointer;
    internal_info.index = child_type->type_handle.index;

    hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));

    return result;
}

Datatype_Array* type_system_make_array(Datatype* element_type, bool count_known, int element_count, Datatype_Template_Parameter* polymorphic_count_variable)
{
    auto& type_system = compiler.type_system;
    assert(!(count_known && element_count <= 0), "Hey");

    if (!count_known) {
        element_count = 1;
    }

    Type_Deduplication dedup;
    dedup.type = Type_Deduplication_Type::ARRAY;
    dedup.options.array_type.element_count = element_count;
    dedup.options.array_type.element_type = element_type;
    dedup.options.array_type.polymorphic_count_variable = polymorphic_count_variable;
    dedup.options.array_type.size_known = count_known;

    // Check if type was already created
    {
        auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
        if (type_opt != nullptr) {
            Datatype* type = *type_opt;
            assert(type->type == Datatype_Type::ARRAY, "");
            return downcast<Datatype_Array>(type);
        }
    }

    Datatype_Array* result = new Datatype_Array;
    result->base = datatype_make_simple_base(Datatype_Type::ARRAY, 1, 1);
    result->base.contains_type_template = element_type->contains_type_template;
    if (polymorphic_count_variable) {
        result->base.contains_type_template = true;
    }

    result->element_type = element_type;
    result->polymorphic_count_variable = polymorphic_count_variable;
    result->count_known = count_known;
    if (!count_known) 
    {
        result->element_count = 1; // Just so the value is initialized and we have some values to use
        result->base.memory_info.available = true;
        result->base.memory_info.value.contains_padding_bytes = false;
        result->base.memory_info.value.alignment = 1;
        result->base.memory_info.value.size = 1;
    }
    else {
        result->element_count = element_count;
    }

    if (element_type->memory_info.available) {
        result->base.memory_info.value.size = element_type->memory_info.value.size * result->element_count;
        result->base.memory_info.value.alignment = element_type->memory_info.value.alignment;
        result->base.memory_info.value.contains_padding_bytes = element_type->memory_info.value.contains_padding_bytes; // Struct size is always padded to alignment
        result->base.memory_info.value.contains_function_pointer = element_type->memory_info.value.contains_function_pointer;
        result->base.memory_info.value.contains_reference = element_type->memory_info.value.contains_reference;
    }
    else {
        result->base.memory_info.available = false;
        result->base.memory_info_workload = element_type->memory_info_workload;
        // Add to struct_finish queue
        dynamic_array_push_back(&element_type->memory_info_workload->struct_type->types_waiting_for_size_finish, upcast(result));
    }

    auto& internal_info = type_system_register_type(upcast(result))->options.array;
    internal_info.element_type = element_type->type_handle;
    internal_info.size = result->element_count;

    hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));

    return result;
}

Datatype_Slice* type_system_make_slice(Datatype* element_type)
{
    auto& type_system = compiler.type_system;

    Type_Deduplication dedup;
    dedup.type = Type_Deduplication_Type::SLICE;
    dedup.options.slice_element_type = element_type;

    // Check if type was already created
    {
        auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
        if (type_opt != nullptr) {
            Datatype* type = *type_opt;
            assert(type->type == Datatype_Type::SLICE, "");
            return downcast<Datatype_Slice>(type);
        }
    }

    Datatype_Slice* result = new Datatype_Slice;
    result->base = datatype_make_simple_base(Datatype_Type::SLICE, 16, 8);
    result->base.memory_info.value.contains_reference = true;
    result->base.memory_info.value.contains_padding_bytes = true; // Currently slice is pointer + int32
    result->base.contains_type_template = element_type->contains_type_template;

    result->element_type = element_type;
    result->data_member.id = compiler.predefined_ids.data;
    result->data_member.type = upcast(type_system_make_pointer(element_type));
    result->data_member.offset = 0;
    result->size_member.id = compiler.predefined_ids.size;
    result->size_member.type = upcast(compiler.type_system.predefined_types.i32_type);
    result->size_member.offset = 8;

    auto& internal_info = type_system_register_type(upcast(result))->options.slice;
    internal_info.element_type = element_type->type_handle;

    hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));
    return result;
}

Datatype* type_system_make_constant(Datatype* datatype)
{
    auto& type_system = compiler.type_system;
    if (datatype->type == Datatype_Type::CONSTANT) return datatype;

    Type_Deduplication dedup;
    dedup.type = Type_Deduplication_Type::CONSTANT;
    dedup.options.non_constant_type = datatype;
    // Check if type was already created
    {
        auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
        if (type_opt != nullptr) {
            Datatype* type = *type_opt;
            assert(type->type == Datatype_Type::CONSTANT, "");
            return type;
        }
    }

    Datatype_Constant* result = new Datatype_Constant;
    result->element_type = datatype;
    result->base = datatype_make_simple_base(Datatype_Type::CONSTANT, 1, 1);
    result->base.contains_type_template = datatype->contains_type_template;

    if (datatype->memory_info.available) {
        result->base.memory_info = datatype->memory_info;
        result->base.memory_info_workload = nullptr;
    }
    else {
        result->base.memory_info.available = false;
        result->base.memory_info_workload = datatype->memory_info_workload;
        dynamic_array_push_back(&datatype->memory_info_workload->struct_type->types_waiting_for_size_finish, upcast(result));
    }

    type_system_register_type(upcast(result))->options.constant = datatype->type_handle;
    hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));

    return upcast(result);
}

Datatype_Struct_Subtype* type_system_make_struct_subtype(Datatype_Struct* base_type, String* subtype_name)
{
    auto& type_system = compiler.type_system;

    Type_Deduplication dedup;
    dedup.type = Type_Deduplication_Type::SUBTYPE;
    dedup.options.subtype.base_type = upcast(base_type);
    dedup.options.subtype.name = subtype_name;
    // Check if type was already created
    {
        auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
        if (type_opt != nullptr) {
            Datatype* type = *type_opt;
            assert(type->type == Datatype_Type::STRUCT_SUBTYPE, "");
            return downcast<Datatype_Struct_Subtype>(type);
        }
    }

    Datatype_Struct_Subtype* result = new Datatype_Struct_Subtype;
    result->structure = base_type;
    result->valid_subtype = false;
    result->subtype_index = -1;
    result->subtype_name = subtype_name;
    result->base = datatype_make_simple_base(Datatype_Type::STRUCT_SUBTYPE, 1, 1);
    result->base.contains_type_template = base_type->base.contains_type_template;

    if (base_type->base.memory_info.available) 
    {
        result->base.memory_info = base_type->base.memory_info;
        result->base.memory_info_workload = nullptr;

        int found_index = -1;
        for (int i = 0; i < base_type->subtypes.size; i++) {
            if (base_type->subtypes[i].name == subtype_name) {
                found_index = i;
                break;
            }
        }

        if (found_index != -1) {
            result->valid_subtype = true;
            result->subtype_index = found_index;
        }
    }
    else {
        result->base.memory_info.available = false;
        result->base.memory_info_workload = base_type->base.memory_info_workload;
        dynamic_array_push_back(&base_type->types_waiting_for_size_finish, upcast(result));
    }

    auto& info_internal = type_system_register_type(upcast(result))->options.struct_subtype;
    info_internal.subtype_index = result->subtype_index;
    info_internal.type = base_type->base.type_handle;
    hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));

    return result;

}

Datatype* type_system_make_type_with_mods(Datatype* base_type, Type_Mods mods)
{
    base_type = base_type->base_type;
    if (type_mods_is_constant(mods, 0)) {
        base_type = type_system_make_constant(base_type);
    }
    for (int i = 0; i < mods.pointer_level; i++) {
        base_type = upcast(type_system_make_pointer(base_type));
        if (type_mods_is_constant(mods, i + 1)) {
            base_type = type_system_make_constant(base_type);
        }
    }
    return base_type;
}

Datatype_Function* type_system_make_function(Dynamic_Array<Function_Parameter> parameters, Datatype* return_type)
{
    auto& type_system = compiler.type_system;

    Type_Deduplication dedup;
    dedup.type = Type_Deduplication_Type::FUNCTION;
    dedup.options.function.parameters = parameters;
    dedup.options.function.return_type = return_type;

    // Check if type was already created
    {
        auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
        if (type_opt != nullptr) {
            Datatype* type = *type_opt;
            assert(type->type == Datatype_Type::FUNCTION, "");
            dynamic_array_destroy(&parameters);
            return downcast<Datatype_Function>(type);
        }
    }

    Datatype_Function* result = new Datatype_Function;
    result->base = datatype_make_simple_base(Datatype_Type::FUNCTION, 8, 8);
    result->base.memory_info.value.contains_function_pointer = true;
    result->parameters = parameters;
    if (return_type != nullptr) {
        result->return_type = optional_make_success(return_type);
    }
    else {
        result->return_type = optional_make_failure<Datatype*>();
    }
    result->parameters_with_default_value_count = 0;
    for (int i = 0; i < parameters.size; i++) {
        auto& param = parameters[i];
        if (param.default_value_exists) {
            result->parameters_with_default_value_count += 1;
        }
        if (param.type->contains_type_template) {
            result->base.contains_type_template = true;
        }
    }
    if (result->return_type.available && result->return_type.value->contains_type_template) {
        result->base.contains_type_template = true;
    }

    auto& internal_info = type_system_register_type(upcast(result))->options.function;
    {
        internal_info.has_return_type = return_type != nullptr;
        internal_info.return_type.index = -1;
        if (internal_info.has_return_type) {
            internal_info.return_type.index = return_type->type_handle.index;
        }

        internal_info.parameters.size = parameters.size;
        if (parameters.size > 0) {
            internal_info.parameters.data_ptr = new Upp_Type_Handle[parameters.size];
            for (int i = 0; i < parameters.size; i++) {
                internal_info.parameters.data_ptr[i] = parameters[i].type->type_handle;
            }
        }
        else {
            internal_info.parameters.data_ptr = nullptr;
        }
    }

    hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));
    return result;
}

Datatype_Function* type_system_make_function(std::initializer_list<Function_Parameter> parameter_types, Datatype* return_type)
{
    Dynamic_Array<Function_Parameter> params = dynamic_array_create<Function_Parameter>(1);
    for (auto& param : parameter_types) {
        dynamic_array_push_back(&params, param);
    }
    return type_system_make_function(params, return_type);
}

Datatype_Struct* type_system_make_struct_empty(AST::Structure_Type struct_type, String* name, Workload_Structure_Body* workload)
{
    Datatype_Struct* result = new Datatype_Struct;
    result->base = datatype_make_simple_base(Datatype_Type::STRUCT, 0, 0);
    result->base.memory_info.available = false;
    result->base.memory_info_workload = workload;
    result->types_waiting_for_size_finish = dynamic_array_create<Datatype*>();

    if (name != 0) {
        result->name = optional_make_success(name);
    }
    else {
        result->name.available = false;
    }
    result->workload = workload;
    result->tag_member.id = 0;
    result->tag_member.offset = 0;
    result->tag_member.type = 0;
    result->struct_type = struct_type;
    result->members = dynamic_array_create<Struct_Member>();
    result->subtypes = dynamic_array_create<Subtype_Info>();

    type_system_register_type(upcast(result)); // Is only initialized when memory_info is done
    return result;
}

void struct_add_member(Datatype_Struct* structure, String* id, Datatype* member_type)
{
    assert(!structure->base.memory_info.available, "Cannot add members to already finished struct");

    Struct_Member member;
    member.id = id;
    member.offset = 0;
    member.type = member_type;
    dynamic_array_push_back(&structure->members, member);
}

void type_system_finish_array(Datatype_Array* array)
{
    auto& type_system = compiler.type_system;
    auto& base = array->base;
    assert(base.type_handle.index < (u32)type_system.internal_type_infos.size, "");
    assert(type_system.internal_type_infos.size == type_system.types.size, "");
    assert(type_size_is_unfinished(upcast(array)), "");
    assert(array->base.type == Datatype_Type::ARRAY, "");
    assert(array->element_type->memory_info.available, "");
    assert(array->count_known, "I am not sure about this condidtion, otherwise we just have to change the calculation below");

    // Finish array
    array->base.memory_info.available = true;
    auto& memory = array->base.memory_info.value;
    memory.size = array->element_type->memory_info.value.size * array->element_count;
    memory.alignment = array->element_type->memory_info.value.alignment;

    // Update internal info to mirror struct info
    Internal_Type_Information* internal_info = type_system.internal_type_infos[base.type_handle.index];
    internal_info->size = memory.size;
    internal_info->alignment = memory.alignment;
    internal_info->type_handle = base.type_handle;
    internal_info->tag = base.type;
}


void type_system_finish_struct(Datatype_Struct* structure)
{
    auto& type_system = compiler.type_system;
    auto& members = structure->members;
    auto& base = structure->base;
    assert(base.type_handle.index < (u32)type_system.internal_type_infos.size, "");
    assert(type_system.internal_type_infos.size == type_system.types.size, "");
    assert(type_size_is_unfinished(upcast(structure)), "");

    // Calculate memory info/layout (Member offsets, alignment, size, contains pointer/others)
    structure->base.memory_info.available = true;
    auto& memory = structure->base.memory_info.value;
    memory.size = 0;
    memory.alignment = 1;
    memory.contains_padding_bytes = false;
    memory.contains_function_pointer = false;
    memory.contains_reference = false;
    for (int i = 0; i < members.size; i++)
    {
        Struct_Member* member = &members[i];
        assert(!type_size_is_unfinished(member->type) && member->type->memory_info.available, "");
        auto& member_memory = member->type->memory_info.value;

        // Calculate member offsets/padding
        int prev_size = memory.size;
        if (structure->struct_type == Structure_Type::STRUCT) {
            memory.size = math_round_next_multiple(memory.size, member_memory.alignment);
            if (memory.size != prev_size) {
                memory.contains_padding_bytes = true;
            }
            member->offset = memory.size;
            memory.size += member_memory.size;
        }
        else {
            memory.size = math_maximum(memory.size, member_memory.size); // Unions
            if (prev_size != 0 && memory.size != prev_size) {
                memory.contains_padding_bytes = true;
            }
            member->offset = 0;
        }

        // Check which types are contained
        if (member_memory.contains_padding_bytes) {
            memory.contains_padding_bytes = true;
        }
        if (member_memory.contains_function_pointer) {
            memory.contains_function_pointer = true;
        }
        if (member_memory.contains_reference) {
            memory.contains_reference = true;
        }

        // Update alignment
        memory.alignment = math_maximum(member_memory.alignment, memory.alignment);
    }

    // Handle subtype-structs
    if (structure->subtypes.size > 0)
    {
        // Create empty tag enum
        Datatype_Enum* tag_type = nullptr;
        {
            String name;
            if (structure->name.available) {
                name = string_copy(*structure->name.value);
            }
            else {
                name = string_create("anon_struct");
            }
            string_append_formated(&name, "_tag");
            String* tag_enum_name = identifier_pool_add(&compiler.identifier_pool, name);
            string_destroy(&name);
            tag_type = type_system_make_enum_empty(tag_enum_name);
        }

        // Calculate subtype layout
        int max_subtype_size = 0;
        for (int i = 0; i < structure->subtypes.size; i++)
        {
            auto& subtype = structure->subtypes[i];
            assert(!type_size_is_unfinished(upcast(subtype.type)) && subtype.type->base.memory_info.available, "");
            auto& subtype_memory = subtype.type->base.memory_info.value;

            // Figure out subtype offset + size
            subtype.offset = math_round_next_multiple(memory.size, subtype_memory.alignment);
            int subtype_size = subtype_memory.size;
            if (subtype.offset != memory.size) {
                memory.contains_padding_bytes = true;
                subtype_size += subtype.offset - memory.size;
            }

            // Keep track of maximum subtype size/alignment
            int prev_max_size = max_subtype_size;
            max_subtype_size = math_maximum(max_subtype_size, subtype_size); // Unions
            if (prev_max_size != 0 && prev_max_size != max_subtype_size) {
                // If we have two subtypes that don't have the same size, there will be padding
                memory.contains_padding_bytes = true;
            }
            memory.alignment = math_maximum(memory.alignment, subtype_memory.alignment);

            // Update info flags
            if (subtype_memory.contains_padding_bytes) {
                memory.contains_padding_bytes = true;
            }
            if (subtype_memory.contains_function_pointer) {
                memory.contains_function_pointer = true;
            }
            if (subtype_memory.contains_reference) {
                memory.contains_reference = true;
            }

            // Add to tag enum
            Enum_Member tag_member;
            tag_member.name = subtype.name;
            tag_member.value = i + 1; // Note: Enum member values start at 1 by default, if this changes the constant pool serialization also needs change
            dynamic_array_push_back(&tag_type->members, tag_member);
        }

        memory.size += max_subtype_size;

        // Finish tag
        type_system_finish_enum(tag_type);
        auto& tag_memory = tag_type->base.memory_info.value;

        Struct_Member tag;
        tag.id = compiler.predefined_ids.tag;
        int prev_size = memory.size;
        memory.size = math_round_next_multiple(memory.size, tag_memory.alignment);
        if (prev_size != memory.size) {
            memory.contains_padding_bytes = true;
        }
        tag.offset = memory.size;
        tag.type = upcast(tag_type);
        // dynamic_array_push_back(&members, tag);
        structure->tag_member = tag;

        // Adjust size/alignment for new tag
        memory.size += tag_memory.size;
        memory.alignment = math_maximum(memory.alignment, tag_memory.alignment);
    }

    // Finalize alignment
    memory.size = math_maximum(1, memory.size);
    memory.size = math_round_next_multiple(memory.size, memory.alignment);



    // Update internal info to mirror struct info
    Internal_Type_Information* internal_info = type_system.internal_type_infos[base.type_handle.index];
    internal_info->size = base.memory_info.value.size;
    internal_info->alignment = base.memory_info.value.alignment;
    internal_info->type_handle = base.type_handle;
    internal_info->tag = base.type;

    // Update name
    if (!structure->name.available) {
        internal_info->options.structure.name.slice.size = 1;
        internal_info->options.structure.name.slice.data_ptr = (const u8*) "";
    }
    else {
        internal_info->options.structure.name.slice.size = structure->name.value->size;
        internal_info->options.structure.name.slice.data_ptr = (const u8*) structure->name.value->characters;
    }

    // Update members
    int member_count = members.size;
    internal_info->options.structure.members.size = member_count;
    if (member_count != 0)
    {
        internal_info->options.structure.members.data_ptr = new Internal_Type_Struct_Member[member_count];
        for (int i = 0; i < member_count; i++)
        {
            Internal_Type_Struct_Member* internal_member = &internal_info->options.structure.members.data_ptr[i];
            Struct_Member* member = &members[i];
            internal_member->name.slice.size = member->id->size + 1;
            internal_member->name.slice.data_ptr = (const u8*) member->id->characters;
            internal_member->offset = member->offset;
            internal_member->type = member->type->type_handle;
        }
    }
    else {
        internal_info->options.structure.members.data_ptr = 0;
        internal_info->options.structure.members.size = 0;
    }

    // Update subtypes
    {
        auto& subtypes = structure->subtypes;
        if (subtypes.size > 0) 
        {
            internal_info->options.structure.subtypes.size = subtypes.size;
            internal_info->options.structure.subtypes.data_ptr = new Internal_Type_Subtype_Info[subtypes.size];
            for (int i = 0; i < subtypes.size; i++) {
                auto& subtype = subtypes[i];
                auto& sub_intern = internal_info->options.structure.subtypes.data_ptr[i];
                sub_intern.name.slice.size = subtype.name->size + 1;
                sub_intern.name.slice.data_ptr = (const u8*)subtype.name->characters;
                sub_intern.type = subtype.type->base.type_handle;
                sub_intern.offset = subtype.offset;
            }
        }
        else {
            internal_info->options.structure.subtypes.data_ptr = nullptr;
            internal_info->options.structure.subtypes.size = 0;
        }
    }
    internal_info->options.structure.is_union = structure->struct_type == AST::Structure_Type::UNION;

    // Finish all arrays/constants waiting on this struct 
    for (int i = 0; i < structure->types_waiting_for_size_finish.size; i++) 
    {
        Datatype* type = structure->types_waiting_for_size_finish[i];
        if (type->type == Datatype_Type::ARRAY) {
            type_system_finish_array(downcast<Datatype_Array>(type));
        }
        else if (type->type == Datatype_Type::CONSTANT) 
        {
            assert(type->type == Datatype_Type::CONSTANT, ""); 
            auto constant = downcast<Datatype_Constant>(type);
            assert(constant->element_type->memory_info.available, ""); 
            type->memory_info = constant->element_type->memory_info;

            type_system.internal_type_infos[type->type_handle.index]->size = constant->element_type->memory_info.value.size;
            type_system.internal_type_infos[type->type_handle.index]->alignment = constant->element_type->memory_info.value.alignment;
        }
        else if (type->type == Datatype_Type::STRUCT_SUBTYPE) 
        {
            auto subtype = downcast<Datatype_Struct_Subtype>(type);
            assert(!subtype->base.memory_info.available, ""); 
            subtype->base.memory_info = structure->base.memory_info;

            // Check if subtype is valid subtype
            subtype->valid_subtype = false;
            {
                int found_index = -1;
                for (int i = 0; i < structure->subtypes.size; i++) {
                    if (structure->subtypes[i].name == subtype->subtype_name) {
                        found_index = i;
                        break;
                    }
                }

                if (found_index != -1) {
                    subtype->valid_subtype = true;
                    subtype->subtype_index = found_index;
                }
            }

            auto& info_internal = type_system.internal_type_infos[subtype->base.type_handle.index];
            info_internal->size = structure->base.memory_info.value.size;
            info_internal->alignment = structure->base.memory_info.value.alignment;
            info_internal->options.struct_subtype.subtype_index = subtype->subtype_index;
        }
    }
}

Datatype_Enum* type_system_make_enum_empty(String* name)
{
    Datatype_Enum* result = new Datatype_Enum;
    result->base = datatype_make_simple_base(Datatype_Type::ENUM, 0, 0);
    result->base.memory_info = optional_make_failure<Datatype_Memory_Info>(); // Is not initialized until enum is finished
    result->name = name;
    result->members = dynamic_array_create<Enum_Member>(3);

    type_system_register_type(upcast(result));
    return result;
}

void type_system_finish_enum(Datatype_Enum* enum_type)
{
    auto& type_system = compiler.type_system;
    auto& base = enum_type->base;
    auto& members = enum_type->members;
    assert(base.type_handle.index < (u32)type_system.internal_type_infos.size, "");
    assert(type_system.internal_type_infos.size == type_system.types.size, "");
    assert(!base.memory_info.available, "");

    // Finish enum
    base.memory_info.available = true;
    base.memory_info.value.size = 4;
    base.memory_info.value.alignment = 4;
    base.memory_info.value.contains_padding_bytes = false;

    enum_type->values_are_sequential = true;
    enum_type->sequence_start_value = 0;
    if (members.size > 0) 
    {
        enum_type->sequence_start_value = members[0].value;
        for (int i = 0; i < members.size; i++) {
            auto value = members[i].value;
            if (value != enum_type->sequence_start_value + i) {
                enum_type->values_are_sequential = false;
                break;
            }
        }
    }

    // Update internal info to mirror enum info
    Internal_Type_Information* internal_info = type_system.internal_type_infos[base.type_handle.index];
    internal_info->size = base.memory_info.value.size;
    internal_info->alignment = base.memory_info.value.size;
    internal_info->type_handle = base.type_handle;
    internal_info->tag = base.type;

    // Make mirroring internal info
    if (enum_type->name == 0) {
        internal_info->options.enumeration.name.slice.size = 1;
        internal_info->options.enumeration.name.slice.data_ptr = (const u8*) "";
    }
    else {
        internal_info->options.enumeration.name.slice.size = enum_type->name->size + 1;
        internal_info->options.enumeration.name.slice.data_ptr = (const u8*) enum_type->name->characters;
    }
    int member_count = members.size;
    internal_info->options.enumeration.members.size = member_count;
    internal_info->options.enumeration.members.data_ptr = new Internal_Type_Enum_Member[member_count];
    for (int i = 0; i < member_count; i++)
    {
        Enum_Member* member = &members[i];
        Internal_Type_Enum_Member* internal_member = &internal_info->options.enumeration.members.data_ptr[i];
        internal_info->options.enumeration.name.slice.size = member->name->size + 1;
        internal_info->options.enumeration.name.slice.data_ptr = (const u8*) member->name->characters;
        internal_member->value = member->value;
    }
}


template<typename T>
void test_type_similarity(Datatype* signature) {
    assert(signature->memory_info.value.size == sizeof(T) && signature->memory_info.value.alignment == alignof(T), "");
}

void type_system_add_predefined_types(Type_System* system)
{
    auto& ids = compiler.predefined_ids;
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
        types->unknown_type = new Datatype;
        *types->unknown_type = datatype_make_simple_base(Datatype_Type::UNKNOWN_TYPE, 1, 1);
        type_system_register_type(types->unknown_type);

        types->type_handle = new Datatype;
        *types->type_handle = datatype_make_simple_base(Datatype_Type::TYPE_HANDLE, sizeof(Upp_Type_Handle), alignof(Upp_Type_Handle));
        type_system_register_type(types->type_handle);

        types->byte_pointer = new Datatype;
        *types->byte_pointer = datatype_make_simple_base(Datatype_Type::BYTE_POINTER, sizeof(void*), alignof(void*));
        type_system_register_type(types->byte_pointer);
    }

    {
        types->cast_mode = type_system_make_enum_empty(ids.cast_mode);
        auto& members = types->cast_mode->members;
        auto add_enum_member = [&](Datatype_Enum* enum_type, String* name, int value) {
            Enum_Member item;
            item.name = name;
            item.value = value;
            dynamic_array_push_back(&enum_type->members, item);
        };
        add_enum_member(types->cast_mode, ids.cast_mode_none, 1);
        add_enum_member(types->cast_mode, ids.cast_mode_explicit, 2);
        add_enum_member(types->cast_mode, ids.cast_mode_inferred, 3);
        add_enum_member(types->cast_mode, ids.cast_mode_implicit, 4);
        type_system_finish_enum(types->cast_mode);

        types->cast_option = type_system_make_enum_empty(ids.cast_option);
        for (int i = 1; i < (int)Cast_Option::MAX_ENUM_VALUE; i++) {
            add_enum_member(types->cast_option, ids.cast_option_enum_values[i], i);
        }
        type_system_finish_enum(types->cast_option);
    }


    using AST::Structure_Type;
    auto make_id = [&](const char* name) -> String* { return identifier_pool_add(&compiler.identifier_pool, string_create_static(name)); };
    auto add_member_cstr = [&](Datatype_Struct* struct_type, const char* member_name, Datatype* member_type) {
        String* id = identifier_pool_add(&compiler.identifier_pool, string_create_static(member_name));
        struct_add_member(struct_type, id, member_type);
    };
    auto add_struct_subtype = [&](Datatype_Struct* struct_type, const char* subtype_name, Datatype_Struct* subtype_type) {
        Subtype_Info subtype;
        subtype.name = identifier_pool_add(&compiler.identifier_pool, string_create_static(subtype_name));
        subtype.offset = 0;
        subtype.type = subtype_type;
        assert(!struct_type->base.memory_info.available, "Must not be finished yet");
        dynamic_array_push_back(&struct_type->subtypes, subtype);
    };
    auto make_single_member_struct = [&](const char* struct_name, const char* member_name, Datatype* type) -> Datatype_Struct* 
    {
        String* name_id = identifier_pool_add(&compiler.identifier_pool, string_create_static(struct_name));
        auto result = type_system_make_struct_empty(AST::Structure_Type::STRUCT, name_id, 0);
        add_member_cstr(result, member_name, type);
        type_system_finish_struct(result);
        return result;
    };

    // Empty structure
    {
        types->empty_struct_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Empty_Struct"), 0);
        type_system_finish_struct(types->empty_struct_type);
    }

    // String
    {
        types->c_string = type_system_make_slice(type_system_make_constant(upcast(types->u8_type)));
        test_type_similarity<Upp_C_String>(upcast(types->c_string));
    }

    // Any
    {
        types->any_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Any"), 0);
        add_member_cstr(types->any_type, "data", upcast(types->byte_pointer));
        add_member_cstr(types->any_type, "type", types->type_handle);
        type_system_finish_struct(types->any_type);
        test_type_similarity<Upp_Any>(upcast(types->any_type));
    }

    // Type Information
    {
        // Primitive
        Datatype_Struct* primitive_info = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Primitive_Info"), 0);
        {
            {
                Datatype_Struct* integer_info_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Integer_Info"), 0);
                add_member_cstr(integer_info_type, "is_signed", upcast(types->bool_type));
                type_system_finish_struct(integer_info_type);
                add_struct_subtype(primitive_info, "Int", integer_info_type);
            }
            add_struct_subtype(primitive_info, "Float", types->empty_struct_type);
            add_struct_subtype(primitive_info, "Bool", types->empty_struct_type);
            type_system_finish_struct(primitive_info);
            test_type_similarity<Internal_Type_Primitive>(upcast(primitive_info));
        }
        // Array
        Datatype_Struct* array_info = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Array_Info"), 0);
        {
            add_member_cstr(array_info, "element_type", types->type_handle);
            add_member_cstr(array_info, "size", upcast(types->i32_type));
            type_system_finish_struct(array_info);
            test_type_similarity<Internal_Type_Array>(upcast(array_info));
        }
        // Slice
        Datatype_Struct* slice_info = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Slice_Info"), 0);
        {
            add_member_cstr(slice_info, "element_type", types->type_handle);
            type_system_finish_struct(slice_info);
            test_type_similarity<Internal_Type_Slice>(upcast(slice_info));
        }
        // Struct
        Datatype_Struct* struct_info = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Struct_Info"), 0);
        {
            {
                Datatype_Struct* struct_member_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Member_Info"), 0);
                add_member_cstr(struct_member_type, "name", upcast(types->c_string));
                add_member_cstr(struct_member_type, "type", types->type_handle);
                add_member_cstr(struct_member_type, "offset", upcast(types->i32_type));
                type_system_finish_struct(struct_member_type);
                add_member_cstr(struct_info, "members", upcast(type_system_make_slice(upcast(struct_member_type))));
                test_type_similarity<Internal_Type_Struct_Member>(upcast(struct_member_type));
            }
            {
                Datatype_Struct* subtype_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Subtype_Info"), 0);
                add_member_cstr(subtype_type, "name", upcast(types->c_string));
                add_member_cstr(subtype_type, "type", types->type_handle);
                add_member_cstr(subtype_type, "offset", upcast(types->i32_type));
                type_system_finish_struct(subtype_type);
                add_member_cstr(struct_info, "subtypes", upcast(type_system_make_slice(upcast(subtype_type))));
                test_type_similarity<Internal_Type_Subtype_Info>(upcast(subtype_type));
            }
            add_member_cstr(struct_info, "name", upcast(types->c_string));
            add_member_cstr(struct_info, "is_union", upcast(types->bool_type));
            type_system_finish_struct(struct_info);
            test_type_similarity<Internal_Type_Struct>(upcast(struct_info));
        }
        // ENUM
        Datatype_Struct* enum_info = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Enum_Info"), 0);
        {
            {
                Datatype_Struct* enum_member_type = type_system_make_struct_empty(Structure_Type::STRUCT, 0, 0);
                add_member_cstr(enum_member_type, "name", upcast(types->c_string));
                add_member_cstr(enum_member_type, "value", upcast(types->i32_type));
                type_system_finish_struct(enum_member_type);
                add_member_cstr(enum_info, "members", upcast(type_system_make_slice(upcast(enum_member_type))));
                test_type_similarity<Internal_Type_Enum_Member>(upcast(enum_member_type));
            }
            add_member_cstr(enum_info, "name", upcast(types->c_string));
            type_system_finish_struct(enum_info);
            test_type_similarity<Internal_Type_Enum>(upcast(enum_info));
        }
        // Function
        Datatype_Struct* function_info = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Function_Info"), 0);
        {
            add_member_cstr(function_info, "parameter_types", upcast(type_system_make_slice(types->type_handle)));
            add_member_cstr(function_info, "return_type", types->type_handle);
            add_member_cstr(function_info, "has_return_type", upcast(types->bool_type));
            type_system_finish_struct(function_info);
            test_type_similarity<Internal_Type_Function>(upcast(function_info));
        }

        // Type Information
        {
            Datatype_Struct* type_info_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Type_Info"), 0);
            add_member_cstr(type_info_type, "type", types->type_handle);
            add_member_cstr(type_info_type, "size", upcast(types->i32_type));
            add_member_cstr(type_info_type, "alignment", upcast(types->i32_type));

            // PRIMITIVE = 1, // Int, float, bool
            // ARRAY, // Array with compile-time known size, like [5]int
            // SLICE, // Pointer + size
            // POINTER,
            // STRUCT,
            // ENUM,
            // FUNCTION,
            // TYPE_HANDLE,
            // BYTE_POINTER, // Same as void* in C++
            // CONSTANT,
            // UNKNOWN_TYPE, // For error propagation

            // Add subtypes in correct order
            add_struct_subtype(type_info_type, "Primitive", primitive_info);
            add_struct_subtype(type_info_type, "Array", array_info);
            add_struct_subtype(type_info_type, "Slice", slice_info);
            add_struct_subtype(type_info_type, "Pointer", make_single_member_struct("Pointer_Info", "element_type", types->type_handle));
            add_struct_subtype(type_info_type, "Struct", struct_info);
            add_struct_subtype(type_info_type, "Enum", enum_info);
            add_struct_subtype(type_info_type, "Function", function_info);
            add_struct_subtype(type_info_type, "Type_Handle", types->empty_struct_type);
            add_struct_subtype(type_info_type, "Byte_Pointer", types->empty_struct_type);
            add_struct_subtype(type_info_type, "Constant", make_single_member_struct("Constant_Info", "element_type", types->type_handle));
            add_struct_subtype(type_info_type, "Unknown", types->empty_struct_type);

            type_system_finish_struct(type_info_type);
            types->type_information_type = type_info_type;
            test_type_similarity<Internal_Type_Information>(upcast(type_info_type));
        }
    }

    // Hardcoded Functions
    {
        auto& ts = *system;
        auto make_param = [&](Datatype* signature, const char* name, bool default_value_exists = false) -> Function_Parameter {
            auto parameter = function_parameter_make_empty();
            parameter.name = identifier_pool_add(&compiler.identifier_pool, string_create_static(name));
            parameter.type = signature;
            parameter.default_value_exists = default_value_exists;
            return parameter;
        };
        types->type_assert = type_system_make_function({ make_param(upcast(types->bool_type), "condition") });
        types->type_free = type_system_make_function({ make_param(upcast(types->byte_pointer), "pointer") });
        types->type_malloc = type_system_make_function({ make_param(upcast(types->i32_type), "size") }, upcast(types->byte_pointer));
        types->type_type_info = type_system_make_function({ make_param(upcast(types->type_handle), "type_handle") }, upcast(type_system_make_pointer(upcast(types->type_information_type))));
        types->type_type_of = type_system_make_function({ make_param(upcast(types->empty_struct_type), "value") }, upcast(types->type_handle));
        types->type_print_bool = type_system_make_function({ make_param(upcast(types->bool_type), "value") });
        types->type_print_i32 = type_system_make_function({ make_param(upcast(types->i32_type), "value") });
        types->type_print_f32 = type_system_make_function({ make_param(upcast(types->f32_type), "value") });
        types->type_print_line = type_system_make_function({});
        types->type_print_string = type_system_make_function({ make_param(upcast(types->c_string), "value") });
        types->type_read_i32 = type_system_make_function({});
        types->type_read_f32 = type_system_make_function({});
        types->type_read_bool = type_system_make_function({});
        types->type_random_i32 = type_system_make_function({}, upcast(types->i32_type));

        types->type_set_cast_option = type_system_make_function({
                make_param(upcast(types->cast_option), "option"), 
                make_param(upcast(types->cast_mode), "cast_mode")
            } 
        );
        types->type_add_binop = type_system_make_function({
                make_param(upcast(types->c_string), "binop"), 
                make_param(upcast(types->any_type), "function"), // Type doesn't matter too much here...
                make_param(upcast(types->bool_type), "commutative", true)
            }
        );
        types->type_add_unop = type_system_make_function({
                make_param(upcast(types->c_string), "unop"), 
                make_param(upcast(types->any_type), "function") // Type doesn't matter too much here...
            }
        );
        types->type_add_array_access = type_system_make_function({
                make_param(upcast(types->any_type), "function") // Type doesn't matter too much here...
            }
        );
        types->type_add_dotcall = type_system_make_function({
                make_param(upcast(types->any_type), "function"), // Type doesn't matter too much here...
                make_param(upcast(types->bool_type), "as_member_access", true),
                make_param(upcast(types->c_string), "name", true)
            }
        );
        types->type_add_iterator = type_system_make_function({
                make_param(upcast(types->any_type), "create"),
                make_param(upcast(types->any_type), "has_next"),
                make_param(upcast(types->any_type), "next"),
                make_param(upcast(types->any_type), "value"),
            }
        );
        types->type_add_cast = type_system_make_function({
                make_param(upcast(types->any_type), "function"), // Type doesn't matter too much here...
                make_param(upcast(types->cast_mode), "cast_mode")
            }
        );
    }
}



void type_system_print(Type_System* system)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Type_System: ");
    for (int i = 0; i < system->types.size; i++)
    {
        Datatype* type = system->types[i];
        string_append_formated(&msg, "\n\t%d: ", i);
        datatype_append_to_string(&msg, type);
        if (type->memory_info.available) {
            string_append_formated(&msg, " size: %d, alignment: %d", type->memory_info.value.size, type->memory_info.value.alignment);
        }
    }
    string_append_formated(&msg, "\n");
    logg("%s", msg.characters);
}

Optional<Enum_Member> enum_type_find_member_by_value(Datatype_Enum* enum_type, int value)
{
    auto& members = enum_type->members;
    for (int i = 0; i < members.size; i++) {
        Enum_Member member = members[i];
        if (member.value == value) return optional_make_success(member);
    }
    return optional_make_failure<Enum_Member>();
}

bool types_are_equal(Datatype* a, Datatype* b) {
    return a == b;
}

bool datatype_is_unknown(Datatype* a) {
    return 
        a->base_type->type == Datatype_Type::UNKNOWN_TYPE ||
        a->base_type->type == Datatype_Type::TEMPLATE_PARAMETER || 
        a->base_type->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE ||
        a->base_type->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE_SUBTYPE;
}

bool type_size_is_unfinished(Datatype* a) {
    return !a->memory_info.available;
}

bool type_mods_is_constant(Type_Mods mods, int pointer_level)
{
    assert(pointer_level < 32, "");
    return (mods.constant_flags & (1 << pointer_level)) != 0;
}

Type_Mods type_mods_make(int pointer_level, u32 const_flags)
{
    Type_Mods result;
    result.pointer_level = pointer_level;
    result.constant_flags = const_flags;
    return result;
}

Datatype* datatype_get_non_const_type(Datatype* datatype)
{
    if (datatype->type == Datatype_Type::CONSTANT) {
        return downcast<Datatype_Constant>(datatype)->element_type;
    }
    return datatype;
}
