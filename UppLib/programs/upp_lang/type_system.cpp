#include "type_system.hpp"
#include "compiler.hpp"
#include "symbol_table.hpp"
#include "semantic_analyser.hpp"
#include "editor_analysis_info.hpp"
#include "../../utility/rich_text.hpp"

#include "syntax_colors.hpp"

void type_base_destroy(Datatype* base) 
{
    // Constants types are duplicates of the non-constant versions, and only keep references to arrays/data
    if (base->type == Datatype_Type::STRUCT) {
        auto structure = downcast<Datatype_Struct>(base);
		dynamic_array_destroy(&structure->members);
		dynamic_array_destroy(&structure->subtypes);
        dynamic_array_destroy(&structure->types_waiting_for_size_finish);
    }
    else if (base->type == Datatype_Type::ENUM) {
        auto enum_type = downcast<Datatype_Enum>(base);
        dynamic_array_destroy(&enum_type->members);
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
    case Datatype_Type::FUNCTION_POINTER: {
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
    case Datatype_Type::INVALID_TYPE: {
        string_append(string, "INVALID_TYPE");
        break;
    }
    case Datatype_Type::STRUCT_PATTERN: {
        // How do we ever print a value of this?
        string_append(string, "STRUCT_PATTERN?");
        break;
    }
    case Datatype_Type::CONSTANT: {
        auto constant = downcast<Datatype_Constant>(type);
        datatype_append_value_to_string(constant->element_type, type_system, value_ptr, string, format, indentation, local_memory, pointer_memory);
        break;
    }
    case Datatype_Type::PATTERN_VARIABLE: {
        string_append_formated(string, "PATTERN_VARIABLE?");
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
            element_count = (int)array_type->element_count;
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
    case Datatype_Type::STRUCT:
    {
        auto structure = downcast<Datatype_Struct>(type);

        if (format.show_datatype) {
			datatype_append_to_string(string, type_system, type, format.datatype_format);
            string_append(string, ".");
        }
        string_append(string, "{ ");

		while (structure->parent_struct != nullptr) {
			structure = structure->parent_struct;
		}
        while (true)
        {
            for (int i = 0; i < structure->members.size; i++)
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

                Struct_Member* mem = &structure->members[i];
                byte* mem_ptr = value_ptr + mem->offset;
                if (format.show_member_names) {
                    string_append_string(string, mem->id);
                    string_append(string, " = ");
                }
                datatype_append_value_to_string(mem->type, type_system, mem_ptr, string, format, indentation + 1, local_memory, pointer_memory);
            }

            // Check if subtype exist
            if (structure->subtypes.size == 0) {
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
            if (!local_memory.read_single_value(value_ptr + structure->tag_member.offset, &subtype_index)) {
                string_append_formated(string, "SUBTYPE_LOAD_ERROR");
                break;
            }
            subtype_index -= 1; // Tags are always stored with +1 offset
            if (subtype_index == -1 || subtype_index > structure->subtypes.size) {
                string_append_formated(string, ", INVALID_SUBTYPE #%d", subtype_index + 1);
                break;
            }
            structure = structure->subtypes[subtype_index];
            string_append_formated(string, ".%s: ", structure->name->characters);
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
    case Datatype_Type::PATTERN_VARIABLE: 
	{
        Datatype_Pattern_Variable* polymorphic = downcast<Datatype_Pattern_Variable>(signature);
		if (polymorphic->variable == nullptr) {
            Rich_Text::append_formated(text, "$?");
			break;
		}

        if (!polymorphic->is_reference) {
            Rich_Text::append_formated(text, "$");
        }
        Rich_Text::set_text_color(text, Syntax_Color::TEXT);
        Rich_Text::append_formated(text, "%s", polymorphic->variable->name->characters);
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
    case Datatype_Type::INVALID_TYPE:
        Rich_Text::append_formated(text, "Invalid-Type");
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
        Rich_Text::set_text_color(text, Syntax_Color::DATATYPE);
        auto primitive = downcast<Datatype_Primitive>(signature);
        auto memory = primitive->base.memory_info.value;
        switch (primitive->primitive_class)
        {
        case Primitive_Class::ADDRESS: 
        {
            Rich_Text::set_text_color(text, Syntax_Color::DATATYPE);
            Rich_Text::append_formated(text, "address");
            break;
        }
        case Primitive_Class::TYPE_HANDLE: {
            Rich_Text::set_text_color(text, Syntax_Color::DATATYPE);
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
        Rich_Text::set_text_color(text, Syntax_Color::DATATYPE);
        auto enum_type = downcast<Datatype_Enum>(signature);
        if (enum_type->name != 0) {
            Rich_Text::append_formated(text, enum_type->name->characters);
        }
        break;
    }
    case Datatype_Type::STRUCT_PATTERN: 
    {
        auto pattern_struct = downcast<Datatype_Struct_Pattern>(signature);
		auto poly_header = pattern_struct->instance->header;
		Rich_Text::set_text_color(text, Syntax_Color::DATATYPE);
        Rich_Text::append(text, *poly_header->name);
		Rich_Text::set_text_color(text);
        if (format.append_struct_poly_parameter_values) 
		{
			Rich_Text::append(text, "(");
			auto variable_states = pattern_struct->instance->variable_states;
			for (int i = 0; i < poly_header->pattern_variables.size; i++)
			{
				auto& variable_state = variable_states[i];
				switch (variable_state.type)
				{
				case Pattern_Variable_State_Type::SET: 
				{
					auto& constant = variable_state.options.value;
					auto string = Rich_Text::start_line_manipulation(text);
					datatype_append_value_to_string(
						constant.type, type_system, constant.memory, string, datatype_value_format_single_line(),
						0, Memory_Source(nullptr), Memory_Source(nullptr));
					Rich_Text::stop_line_manipulation(text);
					break;
				}
				case Pattern_Variable_State_Type::UNSET: {
					Rich_Text::append_formated(text, "_");
					break;
				}
				case Pattern_Variable_State_Type::PATTERN: {
					datatype_append_to_rich_text(variable_state.options.pattern_type, type_system, text, format);
					break;
				}
				default: panic("");
				}
				if (i != poly_header->pattern_variables.size - 1) {
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
		auto& members = struct_type->members;
		if (struct_type->parent_struct == nullptr)
		{
			Rich_Text::set_text_color(text, Syntax_Color::DATATYPE);
			Rich_Text::append_formated(text, struct_type->name->characters);
		}
		else
		{
			Dynamic_Array<Datatype_Struct*> parents = dynamic_array_create<Datatype_Struct*>(2);
			SCOPE_EXIT(dynamic_array_destroy(&parents));
			Datatype_Struct* iter = struct_type;
			while (iter != nullptr) {
				dynamic_array_push_back(&parents, iter);
				iter = iter->parent_struct;
			}
			for (int i = parents.size - 1; i >= 0; i -= 1) {
				Rich_Text::set_text_color(text, Syntax_Color::DATATYPE);
				Rich_Text::append_formated(text, parents[i]->name->characters);
				if (i != 0) {
					Rich_Text::append_character(text, '.');
				}
			}
		}

		// Append polymorphic instance values
		Rich_Text::set_text_color(text, Syntax_Color::TEXT);
		if (struct_type->workload != 0 && format.append_struct_poly_parameter_values) 
		{
			if (struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) 
			{
				Rich_Text::append_formated(text, "(");
				SCOPE_EXIT(Rich_Text::append_formated(text, ")"));

				auto& instance = struct_type->workload->polymorphic.instance.poly_instance;
				auto poly_header = instance->header;
				auto variable_states = instance->variable_states;
				for (int i = 0; i < poly_header->pattern_variables.size; i++)
				{
					auto& variable_state = variable_states[i];
					assert(variable_state.type == Pattern_Variable_State_Type::SET, "True for instances");

					auto& constant = variable_state.options.value;
					auto string = Rich_Text::start_line_manipulation(text);
					datatype_append_value_to_string(
						constant.type, type_system, constant.memory, string, datatype_value_format_single_line(),
						0, Memory_Source(nullptr), Memory_Source(nullptr));
					Rich_Text::stop_line_manipulation(text);

					if (i != poly_header->pattern_variables.size - 1) {
						Rich_Text::append_formated(text, ", ");
					}
				}
			}
		}
		break;
	}
	case Datatype_Type::FUNCTION_POINTER:
	{
		auto function_pointer = downcast<Datatype_Function_Pointer>(signature);
		if (function_pointer->is_optional) {
			Rich_Text::append_character(text, '?');
		}
		call_signature_append_to_rich_text(function_pointer->signature, text, &format, type_system);
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
			a.options.array_type.count_variable_type == b.options.array_type.count_variable_type &&
			a.options.array_type.size_known == b.options.array_type.size_known;
	}
	case Type_Deduplication_Type::CONSTANT: {
		return a.options.non_constant_type == b.options.non_constant_type;
	}
	case Type_Deduplication_Type::OPTIONAL: {
		return a.options.optional_child_type == b.options.optional_child_type;
	}
	case Type_Deduplication_Type::SLICE: {
		return a.options.slice_element_type == b.options.slice_element_type;
	}
	case Type_Deduplication_Type::POINTER: {
		return a.options.pointer.child_type == b.options.pointer.child_type &&
			a.options.pointer.is_optional == b.options.pointer.is_optional;
	}
	case Type_Deduplication_Type::FUNCTION_POINTER:
	{
		// Check return types
		auto& pa = a.options.function_pointer;
		auto& pb = b.options.function_pointer;
		return pa.is_optional == pb.is_optional && pa.signature == pb.signature;
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
	case Type_Deduplication_Type::ARRAY: {
		auto& infos = dedup->options.array_type;
		hash = hash_combine(hash, hash_pointer(infos.element_type));
		hash = hash_bool(hash, infos.size_known);
		hash = hash_combine(hash, hash_pointer(infos.count_variable_type));
		hash = hash_combine(hash, hash_i32(&infos.element_count));
		break;
	}
	case Type_Deduplication_Type::FUNCTION_POINTER: {
		auto& pointer = dedup->options.function_pointer;
		hash = hash_combine(hash, hash_pointer(pointer.signature));
		hash = hash_bool(hash, pointer.is_optional);
		break;
	}
	default: panic("");
	}

	return hash;
}

void internal_type_struct_destroy(Internal_Type_Struct* structure)
{
	if (structure->subtypes.data != nullptr) {
		delete[] structure->subtypes.data;
		structure->subtypes.data = 0;
	}
	if (structure->members.data != nullptr) {
		delete[] structure->members.data;
		structure->members.data = 0;
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
	case Datatype_Type::FUNCTION_POINTER:
		if (info->options.function.parameters.data != 0) {
			delete[]info->options.function.parameters.data;
			info->options.function.parameters.data = 0;
		}
		break;
	case Datatype_Type::STRUCT: {
		internal_type_struct_destroy(&info->options.structure);
		break;
	}
	default: break;
	}
	delete info;
}



Type_System type_system_create()
{
	Type_System result;
	result.deduplication_table = hashtable_create_empty<Type_Deduplication, Datatype*>(32, type_deduplication_hash, type_deduplication_is_equal);
	result.types = dynamic_array_create<Datatype*>(256);
	result.internal_type_infos = dynamic_array_create<Internal_Type_Information*>(256);
	result.register_time = 0;
	return result;
}

void type_system_destroy(Type_System* system)
{
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



// Type_System takes ownership of base_type pointer afterwards
Internal_Type_Information* type_system_register_type(Datatype* datatype, Type_System* type_system)
{
	// Finish type info
	datatype->type_handle.index = type_system->types.size;
	dynamic_array_push_back(&type_system->types, datatype);

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

	dynamic_array_push_back(&type_system->internal_type_infos, internal_info);
	return internal_info;
}

Datatype datatype_make_simple_base(Datatype_Type type, int size, int alignment) {
	Datatype result;
	result.type_handle.index = -1; // This should be the case until the type is registered
	result.type = type;
	result.contains_pattern = false;
	result.contains_partial_pattern = false;
	result.contains_pattern_variable_definition = false;
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

	auto& internal_info = type_system_register_type(upcast(result), &compiler.analysis_data->type_system)->options.primitive;
	internal_info.type = type;

	return result;
}

Datatype_Pattern_Variable* type_system_make_pattern_variable_type(Pattern_Variable* pattern_variable)
{
	Datatype_Pattern_Variable* result = new Datatype_Pattern_Variable;
	result->base = datatype_make_simple_base(Datatype_Type::PATTERN_VARIABLE, 1, 1);
	result->base.contains_pattern = true;
	result->base.contains_pattern_variable_definition = true;

	result->variable = pattern_variable;
	result->is_reference = false;
	result->mirrored_type = 0;

	auto internal_info = type_system_register_type(upcast(result), &compiler.analysis_data->type_system);
	internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

	// Create mirror type exactly the same way as normal type, but set mirror flag
	Datatype_Pattern_Variable* mirror_type = new Datatype_Pattern_Variable;
	*mirror_type = *result;
	mirror_type->mirrored_type = result;
	mirror_type->is_reference = true;
	mirror_type->base.contains_pattern_variable_definition = false;
	result->mirrored_type = mirror_type;

	internal_info = type_system_register_type(upcast(mirror_type), &compiler.analysis_data->type_system);
	internal_info->tag = Datatype_Type::UNKNOWN_TYPE;

	return result;
}

Datatype_Struct_Pattern* type_system_make_struct_pattern(
	Poly_Instance* instance, bool is_partial_pattern, bool contains_pattern_variable_definition)
{
	Datatype_Struct_Pattern* result = new Datatype_Struct_Pattern;
	result->base = datatype_make_simple_base(Datatype_Type::STRUCT_PATTERN, 1, 1);
	result->base.contains_pattern = true;
	result->base.contains_partial_pattern = is_partial_pattern; // e.g. Node(_)
	result->base.contains_pattern_variable_definition = contains_pattern_variable_definition;
	result->instance = instance;
	auto internal_info = type_system_register_type(upcast(result), &compiler.analysis_data->type_system);
	internal_info->tag = Datatype_Type::UNKNOWN_TYPE;
	return result;
}

Datatype_Pointer* type_system_make_pointer(Datatype* child_type, bool is_optional, Type_System* type_system)
{
	if (type_system == nullptr) {
		type_system = &compiler.analysis_data->type_system;
	}

	Type_Deduplication dedup;
	dedup.type = Type_Deduplication_Type::POINTER;
	dedup.options.pointer.child_type = child_type;
	dedup.options.pointer.is_optional = is_optional;

	// Check if type was already created
	{
		auto type_opt = hashtable_find_element(&type_system->deduplication_table, dedup);
		if (type_opt != nullptr) {
			Datatype* type = *type_opt;
			assert(type->type == Datatype_Type::POINTER, "");
			return downcast<Datatype_Pointer>(type);
		}
	}

	Datatype_Pointer* result = new Datatype_Pointer;
	result->base = datatype_make_simple_base(Datatype_Type::POINTER, 8, 8);
	result->base.memory_info.value.contains_reference = true;
	result->base.contains_pattern = child_type->contains_pattern;
	result->base.contains_partial_pattern = child_type->contains_partial_pattern;
	result->base.contains_pattern_variable_definition = child_type->contains_pattern_variable_definition;
	result->element_type = child_type;
	result->is_optional = is_optional;

	auto& internal_info = type_system_register_type(upcast(result), type_system)->options.pointer;
	internal_info.child_type = child_type->type_handle;
	internal_info.is_optional = is_optional;

	hashtable_insert_element(&type_system->deduplication_table, dedup, upcast(result));

	return result;
}

Datatype* type_system_make_array(Datatype* element_type, bool count_known, int element_count, Datatype_Pattern_Variable* count_variable_type)
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
	dedup.options.array_type.count_variable_type = count_variable_type;
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
	result->base.contains_pattern = element_type->contains_pattern || count_variable_type != nullptr;
	result->base.contains_partial_pattern = element_type->contains_partial_pattern;
	result->base.contains_pattern_variable_definition = element_type->contains_pattern_variable_definition;
	if (count_variable_type != nullptr) {
		result->base.contains_pattern_variable_definition =
			result->base.contains_pattern_variable_definition ||
			!count_variable_type->is_reference;
	}

	result->element_type = element_type;
	result->count_variable_type = count_variable_type;
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

	auto& internal_info = type_system_register_type(upcast(result), &type_system)->options.array;
	internal_info.element_type = element_type->type_handle;
	internal_info.size = (int)result->element_count;

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
	result->base.contains_pattern = element_type->contains_pattern;
	result->base.contains_partial_pattern = element_type->contains_partial_pattern;
	result->base.contains_pattern_variable_definition = element_type->contains_pattern_variable_definition;

	result->element_type = element_type;
	result->data_member = struct_member_make(upcast(type_system_make_pointer(element_type, true)), ids.data, nullptr, 0, nullptr);
	result->size_member = struct_member_make(upcast(types.usize), ids.size, nullptr, 8, nullptr);
	result->slice_initializer_signature_cached = nullptr;

	auto& internal_info = type_system_register_type(upcast(result), &type_system)->options.slice;
	internal_info.element_type = element_type->type_handle;

	hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));
	return result;
}

Datatype* type_system_make_constant(Datatype* datatype, Type_System* type_system)
{
	if (datatype->type == Datatype_Type::CONSTANT) return datatype;

	if (type_system == nullptr) {
		type_system = &compiler.analysis_data->type_system;
	}

	Type_Deduplication dedup;
	dedup.type = Type_Deduplication_Type::CONSTANT;
	dedup.options.non_constant_type = datatype;
	// Check if type was already created
	{
		auto type_opt = hashtable_find_element(&type_system->deduplication_table, dedup);
		if (type_opt != nullptr) {
			Datatype* type = *type_opt;
			assert(type->type == Datatype_Type::CONSTANT, "");
			return type;
		}
	}

	Datatype_Constant* result = new Datatype_Constant;
	result->element_type = datatype;
	result->base = datatype_make_simple_base(Datatype_Type::CONSTANT, 1, 1);
	result->base.contains_pattern = datatype->contains_pattern;
	result->base.contains_partial_pattern = datatype->contains_partial_pattern;
	result->base.contains_pattern_variable_definition = datatype->contains_pattern_variable_definition;

	if (datatype->memory_info.available) {
		result->base.memory_info = datatype->memory_info;
		result->base.memory_info_workload = nullptr;
	}
	else {
		result->base.memory_info.available = false;
		result->base.memory_info_workload = datatype->memory_info_workload;
		dynamic_array_push_back(&datatype->memory_info_workload->struct_type->types_waiting_for_size_finish, upcast(result));
	}

	type_system_register_type(upcast(result), type_system)->options.constant = datatype->type_handle;
	hashtable_insert_element(&type_system->deduplication_table, dedup, upcast(result));

	return upcast(result);
}

Datatype_Optional* type_system_make_optional(Datatype* child_type)
{
	auto& type_system = compiler.analysis_data->type_system;
	auto& ids = compiler.identifier_pool.predefined_ids;
	bool child_is_constant = child_type->type == Datatype_Type::CONSTANT;
	child_type = datatype_get_non_const_type(child_type);
	assert(child_type->type != Datatype_Type::POINTER, "Should be handled elsewhere i guess");

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
	result->base.contains_pattern = child_type->contains_pattern;
	result->base.contains_partial_pattern = child_type->contains_partial_pattern;
	result->base.contains_pattern_variable_definition = child_type->contains_pattern_variable_definition;

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

	auto& opt = type_system_register_type(upcast(result), &type_system)->options.optional;
	opt.child_type = child_type->type_handle;
	opt.available_offset = result->is_available_member.offset;
	hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));

	return result;
}

Datatype_Function_Pointer* type_system_make_function_pointer(Call_Signature* signature, bool is_optional)
{
	auto& type_system = compiler.analysis_data->type_system;

	Type_Deduplication dedup;
	dedup.type = Type_Deduplication_Type::FUNCTION_POINTER;
	dedup.options.function_pointer.signature = signature;
	dedup.options.function_pointer.is_optional = is_optional;

	// Check if type was already created
	{
		auto type_opt = hashtable_find_element(&type_system.deduplication_table, dedup);
		if (type_opt != nullptr) {
			Datatype* type = *type_opt;
			assert(type->type == Datatype_Type::FUNCTION_POINTER, "");
			return downcast<Datatype_Function_Pointer>(type);
		}
	}

	auto& parameters = signature->parameters;

	Datatype_Function_Pointer* result = new Datatype_Function_Pointer;
	result->base = datatype_make_simple_base(Datatype_Type::FUNCTION_POINTER, 8, 8);
	result->base.memory_info.value.contains_function_pointer = true;
	result->signature = signature;
	result->is_optional = is_optional;
	for (int i = 0; i < parameters.size; i++) {
		auto& param = parameters[i];
		assert(param.comptime_variable_index == -1, "Function pointers should only have normal arguments");
		if (param.datatype->contains_pattern) {
			result->base.contains_pattern = true;
		}
		if (param.datatype->contains_partial_pattern) {
			result->base.contains_partial_pattern = true;
		}
		if (param.datatype->contains_pattern_variable_definition) {
			result->base.contains_pattern_variable_definition = true;
		}
	}

	auto& internal_info = type_system_register_type(upcast(result), &type_system)->options.function;
	{
		internal_info.parameters.size = parameters.size;
		internal_info.has_return_type = signature->return_type_index != -1;
		if (parameters.size > 0) {
			internal_info.parameters.data = new Upp_Type_Handle[parameters.size];
			for (int i = 0; i < parameters.size; i++) {
				internal_info.parameters.data[i] = parameters[i].datatype->type_handle;
			}
		}
		else {
			internal_info.parameters.data = nullptr;
		}
	}

	hashtable_insert_element(&type_system.deduplication_table, dedup, upcast(result));
	return result;
}

Datatype_Struct* type_system_make_struct_empty(String* name, bool is_union, Datatype_Struct* parent, Workload_Structure_Body* workload)
{
	assert(name != 0, "");

	Datatype_Struct* result = new Datatype_Struct;
	result->base = datatype_make_simple_base(Datatype_Type::STRUCT, 0, 0);
	result->base.memory_info.available = false;
	result->base.memory_info_workload = workload;
	result->types_waiting_for_size_finish = dynamic_array_create<Datatype*>();

	result->name = name;
	result->workload = workload;
	result->is_union = is_union;
	result->is_extern_struct = false;

	result->initializer_signature_cached = nullptr;
	result->parent_struct = parent;
	result->subtype_index = 0;
	if (parent != nullptr) {
		dynamic_array_push_back(&parent->subtypes, result);
		result->subtype_index = parent->subtypes.size - 1;
	}
	result->members = dynamic_array_create<Struct_Member>();
	result->subtypes = dynamic_array_create<Datatype_Struct*>();
	result->definition_node = nullptr;
	if (workload != nullptr) {
		result->definition_node = upcast(workload->struct_node);
	}
	result->tag_member = struct_member_make(
		nullptr, compiler.identifier_pool.predefined_ids.tag, result, 0, result->definition_node
	);

	type_system_register_type(upcast(result), &compiler.analysis_data->type_system); // Is only initialized when memory_info is done
	return result;
}

Struct_Member struct_member_make(Datatype* type, String* id, Datatype_Struct* structure, int offset, AST::Node* definition_node)
{
	Struct_Member member;
	member.type = type;
	member.id = id;
	member.structure = structure;
	member.offset = offset;
	member.definition_node = definition_node;
	return member;
}

void struct_add_member(Datatype_Struct* structure, String* id, Datatype* member_type, AST::Node* definition_node)
{
	assert(!structure->base.memory_info.available, "Cannot add member to already finished struct");
	dynamic_array_push_back(&structure->members, struct_member_make(member_type, id, structure, 0, definition_node));
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
void internal_type_struct_mirror_recursive(Datatype_Struct* structure)
{
	Internal_Type_Information* internal_info = compiler.analysis_data->type_system.internal_type_infos[structure->base.type_handle.index];
	internal_info->size = structure->base.memory_info.value.size;
	internal_info->alignment = structure->base.memory_info.value.alignment;
	internal_info->type_handle = structure->base.type_handle;
	internal_info->tag = structure->base.type;

	Internal_Type_Struct* internal = &internal_info->options.structure;
	internal->name = upp_c_string_from_id(structure->name);
	internal->is_union = structure->is_union;

	if (structure->subtypes.size > 0) {
		internal->tag_member.name = upp_c_string_from_id(structure->tag_member.id);
		internal->tag_member.offset = structure->tag_member.offset;
		internal->tag_member.type = structure->tag_member.type->type_handle;
	}
	else {
		internal->tag_member.name = upp_c_string_empty();
		internal->tag_member.offset = 0;
		internal->tag_member.type = compiler.analysis_data->type_system.predefined_types.unknown_type->type_handle;
	}

	// Copy members
	if (structure->members.size > 0)
	{
		internal->members.data = new Internal_Type_Struct_Member[structure->members.size];
		internal->members.size = structure->members.size;
		for (int i = 0; i < structure->members.size; i++)
		{
			Internal_Type_Struct_Member* mem_i = &internal->members.data[i];
			Struct_Member& mem = structure->members[i];
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
	if (structure->subtypes.size > 0)
	{
		internal->subtypes.data = new Upp_Type_Handle[structure->subtypes.size];
		internal->subtypes.size = structure->subtypes.size;
		// Copy subtypes recursive
		for (int i = 0; i < structure->subtypes.size; i++) {
			internal->subtypes.data[i] = structure->subtypes[i]->base.type_handle;
			internal_type_struct_mirror_recursive(structure->subtypes[i]);
		}
	}
	else {
		internal->subtypes.data = nullptr;
		internal->subtypes.size = 0;
	}
}

int struct_alignment_finish_recursive(Datatype_Struct* structure)
{
	assert(!structure->base.memory_info.available, "Struct cannot be finished yet");
	structure->base.memory_info.available = true;
	auto& memory = structure->base.memory_info.value;
	memory.size = 0;
	memory.alignment = 1;
	memory.contains_function_pointer = false;
	memory.contains_padding_bytes = false;
	memory.contains_reference = false;

	for (int i = 0; i < structure->members.size; i++) {
		auto member_memory = structure->members[i].type->memory_info;
		assert(member_memory.available, "");
		memory.alignment = math_maximum(memory.alignment, member_memory.value.alignment);
	}

	for (int i = 0; i < structure->subtypes.size; i++) {
		memory.alignment = math_maximum(memory.alignment, struct_alignment_finish_recursive(structure->subtypes[i]));
	}

	return memory.alignment;
}

// Note: Struct subtypes all share one memory info, so we only use the base memory and distribute that later
void struct_size_finish_recursive(Datatype_Struct* structure, Datatype_Memory_Info& memory)
{
	// Calculate memory info/layout (Member offsets, contains pointers/padding/reference)
	bool is_union = structure->is_union;
	int offset = memory.size;
	int initial_offset = offset;
	int largest_offset = initial_offset;
	for (int i = 0; i < structure->members.size; i++)
	{
		Struct_Member* member = &structure->members[i];
		assert(member->type->memory_info.available, "");
		auto& member_memory = member->type->memory_info.value;

		// Calculate member offsets/padding
		if (is_union) { offset = initial_offset; }
		int prev_offset = offset;
		offset = math_round_next_multiple(offset, member_memory.alignment);
		if (offset != prev_offset) {
			memory.contains_padding_bytes = true;
		}
		member->offset = offset;

		offset += member_memory.size;
		if (is_union) {
			if (offset > largest_offset) {
				largest_offset = offset;
				if (i != 0) {
					memory.contains_padding_bytes = true;
				}
			}
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
	}

	if (is_union) {
		offset = largest_offset;
	}
	memory.size = offset;

	// Handle subtypes
	if (structure->subtypes.size > 0)
	{
		// Create empty tag enum
		Datatype_Enum* tag_type = nullptr;
		{
			String name = string_copy(*structure->name);
			string_append_formated(&name, "_tag");
			String* tag_enum_name = identifier_pool_lock_and_add(&compiler.identifier_pool, name);
			string_destroy(&name);
			tag_type = type_system_make_enum_empty(tag_enum_name);
		}

		// Finish subtypes
		int max_subtype_alignment = 1;
		for (int i = 0; i < structure->subtypes.size; i++) {
			max_subtype_alignment = math_maximum(max_subtype_alignment, structure->subtypes[i]->base.memory_info.value.alignment);
		}

		int subtype_start_offset = math_round_next_multiple(offset, max_subtype_alignment);
		if (subtype_start_offset != offset) {
			memory.contains_padding_bytes = true;
		}
		offset = subtype_start_offset;
		int largest_end = offset;
		for (int i = 0; i < structure->subtypes.size; i++)
		{
			auto subtype = structure->subtypes[i];
			memory.size = subtype_start_offset;
			struct_size_finish_recursive(subtype, memory);
			int end_offset = memory.size;
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
		offset = math_round_next_multiple(largest_end, tag_type->base.memory_info.value.alignment);
		if (offset != largest_end) {
			memory.contains_padding_bytes = true;
		}
		structure->tag_member.offset = offset;
		structure->tag_member.id = compiler.identifier_pool.predefined_ids.tag;
		structure->tag_member.type = upcast(tag_type);

		offset += tag_type->base.memory_info.value.size;
	}

	int prev_offset = offset;
	offset = math_round_next_multiple(offset, memory.alignment);
	if (prev_offset != offset) {
		memory.contains_padding_bytes = true;
	}
	memory.size = offset;
}

void struct_distribute_memory_info_to_subtypes(Datatype_Struct* structure, Datatype_Struct* parent_type)
{
	structure->base.memory_info = parent_type->base.memory_info;
	for (int i = 0; i < structure->subtypes.size; i++) {
		struct_distribute_memory_info_to_subtypes(structure->subtypes[i], parent_type);
	}

	// Finish all arrays/constants waiting on this struct 
	auto& type_system = compiler.analysis_data->type_system;
	for (int i = 0; i < structure->types_waiting_for_size_finish.size; i++)
	{
		Datatype* type = structure->types_waiting_for_size_finish[i];
		if (type->type == Datatype_Type::ARRAY) {
			type_system_finish_array(downcast<Datatype_Array>(type));
			continue;
		}
		else if (type->type == Datatype_Type::CONSTANT)
		{
			assert(type->type == Datatype_Type::CONSTANT, "");
			auto constant = downcast<Datatype_Constant>(type);
			assert(constant->element_type->memory_info.available, "");
			type->memory_info = constant->element_type->memory_info;
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
		else {
			panic("I don't think other types can wait for this");
		}

		auto& info_internal = type_system.internal_type_infos[type->type_handle.index];
		info_internal->size = structure->base.memory_info.value.size;
		info_internal->alignment = structure->base.memory_info.value.alignment;
	}
}

void type_system_finish_struct(Datatype_Struct* structure)
{
	auto& type_system = compiler.analysis_data->type_system;
	auto& base = structure->base;
	assert(structure->parent_struct == nullptr, "Finish struct should only be called on base");
	assert(base.type_handle.index < (u32)type_system.internal_type_infos.size, "");
	assert(type_system.internal_type_infos.size == type_system.types.size, "");
	assert(type_size_is_unfinished(upcast(structure)), "");

	// Calculate memory info/layout (Member offsets, alignment, size, contains pointer/others)
	auto& memory = structure->base.memory_info.value;
	struct_alignment_finish_recursive(structure);
	struct_size_finish_recursive(structure, memory);
	assert(memory.size % memory.alignment == 0, "Should be true by now");
	struct_distribute_memory_info_to_subtypes(structure, structure);
	internal_type_struct_mirror_recursive(structure);
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

	type_system_register_type(upcast(result), &compiler.analysis_data->type_system);
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
		type_system_register_type(types->unknown_type, system);

		types->invalid_type = new Datatype;
		*types->invalid_type = datatype_make_simple_base(Datatype_Type::INVALID_TYPE, 1, 1);
		type_system_register_type(types->invalid_type, system);
	}

	{
		types->empty_pattern_variable = upcast(type_system_make_pattern_variable_type(nullptr));
	}

	{
		types->cast_type_enum = type_system_make_enum_empty(ids.cast_type);
		auto cast_type_names = ids.cast_type_enum_values;
		for (int i = 0; i < (int)Cast_Type::NO_CAST; i += 1) {
			Enum_Member item;
			item.name = cast_type_names[i];
			item.value = i; // Note: cast-type already starts with 1
			dynamic_array_push_back(&types->cast_type_enum->members, item);
		}
		type_system_finish_enum(types->cast_type_enum);
	}

	auto make_id = [&](const char* name) -> String* { return identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(name)); };
	auto add_member_cstr = [&](Datatype_Struct* structure, const char* member_name, Datatype* member_type) {
		String* id = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(member_name));
		struct_add_member(structure, id, member_type);
		};
	auto add_struct_subtype = [&](Datatype_Struct* structure, const char* member_name) -> Datatype_Struct* {
		String* id = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(member_name));
		return type_system_make_struct_empty(id, false, structure, nullptr);
	};
	auto make_single_member_struct = [&](const char* struct_name, const char* member_name, Datatype* type) -> Datatype_Struct*
	{
		String* name_id = identifier_pool_lock_and_add(&compiler.identifier_pool, string_create_static(struct_name));
		auto result = type_system_make_struct_empty(name_id, false);
		add_member_cstr(result, member_name, type);
		type_system_finish_struct(result);
		return result;
	};

	// Empty structure
	{
		types->empty_struct_type = type_system_make_struct_empty(make_id("Empty_Struct"));
		type_system_finish_struct(types->empty_struct_type);
	}

	// Bytes
	{
		types->bytes = type_system_make_struct_empty(make_id("Bytes"));
		add_member_cstr(types->bytes, "data", upcast(types->address));
		add_member_cstr(types->bytes, "size", upcast(types->usize));
		type_system_finish_struct(types->bytes);
	}

	// String
	{
		Datatype_Struct* c_string = type_system_make_struct_empty(ids.c_string);
		Datatype_Slice* slice_type = type_system_make_slice(type_system_make_constant(upcast(types->c_char)));
		struct_add_member(c_string, ids.bytes, upcast(slice_type));
		type_system_finish_struct(c_string);
		types->c_string = upcast(c_string);
		test_type_similarity<Upp_C_String>(types->c_string);
	}

	// Any
	{
		types->any_type = type_system_make_struct_empty(make_id("Any"));
		add_member_cstr(types->any_type, "data", upcast(types->address));
		add_member_cstr(types->any_type, "type", types->type_handle);
		type_system_finish_struct(types->any_type);
		test_type_similarity<Upp_Any>(upcast(types->any_type));
	}

	// Allocator + Allocator-Functions
	{
		types->allocator = type_system_make_struct_empty(ids.allocator);
		Datatype* allocator_pointer = upcast(type_system_make_pointer(upcast(types->allocator), false));

		Call_Signature* signature = call_signature_create_empty();
		call_signature_add_parameter(signature, make_id("allocator"), type_system_make_constant(upcast(allocator_pointer)), true, false, false);
		call_signature_add_parameter(signature, make_id("size"),      type_system_make_constant(upcast(types->usize)), true, false, false);
		call_signature_add_parameter(signature, make_id("alignment"), type_system_make_constant(upcast(types->u32_type)), true, false, false);
		call_signature_add_return_type(signature, upcast(types->address));
		types->allocate_function = type_system_make_function_pointer(call_signature_register(signature), false);

		signature = call_signature_create_empty();
		call_signature_add_parameter(signature, make_id("allocator"), type_system_make_constant(upcast(allocator_pointer)), true, false, false);
		call_signature_add_parameter(signature, make_id("pointer"),   type_system_make_constant(upcast(types->address)), true, false, false);
		call_signature_add_parameter(signature, make_id("size"),      type_system_make_constant(upcast(types->usize)), true, false, false);
		types->free_function = type_system_make_function_pointer(call_signature_register(signature), false);

		signature = call_signature_create_empty();
		call_signature_add_parameter(signature, make_id("allocator"), type_system_make_constant(upcast(allocator_pointer)), true, false, false);
		call_signature_add_parameter(signature, make_id("pointer"),   type_system_make_constant(upcast(types->address)), true, false, false);
		call_signature_add_parameter(signature, make_id("old_size"),  type_system_make_constant(upcast(types->usize)), true, false, false);
		call_signature_add_parameter(signature, make_id("new_size"),  type_system_make_constant(upcast(types->usize)), true, false, false);
		call_signature_add_return_type(signature, upcast(types->bool_type));
		types->resize_function = type_system_make_function_pointer(call_signature_register(signature), false);

		add_member_cstr(types->allocator, "allocate_fn", upcast(types->allocate_function));
		add_member_cstr(types->allocator, "free_fn", upcast(types->free_function));
		add_member_cstr(types->allocator, "resize_fn", upcast(types->resize_function));
		type_system_finish_struct(types->allocator);
	}

	// Type Information
	{
		// Create type_info type
		Datatype_Struct* type_info_type = type_system_make_struct_empty(make_id("Type_Info"));
		types->type_information_type = type_info_type;
		add_member_cstr(type_info_type, "type", types->type_handle);
		add_member_cstr(type_info_type, "size", upcast(types->i32_type));
		add_member_cstr(type_info_type, "alignment", upcast(types->i32_type));

		// Add subtypes in correct order (See Datatype_Type enum)
		auto subtype_primitive = add_struct_subtype(type_info_type, "Primitive");
		auto subtype_array =     add_struct_subtype(type_info_type, "Array");
		auto subtype_slice =     add_struct_subtype(type_info_type, "Slice");
		auto subtype_struct =    add_struct_subtype(type_info_type, "Struct");
		auto subtype_enum =      add_struct_subtype(type_info_type, "Enum");
		auto subtype_function =  add_struct_subtype(type_info_type, "Function");
		auto subtype_unknown =   add_struct_subtype(type_info_type, "Unknown");
		auto subtype_invalid =   add_struct_subtype(type_info_type, "Invalid");

		auto subtype_pointer =   add_struct_subtype(type_info_type, "Pointer");
		auto subtype_constant =  add_struct_subtype(type_info_type, "Constant");
		auto subtype_optional =  add_struct_subtype(type_info_type, "Optional");

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
				Datatype_Struct* struct_member_type = type_system_make_struct_empty(make_id("Member_Info"));
				{
					add_member_cstr(struct_member_type, "name", upcast(types->c_string));
					add_member_cstr(struct_member_type, "type", types->type_handle);
					add_member_cstr(struct_member_type, "offset", upcast(types->i32_type));
					type_system_finish_struct(struct_member_type);
				}
				types->internal_member_info_type = struct_member_type;

				{
					add_member_cstr(subtype_struct, "members", upcast(type_system_make_slice(upcast(struct_member_type))));
					add_member_cstr(subtype_struct, "name", upcast(types->c_string));
					add_member_cstr(subtype_struct, "is_union", upcast(types->bool_type));
					add_member_cstr(subtype_struct, "subtypes", upcast(type_system_make_slice(types->type_handle)));
					add_member_cstr(subtype_struct, "parent_struct", types->type_handle);
					add_member_cstr(subtype_struct, "tag_member", upcast(struct_member_type));
				}
				types->internal_struct_info_type = subtype_struct;
			}
			// ENUM
			{
				{
					String* id = make_id("Enum_Member");
					Datatype_Struct* enum_member_type = type_system_make_struct_empty(id);
					add_member_cstr(enum_member_type, "name", upcast(types->c_string));
					add_member_cstr(enum_member_type, "value", upcast(types->i32_type));
					type_system_finish_struct(enum_member_type);
					add_member_cstr(subtype_enum, "members", upcast(type_system_make_slice(upcast(enum_member_type))));
					types->internal_enum_member_info_type = enum_member_type;
				}
				add_member_cstr(subtype_enum, "name", upcast(types->c_string));
			}
			// Function
			{
				add_member_cstr(subtype_function, "parameter_types", upcast(type_system_make_slice(types->type_handle)));
				add_member_cstr(subtype_function, "has_return_type", upcast(types->bool_type));
			}
		}

		// Finish type-information
		type_system_finish_struct(type_info_type);
		test_type_similarity<Internal_Type_Information>(upcast(type_info_type));
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

bool type_size_is_unfinished(Datatype* a) {
	return !a->memory_info.available;
}

Datatype* datatype_get_undecorated(
    Datatype* datatype, bool remove_pointer, bool remove_subtype, bool remove_const, bool remove_optional_pointer, bool remove_optional
)
{
	while (true)
	{
		if (remove_pointer && datatype->type == Datatype_Type::POINTER) {
			Datatype_Pointer* pointer = downcast<Datatype_Pointer>(datatype);
			if (pointer->is_optional && !remove_optional_pointer) return datatype;
			datatype = pointer->element_type;
		}
		else if (remove_subtype && datatype->type == Datatype_Type::STRUCT) 
		{
			Datatype_Struct* structure = downcast<Datatype_Struct>(datatype);
			while (structure->parent_struct != nullptr) {
				structure = structure->parent_struct;
			}
			return upcast(structure);
		}
		else if (remove_const && datatype->type == Datatype_Type::CONSTANT) {
			datatype = downcast<Datatype_Constant>(datatype)->element_type;
		}
		else if (remove_optional && datatype->type == Datatype_Type::OPTIONAL_TYPE) {
			datatype = downcast<Datatype_Optional>(datatype)->child_type;
		}
		else {
			return datatype;
		}
	}

	return datatype;
}

Datatype* datatype_get_non_const_type(Datatype* datatype, bool* out_was_const)
{
	if (out_was_const != nullptr) {
		*out_was_const = datatype->type == Datatype_Type::CONSTANT;
	}
	if (datatype->type == Datatype_Type::CONSTANT) {
		return downcast<Datatype_Constant>(datatype)->element_type;
	}
	return datatype;
}

bool datatype_is_unknown(Datatype* datatype) {
	datatype = datatype_get_undecorated(datatype, true, true, true, true);
	return datatype->type == Datatype_Type::UNKNOWN_TYPE;
}

bool datatype_is_pointer(Datatype* datatype, bool* out_is_optional)
{
	datatype = datatype_get_non_const_type(datatype);
	if (out_is_optional != nullptr) {
		*out_is_optional = false;
	}

	// Note: The decision was made to make datatype address not count as pointer
	if (datatype->type == Datatype_Type::POINTER) {
		if (out_is_optional != nullptr) {
			*out_is_optional = downcast<Datatype_Pointer>(datatype)->is_optional;
		}
		return true;
	}
	else if (datatype->type == Datatype_Type::FUNCTION_POINTER) {
		if (out_is_optional != nullptr) {
			*out_is_optional = downcast<Datatype_Function_Pointer>(datatype)->is_optional;
		}
		return true;
	}

	return false;
}

Type_Modifier_Info datatype_get_modifier_info(Datatype* datatype)
{
	Type_Modifier_Info info;
	info.initial_type = datatype;
	info.pointer_level = 0;
	info.const_flags = 0;
	info.optional_flags = 0;

	while (true)
	{
		if (datatype->type == Datatype_Type::POINTER) 
		{
			info.pointer_level += 1;
			info.const_flags    = info.const_flags << 1;
			info.optional_flags = info.optional_flags << 1;

			auto ptr_type = downcast<Datatype_Pointer>(datatype);
			if (ptr_type->is_optional) {
				info.optional_flags = info.optional_flags | 1;
			}
			datatype = ptr_type->element_type;
		}
		else if (datatype->type == Datatype_Type::FUNCTION_POINTER) 
		{
			if (downcast<Datatype_Function_Pointer>(datatype)->is_optional) {
				info.optional_flags = info.optional_flags | 1;
			}
			break;
		}
		else if (datatype->type == Datatype_Type::CONSTANT) 
		{
			datatype = downcast<Datatype_Constant>(datatype)->element_type;
			info.const_flags = info.const_flags | 1;
		}
		else if (datatype->type == Datatype_Type::OPTIONAL_TYPE) 
		{
			datatype = downcast<Datatype_Optional>(datatype)->child_type;
			info.optional_flags = info.optional_flags | 1;
			break;
		}
		else {
			break;
		}
	}

	info.base_type = datatype;
	info.struct_subtype = nullptr;
	if (datatype->type == Datatype_Type::STRUCT) {
		Datatype_Struct* structure = downcast<Datatype_Struct>(datatype);
		info.struct_subtype = structure;
		// Find parent
		while (structure->parent_struct != nullptr) {
			structure = structure->parent_struct;
		}
		info.base_type = upcast(structure);
	}

	return info;
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

