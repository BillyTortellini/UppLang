#include "type_system.hpp"
#include "compiler.hpp"
#include "symbol_table.hpp"
#include "semantic_analyser.hpp"
#include "editor_analysis_info.hpp"

#include "syntax_colors.hpp"

using AST::Structure_Type;

Function_Parameter function_parameter_make_empty() {
    Function_Parameter result;
    result.name = compiler.identifier_pool.predefined_ids.empty_string;
    result.type = compiler.analysis_data->type_system.predefined_types.unknown_type;
    result.default_value_exists = false;
    result.value_expr = nullptr;
    result.value_pass = nullptr;
    return result;
}

void struct_content_destroy(Struct_Content* content)
{
    for (int i = 0; i < content->subtypes.size; i++) {
        struct_content_destroy(content->subtypes[i]);
        delete content->subtypes[i];
    }
    dynamic_array_destroy(&content->subtypes);
    dynamic_array_destroy(&content->members);
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
        struct_content_destroy(&st->content);
        dynamic_array_destroy(&st->types_waiting_for_size_finish);
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

void datatype_append_value_to_string(
    Datatype* type, Type_System* type_system, byte* value_ptr, String* string, Datatype_Value_Format format, 
    int indentation, Memory_Source local_memory, Memory_Source pointer_memory)
{
    // Add indentation
    bool single_line = indentation >= format.max_indentation_before_single_line || format.single_line;
    auto append_indentation = [&](int extra) {
        if (!single_line) {
            for (int i = 0; i < format.indentation_spaces * (indentation + extra); i++) {
                string_append_character(string, ' ');
            }
        }
    };

    // Check for errors
    if (!type->memory_info.available) {
        string_append_formated(string, "TYPE_SIZE_NOT_FINISHED(");
        datatype_append_to_string(string, type_system, type, format.datatype_format);
        string_append(string, ")");
        return;
    }
    auto memory = type->memory_info.value;
    if (!local_memory.get_page_info(value_ptr, memory.size).readable) {
        string_append_formated(string, "MEMORY_NOT_READABLE");
        return;
    }

    // Handle special types (string, any)
    auto& types = type_system->predefined_types;
    if (types_are_equal(type, upcast(types.any_type))) 
    {
        Upp_Any any_value;
        if (!local_memory.read_single_value(value_ptr, &any_value)) {
            string_append_formated(string, "MEMORY_NOT_READABLE");
            return;
        }

        Datatype* type = nullptr;
        if (any_value.type.index < (u32) type_system->types.size) {
            type = type_system->types[any_value.type.index];
            datatype_append_to_string(string, type_system, type, format.datatype_format);
        }
        else {
            string_append_formated(string, "Any_With_Invalid_Type_Handle(#%d)", any_value.type.index);
            return;
        }

        string_append(string, "Any.{");
        if (single_line) {
            string_append(string, " ");
        }
        else {
            string_append(string, "\n");
            append_indentation(1);
        }

        datatype_append_value_to_string(type, type_system, (byte*)any_value.data, string, format, indentation + 1, pointer_memory, pointer_memory);

        if (single_line) {
            string_append(string, " }");
        }
        else {
            string_append(string, "\n");
            append_indentation(0);
            string_append(string, "}");
        }

        return;
    }
    else if (types_are_equal(type, types.c_string))
    {
        Upp_C_String c_string;
        if (!local_memory.read_single_value(value_ptr, &c_string)) {
            string_append_formated(string, "MEMORY_NOT_READABLE");
            return;
        }
        if (c_string.bytes.data == nullptr || c_string.bytes.size == 0) {
            string_append_formated(string, "c_string.{ size = %d, bytes = %p }", c_string.bytes.size, c_string.bytes.data);
            return;
        }

        String tmp = string_create();
        SCOPE_EXIT(string_destroy(&tmp));
        Dynamic_Array<u8> buffer = dynamic_array_create<u8>(math_minimum(1024ull, c_string.bytes.size));
        SCOPE_EXIT(dynamic_array_destroy(&buffer));

        pointer_memory.read_null_terminated_string((void*)c_string.bytes.data, &tmp, c_string.bytes.size + 1, false, &buffer);
        string_append_formated(string, "\"%s\"", tmp.characters);
        return;
    }

    switch (type->type)
    {
    case Datatype_Type::FUNCTION: {
        void* ptr = nullptr;
        if (!local_memory.read_single_value(value_ptr, &ptr)) {
            string_append_formated(string, "MEMORY_NOT_READABLE");
            break;
        }
        // TODO: At some point we could probably print function information here
        // E.g. add Debugger* to format struct, so we can query infos
        if (format.show_datatype) {
            string_append(string, "Function: ");
        }
        string_append_formated(string, "%p", ptr);
        break;
    }
    case Datatype_Type::UNKNOWN_TYPE: {
        string_append(string, "UNKNOWN_TYPE");
        break;
    }
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE: {
        // How do we ever print a value of this?
        string_append(string, "STRUCT_INSTANCE_TEMPLATE?");
        break;
    }
    case Datatype_Type::CONSTANT: {
        auto constant = downcast<Datatype_Constant>(type);
        datatype_append_value_to_string(constant->element_type, type_system, value_ptr, string, format, indentation, local_memory, pointer_memory);
        break;
    }
    case Datatype_Type::TEMPLATE_TYPE: {
        string_append_formated(string, "TEMPLATE_TYPE?");
        break;
    }
    case Datatype_Type::ARRAY:
    case Datatype_Type::SLICE:
    {
        int element_count = 0;
        Datatype* element_type = 0;
        byte* array_data = 0;
        if (type->type == Datatype_Type::ARRAY) 
        {
            auto array_type = downcast<Datatype_Array>(type);
            element_type = array_type->element_type;
            if (!array_type->count_known) {
                string_append_formated(string, "ARRAY_UNKNOWN_SIZE");
                return;
            }
            element_count = array_type->element_count;
            array_data = value_ptr;
        }
        else {
            Upp_Slice_Base slice;
            if (!local_memory.read_single_value(value_ptr, &slice)) {
                string_append_formated(string, "SLICE_MEMORY_NOT_AVAILABLE");
                return;
            }

            local_memory = pointer_memory; // For data-access we now need to use pointer-memory (E.g. of another process)
            auto slice_type = downcast<Datatype_Slice>(type);
            element_type = slice_type->element_type;
            array_data = (byte*) slice.data;
            element_count = slice.size;
        }

        if (!element_type->memory_info.available) {
            string_append_formated(string, "[ELEMENT_MEMORY_NOT_AVAILABLE]");
            return;
        }

        if (format.show_datatype) {
            datatype_append_to_string(string, type_system, element_type, format.datatype_format);
            string_append_formated(string, ".");
        }

        if (element_count <= 0 || array_data == nullptr) {
            string_append_formated(string, "[data = %p, size= %d]", array_data, element_count);
            return;
        }
        if (!local_memory.get_page_info(array_data, element_type->memory_info.value.size * element_count).readable) {
            string_append_formated(string, "[data = %p, size= %d, MEMORY_NOT_READABLE]", array_data, element_count);
            return;
        }

        string_append_character(string, '[');
        if (element_count > format.max_array_display_size) {
            string_append_formated(string, "#%d | ", element_count);
        }
        int display_count = math_minimum(element_count, format.max_array_display_size);
        for (int i = 0; i < display_count; i += 1) {
            if (single_line) {
                if (i != 0) {
                    string_append(string, ", ");
                }
            }
            else {
                string_append(string, "\n");
                append_indentation(1);
            }
            byte* data = array_data + i * element_type->memory_info.value.size;
            datatype_append_value_to_string(element_type, type_system, data, string, format, indentation + 1, local_memory, pointer_memory);
        }

        // Append ... if we did not display all elements
        if (element_count > format.max_array_display_size) {
            if (single_line) {
                string_append(string, ", ...");
            }
            else {
                string_append(string, "\n");
                append_indentation(1);
                string_append(string, "...");
            }
        }
        
        if (single_line) {
            string_append(string, "]");
        }
        else {
            string_append(string, "\n");
            append_indentation(0);
            string_append(string, "]");
        }
        break;
    }
    case Datatype_Type::POINTER:
    {
        Datatype_Pointer* pointer = downcast<Datatype_Pointer>(type);
        byte* data;
        if (!local_memory.read_single_value(value_ptr, &data)) {
            string_append_formated(string, "COULD_NOT_READ_ADDRESS");
            return;
        }
        if (data == 0) {
            string_append_formated(string, "null");
            return;
        }
        local_memory = pointer_memory;

        string_append_formated(string, "%p { ", data);
        if (!single_line) {
            string_append(string, "\n");
            append_indentation(indentation + 1);
        }
        datatype_append_value_to_string(pointer->element_type, type_system, data, string, format, indentation + 1, local_memory, pointer_memory);
        if (!single_line) {
            string_append(string, "\n");
            append_indentation(0);
            string_append(string, "}");
        }
        else {
            string_append(string, " }");
        }
        break;
    }
    case Datatype_Type::OPTIONAL_TYPE:
    {
        auto opt = downcast<Datatype_Optional>(type);
        byte* available_ptr = value_ptr + opt->is_available_member.offset;
        bool available = false;
        bool success = local_memory.read_single_value(available_ptr, &available);
        if (!success) {
            string_append_formated(string, "ERROR_ACCESSING_AVAILABLE_MEMBER");
            break;
        }

        if (available) {
            string_append_formated(string, "Opt-Value: ");
            datatype_append_value_to_string(
                opt->child_type, type_system, value_ptr + opt->value_member.offset, string, format, indentation + 1, local_memory, pointer_memory
            );
        }
        else {
            string_append_formated(string, "Optional Unavailable");
        }
        break;
    }
    case Datatype_Type::SUBTYPE: 
    {
        auto subtype = downcast<Datatype_Subtype>(type);
        datatype_append_value_to_string(subtype->base_type, type_system, value_ptr, string, format, indentation + 1, local_memory, pointer_memory);
        break;
    }
    case Datatype_Type::STRUCT:
    {
        auto struct_type = downcast<Datatype_Struct>(type);

        if (format.show_datatype) {
			datatype_append_to_string(string, type_system, type, format.datatype_format);
            string_append(string, ".");
        }
        string_append(string, "{ ");

        Struct_Content* content = &struct_type->content;
        while (true)
        {
            for (int i = 0; i < content->members.size; i++)
            {
                if (single_line) {
                    if (i != 0) {
                        string_append(string, ", ");
                    }
                }
                else {
                    string_append(string, "\n");
                    append_indentation(1);
                }

                Struct_Member* mem = &content->members[i];
                byte* mem_ptr = value_ptr + mem->offset;
                if (format.show_member_names) {
                    string_append_string(string, mem->id);
                    string_append(string, " = ");
                }
                datatype_append_value_to_string(mem->type, type_system, mem_ptr, string, format, indentation + 1, local_memory, pointer_memory);
            }

            // Check if subtype exist
            if (content->subtypes.size == 0) {
                break;
            }

            if (single_line) {
                string_append(string, " ");
            }
            else {
                string_append(string, "\n");
                append_indentation(1);
            }

            int subtype_index = -1;
            if (!local_memory.read_single_value(value_ptr + struct_type->content.tag_member.offset, &subtype_index)) {
                string_append_formated(string, "SUBTYPE_LOAD_ERROR");
                break;
            }
            subtype_index -= 1; // Tags are always stored with +1 offset
            if (subtype_index == -1 || subtype_index > content->subtypes.size) {
                string_append_formated(string, ", INVALID_SUBTYPE #%d", subtype_index + 1);
                break;
            }
            content = content->subtypes[subtype_index];
            string_append_formated(string, ".%s: ", content->name->characters);
        }

        if (single_line) {
            string_append(string, " }");
        }
        else {
            string_append(string, "\n");
            append_indentation(0);
            string_append(string, "}");
        }
        break;
    }
    case Datatype_Type::ENUM:
    {
        auto enum_type = downcast<Datatype_Enum>(type);
        auto& members = enum_type->members;
        if (format.show_datatype) {
            if (enum_type->name != 0) {
                string_append_formated(string, "%s", enum_type->name->characters);
            }
            else {
                string_append_formated(string, "Enum");
            }
        }
        string_append(string, ".");

        int value = 0;
        if (!local_memory.read_single_value(value_ptr, &value)) {
            string_append_formated(string, "ACCESS_ERROR");
            break;
        }

        Optional<Enum_Member> member = enum_type_find_member_by_value(enum_type, value);
        if (!member.available) {
            string_append_formated(string, "INVALID_VALUE(#%d)", value);
        }
        else {
            string_append_formated(string, member.value.name->characters);
        }
        break;
    }
    case Datatype_Type::PRIMITIVE:
    {
        auto primitive = downcast<Datatype_Primitive>(type);
        int size = primitive->base.memory_info.value.size;

        switch (primitive->primitive_class)
        {
        case Primitive_Class::ADDRESS: 
        {
            void* data = nullptr;
            if (!local_memory.read_single_value(value_ptr, &data)) {
                string_append(string, "CANNOT_ACCESS_POINTER_ADDRESS");
                break;
            }
            if (data == 0) {
                string_append_formated(string, "null");
                return;
            }
            string_append_formated(string, "Ptr %p", data);
            break;
        }
        case Primitive_Class::TYPE_HANDLE: 
        {
            Upp_Type_Handle handle;
            if (!local_memory.read_single_value(value_ptr, &handle)) {
                string_append(string, "PRIMITIVE_ACCESS_ERROR");
                break;
            }
            if (handle.index < (u32) type_system->types.size) {
                Datatype* type = type_system->types[handle.index];
                datatype_append_to_string(string, type_system, type, format.datatype_format);
            }
            else {
                string_append_formated(string, "Invalid_Type_Handle(#%d)", handle.index);
            }
            break;
        }
        case Primitive_Class::BOOLEAN: {
            bool val = false;
            if (!local_memory.read_single_value(value_ptr, &val)) {
                string_append(string, "PRIMITIVE_ACCESS_ERROR");
                break;
            }
            string_append_formated(string, "%s", val ? "TRUE" : "FALSE");
            break;
        }
        case Primitive_Class::INTEGER: {
            int value = 0;
            bool success = true;

            u8 buffer[8];
            void* buffer_ptr = &buffer[0];

            if (!local_memory.read(buffer_ptr, value_ptr, size)) {
                string_append(string, "PRIMITIVE_ACCESS_ERROR");
                break;
            }
            if (primitive->is_signed)
            {
                switch (size)
                {
                case 1: value = (i32) * (i8*)buffer_ptr; break;
                case 2: value = (i32) * (i16*)buffer_ptr; break;
                case 4: value = (i32) * (i32*)buffer_ptr; break;
                case 8: value = (i32) * (i64*)buffer_ptr; break;
                default: panic("HEY");
                }
            }
            else
            {
                switch (size)
                {
                case 1: value = (i32) * (u8*)buffer_ptr; break;
                case 2: value = (i32) * (u16*)buffer_ptr; break;
                case 4: value = (i32) * (u32*)buffer_ptr; break;
                case 8: value = (i32) * (u64*)buffer_ptr; break;
                default: panic("HEY");
                }
            }
            string_append_formated(string, "%d", value);
            break;
        }
        case Primitive_Class::FLOAT: {
            if (size == 4) {
                float value = 0.0f;
                if (!local_memory.read_single_value(value_ptr, &value)) {
                    string_append(string, "PRIMITIVE_ACCESS_ERROR");
                    break;
                }
                string_append_formated(string, "%4.3f", value);
            }
            else if (size == 8) {
                double value = 0.0f;
                if (!local_memory.read_single_value(value_ptr, &value)) {
                    string_append(string, "PRIMITIVE_ACCESS_ERROR");
                    break;
                }
                string_append_formated(string, "%4.3f", value);
            }
            else {
                panic("HEY"); 
            }
            break;
        }
        default: panic("HEY");
        }
        break;
    }
    default: panic("HEY");
    }
}

Datatype_Format datatype_format_make_default()
{
    Datatype_Format format;
    format.append_struct_poly_parameter_values = true;
    format.highlight_parameter_index = -1;
    format.remove_const_from_function_params = true;
    format.highlight_color = vec3(0.3f);
    return format;
}

Datatype_Value_Format datatype_value_format_single_line() {
    Datatype_Value_Format format;
    format.follow_pointers = false;
    format.indentation_spaces = 0;
    format.single_line = true;
    format.max_indentation_before_single_line = 0;
    format.max_array_display_size = 3;
    format.show_datatype = false;
    format.show_member_names = false;
    format.datatype_format = datatype_format_make_default();
    return format;
}

Datatype_Value_Format datatype_value_format_multi_line(int max_array_values, int max_indentation_before_single_line) 
{
    Datatype_Value_Format format;
    format.follow_pointers = true;
    format.indentation_spaces = 2;
    format.single_line = false;
    format.max_indentation_before_single_line = max_indentation_before_single_line;
    format.max_array_display_size = max_array_values;
    format.show_datatype = true;
    format.show_member_names = true;
    format.datatype_format = datatype_format_make_default();
    return format;
}

void datatype_append_to_rich_text(Datatype* signature, Type_System* type_system, Rich_Text::Rich_Text* text, Datatype_Format format)
{
    Rich_Text::set_text_color(text, Syntax_Color::TEXT);

    switch (signature->type)
    {
    case Datatype_Type::TEMPLATE_TYPE: {
        Datatype_Template* polymorphic = downcast<Datatype_Template>(signature);
        assert(polymorphic->symbol != 0, "");
        if (!polymorphic->is_reference) {
            Rich_Text::append_formated(text, "$");
        }
        Rich_Text::set_text_color(text, Syntax_Color::TEXT);
        Rich_Text::append_formated(text, "%s", polymorphic->symbol->id->characters);
        break;
    }
    case Datatype_Type::CONSTANT: {
        Datatype_Constant* constant = downcast<Datatype_Constant>(signature);
        Rich_Text::set_text_color(text, Syntax_Color::KEYWORD);
        Rich_Text::append_formated(text, "const ");
        datatype_append_to_rich_text(constant->element_type, type_system, text, format);
        break;
    }
    case Datatype_Type::ARRAY: {
        auto array_type = downcast<Datatype_Array>(signature);
        if (array_type->count_known) {
            Rich_Text::append_formated(text, "[%d]", array_type->element_count);
        }
        else {
            Rich_Text::append_formated(text, "[Unknown]");
        }
        datatype_append_to_rich_text(array_type->element_type, type_system, text, format);
        break;
    }
    case Datatype_Type::SLICE: {
        auto slice_type = downcast<Datatype_Slice>(signature);
        Rich_Text::append_formated(text, "[]");
        datatype_append_to_rich_text(slice_type->element_type, type_system, text, format);
        break;
    }
    case Datatype_Type::UNKNOWN_TYPE:
        Rich_Text::append_formated(text, "Unknown-Type");
        break;
    case Datatype_Type::POINTER: {
        auto pointer_type = downcast<Datatype_Pointer>(signature);
        if (pointer_type->is_optional) {
            Rich_Text::append_character(text, '?');
        }
        Rich_Text::append_formated(text, "*");
        datatype_append_to_rich_text(pointer_type->element_type, type_system, text, format);
        break;
    }
    case Datatype_Type::OPTIONAL_TYPE: {
        auto opt = downcast<Datatype_Optional>(signature);
        Rich_Text::append_character(text, '?');
        datatype_append_to_rich_text(opt->child_type, type_system, text, format);
        break;
    }
    case Datatype_Type::PRIMITIVE: 
    {
        Rich_Text::set_text_color(text, Syntax_Color::TYPE);
        auto primitive = downcast<Datatype_Primitive>(signature);
        auto memory = primitive->base.memory_info.value;
        switch (primitive->primitive_class)
        {
        case Primitive_Class::ADDRESS: 
        {
            Rich_Text::set_text_color(text, Syntax_Color::TYPE);
            Rich_Text::append_formated(text, "address");
            break;
        }
        case Primitive_Class::TYPE_HANDLE: {
            Rich_Text::set_text_color(text, Syntax_Color::TYPE);
            Rich_Text::append_formated(text, "Type_Handle");
            break;
        }
        case Primitive_Class::BOOLEAN: Rich_Text::append_formated(text, "bool"); break;
        case Primitive_Class::INTEGER: {
            if (memory.size == 4 && primitive->is_signed) {
                Rich_Text::append(text, "int");
            }
            else if (primitive->primitive_type == Primitive_Type::C_CHAR) {
                Rich_Text::append(text, "c_char"); break;
            }
            else if (primitive->primitive_type == Primitive_Type::ISIZE) {
                Rich_Text::append(text, "isize"); break;
            }
            else if (primitive->primitive_type == Primitive_Type::USIZE) {
                Rich_Text::append(text, "usize"); break;
            }
            else {
                Rich_Text::append_formated(text, "%s%d", (primitive->is_signed ? "i" : "u"), memory.size * 8); break;
            }
            break;
        }
        case Primitive_Class::FLOAT: {
            if (memory.size == 4) {
                Rich_Text::append(text, "float");
            }
            else {
                Rich_Text::append_formated(text, "f%d", memory.size * 8);
            }
            break;
        }
        default: panic("Heyo");
        }
        break;
    }
    case Datatype_Type::ENUM:
    {
        Rich_Text::set_text_color(text, Syntax_Color::TYPE);
        auto enum_type = downcast<Datatype_Enum>(signature);
        if (enum_type->name != 0) {
            Rich_Text::append_formated(text, enum_type->name->characters);
        }
        break;
    }
    case Datatype_Type::SUBTYPE: {
        auto subtype = downcast<Datatype_Subtype>(signature);
        datatype_append_to_rich_text(upcast(subtype->base_type), type_system, text, format);
        Rich_Text::set_text_color(text, Syntax_Color::TEXT);
        Rich_Text::append_character(text, '.');
        Rich_Text::set_text_color(text, Syntax_Color::SUBTYPE);
        Rich_Text::append(text, subtype->subtype_name->characters);
        break;
    }
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE: 
    {
        auto instance_template = downcast<Datatype_Struct_Instance_Template>(signature);
        auto structure = instance_template->struct_base->body_workload->struct_type;
		Rich_Text::set_text_color(text, Syntax_Color::TYPE);
        Rich_Text::append(text, *structure->content.name);
		Rich_Text::set_text_color(text);
        if (format.append_struct_poly_parameter_values) 
		{
			Rich_Text::append(text, "(");
			auto& instance_values = instance_template->instance_values;
			for (int i = 0; i < instance_values.size; i++)
			{
				auto& poly_value = instance_values[i];
				switch (poly_value.type)
				{
				case Poly_Value_Type::SET: 
				{
					auto& constant = poly_value.options.value;
					auto string = Rich_Text::start_line_manipulation(text);
					datatype_append_value_to_string(
						constant.type, type_system, constant.memory, string, datatype_value_format_single_line(),
						0, Memory_Source(nullptr), Memory_Source(nullptr));
					Rich_Text::stop_line_manipulation(text);
					break;
				}
				case Poly_Value_Type::UNSET: {
					datatype_append_to_rich_text(poly_value.options.unset_type, type_system, text, format);
					break;
				}
				case Poly_Value_Type::TEMPLATED_TYPE: {
					datatype_append_to_rich_text(poly_value.options.template_type, type_system, text, format);
					break;
				}
				default: panic("");
				}
				if (i != instance_values.size - 1) {
					Rich_Text::append_formated(text, ", ");
				}
			}
			Rich_Text::append(text, ")");
		}
		else {
			Rich_Text::append(text, "(..)");
		}
		break;
	}
	case Datatype_Type::STRUCT:
	{
		auto struct_type = downcast<Datatype_Struct>(signature);
		auto& members = struct_type->content.members;
		Rich_Text::set_text_color(text, Syntax_Color::TYPE);
		Rich_Text::append_formated(text, struct_type->content.name->characters);

		// Append polymorphic instance values
		Rich_Text::set_text_color(text, Syntax_Color::TEXT);
		if (struct_type->workload != 0 && format.append_struct_poly_parameter_values) {
			if (struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
				Rich_Text::append_formated(text, "(");
				SCOPE_EXIT(Rich_Text::append_formated(text, ")"));
				auto& instance = struct_type->workload->polymorphic.instance;
				auto instance_values = instance.parent->poly_header->instances[instance.instance_index].instance_values;
				for (int i = 0; i < instance_values.size; i++)
				{
					auto& poly_value = instance_values[i];
					assert(poly_value.type == Poly_Value_Type::SET, "True for instances");
					auto& constant = poly_value.options.value;

					auto string = Rich_Text::start_line_manipulation(text);
					datatype_append_value_to_string(
						constant.type, type_system, constant.memory, string, datatype_value_format_single_line(),
						0, Memory_Source(nullptr), Memory_Source(nullptr));
					Rich_Text::stop_line_manipulation(text);
					if (i != instance_values.size - 1) {
						Rich_Text::append_formated(text, ", ");
					}
				}
			}
		}
		break;
	}
	case Datatype_Type::FUNCTION:
	{
		auto function_type = downcast<Datatype_Function>(signature);
		auto& parameters = function_type->parameters;
		if (function_type->is_optional) {
			Rich_Text::append_character(text, '?');
		}
		Rich_Text::append_formated(text, "(");

		int highlight_index = format.highlight_parameter_index;
		format.highlight_parameter_index = 0;
		for (int i = 0; i < function_type->parameters.size; i++)
		{
			auto& param = function_type->parameters[i];
			auto param_type = param.type;
			if (format.remove_const_from_function_params) {
				param_type = datatype_get_non_const_type(param_type);
			}
			Rich_Text::set_text_color(text, Syntax_Color::VALUE_DEFINITION);
			if (highlight_index == i) {
				Rich_Text::set_bg(text, format.highlight_color);
			}
			Rich_Text::append_formated(text, "%s: ", param.name->characters);
			datatype_append_to_rich_text(param_type, type_system, text, format);
			Rich_Text::set_text_color(text, Syntax_Color::TEXT);
			if (highlight_index == i) {
				Rich_Text::stop_bg(text);
			}

			if (param.default_value_exists) {
				Rich_Text::append_formated(text, " = ...");
			}
			if (i != parameters.size - 1) {
				Rich_Text::append_formated(text, ", ");
			}
		}
		Rich_Text::append_formated(text, ")");
		if (function_type->return_type.available) {
			Rich_Text::append(text, " -> ");
			datatype_append_to_rich_text(function_type->return_type.value, type_system, text, format);
			Rich_Text::set_text_color(text, Syntax_Color::TEXT);
		}
		break;
	}
	default: panic("Fugg");
	}
}

void datatype_append_to_string(String* string, Type_System* type_system, Datatype* signature, Datatype_Format format)
{
	Rich_Text::Rich_Text text = Rich_Text::create(vec3(1.0f));
	SCOPE_EXIT(Rich_Text::destroy(&text));
	Rich_Text::add_line(&text);
	datatype_append_to_rich_text(signature, type_system, &text, format);
	Rich_Text::append_to_string(&text, string, 2);
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
	case Type_Deduplication_Type::OPTIONAL: {
		return a.options.optional_child_type == b.options.optional_child_type;
	}
	case Type_Deduplication_Type::SUBTYPE: {
		return
			a.options.subtype.base_type == b.options.subtype.base_type &&
			a.options.subtype.name == b.options.subtype.name &&
			a.options.subtype.index == b.options.subtype.index;
	}
	case Type_Deduplication_Type::SLICE: {
		return a.options.slice_element_type == b.options.slice_element_type;
	}
	case Type_Deduplication_Type::POINTER: {
		return a.options.pointer.child_type == b.options.pointer.child_type &&
			a.options.pointer.is_optional == b.options.pointer.is_optional;
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
			if (p_a.default_value_exists) {
				if (p_a.value_expr != p_b.value_expr || p_a.value_pass != p_b.value_pass) {
					return false;
				}
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
	case Type_Deduplication_Type::OPTIONAL: {
		hash = hash_combine(hash, hash_pointer(dedup->options.optional_child_type));
		break;
	}
	case Type_Deduplication_Type::POINTER: {
		hash = hash_combine(hash, hash_pointer(dedup->options.pointer.child_type));
		hash = hash_bool(hash, dedup->options.pointer.is_optional);
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
		hash = hash_combine(hash, hash_i32(&dedup->options.subtype.index));
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

void internal_type_struct_content_destroy(Internal_Type_Struct_Content* content)
{
	for (int i = 0; i < content->subtypes.size; i++) {
		internal_type_struct_content_destroy(&content->subtypes.data[i]);
	}
	if (content->subtypes.data != nullptr) {
		delete[] content->subtypes.data;
		content->subtypes.data = 0;
	}
	if (content->members.data != nullptr) {
		delete[] content->members.data;
		content->members.data = 0;
	}
}

void internal_type_info_destroy(Internal_Type_Information* info)
{
	switch (info->tag)
	{
	case Datatype_Type::ENUM: {
		if (info->options.enumeration.members.data != 0) {
			delete[]info->options.enumeration.members.data;
			info->options.enumeration.members.data = 0;
		}
		break;
	}
	case Datatype_Type::FUNCTION:
		if (info->options.function.parameters.data != 0) {
			delete[]info->options.function.parameters.data;
			info->options.function.parameters.data = 0;
		}
		break;
	case Datatype_Type::STRUCT: {
		internal_type_struct_content_destroy(&info->options.structure.content);
		break;
	}
	default: break;
	}
	delete info;
}

u64 subtype_index_hash(Subtype_Index** index_ptr) {
	auto& indices = (*index_ptr)->indices;
	u64 hash = hash_i32(&indices.size);
	for (int i = 0; i < indices.size; i++) {
		hash = hash_combine(hash, hash_i32(&indices[i].index));
		hash = hash_combine(hash, hash_pointer(indices[i].name));
	}
	return hash;
}

bool subtype_index_equals(Subtype_Index** a_ptr, Subtype_Index** b_ptr)
{
	auto& a = (**a_ptr).indices;
	auto& b = (**b_ptr).indices;
	if (a.size != b.size) return false;
	for (int i = 0; i < a.size; i++) {
		if (a[i].index != b[i].index || a[i].name != b[i].name) return false;
	}
	return true;
}

// Takes ownership of array
Subtype_Index* subtype_index_make(Dynamic_Array<Named_Index> indices)
{
	if (indices.size == 0) {
		return &compiler.analysis_data->type_system.subtype_base_index;
	}

	Subtype_Index index;
	index.indices = indices;
	Subtype_Index* ptr = &index;

	auto deduplicated = hashset_find(&compiler.analysis_data->type_system.subtype_index_deduplication, ptr);
	if (deduplicated != 0) {
		dynamic_array_destroy(&indices);
		return *deduplicated;
	}
	else {
		Subtype_Index* new_index = new Subtype_Index;
		new_index->indices = indices;
		hashset_insert_element(&compiler.analysis_data->type_system.subtype_index_deduplication, new_index);
		return new_index;
	}
}

Subtype_Index* subtype_index_make_subtype(Subtype_Index* base_index, String* name, int index)
{
	Dynamic_Array<Named_Index> new_indices = dynamic_array_create_copy(base_index->indices.data, base_index->indices.size);
	Named_Index named_index;
	named_index.name = name;
	named_index.index = index;
	dynamic_array_push_back(&new_indices, named_index);
	return subtype_index_make(new_indices);
}

Type_System type_system_create()
{
	Type_System result;
	result.deduplication_table = hashtable_create_empty<Type_Deduplication, Datatype*>(32, type_deduplication_hash, type_deduplication_is_equal);
	result.types = dynamic_array_create<Datatype*>(256);
	result.internal_type_infos = dynamic_array_create<Internal_Type_Information*>(256);
	result.subtype_index_deduplication = hashset_create_empty<Subtype_Index*>(1, subtype_index_hash, subtype_index_equals);
	result.register_time = 0;
	result.subtype_base_index.indices = dynamic_array_create<Named_Index>();
	return result;
}

void type_system_destroy(Type_System* system)
{
	type_system_reset(system);
	dynamic_array_destroy(&system->types);
	hashtable_destroy(&system->deduplication_table);

	{
		auto iter = hashset_iterator_create(&system->subtype_index_deduplication);
		while (hashset_iterator_has_next(&iter)) {
			Subtype_Index* index = *iter.value;
			dynamic_array_destroy(&index->indices);
			delete index;
			hashset_iterator_next(&iter);
		}
		hashset_destroy(&system->subtype_index_deduplication);
	}

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

	auto iter = hashset_iterator_create(&system->subtype_index_deduplication);
	while (hashset_iterator_has_next(&iter)) {
		Subtype_Index* index = *iter.value;
		dynamic_array_destroy(&index->indices);
		delete index;
		hashset_iterator_next(&iter);
	}
	hashset_reset(&system->subtype_index_deduplication);
}



// Type_System takes ownership of base_type pointer afterwards
Internal_Type_Information* type_system_register_type(Datatype* datatype)
{
	auto& type_system = compiler.analysis_data->type_system;

	// Finish type info
	Dynamic_Array<Named_Index> subtype_indices = dynamic_array_create<Named_Index>();
	datatype->type_handle.index = type_system.types.size;
	datatype->mods.constant_flags = 0;
	datatype->mods.optional_flags = 0;
	datatype->mods.pointer_level = 0;
	datatype->mods.is_constant = false;
	datatype->base_type = datatype;
	while (true)
	{
		if (datatype->base_type->type == Datatype_Type::POINTER)
		{
			auto ptr = downcast<Datatype_Pointer>(datatype->base_type);
			datatype->mods.pointer_level += 1;
			datatype->base_type = ptr->element_type;
			datatype->mods.constant_flags = datatype->mods.constant_flags << 1;
			datatype->mods.optional_flags = datatype->mods.optional_flags << 1;
			if (datatype->mods.is_constant) {
				datatype->mods.constant_flags = datatype->mods.constant_flags | 1;
				datatype->mods.is_constant = false;
			}
			if (ptr->is_optional) {
				datatype->mods.optional_flags = datatype->mods.optional_flags | 1;
			}
		}
		else if (datatype->base_type->type == Datatype_Type::CONSTANT)
		{
			datatype->base_type = downcast<Datatype_Constant>(datatype->base_type)->element_type;
			datatype->mods.is_constant = true;
		}
		else if (datatype->base_type->type == Datatype_Type::SUBTYPE) {
			auto subtype = downcast<Datatype_Subtype>(datatype->base_type);
			Named_Index index;
			index.index = subtype->subtype_index;
			index.name = subtype->subtype_name;
			dynamic_array_push_back(&subtype_indices, index);
			datatype->base_type = subtype->base_type;
		}
		else {
			break;
		}
	}
	assert(datatype->mods.pointer_level < 32, "This is the max value on supported pointer constant flags");
	dynamic_array_reverse_order(&subtype_indices);
	datatype->mods.subtype_index = subtype_index_make(subtype_indices);

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
	result.contains_template = false;
	result.memory_info_workload = 0;
	result.memory_info.available = true;
	result.memory_info.value.size = size;
	result.memory_info.value.alignment = alignment;
	result.memory_info.value.contains_padding_bytes = false;
	result.memory_info.value.contains_function_pointer = false;
	result.memory_info.value.contains_reference = false;
	return result;
}

Datatype_Primitive* type_system_make_primitive(Primitive_Type type, Primitive_Class primitive_class, int size, bool is_signed)
{
	Datatype_Primitive* result = new Datatype_Primitive;
	result->base = datatype_make_simple_base(Datatype_Type::PRIMITIVE, size, size);
	result->primitive_type = type;
	result->primitive_class = primitive_class;
	result->is_signed = is_signed;

	auto& internal_info = type_system_register_type(upcast(result))->options.primitive;
	internal_info.type = type;

	return result;
}

Datatype_Template* type_system_make_template_type(Symbol* symbol, int value_access_index, int defined_in_parameter_index)
{
	Datatype_Template* result = new Datatype_Template;
	result->base = datatype_make_simple_base(Datatype_Type::TEMPLATE_TYPE, 1, 1);
	result->base.contains_template = true;
	assert(symbol->type == Symbol_Type::POLYMORPHIC_VALUE, "");

	result->symbol = symbol;
	result->is_reference = false;
	result->mirrored_type = 0;
	result->defined_in_parameter_index = defined_in_parameter_index;
	result->value_access_index = value_access_index;

	auto internal_info = type_system_register_type(upcast(result));
	internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

	// Create mirror type exactly the same way as normal type, but set mirror flag
	Datatype_Template* mirror_type = new Datatype_Template;
	*mirror_type = *result;
	mirror_type->mirrored_type = result;
	mirror_type->is_reference = true;
	result->mirrored_type = mirror_type;

	internal_info = type_system_register_type(upcast(mirror_type));
	internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

	return result;
}

Datatype_Struct_Instance_Template* type_system_make_struct_instance_template(
	Workload_Structure_Polymorphic* base, Array<Poly_Value> instance_values)
{
	Datatype_Struct_Instance_Template* result = new Datatype_Struct_Instance_Template;
	result->base = datatype_make_simple_base(Datatype_Type::STRUCT_INSTANCE_TEMPLATE, 1, 1);
	result->base.contains_template = true;

	result->instance_values = instance_values;
	result->struct_base = base;
	auto internal_info = type_system_register_type(upcast(result));
	internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

	return result;
}

Datatype_Pointer* type_system_make_pointer(Datatype* child_type, bool is_optional)
{
	auto& type_system = compiler.analysis_data->type_system;

	Type_Deduplication dedup;
	dedup.type = Type_Deduplication_Type::POINTER;
	dedup.options.pointer.child_type = child_type;
	dedup.options.pointer.is_optional = is_optional;

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
	result->base.contains_template = child_type->contains_template;
	result->element_type = child_type;
	result->is_optional = is_optional;

	auto& internal_info = type_system_register_type(upcast(result))->options.pointer;
	internal_info.child_type = child_type->type_handle;
	internal_info.is_optional = is_optional;

	hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));

	return result;
}

Datatype* type_system_make_array(Datatype* element_type, bool count_known, int element_count, Datatype_Template* polymorphic_count_variable)
{
	auto& type_system = compiler.analysis_data->type_system;
	assert(!(count_known && element_count <= 0), "Hey");

	if (!count_known) {
		element_count = 1;
	}

	bool element_type_is_const = element_type->type == Datatype_Type::CONSTANT;
	element_type = datatype_get_non_const_type(element_type);

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
			assert(type->type == Datatype_Type::CONSTANT, "");
			assert(datatype_get_non_const_type(type)->type == Datatype_Type::ARRAY, "");
			if (!element_type_is_const) {
				return datatype_get_non_const_type(type);
			}
			return type;
		}
	}

	Datatype_Array* result = new Datatype_Array;
	result->base = datatype_make_simple_base(Datatype_Type::ARRAY, 1, 1);
	result->base.contains_template = element_type->contains_template;
	if (polymorphic_count_variable) {
		result->base.contains_template = true;
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

	// We always store the constant array type in deduplication, and switch path depending on element type
	Datatype* const_array_type = type_system_make_constant(upcast(result));
	hashtable_insert_element(&type_system.deduplication_table, dedup, const_array_type);

	if (!element_type_is_const) {
		return upcast(result);
	}
	return const_array_type;
}

Datatype_Slice* type_system_make_slice(Datatype* element_type)
{
	auto& type_system = compiler.analysis_data->type_system;
	auto& types = type_system.predefined_types;
	auto& ids = compiler.identifier_pool.predefined_ids;

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
	result->base.memory_info.value.contains_padding_bytes = false;
	result->base.contains_template = element_type->contains_template;

	result->element_type = element_type;
	result->data_member = struct_member_make(upcast(type_system_make_pointer(element_type, true)), ids.data, nullptr, 0, nullptr);
	result->size_member = struct_member_make(upcast(types.usize), ids.size, nullptr, 8, nullptr);

	auto& internal_info = type_system_register_type(upcast(result))->options.slice;
	internal_info.element_type = element_type->type_handle;

	hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));
	return result;
}

Datatype* type_system_make_constant(Datatype* datatype)
{
	auto& type_system = compiler.analysis_data->type_system;
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
	result->base.contains_template = datatype->contains_template;

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

Datatype_Optional* type_system_make_optional(Datatype* child_type)
{
	auto& type_system = compiler.analysis_data->type_system;
	auto& ids = compiler.identifier_pool.predefined_ids;
	bool child_is_constant = child_type->type == Datatype_Type::CONSTANT;
	child_type = datatype_get_non_const_type(child_type);

	Type_Deduplication dedup;
	dedup.type = Type_Deduplication_Type::OPTIONAL;
	dedup.options.optional_child_type = child_type;
	// Check if type was already created
	{
		auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
		if (type_opt != nullptr) {
			Datatype* type = *type_opt;
			assert(type->type == Datatype_Type::OPTIONAL_TYPE, "");
			return downcast<Datatype_Optional>(type);
		}
	}

	Datatype_Optional* result = new Datatype_Optional;
	result->child_type = child_type;
	result->base = datatype_make_simple_base(Datatype_Type::OPTIONAL_TYPE, 1, 1);
	result->base.contains_template = child_type->contains_template;

	result->value_member = struct_member_make(child_type, ids.value, nullptr, 0, nullptr);
	result->is_available_member = struct_member_make(upcast(type_system.predefined_types.bool_type), ids.is_available, nullptr, 0, nullptr);

	// Note: is_available offset will be set when type is finished
	if (child_type->memory_info.available) {
		result->base.memory_info = child_type->memory_info;
		result->base.memory_info_workload = nullptr;
		auto& mem = result->base.memory_info.value;
		result->is_available_member.offset = mem.size;
		mem.size = math_round_next_multiple(mem.size + 1, (u64)mem.alignment);
	}
	else {
		result->base.memory_info.available = false;
		result->base.memory_info_workload = child_type->memory_info_workload;
		dynamic_array_push_back(&child_type->memory_info_workload->struct_type->types_waiting_for_size_finish, upcast(result));
	}

	auto& opt = type_system_register_type(upcast(result))->options.optional;
	opt.child_type = child_type->type_handle;
	opt.available_offset = result->is_available_member.offset;
	hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));

	return result;
}

Datatype* type_system_make_subtype(Datatype* base_type, String* subtype_name, int subtype_index)
{
	auto& type_system = compiler.analysis_data->type_system;

	Type_Deduplication dedup;
	dedup.type = Type_Deduplication_Type::SUBTYPE;
	dedup.options.subtype.base_type = base_type;
	dedup.options.subtype.name = subtype_name;
	dedup.options.subtype.index = subtype_index;
	// Check if type was already created
	{
		auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
		if (type_opt != nullptr) {
			Datatype* type = *type_opt;
			assert(type->type == Datatype_Type::SUBTYPE || type->type == Datatype_Type::CONSTANT, "");
			return type;
		}
	}

	bool is_const = false;
	if (base_type->type == Datatype_Type::CONSTANT) {
		is_const = true;
		base_type = downcast<Datatype_Constant>(base_type)->element_type;
	}
	assert(base_type->type == Datatype_Type::STRUCT || base_type->type == Datatype_Type::SUBTYPE, "Base type must be struct!");

	Datatype_Subtype* result = new Datatype_Subtype;
	result->base_type = base_type;
	result->subtype_name = subtype_name;
	result->subtype_index = subtype_index;
	result->base = datatype_make_simple_base(Datatype_Type::SUBTYPE, 1, 1);
	result->base.contains_template = base_type->contains_template;

	if (base_type->memory_info.available)
	{
		result->base.memory_info = base_type->memory_info;
		result->base.memory_info_workload = nullptr;
	}
	else {
		result->base.memory_info.available = false;
		result->base.memory_info_workload = base_type->memory_info_workload;
		dynamic_array_push_back(&base_type->memory_info_workload->struct_type->types_waiting_for_size_finish, upcast(result));
	}

	auto& info_internal = type_system_register_type(upcast(result))->options.struct_subtype;
	info_internal.type = base_type->type_handle;
	info_internal.subtype_name = upp_c_string_from_id(subtype_name);

	Datatype* final_type = upcast(result);
	if (is_const) {
		final_type = type_system_make_constant(final_type);
	}
	hashtable_insert_element(&type_system.deduplication_table, dedup, final_type);
	return final_type;
}

Datatype* type_system_make_type_with_mods(Datatype* base_type, Type_Mods mods)
{
	base_type = base_type->base_type;
	for (int i = 0; i < mods.subtype_index->indices.size; i++) {
		auto index = mods.subtype_index->indices[i];
		base_type = type_system_make_subtype(base_type, index.name, index.index);
	}
	if (mods.is_constant) {
		base_type = type_system_make_constant(base_type);
	}
	for (int i = 0; i < mods.pointer_level; i++) {
		base_type = upcast(type_system_make_pointer(base_type, type_mods_pointer_is_optional(mods, i)));
		if (type_mods_pointer_is_constant(mods, i)) {
			base_type = type_system_make_constant(base_type);
		}
	}
	return base_type;
}

Datatype_Function* make_function_internal_no_dedup(Dynamic_Array<Function_Parameter> parameters, Datatype* return_type, bool is_optional)
{
	Datatype_Function* result = new Datatype_Function;
	result->base = datatype_make_simple_base(Datatype_Type::FUNCTION, 8, 8);
	result->base.memory_info.value.contains_function_pointer = true;
	result->parameters = parameters;
	result->is_optional = is_optional;
	result->non_optional_type = result;
	if (return_type != nullptr) {
		result->return_type = optional_make_success(return_type);
	}
	else {
		result->return_type = optional_make_failure<Datatype*>();
	}
	for (int i = 0; i < parameters.size; i++) {
		auto& param = parameters[i];
		if (param.type->contains_template) {
			result->base.contains_template = true;
		}
	}
	if (result->return_type.available && result->return_type.value->contains_template) {
		result->base.contains_template = true;
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
			internal_info.parameters.data = new Upp_Type_Handle[parameters.size];
			for (int i = 0; i < parameters.size; i++) {
				internal_info.parameters.data[i] = parameters[i].type->type_handle;
			}
		}
		else {
			internal_info.parameters.data = nullptr;
		}
	}

	return result;
}

Datatype_Function* type_system_make_function(Dynamic_Array<Function_Parameter> parameters, Datatype* return_type)
{
	auto& type_system = compiler.analysis_data->type_system;

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

	Datatype_Function* result = make_function_internal_no_dedup(parameters, return_type, false);
	hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));
	return result;
}

Datatype_Function* type_system_make_function(std::initializer_list<Function_Parameter> parameter_types, Datatype* return_type)
{
	Dynamic_Array<Function_Parameter> params = dynamic_array_create<Function_Parameter>((int)parameter_types.size());
	for (auto& param : parameter_types) {
		dynamic_array_push_back(&params, param);
	}
	return type_system_make_function(params, return_type);
}

Datatype_Function* type_system_make_function_optional(Datatype_Function* function)
{
	auto& type_system = compiler.analysis_data->type_system;

	if (function->is_optional) return function;

	Type_Deduplication dedup;
	dedup.type = Type_Deduplication_Type::OPTIONAL;
	dedup.options.optional_child_type = upcast(function);

	// Check if type was already created
	{
		auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
		if (type_opt != nullptr) {
			Datatype* type = *type_opt;
			assert(type->type == Datatype_Type::FUNCTION, "");
			return downcast<Datatype_Function>(type);
		}
	}

	Dynamic_Array<Function_Parameter> params = dynamic_array_create<Function_Parameter>(function->parameters.size);
	for (int i = 0; i < function->parameters.size; i++) {
		dynamic_array_push_back(&params, function->parameters[i]);
	}
	Datatype* return_type = 0;
	if (function->return_type.available) {
		return_type = function->return_type.value;
	}

	Datatype_Function* result = make_function_internal_no_dedup(params, return_type, true);
	result->non_optional_type = function;
	hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));
	return result;
}

Datatype_Struct* type_system_make_struct_empty(AST::Structure_Type struct_type, String* name, Workload_Structure_Body* workload)
{
	assert(name != 0, "");

	Datatype_Struct* result = new Datatype_Struct;
	result->base = datatype_make_simple_base(Datatype_Type::STRUCT, 0, 0);
	result->base.memory_info.available = false;
	result->base.memory_info_workload = workload;
	result->types_waiting_for_size_finish = dynamic_array_create<Datatype*>();

	result->workload = workload;
	result->struct_type = struct_type;
	result->is_extern_struct = false;

	result->content.name = name;
	result->content.structure = result;
	result->content.index = &compiler.analysis_data->type_system.subtype_base_index;
	result->content.members = dynamic_array_create<Struct_Member>();
	result->content.subtypes = dynamic_array_create<Struct_Content*>();
	result->content.definition_node = nullptr;
	if (workload != nullptr) {
		result->content.definition_node = upcast(workload->struct_node);
	}
	result->content.tag_member = struct_member_make(
		nullptr, compiler.identifier_pool.predefined_ids.tag, &result->content, 0, result->content.definition_node
	);

	type_system_register_type(upcast(result)); // Is only initialized when memory_info is done
	return result;
}

Struct_Member struct_member_make(Datatype* type, String* id, Struct_Content* content, int offset, AST::Node* definition_node)
{
	Struct_Member member;
	member.type = type;
	member.id = id;
	member.content = content;
	member.offset = offset;
	member.definition_node = definition_node;
	return member;
}

void struct_add_member(Struct_Content* content, String* id, Datatype* member_type, AST::Node* definition_node)
{
	assert(!content->structure->base.memory_info.available, "Cannot add member to already finished struct");
	dynamic_array_push_back(&content->members, struct_member_make(member_type, id, content, 0, definition_node));
}

Struct_Content* struct_add_subtype(Struct_Content* content, String* id, AST::Node* definition_node)
{
	Struct_Content* subtype = new Struct_Content;
	subtype->members = dynamic_array_create<Struct_Member>();
	subtype->subtypes = dynamic_array_create<Struct_Content*>();
	subtype->name = id;
	subtype->tag_member = struct_member_make(
		compiler.analysis_data->type_system.predefined_types.unknown_type, 
		compiler.identifier_pool.predefined_ids.tag,
		subtype,
		-1,
		definition_node
	);
	subtype->max_alignment = 0;
	subtype->structure = content->structure;
	subtype->index = subtype_index_make_subtype(content->index, id, content->subtypes.size);
	subtype->definition_node = definition_node;
	dynamic_array_push_back(&content->subtypes, subtype);
	return subtype;
}

Struct_Content* struct_content_get_parent(Struct_Content* content)
{
	if (content->index->indices.size == 0) {
		return nullptr;
	}

	Struct_Content* base = &content->structure->content;
	for (int i = 0; i < content->index->indices.size - 1; i++) { // Note: We only go to 
		int next_index = content->index->indices[i].index;
		base = base->subtypes[next_index];
	}
	return base;
}

void type_system_finish_array(Datatype_Array* array)
{
	auto& type_system = compiler.analysis_data->type_system;
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

// Returns size after offset
int struct_content_finish_recursive(Struct_Content* content, int memory_offset, int& max_global_alignment, Datatype_Memory_Info& memory)
{
	// Calculate memory info/layout (Member offsets, alignment, size, contains pointers/others)
	int max_local_alignment = 1;
	for (int i = 0; i < content->members.size; i++)
	{
		Struct_Member* member = &content->members[i];
		assert(!type_size_is_unfinished(member->type) && member->type->memory_info.available, "");
		auto& member_memory = member->type->memory_info.value;

		// Calculate member offsets/padding
		int prev_size = memory_offset;
		memory_offset = math_round_next_multiple(memory_offset, member_memory.alignment);
		if (memory_offset != prev_size) {
			memory.contains_padding_bytes = true;
		}
		member->offset = memory_offset;
		memory_offset += member_memory.size;

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
		max_local_alignment = math_maximum(member_memory.alignment, max_local_alignment);
	}

	// Handle subtypes
	if (content->subtypes.size > 0)
	{
		// Create empty tag enum
		Datatype_Enum* tag_type = nullptr;
		{
			String name = string_copy(*content->name);
			string_append_formated(&name, "_tag");
			String* tag_enum_name = identifier_pool_lock_and_add(&compiler.identifier_pool, name);
			string_destroy(&name);
			tag_type = type_system_make_enum_empty(tag_enum_name);
		}

		// Finish subtypes
		int subtype_start_offset = math_round_next_multiple(memory_offset, content->max_alignment); // All subtypes are handled as a single union
		if (subtype_start_offset != memory_offset) {
			memory.contains_padding_bytes = true;
		}
		memory_offset = subtype_start_offset;
		int largest_end = memory_offset;
		for (int i = 0; i < content->subtypes.size; i++)
		{
			auto subtype = content->subtypes[i];
			int end_offset = struct_content_finish_recursive(subtype, subtype_start_offset, max_local_alignment, memory);
			if (end_offset > largest_end) {
				largest_end = end_offset;
				if (i != 0) {
					memory.contains_padding_bytes = true;
				}
			}

			// Add to tag enum
			Enum_Member tag_member;
			tag_member.name = subtype->name;
			tag_member.value = i + 1; // Note: Enum member values start at 1 by default, if this changes the constant pool serialization also needs change
			dynamic_array_push_back(&tag_type->members, tag_member);
		}

		// Add tag-member
		type_system_finish_enum(tag_type);
		memory_offset = math_round_next_multiple(largest_end, tag_type->base.memory_info.value.alignment);
		if (memory_offset != largest_end) {
			memory.contains_padding_bytes = true;
		}
		content->tag_member.offset = memory_offset;
		content->tag_member.id = compiler.identifier_pool.predefined_ids.tag;
		content->tag_member.type = upcast(tag_type);

		memory_offset += tag_type->base.memory_info.value.size;
		memory.alignment = math_maximum(memory.alignment, tag_type->base.memory_info.value.alignment);
	}

	// As each subtype counts as a small 'struct', align to max alignment
	int prev_size = memory_offset;
	memory_offset = math_round_next_multiple(memory_offset, max_local_alignment);
	if (prev_size != memory_offset) {
		memory.contains_padding_bytes = true;
	}
	max_global_alignment = math_round_next_multiple(max_global_alignment, max_local_alignment);

	return memory_offset;
}

void struct_content_mirror_internal_info(Struct_Content* content, Internal_Type_Struct_Content* internal)
{
	internal->name = upp_c_string_from_id(content->name);

	if (content->subtypes.size > 0) {
		internal->tag_member.name = upp_c_string_from_id(content->tag_member.id);
		internal->tag_member.offset = content->tag_member.offset;
		internal->tag_member.type = content->tag_member.type->type_handle;
	}
	else {
		internal->tag_member.name = upp_c_string_empty();
		internal->tag_member.offset = 0;
		internal->tag_member.type = compiler.analysis_data->type_system.predefined_types.unknown_type->type_handle;
	}

	// Copy members
	if (content->members.size > 0)
	{
		internal->members.data = new Internal_Type_Struct_Member[content->members.size];
		internal->members.size = content->members.size;
		for (int i = 0; i < content->members.size; i++)
		{
			Internal_Type_Struct_Member* mem_i = &internal->members.data[i];
			Struct_Member& mem = content->members[i];
			mem_i->name = upp_c_string_from_id(mem.id);
			mem_i->offset = mem.offset;
			mem_i->type = mem.type->type_handle;
		}
	}
	else {
		internal->members.data = nullptr;
		internal->members.size = 0;
	}

	// Copy Subtypes recursive
	if (content->subtypes.size > 0)
	{
		internal->subtypes.data = new Internal_Type_Struct_Content[content->subtypes.size];
		internal->subtypes.size = content->subtypes.size;
		// Copy subtypes recursive
		for (int i = 0; i < content->subtypes.size; i++) {
			struct_content_mirror_internal_info(content->subtypes[i], &internal->subtypes.data[i]);
		}
	}
	else {
		internal->subtypes.data = nullptr;
		internal->subtypes.size = 0;
	}
}

int struct_content_find_max_alignment_recursive(Struct_Content* content)
{
	int max_alignment = 1;
	for (int i = 0; i < content->members.size; i++) {
		auto& member = content->members[i];
		assert(!type_size_is_unfinished(member.type), "");
		max_alignment = math_maximum(max_alignment, member.type->memory_info.value.alignment);
	}

	for (int i = 0; i < content->subtypes.size; i++) {
		max_alignment = math_maximum(max_alignment, struct_content_find_max_alignment_recursive(content->subtypes[i]));
	}
	content->max_alignment = max_alignment;
	return max_alignment;
}

void type_system_finish_struct(Datatype_Struct* structure)
{
	auto& type_system = compiler.analysis_data->type_system;
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


	// Handle unions
	if (structure->struct_type == Structure_Type::STRUCT) {
		struct_content_find_max_alignment_recursive(&structure->content); // First figure out subtype alignments (Required)
		memory.size = struct_content_finish_recursive(&structure->content, 0, memory.alignment, memory);
	}
	else
	{
		for (int i = 0; i < structure->content.members.size; i++)
		{
			auto member = &structure->content.members[i];
			assert(!type_size_is_unfinished(member->type) && member->type->memory_info.available, "");
			auto& member_memory = member->type->memory_info.value;

			// Calculate member offsets/padding
			int prev_size = memory.size;
			memory.size = math_maximum(memory.size, member_memory.size); // Unions
			if (prev_size != 0 && memory.size != prev_size) {
				memory.contains_padding_bytes = true;
			}
			member->offset = 0;

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
	}

	// Finalize alignment
	memory.size = math_maximum(memory.size, (u64)1);
	memory.size = math_round_next_multiple(memory.size, (u64)memory.alignment);



	// Update internal info to mirror struct info
	Internal_Type_Information* internal_info = type_system.internal_type_infos[base.type_handle.index];
	internal_info->size = base.memory_info.value.size;
	internal_info->alignment = base.memory_info.value.alignment;
	internal_info->type_handle = base.type_handle;
	internal_info->tag = base.type;

	internal_info->options.structure.is_union = structure->struct_type == AST::Structure_Type::UNION;
	struct_content_mirror_internal_info(&structure->content, &internal_info->options.structure.content);

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
		}
		else if (type->type == Datatype_Type::SUBTYPE)
		{
			auto subtype = downcast<Datatype_Subtype>(type);
			assert(!subtype->base.memory_info.available, "");
			subtype->base.memory_info = structure->base.memory_info;
		}
		else if (type->type == Datatype_Type::OPTIONAL_TYPE)
		{
			auto opt = downcast<Datatype_Optional>(type);
			assert(!opt->base.memory_info.available, "");
			opt->base.memory_info = opt->child_type->memory_info;
			auto& mem = opt->base.memory_info.value;
			opt->is_available_member.offset = mem.size;
			mem.size = math_round_next_multiple(mem.size + 1, (u64)mem.alignment);

			auto& info_internal = type_system.internal_type_infos[type->type_handle.index];
			info_internal->options.optional.available_offset = opt->is_available_member.offset;
		}

		auto& info_internal = type_system.internal_type_infos[type->type_handle.index];
		info_internal->size = structure->base.memory_info.value.size;
		info_internal->alignment = structure->base.memory_info.value.alignment;
	}
}

Datatype_Enum* type_system_make_enum_empty(String* name, AST::Node* definition_node)
{
	assert(name != 0, "I've decided that all enums must have names, even if you have to generate them");

	Datatype_Enum* result = new Datatype_Enum;
	result->base = datatype_make_simple_base(Datatype_Type::ENUM, 0, 0);
	result->base.memory_info = optional_make_failure<Datatype_Memory_Info>(); // Is not initialized until enum is finished
	result->name = name;
	result->members = dynamic_array_create<Enum_Member>(3);
	result->definition_node = definition_node;

	type_system_register_type(upcast(result));
	return result;
}

void type_system_finish_enum(Datatype_Enum* enum_type)
{
	auto& type_system = compiler.analysis_data->type_system;
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
		internal_info->options.enumeration.name = upp_c_string_empty();
	}
	else {
		internal_info->options.enumeration.name = upp_c_string_from_id(enum_type->name);
	}
	int member_count = members.size;
	internal_info->options.enumeration.members.size = member_count;
	internal_info->options.enumeration.members.data = new Internal_Type_Enum_Member[member_count];
	for (int i = 0; i < member_count; i++)
	{
		Enum_Member* member = &members[i];
		Internal_Type_Enum_Member* internal_member = &internal_info->options.enumeration.members.data[i];
		internal_member->name = upp_c_string_from_id(member->name);
		internal_member->value = member->value;
	}
}


template<typename T>
void test_type_similarity(Datatype* signature) {
	assert(signature->memory_info.value.size == sizeof(T) && signature->memory_info.value.alignment == alignof(T), "");
}

void type_system_add_predefined_types(Type_System* system)
{
	auto& ids = compiler.identifier_pool.predefined_ids;
	Predefined_Types* types = &system->predefined_types;

	// Primitive types
	{
		types->i8_type = type_system_make_primitive(Primitive_Type::I8, Primitive_Class::INTEGER, 1, true);
		types->i16_type = type_system_make_primitive(Primitive_Type::I16, Primitive_Class::INTEGER, 2, true);
		types->i32_type = type_system_make_primitive(Primitive_Type::I32, Primitive_Class::INTEGER, 4, true);
		types->i64_type = type_system_make_primitive(Primitive_Type::I64, Primitive_Class::INTEGER, 8, true);
		types->u8_type = type_system_make_primitive(Primitive_Type::U8, Primitive_Class::INTEGER, 1, false);
		types->u16_type = type_system_make_primitive(Primitive_Type::U16, Primitive_Class::INTEGER, 2, false);
		types->u32_type = type_system_make_primitive(Primitive_Type::U32, Primitive_Class::INTEGER, 4, false);
		types->u64_type = type_system_make_primitive(Primitive_Type::U64, Primitive_Class::INTEGER, 8, false);

		types->f32_type = type_system_make_primitive(Primitive_Type::F32, Primitive_Class::FLOAT, 4, true);
		types->f64_type = type_system_make_primitive(Primitive_Type::F64, Primitive_Class::FLOAT, 8, true);

		types->c_char = type_system_make_primitive(Primitive_Type::C_CHAR, Primitive_Class::INTEGER, 1, false);
		types->bool_type = type_system_make_primitive(Primitive_Type::BOOLEAN, Primitive_Class::BOOLEAN, 1, false);
		types->address = type_system_make_primitive(Primitive_Type::ADDRESS, Primitive_Class::ADDRESS, 8, false);
		types->isize = type_system_make_primitive(Primitive_Type::ISIZE, Primitive_Class::INTEGER, 8, true);
		types->usize = type_system_make_primitive(Primitive_Type::USIZE, Primitive_Class::INTEGER, 8, false);
		types->type_handle = upcast(type_system_make_primitive(Primitive_Type::TYPE_HANDLE, Primitive_Class::TYPE_HANDLE, 4, false));
	}

	// Other basic types
	{
		// Unknown
		types->unknown_type = new Datatype;
		*types->unknown_type = datatype_make_simple_base(Datatype_Type::UNKNOWN_TYPE, 1, 1);
		type_system_register_type(types->unknown_type);
	}

	{
		types->empty_function = type_system_make_function({}, nullptr);
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
		add_enum_member(types->cast_mode, ids.cast_mode_pointer_explicit, 4);
		add_enum_member(types->cast_mode, ids.cast_mode_pointer_inferred, 5);
		add_enum_member(types->cast_mode, ids.cast_mode_implicit, 6);
		type_system_finish_enum(types->cast_mode);

		types->cast_option = type_system_make_enum_empty(ids.cast_option);
		for (int i = 1; i < (int)Cast_Option::MAX_ENUM_VALUE; i++) {
			add_enum_member(types->cast_option, ids.cast_option_enum_values[i], i);
		}
		type_system_finish_enum(types->cast_option);
	}

	using AST::Structure_Type;
	auto make_id = [&](const char* name) -> String* { return identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(name)); };
	auto add_member_cstr = [&](Struct_Content* content, const char* member_name, Datatype* member_type) {
		String* id = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(member_name));
		struct_add_member(content, id, member_type);
		};
	auto add_struct_subtype = [&](Struct_Content* content, const char* member_name) -> Struct_Content* {
		String* id = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(member_name));
		return struct_add_subtype(content, id);
		};
	auto make_single_member_struct = [&](const char* struct_name, const char* member_name, Datatype* type) -> Datatype_Struct*
		{
			String* name_id = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(struct_name));
			auto result = type_system_make_struct_empty(AST::Structure_Type::STRUCT, name_id, 0);
			add_member_cstr(&result->content, member_name, type);
			type_system_finish_struct(result);
			return result;
		};
	auto make_param = [&](Datatype* signature, const char* name, bool default_value_exists = false) -> Function_Parameter {
		auto parameter = function_parameter_make_empty();
		parameter.name = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(name));
		parameter.type = signature;
		parameter.default_value_exists = default_value_exists;
		return parameter;
		};

	// Empty structure
	{
		types->empty_struct_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Empty_Struct"), 0);
		type_system_finish_struct(types->empty_struct_type);
	}

	// Bytes
	{
		types->bytes = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Bytes"), 0);
		add_member_cstr(&types->bytes->content, "data", upcast(types->address));
		add_member_cstr(&types->bytes->content, "size", upcast(types->usize));
		type_system_finish_struct(types->bytes);
	}

	// String
	{
		Datatype_Struct* c_string = type_system_make_struct_empty(Structure_Type::STRUCT, ids.c_string, 0);
		Datatype_Slice* slice_type = type_system_make_slice(type_system_make_constant(upcast(types->c_char)));
		struct_add_member(&c_string->content, ids.bytes, upcast(slice_type));
		type_system_finish_struct(c_string);
		types->c_string = upcast(c_string);
		test_type_similarity<Upp_C_String>(types->c_string);
	}

	// Any
	{
		types->any_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Any"), 0);
		add_member_cstr(&types->any_type->content, "data", upcast(types->address));
		add_member_cstr(&types->any_type->content, "type", types->type_handle);
		type_system_finish_struct(types->any_type);
		test_type_similarity<Upp_Any>(upcast(types->any_type));
	}

	// Allocator + Allocator-Functions
	{
		types->allocator = type_system_make_struct_empty(Structure_Type::STRUCT, ids.allocator, 0);
		Datatype* allocator_pointer = upcast(type_system_make_pointer(upcast(types->allocator), false));

		// Note: 
		// The type_system_make_constant calls were added, as parameters in Upp are 
		// without further declaration always constant, so I think this makes more sense
		types->allocate_function = type_system_make_function({
				make_param(type_system_make_constant(allocator_pointer),       "allocator"),
				make_param(type_system_make_constant(upcast(types->usize)),    "size"),
				make_param(type_system_make_constant(upcast(types->u32_type)), "alignment")
			},
			upcast(types->address)
		);
		types->free_function = type_system_make_function({
				make_param(type_system_make_constant(allocator_pointer),      "allocator"),
				make_param(type_system_make_constant(upcast(types->address)), "pointer"),
				make_param(type_system_make_constant(upcast(types->usize)),   "size")
			}
		);
		types->resize_function = type_system_make_function({
				make_param(type_system_make_constant(allocator_pointer),      "allocator"),
				make_param(type_system_make_constant(upcast(types->address)), "pointer"),
				make_param(type_system_make_constant(upcast(types->usize)),   "previous_size"),
				make_param(type_system_make_constant(upcast(types->usize)),   "new_size"),
			},
			upcast(types->bool_type)
			);

		add_member_cstr(&types->allocator->content, "allocate_fn", upcast(types->allocate_function));
		add_member_cstr(&types->allocator->content, "free_fn", upcast(types->free_function));
		add_member_cstr(&types->allocator->content, "resize_fn", upcast(types->resize_function));
		type_system_finish_struct(types->allocator);
	}

	// Hardcoded system alloc/free
	{
		types->hardcoded_system_alloc = type_system_make_function({
				make_param(type_system_make_constant(upcast(types->usize)), "size")
			},
			upcast(types->address)
		);
		types->hardcoded_system_free = type_system_make_function({
				make_param(type_system_make_constant(upcast(types->address)), "pointer")
			}
		);
	}

	// Type Information
	{
		// Create type_info type
		Datatype_Struct* type_info_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Type_Info"), 0);
		types->type_information_type = type_info_type;
		add_member_cstr(&type_info_type->content, "type", types->type_handle);
		add_member_cstr(&type_info_type->content, "size", upcast(types->i32_type));
		add_member_cstr(&type_info_type->content, "alignment", upcast(types->i32_type));

		// Add subtypes in correct order (See Datatype_Type enum)
		auto subtype_primitive = add_struct_subtype(&type_info_type->content, "Primitive");
		auto subtype_array = add_struct_subtype(&type_info_type->content, "Array");
		auto subtype_slice = add_struct_subtype(&type_info_type->content, "Slice");
		auto subtype_struct = add_struct_subtype(&type_info_type->content, "Struct");
		auto subtype_enum = add_struct_subtype(&type_info_type->content, "Enum");
		auto subtype_function = add_struct_subtype(&type_info_type->content, "Function");
		auto subtype_unknown = add_struct_subtype(&type_info_type->content, "Unknown");

		auto subtype_pointer = add_struct_subtype(&type_info_type->content, "Pointer");
		auto subtype_constant = add_struct_subtype(&type_info_type->content, "Constant");
		auto subtype_optional = add_struct_subtype(&type_info_type->content, "Optional");
		auto subtype_subtype = add_struct_subtype(&type_info_type->content, "Subtype");

		// Fill subtypes
		{
			add_member_cstr(subtype_constant, "element_type", types->type_handle);

			// Pointer
			{
				add_member_cstr(subtype_pointer, "element_type", types->type_handle);
				add_member_cstr(subtype_pointer, "is_optional", upcast(types->bool_type));
			}

			// Optional
			{
				add_member_cstr(subtype_optional, "child_type", types->type_handle);
				add_member_cstr(subtype_optional, "available_offset", upcast(types->i32_type));
			}

			// Primitive
			{
				types->primitive_type_enum = type_system_make_enum_empty(
					identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static("Primitive_Type"))
				);
				auto add_enum_member = [&](Datatype_Enum* enum_type, const char* name, int value) {
					Enum_Member item;
					item.name = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(name));
					item.value = value;
					dynamic_array_push_back(&enum_type->members, item);
					};

				add_enum_member(types->primitive_type_enum, "I8", 1);
				add_enum_member(types->primitive_type_enum, "I16", 2);
				add_enum_member(types->primitive_type_enum, "I32", 3);
				add_enum_member(types->primitive_type_enum, "I64", 4);
				add_enum_member(types->primitive_type_enum, "U8", 5);
				add_enum_member(types->primitive_type_enum, "U16", 6);
				add_enum_member(types->primitive_type_enum, "U32", 7);
				add_enum_member(types->primitive_type_enum, "U64", 8);
				add_enum_member(types->primitive_type_enum, "ADDRESS", 9);
				add_enum_member(types->primitive_type_enum, "ISIZE", 10);
				add_enum_member(types->primitive_type_enum, "USIZE", 11);
				add_enum_member(types->primitive_type_enum, "TYPE_HANDLE", 12);
				add_enum_member(types->primitive_type_enum, "C_CHAR", 13);
				add_enum_member(types->primitive_type_enum, "F32", 14);
				add_enum_member(types->primitive_type_enum, "F64", 15);
				add_enum_member(types->primitive_type_enum, "BOOL", 16);
				type_system_finish_enum(types->primitive_type_enum);

				add_member_cstr(subtype_primitive, "type", upcast(types->primitive_type_enum));
			}
			// Array
			{
				add_member_cstr(subtype_array, "element_type", types->type_handle);
				add_member_cstr(subtype_array, "size", upcast(types->i32_type));
			}
			// Slice
			{
				add_member_cstr(subtype_slice, "element_type", types->type_handle);
			}
			// Struct
			{
				Datatype_Struct* struct_member_type = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Member_Info"), 0);
				{
					add_member_cstr(&struct_member_type->content, "name", upcast(types->c_string));
					add_member_cstr(&struct_member_type->content, "type", types->type_handle);
					add_member_cstr(&struct_member_type->content, "offset", upcast(types->i32_type));
					type_system_finish_struct(struct_member_type);
				}
				types->internal_member_info_type = struct_member_type;

				Datatype_Struct* internal_content = type_system_make_struct_empty(Structure_Type::STRUCT, make_id("Struct_Content"), 0);
				{
					add_member_cstr(&internal_content->content, "members", upcast(type_system_make_slice(upcast(struct_member_type))));
					add_member_cstr(&internal_content->content, "subtypes", upcast(type_system_make_slice(upcast(internal_content))));
					add_member_cstr(&internal_content->content, "tag_member", upcast(struct_member_type));
					add_member_cstr(&internal_content->content, "name", upcast(types->c_string));
				}
				type_system_finish_struct(internal_content);
				test_type_similarity<Internal_Type_Struct_Content>(upcast(internal_content));
				types->internal_struct_content_type = internal_content;

				add_member_cstr(subtype_struct, "content", upcast(internal_content));
				add_member_cstr(subtype_struct, "is_union", upcast(types->bool_type));
			}
			// Subtype
			{
				add_member_cstr(subtype_subtype, "base_type", types->type_handle);
				add_member_cstr(subtype_subtype, "name", upcast(types->c_string));
				add_member_cstr(subtype_subtype, "index", upcast(types->i32_type));
			}
			// ENUM
			{
				{
					String* id = make_id("Enum_Member");
					Datatype_Struct* enum_member_type = type_system_make_struct_empty(Structure_Type::STRUCT, id, 0);
					add_member_cstr(&enum_member_type->content, "name", upcast(types->c_string));
					add_member_cstr(&enum_member_type->content, "value", upcast(types->i32_type));
					type_system_finish_struct(enum_member_type);
					add_member_cstr(subtype_enum, "members", upcast(type_system_make_slice(upcast(enum_member_type))));
				}
				add_member_cstr(subtype_enum, "name", upcast(types->c_string));
			}
			// Function
			{
				add_member_cstr(subtype_function, "parameter_types", upcast(type_system_make_slice(types->type_handle)));
				add_member_cstr(subtype_function, "return_type", types->type_handle);
				add_member_cstr(subtype_function, "has_return_type", upcast(types->bool_type));
			}
		}

		// Finish type-information
		type_system_finish_struct(type_info_type);
		test_type_similarity<Internal_Type_Information>(upcast(type_info_type));
	}

	// Hardcoded Functions
	{
		auto& ts = *system;

		types->type_assert = type_system_make_function({ make_param(upcast(types->bool_type), "condition") });
		types->type_type_of = type_system_make_function({ make_param(upcast(types->empty_struct_type), "value") }, upcast(types->type_handle));
		types->type_size_of = type_system_make_function({ make_param(types->type_handle, "type") }, upcast(types->usize));
		types->type_align_of = type_system_make_function({ make_param(types->type_handle, "type") }, upcast(types->u32_type));
		types->type_panic = type_system_make_function({}, nullptr);
		types->type_type_info = type_system_make_function(
			{ make_param(upcast(types->type_handle), "type_handle") },
			upcast(type_system_make_pointer(upcast(types->type_information_type)))
		);
		types->hardcode_struct_tag_fn = type_system_make_function({ make_param(upcast(types->empty_struct_type), "value") }, nullptr);
		types->hardcode_return_type_fn = type_system_make_function({}, types->type_handle);

		types->type_memory_copy = type_system_make_function({
				make_param(upcast(types->address), "destination"),
				make_param(upcast(types->address), "source"),
				make_param(upcast(types->usize),   "size")
			},
			nullptr
		);
		types->type_memory_zero = type_system_make_function({
				make_param(upcast(types->address), "destination"),
				make_param(upcast(types->usize), "size")
			},
			nullptr
		);
		types->type_memory_compare = type_system_make_function({
				make_param(upcast(types->address), "a"),
				make_param(upcast(types->address), "b"),
				make_param(upcast(types->usize), "size")
			},
			upcast(types->bool_type)
		);

		types->type_print_bool = type_system_make_function({ make_param(upcast(types->bool_type), "value") });
		types->type_print_i32 = type_system_make_function({ make_param(upcast(types->i32_type), "value") });
		types->type_print_f32 = type_system_make_function({ make_param(upcast(types->f32_type), "value") });
		types->type_print_line = type_system_make_function({});
		types->type_print_string = type_system_make_function({ make_param(upcast(types->c_string), "value") });
		types->type_read_i32 = type_system_make_function({});
		types->type_read_f32 = type_system_make_function({});
		types->type_read_bool = type_system_make_function({});
		types->type_random_i32 = type_system_make_function({}, upcast(types->i32_type));

		types->type_bitwise_unop = type_system_make_function({ make_param(upcast(types->i32_type), "value") }, upcast(types->i32_type));
		types->type_bitwise_binop = type_system_make_function(
			{ make_param(upcast(types->i32_type), "a"), make_param(upcast(types->i32_type), "b") },
			upcast(types->i32_type)
		);

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
		datatype_append_to_string(&msg, system, type);
		if (type->memory_info.available) {
			string_append_formated(&msg, " size: %d, alignment: %d", type->memory_info.value.size, type->memory_info.value.alignment);
		}
	}
	string_append_formated(&msg, "\n");
	logg("%s", msg.characters);
}

Optional<Enum_Member> enum_type_find_member_by_value(Datatype_Enum* enum_type, int value)
{
	if (enum_type->values_are_sequential) {
		int index = value - enum_type->sequence_start_value;
		if (index < 0 || index >= enum_type->members.size) {
			return optional_make_failure<Enum_Member>();
		}
		return optional_make_success(enum_type->members[index]);
	}

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
	return a->base_type->type == Datatype_Type::UNKNOWN_TYPE;
}

bool type_size_is_unfinished(Datatype* a) {
	return !a->memory_info.available;
}

bool type_mods_pointer_is_constant(Type_Mods mods, int pointer_level)
{
	assert(pointer_level < 32, "");
	return (mods.constant_flags & (1 << pointer_level)) != 0;
}

bool type_mods_pointer_is_optional(Type_Mods mods, int pointer_level)
{
	assert(pointer_level < 32, "");
	return (mods.optional_flags & (1 << pointer_level)) != 0;
}

bool type_mods_are_equal(const Type_Mods& a, const Type_Mods& b)
{
	return
		a.constant_flags == b.constant_flags &&
		a.is_constant == b.is_constant &&
		a.optional_flags == b.optional_flags &&
		a.pointer_level == b.pointer_level &&
		a.subtype_index == b.subtype_index;
}

Struct_Content* type_mods_get_subtype(Datatype_Struct* structure, Type_Mods mods, int max_level)
{
	Struct_Content* content = &structure->content;
	for (int i = 0; i < mods.subtype_index->indices.size && (max_level == -1 || i < max_level); i++) {
		content = content->subtypes[mods.subtype_index->indices[i].index];
	}
	return content;
}

Type_Mods type_mods_make(bool is_constant, int pointer_level, u32 const_flags, u32 optional_flags, Subtype_Index* subtype)
{
	Type_Mods result;
	result.is_constant = is_constant;
	result.pointer_level = pointer_level;
	result.constant_flags = const_flags;
	result.optional_flags = optional_flags;
	result.subtype_index = subtype;
	if (subtype == 0) {
		result.subtype_index = &compiler.analysis_data->type_system.subtype_base_index;
	}
	return result;
}

Datatype* datatype_get_non_const_type(Datatype* datatype)
{
	if (datatype->type == Datatype_Type::CONSTANT) {
		return downcast<Datatype_Constant>(datatype)->element_type;
	}
	return datatype;
}

bool datatype_is_pointer(Datatype* datatype, bool* out_is_optional)
{
	datatype = datatype_get_non_const_type(datatype);
	if (out_is_optional != nullptr) {
		*out_is_optional = false;
	}

	// Note: The decision was made to make datatype address not count as pointer

	if (datatype->mods.pointer_level > 0) {
		if (out_is_optional != nullptr) {
			*out_is_optional = type_mods_pointer_is_optional(datatype->mods, datatype->mods.pointer_level - 1);
		}
		return true;
	}
	else if (datatype->type == Datatype_Type::FUNCTION) {
		if (out_is_optional != nullptr) {
			*out_is_optional = downcast<Datatype_Function>(datatype)->is_optional;
		}
		return true;
	}

	return false;
}

Upp_C_String upp_c_string_from_id(String* id)
{
	Upp_C_String result;
	result.bytes.size = id->size;
	result.bytes.data = (const u8*)id->characters;
	return result;
}

Upp_C_String upp_c_string_empty() {
	Upp_C_String result;
	result.bytes.data = (const u8*)"";
	result.bytes.size = 0;
	return result;
}
