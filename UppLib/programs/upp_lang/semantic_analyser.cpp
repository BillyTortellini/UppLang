#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "compiler.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"

bool PRINT_DEPENDENCIES = false;
/*
    TYPE_SIGNATURE
*/
String primitive_type_to_string(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN: return string_create_static("BOOL");
    case Primitive_Type::FLOAT_32: return string_create_static("FLOAT_32");
    case Primitive_Type::FLOAT_64: return string_create_static("FLOAT_64");
    case Primitive_Type::SIGNED_INT_8: return string_create_static("SIGNED_INT_8");
    case Primitive_Type::SIGNED_INT_16: return string_create_static("SIGNED_INT_16");
    case Primitive_Type::SIGNED_INT_32: return string_create_static("SIGNED_INT_32");
    case Primitive_Type::SIGNED_INT_64: return string_create_static("SIGNED_INT_64");
    case Primitive_Type::UNSIGNED_INT_8: return string_create_static("UNSIGNED_INT_8");
    case Primitive_Type::UNSIGNED_INT_16: return string_create_static("UNSIGNED_INT_16");
    case Primitive_Type::UNSIGNED_INT_32: return string_create_static("UNSIGNED_INT_32");
    case Primitive_Type::UNSIGNED_INT_64: return string_create_static("UNSIGNED_iNT_64");
    }
    return string_create_static("INVALID_VALUE_TYPE_ENUM");
}

bool primitive_type_is_integer(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN:
    case Primitive_Type::FLOAT_32:
    case Primitive_Type::FLOAT_64: return false;
    case Primitive_Type::SIGNED_INT_8:
    case Primitive_Type::SIGNED_INT_16:
    case Primitive_Type::SIGNED_INT_32:
    case Primitive_Type::SIGNED_INT_64:
    case Primitive_Type::UNSIGNED_INT_8:
    case Primitive_Type::UNSIGNED_INT_16:
    case Primitive_Type::UNSIGNED_INT_32:
    case Primitive_Type::UNSIGNED_INT_64: return true;
    }
    panic("Shit");
    return false;
}

bool primitive_type_is_signed(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN: return false;
    case Primitive_Type::FLOAT_32:
    case Primitive_Type::FLOAT_64: return true;
    case Primitive_Type::SIGNED_INT_8:
    case Primitive_Type::SIGNED_INT_16:
    case Primitive_Type::SIGNED_INT_32:
    case Primitive_Type::SIGNED_INT_64: return true;
    case Primitive_Type::UNSIGNED_INT_8:
    case Primitive_Type::UNSIGNED_INT_16:
    case Primitive_Type::UNSIGNED_INT_32:
    case Primitive_Type::UNSIGNED_INT_64: return false;
    }
    panic("Shit");
    return false;
}

bool primitive_type_is_float(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN: return false;
    case Primitive_Type::FLOAT_32:
    case Primitive_Type::FLOAT_64: return true;
    case Primitive_Type::SIGNED_INT_8:
    case Primitive_Type::SIGNED_INT_16:
    case Primitive_Type::SIGNED_INT_32:
    case Primitive_Type::SIGNED_INT_64:
    case Primitive_Type::UNSIGNED_INT_8:
    case Primitive_Type::UNSIGNED_INT_16:
    case Primitive_Type::UNSIGNED_INT_32:
    case Primitive_Type::UNSIGNED_INT_64: return false;
    }
    panic("Shit");
    return false;
}

Type_Signature type_signature_make_error() 
{
    Type_Signature result;
    result.type = Signature_Type::ERROR_TYPE;
    result.size_in_bytes = 0;
    result.alignment_in_bytes = 1;
    result.struct_name_handle = -1;
    result.return_type = 0;
    return result;
}

void type_signature_destroy(Type_Signature* sig) {
    if (sig->type == Signature_Type::FUNCTION)
        dynamic_array_destroy(&sig->parameter_types);
    if (sig->type == Signature_Type::STRUCT)
        dynamic_array_destroy(&sig->member_types);
}

Type_Signature type_signature_make_primitive(Primitive_Type type) 
{
    Type_Signature result;
    result.type = Signature_Type::PRIMITIVE;
    result.primitive_type = type;
    switch (type) 
    {
    case Primitive_Type::BOOLEAN: result.size_in_bytes = 1; result.alignment_in_bytes = 1; break;
    case Primitive_Type::SIGNED_INT_8: result.size_in_bytes = 1; result.alignment_in_bytes = 1; break;
    case Primitive_Type::SIGNED_INT_16: result.size_in_bytes = 2; result.alignment_in_bytes = 2; break;
    case Primitive_Type::SIGNED_INT_32: result.size_in_bytes = 4; result.alignment_in_bytes = 4; break;
    case Primitive_Type::SIGNED_INT_64: result.size_in_bytes = 8; result.alignment_in_bytes = 8; break;
    case Primitive_Type::UNSIGNED_INT_8: result.size_in_bytes = 1; result.alignment_in_bytes = 1; break;
    case Primitive_Type::UNSIGNED_INT_16: result.size_in_bytes = 2; result.alignment_in_bytes = 2; break;
    case Primitive_Type::UNSIGNED_INT_32: result.size_in_bytes = 4; result.alignment_in_bytes = 4; break;
    case Primitive_Type::UNSIGNED_INT_64: result.size_in_bytes = 8; result.alignment_in_bytes = 8; break;
    case Primitive_Type::FLOAT_32: result.size_in_bytes = 4; result.alignment_in_bytes = 4; break;
    case Primitive_Type::FLOAT_64: result.size_in_bytes = 8; result.alignment_in_bytes = 8; break;
    default: panic("Wehl scheit 2!\n");
    }
    return result;
}

void type_signature_append_to_string_with_children(String* string, Type_Signature* signature, bool print_child, Semantic_Analyser* analyser)
{
    switch (signature->type)
    {
    case Signature_Type::TEMPLATE_TYPE:
        string_append_formated(string, "TEMPLATE_TYPE");
        break;
    case Signature_Type::VOID_TYPE:
        string_append_formated(string, "VOID");
        break;
    case Signature_Type::ARRAY_SIZED:
        string_append_formated(string, "[%d]", signature->array_element_count);
        type_signature_append_to_string_with_children(string, signature->child_type, print_child, analyser);
        break;
    case Signature_Type::ARRAY_UNSIZED:
        string_append_formated(string, "[]");
        type_signature_append_to_string_with_children(string, signature->child_type, print_child, analyser);
        break;
    case Signature_Type::ERROR_TYPE:
        string_append_formated(string, "ERROR-Type");
        break;
    case Signature_Type::POINTER:
        string_append_formated(string, "*");
        type_signature_append_to_string_with_children(string, signature->child_type, print_child, analyser);
        break;
    case Signature_Type::PRIMITIVE:
        String s = primitive_type_to_string(signature->primitive_type);
        string_append_string(string, &s);
        break;
    case Signature_Type::STRUCT: 
    {
        if (signature->struct_name_handle >= 0 && signature->struct_name_handle < analyser->compiler->identifier_pool->identifiers.size) {
            string_append_formated(string, identifier_pool_index_to_string(analyser->compiler->identifier_pool, signature->struct_name_handle).characters);
        }
        else {
            string_append_formated(string, "STRUCT");
        }
        if (print_child)
        {
            string_append_formated(string, "{");
            for (int i = 0; i < signature->member_types.size && print_child; i++) {
                type_signature_append_to_string_with_children(string, signature->member_types[i].type, false, analyser);
                if (i != signature->parameter_types.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
            string_append_formated(string, "}");
        }
        break;
    }
    case Signature_Type::FUNCTION:
        string_append_formated(string, "(");
        for (int i = 0; i < signature->parameter_types.size; i++) {
            type_signature_append_to_string_with_children(string, signature->parameter_types[i], print_child, analyser);
            if (i != signature->parameter_types.size - 1) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, ") -> ");
        type_signature_append_to_string_with_children(string, signature->return_type, print_child, analyser);
    }
}

void type_signature_append_value_to_string(Type_Signature* type, byte* value_ptr, String* string)
{
    if (!memory_is_readable(value_ptr, type->size_in_bytes)) {
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
    case Signature_Type::ARRAY_SIZED:
    {
        string_append_formated(string, "[#%d ", type->array_element_count);
        if (type->array_element_count > 4) {
            string_append_formated(string, " ...]");
            return;
        }
        for (int i = 0; i < type->array_element_count; i++) {
            byte* element_ptr = value_ptr + (i * type->child_type->size_in_bytes);
            type_signature_append_value_to_string(type->child_type, element_ptr, string);
            string_append_formated(string, ", ");
        }
        string_append_formated(string, "]");
        break;
    }
    case Signature_Type::ARRAY_UNSIZED:
    {
        byte* data_ptr = *((byte**)value_ptr);
        int element_count = *((int*)(value_ptr + 8));
        string_append_formated(string, "[#%d ", element_count);
        if (!memory_is_readable(data_ptr, element_count * type->child_type->size_in_bytes)) {
            string_append_formated(string, "Memory not readable");
        }
        else {
            if (element_count > 4) {
                string_append_formated(string, " ...]");
                return;
            }
            for (int i = 0; i < element_count; i++) {
                byte* element_ptr = data_ptr + (i * type->child_type->size_in_bytes);
                type_signature_append_value_to_string(type->child_type, element_ptr, string);
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
        if (!memory_is_readable(data_ptr, type->child_type->size_in_bytes)) {
            string_append_formated(string, "(UNREADABLE)");
        }
        break;
    }
    case Signature_Type::STRUCT:
    {
        string_append_formated(string, "Struct: {");
        for (int i = 0; i < type->member_types.size; i++) {
            Struct_Member* mem = &type->member_types[i];
            byte* mem_ptr = value_ptr + mem->offset;
            if (memory_is_readable(mem_ptr, mem->type->size_in_bytes)) {
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
        switch (type->primitive_type)
        {
        case Primitive_Type::BOOLEAN: {
            bool val = *(bool*)value_ptr;
            string_append_formated(string, "%s", val ? "TRUE" : "FALSE");
            break;
        }
        case Primitive_Type::SIGNED_INT_8: {
            int val = (i32) * (i8*)value_ptr;
            string_append_formated(string, "%d", val);
            break;
        }
        case Primitive_Type::SIGNED_INT_16: {
            int val = (i32) * (i16*)value_ptr;
            string_append_formated(string, "%d", val);
            break;
        }
        case Primitive_Type::SIGNED_INT_32: {
            int val = (i32) * (i32*)value_ptr;
            string_append_formated(string, "%d", val);
            break;
        }
        case Primitive_Type::SIGNED_INT_64: {
            int val = (i32) * (i64*)value_ptr;
            string_append_formated(string, "%d", val);
            break;
        }
        case Primitive_Type::UNSIGNED_INT_8: {
            int val = (i32) * (u8*)value_ptr;
            string_append_formated(string, "%d", val);
            break;
        }
        case Primitive_Type::UNSIGNED_INT_16: {
            int val = (i32) * (u16*)value_ptr;
            string_append_formated(string, "%d", val);
            break;
        }
        case Primitive_Type::UNSIGNED_INT_32: {
            int val = (i32) * (u32*)value_ptr;
            string_append_formated(string, "%d", val);
            break;
        }
        case Primitive_Type::UNSIGNED_INT_64: {
            int val = (i32) * (u64*)value_ptr;
            string_append_formated(string, "%d", val);
            break;
        }
        case Primitive_Type::FLOAT_32: {
            float val = *(float*)value_ptr;
            string_append_formated(string, "%3.2f", val);
            break;
        }
        case Primitive_Type::FLOAT_64: {
            double val = *(double*)value_ptr;
            string_append_formated(string, "%3.2f", val);
            break;
        }
        }
        break;
    }
    default: panic("HEY");
    }
}

void type_signature_append_to_string(String* string, Type_Signature* signature, Semantic_Analyser* analyser)
{
    type_signature_append_to_string_with_children(string, signature, false, analyser);
}



/*
    TYPE_SYSTEM
*/

void type_system_add_primitives(Type_System* system)
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

    *system->bool_type = type_signature_make_primitive(Primitive_Type::BOOLEAN);
    *system->i8_type = type_signature_make_primitive(Primitive_Type::SIGNED_INT_8);
    *system->i16_type = type_signature_make_primitive(Primitive_Type::SIGNED_INT_16);
    *system->i32_type = type_signature_make_primitive(Primitive_Type::SIGNED_INT_32);
    *system->i64_type = type_signature_make_primitive(Primitive_Type::SIGNED_INT_64);
    *system->u8_type = type_signature_make_primitive(Primitive_Type::UNSIGNED_INT_8);
    *system->u16_type = type_signature_make_primitive(Primitive_Type::UNSIGNED_INT_16);
    *system->u32_type = type_signature_make_primitive(Primitive_Type::UNSIGNED_INT_32);
    *system->u64_type = type_signature_make_primitive(Primitive_Type::UNSIGNED_INT_64);
    *system->f32_type = type_signature_make_primitive(Primitive_Type::FLOAT_32);
    *system->f64_type = type_signature_make_primitive(Primitive_Type::FLOAT_64);
    *system->error_type = type_signature_make_error();
    system->void_type->type = Signature_Type::VOID_TYPE;
    system->void_type->size_in_bytes = 0;
    system->void_type->alignment_in_bytes = 1;

    system->void_ptr_type->type = Signature_Type::POINTER;
    system->void_ptr_type->size_in_bytes = 8;
    system->void_ptr_type->alignment_in_bytes = 8;
    system->void_ptr_type->child_type = system->void_type;

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
        character_buffer_member.name_handle = identifier_pool_add_or_find_identifier_by_string(system->lexer->identifier_pool, string_create_static("character_buffer"));
        character_buffer_member.offset = 0;
        character_buffer_member.type = type_system_make_array_unsized(system, system->u8_type);

        Struct_Member size_member;
        size_member.name_handle = identifier_pool_add_or_find_identifier_by_string(system->lexer->identifier_pool, string_create_static("size"));
        size_member.offset = 16;
        size_member.type = system->i32_type;

        Dynamic_Array<Struct_Member> string_members = dynamic_array_create_empty<Struct_Member>(2);
        dynamic_array_push_back(&string_members, character_buffer_member);
        dynamic_array_push_back(&string_members, size_member);

        system->string_type = new Type_Signature();
        system->string_type->type = Signature_Type::STRUCT;
        system->string_type->alignment_in_bytes = 8;
        system->string_type->size_in_bytes = 20;
        system->string_type->member_types = string_members;
        system->string_type->struct_name_handle = identifier_pool_add_or_find_identifier_by_string(system->lexer->identifier_pool, string_create_static("String"));
        dynamic_array_push_back(&system->types, system->string_type);
    }
}

Type_System type_system_create(Lexer* lexer)
{
    Type_System result;
    result.lexer = lexer;
    result.types = dynamic_array_create_empty<Type_Signature*>(256);
    type_system_add_primitives(&result);
    return result;
}

void type_system_destroy(Type_System* system) {
    dynamic_array_destroy(&system->types);
}

void type_system_reset_all(Type_System* system, Lexer* lexer) {
    for (int i = 0; i < system->types.size; i++) {
        type_signature_destroy(system->types[i]);
        delete system->types[i];
    }
    dynamic_array_reset(&system->types);
    system->lexer = lexer;
    type_system_add_primitives(system);
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
                case Signature_Type::PRIMITIVE: are_equal = sig1->primitive_type == sig2->primitive_type; break;
                case Signature_Type::POINTER: are_equal = sig1->child_type == sig2->child_type; break;
                case Signature_Type::STRUCT: are_equal = false; break;
                case Signature_Type::TEMPLATE_TYPE: are_equal = false; break;
                case Signature_Type::ARRAY_SIZED: are_equal = sig1->child_type == sig2->child_type && sig1->array_element_count == sig2->array_element_count; break;
                case Signature_Type::ARRAY_UNSIZED: are_equal = sig1->child_type == sig2->child_type; break;
                case Signature_Type::FUNCTION: {
                    are_equal = true;
                    if (sig1->return_type != sig2->return_type || sig1->parameter_types.size != sig2->parameter_types.size) {
                        are_equal = false; break;
                    }
                    for (int i = 0; i < sig1->parameter_types.size; i++) {
                        if (sig1->parameter_types[i] != sig2->parameter_types[i]) {
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
    result.child_type = child_type;
    result.size_in_bytes = 8;
    result.alignment_in_bytes = 8;
    return type_system_register_type(system, result);
}


Type_Signature* type_system_make_array_unsized(Type_System* system, Type_Signature* element_type)
{
    Type_Signature result;
    result.type = Signature_Type::ARRAY_UNSIZED;
    result.child_type = element_type;
    result.alignment_in_bytes = 8;
    result.size_in_bytes = 16;
    return type_system_register_type(system, result);
}

Type_Signature* type_system_make_function(Type_System* system, Dynamic_Array<Type_Signature*> parameter_types, Type_Signature* return_type)
{
    Type_Signature result;
    result.type = Signature_Type::FUNCTION;
    result.alignment_in_bytes = 1;
    result.size_in_bytes = 0;
    result.parameter_types = parameter_types;
    result.return_type = return_type;
    return type_system_register_type(system, result);
}

void type_system_print(Type_System* system, Semantic_Analyser* analyser)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Type_System: ");
    for (int i = 0; i < system->types.size; i++)
    {
        Type_Signature* type = system->types[i];
        string_append_formated(&msg, "\n\t%d: ", i);
        type_signature_append_to_string(&msg, type, analyser);
        string_append_formated(&msg, " size: %d, alignment: %d", type->size_in_bytes, type->alignment_in_bytes);
    }
    string_append_formated(&msg, "\n");
    logg("%s", msg.characters);
}



/*
    Symbol Table
*/

Symbol_Table* symbol_table_create(Semantic_Analyser* analyser, Symbol_Table* parent, int node_index, bool register_in_ast_mapping)
{
    Symbol_Table* table = new Symbol_Table();
    table->parent = parent;
    table->modules = hashtable_create_empty<int, Symbol_Table_Module>(4, &hash_i32, equals_i32);
    table->symbols = hashtable_create_empty<int, Symbol>(4, &hash_i32, equals_i32);
    dynamic_array_push_back(&analyser->symbol_tables, table);
    if (register_in_ast_mapping) {
        hashtable_insert_element(&analyser->ast_to_symbol_table, node_index, table);
    }
    return table;
}

void symbol_destroy(Symbol* symbol)
{
    if (symbol->is_templated) {
        dynamic_array_destroy(&symbol->template_parameter_names);
        for (int i = 0; i < symbol->template_instances.size; i++) {
            dynamic_array_destroy(&symbol->template_instances[i].template_arguments);
        }
        dynamic_array_destroy(&symbol->template_instances);
    }
}

void symbol_table_destroy(Symbol_Table* symbol_table)
{
    {
        Hashtable_Iterator<int, Symbol_Table_Module> it = hashtable_iterator_create(&symbol_table->modules);
        while (hashtable_iterator_has_next(&it)) {
            Symbol_Table_Module* table_module = it.value;
            if (table_module->is_templated) {
                dynamic_array_destroy(&table_module->template_parameter_names);
            }
            hashtable_iterator_next(&it);
        }
        hashtable_destroy(&symbol_table->modules);
    }
    {
        Hashtable_Iterator<int, Symbol> it = hashtable_iterator_create(&symbol_table->symbols);
        while (hashtable_iterator_has_next(&it)) {
            symbol_destroy(it.value);
            hashtable_iterator_next(&it);
        }
        hashtable_destroy(&symbol_table->symbols);
    }
    delete symbol_table;
}

Symbol* symbol_table_find_symbol(Symbol_Table* table, int name_handle, bool only_current_scope)
{
    Symbol* symbol = hashtable_find_element(&table->symbols, name_handle);
    if (symbol == 0 && !only_current_scope && table->parent != 0) {
        symbol = symbol_table_find_symbol(table->parent, name_handle, only_current_scope);
    }
    return symbol;
}

Symbol* symbol_table_find_symbol_by_string(Symbol_Table* table, String* string, Identifier_Pool* pool)
{
    int* index = hashtable_find_element(&pool->identifier_index_lookup_table, *string);
    if (index == 0) return 0;
    else {
        return symbol_table_find_symbol(table, *index, false);
    }
}

void symbol_append_to_string(Symbol* symbol, String* string, Semantic_Analyser* analyser)
{
    string_append_formated(string, "%s ", identifier_pool_index_to_string(analyser->compiler->identifier_pool, symbol->name_handle).characters);
    switch (symbol->symbol_type)
    {
    case Symbol_Type::VARIABLE:
        string_append_formated(string, "Variable");
        type_signature_append_to_string(string, ir_data_access_get_type(&symbol->options.variable_access), analyser);
        break;
    case Symbol_Type::TYPE:
        string_append_formated(string, "Type");
        type_signature_append_to_string(string, symbol->options.data_type, analyser);
        break;
    case Symbol_Type::FUNCTION:
        string_append_formated(string, "Function");
        type_signature_append_to_string(string, symbol->options.function->function_type, analyser);
        break;
    case Symbol_Type::HARDCODED_FUNCTION:
        string_append_formated(string, "Hardcoded Function ");
        type_signature_append_to_string(string, symbol->options.hardcoded_function->signature, analyser);
        break;
    case Symbol_Type::EXTERN_FUNCTION:
        string_append_formated(string, "Extern function ");
        type_signature_append_to_string(string, symbol->options.extern_function.function_signature, analyser);
        break;
    default: panic("What");
    }
}

void symbol_table_append_to_string_with_parent_info(String* string, Symbol_Table* table, Semantic_Analyser* analyser, bool is_parent, bool print_root)
{
    Lexer* lexer = &analyser->compiler->lexer;
    if (!print_root && table->parent == 0) return;
    if (!is_parent) {
        string_append_formated(string, "Symbols: \n");
    }
    Hashtable_Iterator<int, Symbol> iter = hashtable_iterator_create(&table->symbols);
    while (hashtable_iterator_has_next(&iter))
    {
        Symbol* s = iter.value;
        if (is_parent) {
            string_append_formated(string, "\t");
        }
        symbol_append_to_string(s, string, analyser);
        string_append_formated(string, "\n");
        hashtable_iterator_next(&iter);
    }
    if (table->parent != 0) {
        symbol_table_append_to_string_with_parent_info(string, table->parent, analyser, true, print_root);
    }
}

void symbol_table_append_to_string(String* string, Symbol_Table* table, Semantic_Analyser* analyser, bool print_root) {
    symbol_table_append_to_string_with_parent_info(string, table, analyser, false, print_root);
}

void semantic_analyser_log_error_new(Semantic_Analyser* analyser, Semantic_Error error);
void symbol_table_define_symbol(Symbol_Table* table, Semantic_Analyser* analyser, Symbol symbol, bool shadowing_enabled)
{
    if (symbol.name_handle < 0) {
        panic("Hey");
    }
    Symbol* found_symbol = symbol_table_find_symbol(table, symbol.name_handle, shadowing_enabled);
    if (found_symbol == 0) {
        hashtable_insert_element(&table->symbols, symbol.name_handle, symbol);
        return;
    }
    Semantic_Error error;
    error.type = Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED;
    error.error_node_index = symbol.definition_node_index;
    error.name_id = symbol.name_handle;
    error.symbol_table = table;
    semantic_analyser_log_error_new(analyser, error);
    symbol_destroy(&symbol);
}



/*
    IR PROGRAM
*/

void ir_exit_code_append_to_string(String* string, IR_Exit_Code code)
{
    switch (code)
    {
    case IR_Exit_Code::OUT_OF_BOUNDS:
        string_append_formated(string, "OUT_OF_BOUNDS");
        break;
    case IR_Exit_Code::RETURN_VALUE_OVERFLOW:
        string_append_formated(string, "RETURN_VALUE_OVERFLOW");
        break;
    case IR_Exit_Code::STACK_OVERFLOW:
        string_append_formated(string, "STACK_OVERFLOW");
        break;
    case IR_Exit_Code::SUCCESS:
        string_append_formated(string, "SUCCESS");
        break;
    default: panic("Hey");
    }
}

void ir_hardcoded_function_type_append_to_string(String* string, IR_Hardcoded_Function_Type hardcoded)
{
    switch (hardcoded)
    {
    case IR_Hardcoded_Function_Type::PRINT_I32:
        string_append_formated(string, "PRINT_I32");
        break;
    case IR_Hardcoded_Function_Type::PRINT_F32:
        string_append_formated(string, "PRINT_F32");
        break;
    case IR_Hardcoded_Function_Type::PRINT_BOOL:
        string_append_formated(string, "PRINT_BOOL");
        break;
    case IR_Hardcoded_Function_Type::PRINT_LINE:
        string_append_formated(string, "PRINT_LINE");
        break;
    case IR_Hardcoded_Function_Type::PRINT_STRING:
        string_append_formated(string, "PRINT_STRING");
        break;
    case IR_Hardcoded_Function_Type::READ_I32:
        string_append_formated(string, "READ_I32");
        break;
    case IR_Hardcoded_Function_Type::READ_F32:
        string_append_formated(string, "READ_F32");
        break;
    case IR_Hardcoded_Function_Type::READ_BOOL:
        string_append_formated(string, "READ_BOOL");
        break;
    case IR_Hardcoded_Function_Type::RANDOM_I32:
        string_append_formated(string, "RANDOM_I32");
        break;
    case IR_Hardcoded_Function_Type::MALLOC_SIZE_I32:
        string_append_formated(string, "MALLOC_SIZE_I32");
        break;
    case IR_Hardcoded_Function_Type::FREE_POINTER:
        string_append_formated(string, "FREE_POINTER");
        break;
    default: panic("Should not happen");
    }
}

Type_Signature* ir_data_access_get_type(IR_Data_Access* access)
{
    Type_Signature* sig = 0;
    switch (access->type)
    {
    case IR_Data_Access_Type::GLOBAL_DATA:
        sig = access->option.program->globals[access->index];
        break;
    case IR_Data_Access_Type::CONSTANT:
        sig = access->option.program->constant_pool.constants[access->index].type;
        break;
    case IR_Data_Access_Type::REGISTER:
        sig = access->option.definition_block->registers[access->index];
        break;
    case IR_Data_Access_Type::PARAMETER:
        sig = access->option.function->function_type->parameter_types[access->index];
        break;
    default: panic("Hey!");
    }
    if (access->is_memory_access) {
        return sig->child_type;
    }
    return sig;
}

void ir_code_block_destroy(IR_Code_Block* block);
void ir_instruction_destroy(IR_Instruction* instruction)
{
    switch (instruction->type)
    {
    case IR_Instruction_Type::FUNCTION_CALL: {
        dynamic_array_destroy(&instruction->options.call.arguments);
        break;
    }
    case IR_Instruction_Type::IF: {
        ir_code_block_destroy(instruction->options.if_instr.true_branch);
        ir_code_block_destroy(instruction->options.if_instr.false_branch);
        break;
    }
    case IR_Instruction_Type::WHILE: {
        ir_code_block_destroy(instruction->options.while_instr.code);
        ir_code_block_destroy(instruction->options.while_instr.condition_code);
        break;
    }
    case IR_Instruction_Type::BLOCK: {
        ir_code_block_destroy(instruction->options.block);
        break;
    }
    case IR_Instruction_Type::BREAK:
    case IR_Instruction_Type::CONTINUE:
    case IR_Instruction_Type::RETURN:
    case IR_Instruction_Type::MOVE:
    case IR_Instruction_Type::CAST:
    case IR_Instruction_Type::ADDRESS_OF:
    case IR_Instruction_Type::UNARY_OP:
    case IR_Instruction_Type::BINARY_OP:
        break;
    default: panic("Lul");
    }
}

IR_Code_Block* ir_code_block_create(IR_Function* function)
{
    IR_Code_Block* block = new IR_Code_Block();
    block->function = function;
    block->instructions = dynamic_array_create_empty<IR_Instruction>(64);
    block->registers = dynamic_array_create_empty<Type_Signature*>(32);
    return block;
}

void ir_code_block_destroy(IR_Code_Block* block)
{
    for (int i = 0; i < block->instructions.size; i++) {
        ir_instruction_destroy(&block->instructions[i]);
    }
    dynamic_array_destroy(&block->instructions);
    dynamic_array_destroy(&block->registers);
    delete block;
}

IR_Function* ir_function_create(IR_Program* program, Type_Signature* signature)
{
    IR_Function* function = new IR_Function();
    function->code = ir_code_block_create(function);
    function->function_type = signature;
    function->program = program;
    dynamic_array_push_back(&program->functions, function);
    return function;
}

void ir_function_destroy(IR_Function* function)
{
    ir_code_block_destroy(function->code);
    delete function;
}

Extern_Program_Sources extern_program_sources_create()
{
    Extern_Program_Sources result;
    result.extern_functions = dynamic_array_create_empty<Extern_Function_Identifier>(8);
    result.headers_to_include = dynamic_array_create_empty<int>(8);
    result.source_files_to_compile = dynamic_array_create_empty<int>(8);
    result.lib_files = dynamic_array_create_empty<int>(8);
    result.extern_type_signatures = hashtable_create_pointer_empty<Type_Signature*, int>(8);
    return result;
}

void extern_program_sources_destroy(Extern_Program_Sources* sources)
{
    dynamic_array_destroy(&sources->extern_functions);
    dynamic_array_destroy(&sources->headers_to_include);
    dynamic_array_destroy(&sources->source_files_to_compile);
    dynamic_array_destroy(&sources->lib_files);
    hashtable_destroy(&sources->extern_type_signatures);
}

IR_Program* ir_program_create(Type_System* type_system)
{
    IR_Program* result = new IR_Program();
    result->constant_pool.constants = dynamic_array_create_empty<IR_Constant>(128);
    result->constant_pool.constant_memory = dynamic_array_create_empty<byte>(2048);
    result->entry_function = 0;
    result->functions = dynamic_array_create_empty<IR_Function*>(64);
    result->globals = dynamic_array_create_empty<Type_Signature*>(64);
    result->extern_program_sources = extern_program_sources_create();

    result->hardcoded_functions = dynamic_array_create_empty<IR_Hardcoded_Function*>((int)IR_Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT);
    for (int i = 0; i < (int)IR_Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT; i++)
    {
        IR_Hardcoded_Function* function = new IR_Hardcoded_Function();
        IR_Hardcoded_Function_Type type = (IR_Hardcoded_Function_Type)i;
        function->type = type;

        Dynamic_Array<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(1);
        Type_Signature* return_type = type_system->void_type;
        switch (type)
        {
        case IR_Hardcoded_Function_Type::PRINT_I32:
            dynamic_array_push_back(&parameter_types, type_system->i32_type);
            break;
        case IR_Hardcoded_Function_Type::PRINT_F32:
            dynamic_array_push_back(&parameter_types, type_system->f32_type);
            break;
        case IR_Hardcoded_Function_Type::PRINT_BOOL:
            dynamic_array_push_back(&parameter_types, type_system->bool_type);
            break;
        case IR_Hardcoded_Function_Type::PRINT_STRING:
            dynamic_array_push_back(&parameter_types, type_system->string_type);
            break;
        case IR_Hardcoded_Function_Type::PRINT_LINE:
            break;
        case IR_Hardcoded_Function_Type::READ_I32:
            return_type = type_system->i32_type;
            break;
        case IR_Hardcoded_Function_Type::READ_F32:
            return_type = type_system->f32_type;
            break;
        case IR_Hardcoded_Function_Type::READ_BOOL:
            return_type = type_system->bool_type;
            break;
        case IR_Hardcoded_Function_Type::RANDOM_I32:
            return_type = type_system->i32_type;
            break;
        case IR_Hardcoded_Function_Type::FREE_POINTER:
            dynamic_array_push_back(&parameter_types, type_system->void_ptr_type);
            return_type = type_system->void_type;
            break;
        case IR_Hardcoded_Function_Type::MALLOC_SIZE_I32:
            dynamic_array_push_back(&parameter_types, type_system->i32_type);
            return_type = type_system->void_ptr_type;
            break;
        default:
            panic("What");
        }
        function->signature = type_system_make_function(type_system, parameter_types, return_type);
        dynamic_array_push_back(&result->hardcoded_functions, function);
    }

    return result;
}

void ir_program_destroy(IR_Program* program)
{
    dynamic_array_destroy(&program->constant_pool.constants);
    dynamic_array_destroy(&program->constant_pool.constant_memory);
    dynamic_array_destroy(&program->globals);
    extern_program_sources_destroy(&program->extern_program_sources);
    for (int i = 0; i < program->functions.size; i++) {
        ir_function_destroy(program->functions[i]);
    }
    for (int i = 0; i < program->hardcoded_functions.size; i++) {
        delete program->hardcoded_functions[i];
    }
    dynamic_array_destroy(&program->hardcoded_functions);
    dynamic_array_destroy(&program->functions);
    delete program;
}

void ir_data_access_append_to_string(IR_Data_Access* access, String* string, IR_Code_Block* current_block, Semantic_Analyser* analyser)
{
    switch (access->type)
    {
    case IR_Data_Access_Type::CONSTANT: {
        IR_Constant* constant = &access->option.program->constant_pool.constants[access->index];
        string_append_formated(string, "Constant #%d ", access->index);
        type_signature_append_to_string(string, constant->type, analyser);
        string_append_formated(string, " ", access->index);
        type_signature_append_value_to_string(constant->type, &access->option.program->constant_pool.constant_memory[constant->offset], string);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: {
        Type_Signature* sig = access->option.program->globals[access->index];
        string_append_formated(string, "Global #%d, type: ", access->index);
        type_signature_append_to_string(string, sig, analyser);
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        Type_Signature* sig = access->option.function->function_type->parameter_types[access->index];
        string_append_formated(string, "Param #%d, type: ", access->index);
        type_signature_append_to_string(string, sig, analyser);
        break;
    }
    case IR_Data_Access_Type::REGISTER: {
        Type_Signature* sig = access->option.definition_block->registers[access->index];
        string_append_formated(string, "Register #%d, type: ", access->index);
        type_signature_append_to_string(string, sig, analyser);
        if (access->option.definition_block != current_block) {
            string_append_formated(string, " (Not local)", access->index);
        }
        break;
    }
    }

    if (access->is_memory_access) {
        string_append_formated(string, " MEMORY_ACCESS");
    }
}

void indent_string(String* string, int indentation) {
    for (int i = 0; i < indentation; i++) {
        string_append_formated(string, "    ");
    }
}

void ir_code_block_append_to_string(IR_Code_Block* code_block, String* string, int indentation, Semantic_Analyser* analyser);
void ir_instruction_append_to_string(IR_Instruction* instruction, String* string, int indentation, Semantic_Analyser* analyser, IR_Code_Block* code_block)
{
    Type_System* type_system = &analyser->compiler->type_system;
    indent_string(string, indentation);
    switch (instruction->type)
    {
    case IR_Instruction_Type::ADDRESS_OF:
    {
        IR_Instruction_Address_Of* address_of = &instruction->options.address_of;
        string_append_formated(string, "ADDRESS_OF\n");
        indent_string(string, indentation + 1);
        if (address_of->type != IR_Instruction_Address_Of_Type::FUNCTION) {
            string_append_formated(string, "src: ");
            ir_data_access_append_to_string(&address_of->source, string, code_block, analyser);
            string_append_formated(string, "\n");
            indent_string(string, indentation + 1);
        }
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&address_of->destination, string, code_block, analyser);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "type: ");
        switch (address_of->type)
        {
        case IR_Instruction_Address_Of_Type::ARRAY_ELEMENT:
            string_append_formated(string, "ARRAY_ELEMENT index: ");
            ir_data_access_append_to_string(&address_of->options.index_access, string, code_block, analyser);
            break;
        case IR_Instruction_Address_Of_Type::DATA:
            string_append_formated(string, "DATA");
            break;
        case IR_Instruction_Address_Of_Type::FUNCTION:
            string_append_formated(string, "FUNCTION");
            break;
        case IR_Instruction_Address_Of_Type::STRUCT_MEMBER:
            string_append_formated(string, "STRUCT_MEMBER, offset: %d, type: ", address_of->options.member.offset);
            type_signature_append_to_string(string, address_of->options.member.type, analyser);
            break;
        }
        break;
    }
    case IR_Instruction_Type::BINARY_OP:
    {
        string_append_formated(string, "BINARY_OP ");
        switch (instruction->options.binary_op.type)
        {
        case IR_Instruction_Binary_OP_Type::ADDITION:
            string_append_formated(string, "ADDITION");
            break;
        case IR_Instruction_Binary_OP_Type::AND:
            string_append_formated(string, "AND");
            break;
        case IR_Instruction_Binary_OP_Type::DIVISION:
            string_append_formated(string, "DIVISION");
            break;
        case IR_Instruction_Binary_OP_Type::EQUAL:
            string_append_formated(string, "EQUAL");
            break;
        case IR_Instruction_Binary_OP_Type::GREATER_EQUAL:
            string_append_formated(string, "GREATER_EQUAL");
            break;
        case IR_Instruction_Binary_OP_Type::GREATER_THAN:
            string_append_formated(string, "GREATER_THAN");
            break;
        case IR_Instruction_Binary_OP_Type::LESS_EQUAL:
            string_append_formated(string, "LESS_EQUAL");
            break;
        case IR_Instruction_Binary_OP_Type::LESS_THAN:
            string_append_formated(string, "LESS_THAN ");
            break;
        case IR_Instruction_Binary_OP_Type::MODULO:
            string_append_formated(string, "MODULO");
            break;
        case IR_Instruction_Binary_OP_Type::MULTIPLICATION:
            string_append_formated(string, "MULTIPLICATION ");
            break;
        case IR_Instruction_Binary_OP_Type::NOT_EQUAL:
            string_append_formated(string, "NOT_EQUAL");
            break;
        case IR_Instruction_Binary_OP_Type::OR:
            string_append_formated(string, "OR ");
            break;
        case IR_Instruction_Binary_OP_Type::SUBTRACTION:
            string_append_formated(string, "SUBTRACTION");
            break;
        }

        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "left: ");
        ir_data_access_append_to_string(&instruction->options.binary_op.operand_left, string, code_block, analyser);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "right: ");
        ir_data_access_append_to_string(&instruction->options.binary_op.operand_right, string, code_block, analyser);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&instruction->options.binary_op.destination, string, code_block, analyser);
        break;
    }
    case IR_Instruction_Type::BLOCK: {
        string_append_formated(string, "BLOCK\n");
        ir_code_block_append_to_string(instruction->options.block, string, indentation + 1, analyser);
        break;
    }
    case IR_Instruction_Type::BREAK: {
        string_append_formated(string, "BREAK");
        break;
    }
    case IR_Instruction_Type::CONTINUE: {
        string_append_formated(string, "CONTINUE");
        break;
    }
    case IR_Instruction_Type::CAST:
    {
        IR_Instruction_Cast* cast = &instruction->options.cast;
        string_append_formated(string, "CAST ");
        switch (cast->type)
        {
        case IR_Instruction_Cast_Type::ARRAY_SIZED_TO_UNSIZED:
            string_append_formated(string, "ARRAY_SIZED_TO_UNSIZED");
            break;
        case IR_Instruction_Cast_Type::POINTERS:
            string_append_formated(string, "POINTERS");
            break;
        case IR_Instruction_Cast_Type::POINTER_TO_U64:
            string_append_formated(string, "POINTER_TO_U64");
            break;
        case IR_Instruction_Cast_Type::PRIMITIVE_TYPES:
            string_append_formated(string, "PRIMITIVE_TYPES");
            break;
        case IR_Instruction_Cast_Type::U64_TO_POINTER:
            string_append_formated(string, "U64_TO_POINTER");
            break;
        }

        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "src: ");
        ir_data_access_append_to_string(&cast->source, string, code_block, analyser);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&cast->destination, string, code_block, analyser);
        break;
    }
    case IR_Instruction_Type::FUNCTION_CALL:
    {
        IR_Instruction_Call* call = &instruction->options.call;
        string_append_formated(string, "FUNCTION_CALL\n");
        indent_string(string, indentation + 1);

        Type_Signature* function_sig;
        switch (call->call_type)
        {
        case IR_Instruction_Call_Type::FUNCTION_CALL:
            function_sig = call->options.function->function_type;
            break;
        case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
            function_sig = ir_data_access_get_type(&call->options.pointer_access)->child_type;
            break;
        case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
            function_sig = call->options.hardcoded->signature;
            break;
        case IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL:
            function_sig = call->options.extern_function.function_signature;
            break;
        default:
            panic("Hey");
            return;
        }
        if (function_sig->return_type != type_system->void_type) {
            string_append_formated(string, "dst: ");
            ir_data_access_append_to_string(&call->destination, string, code_block, analyser);
            string_append_formated(string, "\n");
            indent_string(string, indentation + 1);
        }
        string_append_formated(string, "args: (%d)\n", call->arguments.size);
        for (int i = 0; i < call->arguments.size; i++) {
            indent_string(string, indentation + 2);
            ir_data_access_append_to_string(&call->arguments[i], string, code_block, analyser);
            string_append_formated(string, "\n");
        }

        indent_string(string, indentation + 1);
        string_append_formated(string, "Call-Type: ");
        switch (call->call_type)
        {
        case IR_Instruction_Call_Type::FUNCTION_CALL:
            string_append_formated(string, "FUNCTION (later)");
            break;
        case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
            string_append_formated(string, "FUNCTION_POINTER_CALL, access: ");
            ir_data_access_append_to_string(&call->options.pointer_access, string, code_block, analyser);
            break;
        case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
            string_append_formated(string, "HARDCODED_FUNCTION_CALL, type: ");
            ir_hardcoded_function_type_append_to_string(string, call->options.hardcoded->type);
            break;
        case IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL:
            string_append_formated(string, "EXTERN_FUNCTION_CALL, type: ");
            type_signature_append_to_string(string, call->options.extern_function.function_signature, analyser);
            break;
        }
        break;
    }
    case IR_Instruction_Type::IF: {
        string_append_formated(string, "IF ");
        ir_data_access_append_to_string(&instruction->options.if_instr.condition, string, code_block, analyser);
        string_append_formated(string, "\n");
        ir_code_block_append_to_string(instruction->options.if_instr.true_branch, string, indentation + 1, analyser);
        indent_string(string, indentation);
        string_append_formated(string, "ELSE\n");
        ir_code_block_append_to_string(instruction->options.if_instr.true_branch, string, indentation + 1, analyser);
        break;
    }
    case IR_Instruction_Type::MOVE: {
        string_append_formated(string, "MOVE\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "src: ");
        ir_data_access_append_to_string(&instruction->options.move.source, string, code_block, analyser);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&instruction->options.move.destination, string, code_block, analyser);
        break;
    }
    case IR_Instruction_Type::WHILE: {
        string_append_formated(string, "WHILE\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition code: \n");
        ir_code_block_append_to_string(instruction->options.while_instr.condition_code, string, indentation + 2, analyser);
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition access: ");
        ir_data_access_append_to_string(&instruction->options.while_instr.condition_access, string, code_block, analyser);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Body: \n");
        ir_code_block_append_to_string(instruction->options.while_instr.code, string, indentation + 2, analyser);
        break;
    }
    case IR_Instruction_Type::RETURN: {
        IR_Instruction_Return* return_instr = &instruction->options.return_instr;
        switch (return_instr->type)
        {
        case IR_Instruction_Return_Type::EXIT:
            string_append_formated(string, "EXIT ");
            ir_exit_code_append_to_string(string, return_instr->options.exit_code);
            break;
        case IR_Instruction_Return_Type::RETURN_DATA:
            string_append_formated(string, "RETURN ");
            ir_data_access_append_to_string(&return_instr->options.return_value, string, code_block, analyser);
            break;
        case IR_Instruction_Return_Type::RETURN_EMPTY:
            string_append_formated(string, "RETURN");
            break;
        }
        break;
    }
    case IR_Instruction_Type::UNARY_OP:
    {
        string_append_formated(string, "Unary_OP ");
        switch (instruction->options.unary_op.type)
        {
        case IR_Instruction_Unary_OP_Type::NEGATE:
            string_append_formated(string, "NEGATE");
            break;
        case IR_Instruction_Unary_OP_Type::NOT:
            string_append_formated(string, "NOT");
            break;
        }

        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&instruction->options.unary_op.destination, string, code_block, analyser);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "operand: ");
        ir_data_access_append_to_string(&instruction->options.unary_op.source, string, code_block, analyser);
        break;
    }
    default: panic("What");
    }
}

void ir_code_block_append_to_string(IR_Code_Block* code_block, String* string, int indentation, Semantic_Analyser* analyser)
{
    indent_string(string, indentation);
    string_append_formated(string, "Registers:\n");
    for (int i = 0; i < code_block->registers.size; i++) {
        indent_string(string, indentation + 1);
        string_append_formated(string, "#%d: ", i);
        type_signature_append_to_string(string, code_block->registers[i], analyser);
        string_append_formated(string, "\n");
    }
    indent_string(string, indentation);
    string_append_formated(string, "Instructions:\n");
    for (int i = 0; i < code_block->instructions.size; i++) {
        ir_instruction_append_to_string(&code_block->instructions[i], string, indentation + 1, analyser, code_block);
        string_append_formated(string, "\n");
    }
}

void ir_function_append_to_string(IR_Function* function, String* string, int indentation, Semantic_Analyser* analyser)
{
    indent_string(string, indentation);
    string_append_formated(string, "Function-Type:");
    type_signature_append_to_string(string, function->function_type, analyser);
    string_append_formated(string, "\n");
    ir_code_block_append_to_string(function->code, string, indentation, analyser);
}

void ir_program_append_to_string(IR_Program* program, String* string, Semantic_Analyser* analyser)
{
    string_append_formated(string, "Program Dump:\n-----------------\n");
    for (int i = 0; i < program->functions.size; i++)
    {
        string_append_formated(string, "Function #%d ", i);
        ir_function_append_to_string(program->functions[i], string, 0, analyser);
        string_append_formated(string, "\n");
    }
}

void ir_data_access_change_type(IR_Data_Access access, Type_Signature* new_type)
{
    switch (access.type)
    {
    case IR_Data_Access_Type::CONSTANT: {
        panic("Does not work");
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: {
        access.option.program->globals[access.index] = new_type;
        break;
    }
    case IR_Data_Access_Type::REGISTER: {
        access.option.definition_block->registers[access.index] = new_type;
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        access.option.function->function_type->parameter_types[access.index] = new_type;
        break;
    }
    default: panic("Hey");
    }
}

IR_Data_Access ir_data_access_create_intermediate(IR_Code_Block* block, Type_Signature* signature)
{
    IR_Data_Access access;
    if (signature->type == Signature_Type::VOID_TYPE) {
        access.is_memory_access = false;
        access.type = IR_Data_Access_Type::GLOBAL_DATA;
        access.option.program = 0;
        access.index = 0;
        return access;
    }
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::REGISTER;
    access.option.definition_block = block;
    dynamic_array_push_back(&block->registers, signature);
    access.index = block->registers.size - 1;
    return access;
}

IR_Data_Access ir_data_access_create_constant_access(IR_Program* program, Type_Signature* signature, Array<byte> bytes)
{
    dynamic_array_reserve(&program->constant_pool.constant_memory,
        program->constant_pool.constant_memory.size + signature->alignment_in_bytes + signature->size_in_bytes);
    while (program->constant_pool.constant_memory.size % signature->alignment_in_bytes != 0) {
        dynamic_array_push_back(&program->constant_pool.constant_memory, (byte)0);
    }

    IR_Constant constant;
    constant.type = signature;
    constant.offset = program->constant_pool.constant_memory.size;
    dynamic_array_push_back(&program->constant_pool.constants, constant);

    for (int i = 0; i < bytes.size; i++) {
        dynamic_array_push_back(&program->constant_pool.constant_memory, bytes[i]);
    }

    IR_Data_Access access;
    access.type = IR_Data_Access_Type::CONSTANT;
    access.index = program->constant_pool.constants.size - 1;
    access.is_memory_access = false;
    access.option.program = program;
    return access;
}

IR_Data_Access ir_data_access_create_constant_i32(Semantic_Analyser* analyser, i32 value)
{
    return ir_data_access_create_constant_access(analyser->program, analyser->compiler->type_system.i32_type, array_create_static((byte*)&value, 4));
}

IR_Data_Access ir_data_access_dereference_pointer(IR_Code_Block* block, IR_Data_Access pointer_access)
{
    Type_Signature* pointer_sig = ir_data_access_get_type(&pointer_access);
    if (pointer_sig->type != Signature_Type::POINTER) {
        panic("Hey, this should not happen!");
    }

    if (pointer_access.is_memory_access) {
        IR_Data_Access loaded_ptr_access = ir_data_access_create_intermediate(block, pointer_sig);
        IR_Instruction move_instr;
        move_instr.type = IR_Instruction_Type::MOVE;
        move_instr.options.move.destination = loaded_ptr_access;
        move_instr.options.move.source = pointer_access;
        dynamic_array_push_back(&block->instructions, move_instr);

        IR_Data_Access result = loaded_ptr_access;
        result.is_memory_access = true;
        return result;
    }
    else {
        IR_Data_Access result = pointer_access;
        result.is_memory_access = true;
        return result;
    }
}



/*
    SEMANTIC ANALYSER
*/
struct Type_Analysis_Result
{
    Analysis_Result_Type type;
    union {
        Type_Signature* result_type;
        Workload_Dependency dependency;
    } options;
};

struct Expression_Analysis_Result_Success
{
    bool has_memory_address;
    Type_Signature* result_type;
    void* value;
};

struct Expression_Analysis_Result
{
    Analysis_Result_Type type;
    union
    {
        Expression_Analysis_Result_Success success;
        Workload_Dependency dependency;
    } options;
};

struct Variable_Creation_Analysis_Result
{
    Analysis_Result_Type type;
    Workload_Dependency dependency;
};

Workload_Dependency workload_dependency_make_code_block_finished(IR_Code_Block* code_block, int node_index)
{
    Workload_Dependency dependency;
    dependency.node_index = node_index;
    dependency.type = Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED;
    dependency.options.code_block = code_block;
    return dependency;
}

Workload_Dependency workload_dependency_make_type_size_unknown(Type_Signature* type, int node_index)
{
    Workload_Dependency dependency;
    dependency.node_index = node_index;
    dependency.type = Workload_Dependency_Type::TYPE_SIZE_UNKNOWN;
    dependency.options.type_signature = type;
    return dependency;
}

Workload_Dependency workload_dependency_make_identifier_not_found(
    Symbol_Table* symbol_table, int identifier_or_path_node_index, bool current_scope_only,
    Dynamic_Array<Type_Signature*> template_parameter_names)
{
    Workload_Dependency dependency;
    dependency.type = Workload_Dependency_Type::IDENTIFER_NOT_FOUND;
    dependency.node_index = identifier_or_path_node_index;
    dependency.options.identifier_not_found.current_scope_only = current_scope_only;
    dependency.options.identifier_not_found.symbol_table = symbol_table;
    dependency.options.identifier_not_found.template_parameter_names = dynamic_array_create_copy(template_parameter_names.data, template_parameter_names.size);
    return dependency;
}

void semantic_analyser_log_error_new(Semantic_Analyser* analyser, Semantic_Error error) {
    if ((int)error.type < 0) {
        logg("Yo didgg");
    }
    dynamic_array_push_back(&analyser->errors, error);
}

void semantic_analyser_define_type_symbol(Semantic_Analyser* analyser, Symbol_Table* table, int name_id, Type_Signature* type, int definition_node_index)
{
    Symbol s;
    s.symbol_type = Symbol_Type::TYPE;
    s.is_templated = false;
    s.options.data_type = type;
    s.name_handle = name_id;
    s.definition_node_index = definition_node_index;
    symbol_table_define_symbol(table, analyser, s, false);
}

Identifier_Analysis_Result semantic_analyser_instanciate_template(Semantic_Analyser* analyser, Symbol_Table* table, Symbol* symbol,
    Dynamic_Array<Type_Signature*> template_arguments, int instance_node_index)
{
    assert(symbol->is_templated, "HEY");
    // Check if arguments match
    if (symbol->template_parameter_names.size != template_arguments.size) {
        Semantic_Error error;
        error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT;
        error.symbol_table = table;
        error.identifier_node_index = instance_node_index;
        error.error_node_index = instance_node_index;
        error.invalid_argument_count.expected = symbol->template_parameter_names.size;
        error.invalid_argument_count.given = template_arguments.size;
        semantic_analyser_log_error_new(analyser, error);
        Identifier_Analysis_Result result;
        result.type = Analysis_Result_Type::ERROR_OCCURED;
        return result;
    }

    // Arguments must have size calculated (Prevents Templates circulary creating new templates, e.g. Struct Node with member x: Node<Node<T>>)
    for (int i = 0; i < template_arguments.size; i++) {
        if (template_arguments[i]->size_in_bytes == 0 && template_arguments[i]->alignment_in_bytes == 0) {
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = workload_dependency_make_type_size_unknown(template_arguments[i], instance_node_index);
            return result;
        }
    }

    // Search for already instanciated template
    Symbol_Template_Instance* found_instance = 0;
    int found_instance_index = 0;
    for (int i = 0; i < symbol->template_instances.size; i++)
    {
        Symbol_Template_Instance* instance = &symbol->template_instances[i];
        bool matches = true;
        for (int j = 0; j < instance->template_arguments.size; j++) {
            if (instance->template_arguments[j] != template_arguments[j]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            found_instance = instance;
            found_instance_index = i;
            break;
        }
    }

    // Instanciate template if necessary
    if (found_instance == 0)
    {
        if (PRINT_DEPENDENCIES)
        {
            String tmp = string_create_empty(64);
            SCOPE_EXIT(string_destroy(&tmp));
            string_append_formated(&tmp, "No instance of template found, instanciating: %s<",
                identifier_pool_index_to_string(analyser->compiler->identifier_pool, symbol->name_handle).characters);
            for (int i = 0; i < template_arguments.size; i++) {
                type_signature_append_to_string(&tmp, template_arguments[i], analyser);
                if (i != template_arguments.size - 1) {
                    string_append_formated(&tmp, ", ");
                }
            }
            string_append_formated(&tmp, ">\n");
            logg("%s", tmp.characters);
        }
        // Find original symbol definition table
        Symbol_Table* symbol_definition_table = 0;
        {
            int node_index = analyser->compiler->parser.nodes[symbol->definition_node_index].parent;
            AST_Node* node = &analyser->compiler->parser.nodes[node_index];
            while (true)
            {
                Symbol_Table** table = hashtable_find_element(&analyser->ast_to_symbol_table, node_index);
                if (table != 0) {
                    symbol_definition_table = *table;
                    break;
                }
                if (node->parent == -1) {
                    break;
                }
                node_index = node->parent;
                node = &analyser->compiler->parser.nodes[node_index];
            }
            assert(symbol_definition_table != 0, "HEY");
            Symbol* assert_sym = symbol_table_find_symbol(symbol_definition_table, symbol->name_handle, true);
            if (assert_sym == 0) {
                String tmp = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&tmp));
                string_append_formated(&tmp, "Not found identifier: ");
                string_append_formated(&tmp, identifier_pool_index_to_string(analyser->compiler->identifier_pool, symbol->name_handle).characters);
                string_append_formated(&tmp, "\n");
                symbol_table_append_to_string_with_parent_info(&tmp, symbol_definition_table, analyser, false, false);
                logg("%s\n", tmp.characters);
            }
            assert(assert_sym != 0, "HEY");
        }

        // Create instance template table, where templates are filled out
        Symbol_Table* template_instance_table = symbol_table_create(analyser, symbol_definition_table, symbol->definition_node_index, false);
        for (int i = 0; i < symbol->template_parameter_names.size; i++)
        {
            Symbol template_symbol;
            template_symbol.symbol_type = Symbol_Type::TYPE;
            template_symbol.name_handle = symbol->template_parameter_names[i];
            template_symbol.definition_node_index = instance_node_index;
            template_symbol.is_templated = false;
            template_symbol.options.data_type = template_arguments[i];
            symbol_table_define_symbol(template_instance_table, analyser, template_symbol, true);
        }

        // Create Instance;
        {
            Symbol_Template_Instance instance;
            instance.instanciated = false;
            instance.template_arguments = dynamic_array_create_copy(template_arguments.data, template_arguments.size);
            dynamic_array_push_back(&symbol->template_instances, instance);
            found_instance = &symbol->template_instances[symbol->template_instances.size - 1];
            found_instance_index = symbol->template_instances.size - 1;
        }

        // Create workload
        switch (symbol->symbol_type)
        {
        case Symbol_Type::VARIABLE:
        case Symbol_Type::EXTERN_FUNCTION:
        case Symbol_Type::HARDCODED_FUNCTION: {
            panic("What");
            break;
        }
        case Symbol_Type::FUNCTION:
        {
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::FUNCTION_HEADER;
            workload.node_index = symbol->definition_node_index;
            workload.symbol_table = symbol_definition_table;
            workload.options.function_header.type_lookup_table = template_instance_table;
            workload.options.function_header.is_template_instance = true;
            workload.options.function_header.is_template_analysis = false;
            workload.options.function_header.symbol_name_id = symbol->name_handle;
            workload.options.function_header.symbol_instance_index = found_instance_index;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case Symbol_Type::TYPE:
        {
            if (symbol->options.data_type->type != Signature_Type::STRUCT) {
                panic("Should not happen");
            }

            AST_Node* struct_node = &analyser->compiler->parser.nodes[symbol->definition_node_index];
            Type_Signature* struct_instance_signature;
            {
                Type_Signature struct_sig;
                struct_sig.type = Signature_Type::STRUCT;
                struct_sig.member_types = dynamic_array_create_empty<Struct_Member>(struct_node->children.size);
                struct_sig.alignment_in_bytes = 0;
                struct_sig.size_in_bytes = 0;
                struct_sig.struct_name_handle = struct_node->name_id;
                struct_instance_signature = type_system_register_type(&analyser->compiler->type_system, struct_sig);
            }
            found_instance->instanciated = true;
            found_instance->options.data_type = struct_instance_signature;

            Analysis_Workload workload;
            workload.node_index = symbol->definition_node_index;
            workload.symbol_table = symbol_definition_table;
            workload.type = Analysis_Workload_Type::STRUCT_BODY;
            workload.options.struct_body.struct_signature = struct_instance_signature;
            workload.options.struct_body.type_lookup_table = template_instance_table;
            workload.options.struct_body.offset = 0;
            workload.options.struct_body.alignment = 0;
            workload.options.struct_body.current_child_index = 0;
            workload.options.struct_body.is_template_instance = true;
            workload.options.struct_body.symbol_instance_index = found_instance_index;
            workload.options.struct_body.symbol_name_id = symbol->name_handle;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        default: panic("Hey"); break;
        }
    }

    // Create dependency if template is not finished
    if (!found_instance->instanciated)
    {
        Workload_Dependency dependency;
        dependency.node_index = instance_node_index;
        dependency.type = Workload_Dependency_Type::TEMPLATE_INSTANCE_NOT_FINISHED;
        dependency.options.template_not_finished.instance_index = found_instance_index;
        dependency.options.template_not_finished.symbol_name_id = symbol->name_handle;
        dependency.options.template_not_finished.symbol_table = table;

        Identifier_Analysis_Result result;
        result.type = Analysis_Result_Type::DEPENDENCY;
        result.options.dependency = dependency;
        return result;
    }

    // Create success
    Identifier_Analysis_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    result.options.symbol = *symbol;
    result.options.symbol.is_templated = false;
    result.options.symbol.template_instances.data = 0;
    result.options.symbol.options = found_instance->options;
    return result;
}

Type_Analysis_Result semantic_analyser_analyse_type(Semantic_Analyser* analyser, Symbol_Table* table, int type_node_index);
Identifier_Analysis_Result semantic_analyser_analyse_identifier_node_with_template_arguments(
    Semantic_Analyser* analyser, Symbol_Table* table, AST_Parser* parser, int node_index, bool only_current_scope,
    Dynamic_Array<Type_Signature*> template_arguments)
{
    AST_Node* node = &parser->nodes[node_index];
    assert(node->type == AST_Node_Type::IDENTIFIER_NAME ||
        node->type == AST_Node_Type::IDENTIFIER_PATH ||
        node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED ||
        node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED, "Cannot lookup symbol of non identifer node");

    switch (node->type)
    {
    case AST_Node_Type::IDENTIFIER_NAME:
    {
        Symbol* symbol = symbol_table_find_symbol(table, node->name_id, only_current_scope);
        if (symbol == 0) {
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = workload_dependency_make_identifier_not_found(table, node_index, only_current_scope, template_arguments);
            return result;
        }
        if (symbol->is_templated) {
            return semantic_analyser_instanciate_template(analyser, table, symbol, template_arguments, node_index);
        }
        Identifier_Analysis_Result result;
        result.type = Analysis_Result_Type::SUCCESS;
        result.options.symbol = *symbol;
        return result;
    }
    case AST_Node_Type::IDENTIFIER_PATH:
    {
        Symbol_Table_Module* table_module = hashtable_find_element(&table->modules, node->name_id);
        if (table_module == 0) {
            if (table->parent != 0 && !only_current_scope) {
                return semantic_analyser_analyse_identifier_node_with_template_arguments(
                    analyser, table->parent, parser, node_index, false, template_arguments
                );
            }
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = workload_dependency_make_identifier_not_found(table, node_index, only_current_scope, template_arguments);
            return result;
        }
        else {
            if (table_module->is_templated) {
                Semantic_Error error;
                error.symbol_table = table;
                error.identifier_node_index = node_index;
                error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED;
                error.error_node_index = node_index;
                semantic_analyser_log_error_new(analyser, error);
                Identifier_Analysis_Result result;
                result.type = Analysis_Result_Type::ERROR_OCCURED;
                return result;
            }
            return semantic_analyser_analyse_identifier_node_with_template_arguments(
                analyser, table_module->module_table, parser, node->children[0], true, template_arguments
            );
        }
        break;
    }
    case AST_Node_Type::IDENTIFIER_NAME_TEMPLATED: {
        // Find Symbol
        Symbol* symbol = symbol_table_find_symbol(table, node->name_id, only_current_scope);
        if (symbol == 0) {
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = workload_dependency_make_identifier_not_found(table, node_index, only_current_scope, template_arguments);
            return result;
        }

        // Check if templated
        if (!symbol->is_templated) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE;
            error.symbol_table = table;
            error.identifier_node_index = node_index;
            error.error_node_index = node_index;
            semantic_analyser_log_error_new(analyser, error);
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::ERROR_OCCURED;
            return result;
        }

        // Create parameters array if not already created
        bool delete_parameter = false;
        if (template_arguments.data == 0) {
            delete_parameter = true;
            template_arguments = dynamic_array_create_empty<Type_Signature*>(2);
        }
        SCOPE_EXIT(
            if (delete_parameter) dynamic_array_destroy(&template_arguments);
        );

        // Analyse arguments, add to parameters
        AST_Node* unnamed_parameter_node = &analyser->compiler->parser.nodes[node->children[0]];
        if (unnamed_parameter_node->children.size != symbol->template_parameter_names.size) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT;
            error.error_node_index = node_index;
            error.symbol_table = table;
            error.identifier_node_index = node_index;
            error.invalid_argument_count.expected = symbol->template_parameter_names.size;
            error.invalid_argument_count.given = template_arguments.size;
            semantic_analyser_log_error_new(analyser, error);
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::ERROR_OCCURED;
            return result;
        }
        for (int i = 0; i < unnamed_parameter_node->children.size; i++)
        {
            Type_Analysis_Result type_result = semantic_analyser_analyse_type(analyser, table, unnamed_parameter_node->children[i]);
            switch (type_result.type)
            {
            case Analysis_Result_Type::SUCCESS:
                dynamic_array_push_back(&template_arguments, type_result.options.result_type);
                break;
            case Analysis_Result_Type::DEPENDENCY: {
                Identifier_Analysis_Result result;
                result.type = Analysis_Result_Type::DEPENDENCY;
                result.options.dependency = type_result.options.dependency;
                return result;
            }
            case Analysis_Result_Type::ERROR_OCCURED: {
                Identifier_Analysis_Result result;
                result.type = Analysis_Result_Type::ERROR_OCCURED;
                return result;
            }
            }
        }

        // Instanciate if necessary
        return semantic_analyser_instanciate_template(analyser, table, symbol, template_arguments, node_index);
    }
    case AST_Node_Type::IDENTIFIER_PATH_TEMPLATED:
    {
        Symbol_Table_Module* table_module = hashtable_find_element(&table->modules, node->name_id);
        if (table_module == 0) {
            if (table->parent != 0 && !only_current_scope) {
                return semantic_analyser_analyse_identifier_node_with_template_arguments(
                    analyser, table->parent, parser, node_index, false, template_arguments
                );
            }
            Identifier_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = workload_dependency_make_identifier_not_found(table, node_index, only_current_scope, template_arguments);
            return result;
        }
        else
        {
            if (!table_module->is_templated) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE;
                error.symbol_table = table;
                error.identifier_node_index = node_index;
                error.error_node_index = node_index;
                semantic_analyser_log_error_new(analyser, error);
                Identifier_Analysis_Result result;
                result.type = Analysis_Result_Type::ERROR_OCCURED;
                return result;
            }

            bool delete_parameter = false;
            if (template_arguments.data == 0) {
                delete_parameter = true;
                template_arguments = dynamic_array_create_empty<Type_Signature*>(2);
            }
            SCOPE_EXIT(
                if (delete_parameter) dynamic_array_destroy(&template_arguments);
            );

            // Analyse template arguments
            AST_Node* unnamed_parameter_node = &analyser->compiler->parser.nodes[node->children[0]];
            if (unnamed_parameter_node->children.size != table_module->template_parameter_names.size) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT;
                error.error_node_index = node_index;
                error.symbol_table = table;
                error.identifier_node_index = node_index;
                error.invalid_argument_count.expected = table_module->template_parameter_names.size;
                error.invalid_argument_count.given = template_arguments.size;
                semantic_analyser_log_error_new(analyser, error);
                Identifier_Analysis_Result result;
                result.type = Analysis_Result_Type::ERROR_OCCURED;
                return result;
            }
            for (int i = 0; i < unnamed_parameter_node->children.size; i++)
            {
                Type_Analysis_Result type_result = semantic_analyser_analyse_type(analyser, table, unnamed_parameter_node->children[i]);
                switch (type_result.type)
                {
                case Analysis_Result_Type::SUCCESS:
                    dynamic_array_push_back(&template_arguments, type_result.options.result_type);
                    break;
                case Analysis_Result_Type::DEPENDENCY: {
                    Identifier_Analysis_Result result;
                    result.type = Analysis_Result_Type::DEPENDENCY;
                    result.options.dependency = type_result.options.dependency;
                    return result;
                }
                case Analysis_Result_Type::ERROR_OCCURED: {
                    Identifier_Analysis_Result result;
                    result.type = Analysis_Result_Type::ERROR_OCCURED;
                    return result;
                }
                }
            }

            return semantic_analyser_analyse_identifier_node_with_template_arguments(
                analyser, table_module->module_table, parser, node->children[1], true, template_arguments
            );
        }
        break;
    }
    }

    panic("Should not happen");
    Identifier_Analysis_Result result;
    result.type = Analysis_Result_Type::ERROR_OCCURED;
    return result;
}

Identifier_Analysis_Result semantic_analyser_analyse_identifier_node(
    Semantic_Analyser* analyser, Symbol_Table* table, AST_Parser* parser, int node_index, bool only_current_scope)
{
    Dynamic_Array<Type_Signature*> template_arguments;
    template_arguments.data = 0;
    template_arguments.size = 0;
    template_arguments.capacity = 0;
    return semantic_analyser_analyse_identifier_node_with_template_arguments(
        analyser, table, parser, node_index, only_current_scope,
        template_arguments
    );
}

Type_Analysis_Result type_analysis_result_make_success(Type_Signature* result_type)
{
    Type_Analysis_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    result.options.result_type = result_type;
    return result;
}

Type_Analysis_Result type_analysis_result_make_error()
{
    Type_Analysis_Result result;
    result.type = Analysis_Result_Type::ERROR_OCCURED;
    return result;
}

Type_Analysis_Result semantic_analyser_analyse_type(Semantic_Analyser* analyser, Symbol_Table* table, int type_node_index)
{
    AST_Node* type_node = &analyser->compiler->parser.nodes[type_node_index];
    switch (type_node->type)
    {
    case AST_Node_Type::TYPE_IDENTIFIER:
    {
        Symbol* symbol = 0;
        Identifier_Analysis_Result identifier_result =
            semantic_analyser_analyse_identifier_node(analyser, table, &analyser->compiler->parser, type_node->children[0], false);
        switch (identifier_result.type)
        {
        case Analysis_Result_Type::SUCCESS:
            symbol = &identifier_result.options.symbol;
            break;
        case Analysis_Result_Type::DEPENDENCY: {
            Type_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.options.dependency = identifier_result.options.dependency;
            return result;
        }
        case Analysis_Result_Type::ERROR_OCCURED: {
            return type_analysis_result_make_error();
        }
        default: {
            panic("What");
            return type_analysis_result_make_error();
        }
        }

        if (symbol->symbol_type == Symbol_Type::TYPE) {
            if (symbol->options.data_type == analyser->compiler->type_system.error_type) {
                return type_analysis_result_make_error();
            }
        }
        else {
            Semantic_Error error;
            error.error_node_index = type_node_index;
            error.symbol_table = table;
            error.identifier_node_index = type_node->children[0];
            error.type = Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER;
            semantic_analyser_log_error_new(analyser, error);
            return type_analysis_result_make_error();
        }
        return type_analysis_result_make_success(symbol->options.data_type);
    }
    case AST_Node_Type::TYPE_POINTER_TO: {
        Type_Analysis_Result result = semantic_analyser_analyse_type(analyser, table, type_node->children[0]);
        if (result.type == Analysis_Result_Type::SUCCESS) {
            return type_analysis_result_make_success(type_system_make_pointer(&analyser->compiler->type_system, result.options.result_type));
        }
        else {
            return result;
        }
    }
    case AST_Node_Type::TYPE_ARRAY_SIZED:
    {
        // TODO: check if expression is compile time known, currently only literal value is supported
        int index_node_array_size = type_node->children[0];
        AST_Node* node_array_size = &analyser->compiler->parser.nodes[index_node_array_size];
        if (node_array_size->type != AST_Node_Type::EXPRESSION_LITERAL) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::MISSING_FEATURE_NON_INTEGER_ARRAY_SIZE_EVALUATION;
            error.error_node_index = index_node_array_size;
            semantic_analyser_log_error_new(analyser, error);
            return type_analysis_result_make_error();
        }
        Token literal_token = analyser->compiler->lexer.tokens[analyser->compiler->parser.token_mapping[index_node_array_size].start_index];
        if (literal_token.type != Token_Type::INTEGER_LITERAL) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::MISSING_FEATURE_NON_INTEGER_ARRAY_SIZE_EVALUATION;
            error.error_node_index = index_node_array_size;
            semantic_analyser_log_error_new(analyser, error);
            return type_analysis_result_make_error();
        }

        Type_Analysis_Result element_result = semantic_analyser_analyse_type(analyser, table, type_node->children[1]);
        if (element_result.type != Analysis_Result_Type::SUCCESS) {
            return element_result;
        }

        Type_Signature* element_type = element_result.options.result_type;
        if (element_type == analyser->compiler->type_system.void_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
            error.error_node_index = index_node_array_size;
            semantic_analyser_log_error_new(analyser, error);
            return type_analysis_result_make_error();
        }

        Type_Signature array_type;
        array_type.type = Signature_Type::ARRAY_SIZED;
        array_type.child_type = element_type;
        array_type.array_element_count = literal_token.attribute.integer_value;
        array_type.alignment_in_bytes = 0;
        array_type.size_in_bytes = 0;
        Type_Signature* final_type = type_system_register_type(&analyser->compiler->type_system, array_type);

        if (element_type->size_in_bytes != 0 && element_type->alignment_in_bytes != 0)
        {
            // Just calculate the size now
            final_type->alignment_in_bytes = final_type->child_type->alignment_in_bytes;
            final_type->size_in_bytes = math_round_next_multiple(final_type->child_type->size_in_bytes, final_type->child_type->alignment_in_bytes) * final_type->array_element_count;
        }
        else {
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::SIZED_ARRAY_SIZE;
            workload.symbol_table = table;
            workload.node_index = type_node_index;
            workload.options.sized_array_type = final_type;

            Waiting_Workload waiting;
            waiting.workload = workload;
            waiting.dependency = workload_dependency_make_type_size_unknown(final_type->child_type, type_node_index);
            dynamic_array_push_back(&analyser->waiting_workload, waiting);
        }

        return type_analysis_result_make_success(final_type);
    }
    case AST_Node_Type::TYPE_ARRAY_UNSIZED: {
        Type_Analysis_Result element_result = semantic_analyser_analyse_type(analyser, table, type_node->children[0]);
        if (element_result.type != Analysis_Result_Type::SUCCESS) {
            return element_result;
        }

        Type_Signature* element_type = element_result.options.result_type;
        if (element_type == analyser->compiler->type_system.void_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
            error.error_node_index = type_node->children[0];
            semantic_analyser_log_error_new(analyser, error);
            return type_analysis_result_make_error();
        }
        return type_analysis_result_make_success(type_system_make_array_unsized(&analyser->compiler->type_system, element_type));
    }
    case AST_Node_Type::TYPE_FUNCTION_POINTER:
    {
        Type_Signature* return_type;
        if (type_node->children.size == 2)
        {
            Type_Analysis_Result return_type_result = semantic_analyser_analyse_type(analyser, table, type_node->children[1]);
            if (return_type_result.type != Analysis_Result_Type::SUCCESS) {
                return return_type_result;
            }
            return_type = return_type_result.options.result_type;
        }
        else {
            return_type = analyser->compiler->type_system.void_type;
        }

        AST_Node* parameter_block = &analyser->compiler->parser.nodes[type_node->children[0]];
        Dynamic_Array<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(parameter_block->children.size);
        for (int i = 0; i < parameter_block->children.size; i++)
        {
            int param_type_index = parameter_block->children[i];
            Type_Analysis_Result param_result = semantic_analyser_analyse_type(analyser, table, param_type_index);
            if (param_result.type != Analysis_Result_Type::SUCCESS) {
                dynamic_array_destroy(&parameter_types);
                return param_result;
            }
            dynamic_array_push_back(&parameter_types, param_result.options.result_type);
        }

        Type_Signature* function_type = type_system_make_function(&analyser->compiler->type_system, parameter_types, return_type);
        return type_analysis_result_make_success(type_system_make_pointer(&analyser->compiler->type_system, function_type));
    }
    }

    panic("This should not happen, this means that the child was not a type!\n");
    return type_analysis_result_make_error();
}

Expression_Analysis_Result expression_analysis_result_make_success(Type_Signature* expression_result, bool has_memory_address, void* value)
{
    Expression_Analysis_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    result.options.success.has_memory_address = has_memory_address;
    result.options.success.result_type = expression_result;
    result.options.success.value = value;
    return result;
}

Expression_Analysis_Result expression_analysis_result_make_error()
{
    Expression_Analysis_Result result;
    result.type = Analysis_Result_Type::ERROR_OCCURED;
    return result;
}

Expression_Analysis_Result expression_analysis_result_make_dependency(Workload_Dependency dependency)
{
    Expression_Analysis_Result result;
    result.type = Analysis_Result_Type::DEPENDENCY;
    result.options.dependency = dependency;
    return result;
}

bool semantic_analyser_cast_implicit_if_possible(Semantic_Analyser* analyser, IR_Code_Block* block, IR_Data_Access source, IR_Data_Access destination)
{
    Type_Signature* source_type = ir_data_access_get_type(&source);
    Type_Signature* destination_type = ir_data_access_get_type(&destination);

    IR_Instruction cast_instr;
    cast_instr.type = IR_Instruction_Type::CAST;
    cast_instr.options.cast.source = source;
    cast_instr.options.cast.destination = destination;

    // Pointer casting
    if (source_type->type == Signature_Type::POINTER && destination_type->type == Signature_Type::POINTER) {
        if (source_type == analyser->compiler->type_system.void_ptr_type || destination_type == analyser->compiler->type_system.void_ptr_type) {
            cast_instr.options.cast.type = IR_Instruction_Cast_Type::POINTERS;
            dynamic_array_push_back(&block->instructions, cast_instr);
            return true;
        }
        return false;
    }
    // Primitive Casting:
    if (source_type->type == Signature_Type::PRIMITIVE && destination_type->type == Signature_Type::PRIMITIVE)
    {
        bool cast_valid = false;
        if (primitive_type_is_integer(source_type->primitive_type) && primitive_type_is_integer(destination_type->primitive_type)) {
            cast_valid = primitive_type_is_signed(source_type->primitive_type) == primitive_type_is_signed(destination_type->primitive_type);
        }
        if (!cast_valid) {
            if (primitive_type_is_float(destination_type->primitive_type) && primitive_type_is_integer(source_type->primitive_type)) {
                cast_valid = true;
            }
        }
        if (!cast_valid) {
            if (primitive_type_is_float(destination_type->primitive_type) && primitive_type_is_float(source_type->primitive_type)) {
                cast_valid = destination_type->size_in_bytes > source_type->size_in_bytes;
            }
        }
        if (!cast_valid) {
            if (source_type->primitive_type == Primitive_Type::BOOLEAN || destination_type->primitive_type == Primitive_Type::BOOLEAN) {
                return false;
            }
        }

        if (cast_valid) {
            cast_instr.options.cast.type = IR_Instruction_Cast_Type::PRIMITIVE_TYPES;
            dynamic_array_push_back(&block->instructions, cast_instr);
            return true;
        }
        else {
            return false;
        }
    }
    // Array casting
    if (source_type->type == Signature_Type::ARRAY_SIZED && destination_type->type == Signature_Type::ARRAY_UNSIZED) {
        if (source_type->child_type == destination_type->child_type) {
            cast_instr.options.cast.type = IR_Instruction_Cast_Type::ARRAY_SIZED_TO_UNSIZED;
            dynamic_array_push_back(&block->instructions, cast_instr);
            return true;
        }
    }
    return false;
}

Expression_Analysis_Result semantic_analyser_analyse_expression(
    Semantic_Analyser* analyser, Symbol_Table* symbol_table, int expression_index, IR_Code_Block* code_block, bool create_temporary_access, IR_Data_Access* access)
{
    AST_Node* expression_node = &analyser->compiler->parser.nodes[expression_index];
    Type_System* type_system = &analyser->compiler->type_system;
    Dynamic_Array<AST_Node>* nodes = &analyser->compiler->parser.nodes;

    int rollback_instruction_index = code_block->instructions.size;
    int rollback_register_index = code_block->registers.size;
    bool rollback_on_exit = false;
    SCOPE_EXIT(
        if (rollback_on_exit)
        {
            for (int i = rollback_instruction_index; i < code_block->instructions.size; i++) {
                ir_instruction_destroy(&code_block->instructions[i]);
            }
            dynamic_array_rollback_to_size(&code_block->instructions, rollback_instruction_index);
            dynamic_array_rollback_to_size(&code_block->registers, rollback_register_index);
        }
    );

    bool is_binary_op = false;
    IR_Instruction_Binary_OP_Type binary_op_type;

    switch (expression_node->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
    {
        IR_Instruction call_instruction;
        call_instruction.type = IR_Instruction_Type::FUNCTION_CALL;

        Identifier_Analysis_Result function_identifier_result = semantic_analyser_analyse_identifier_node(
            analyser, symbol_table, &analyser->compiler->parser, expression_node->children[0], false
        );
        switch (function_identifier_result.type)
        {
        case Analysis_Result_Type::DEPENDENCY: {
            return expression_analysis_result_make_dependency(function_identifier_result.options.dependency);
        }
        case Analysis_Result_Type::ERROR_OCCURED:
            return expression_analysis_result_make_error();
        case Analysis_Result_Type::SUCCESS:
            break;
        }

        Type_Signature* signature = 0;
        Symbol* symbol = &function_identifier_result.options.symbol;
        switch (symbol->symbol_type)
        {
        case Symbol_Type::VARIABLE:
        {
            Type_Signature* var_type = ir_data_access_get_type(&symbol->options.variable_access);
            if (var_type->type != Signature_Type::POINTER && var_type->child_type->type != Signature_Type::FUNCTION) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL_EXPECTED_FUNCTION_POINTER;
                error.given_type = var_type;
                error.error_node_index = expression_node->children[0];
                semantic_analyser_log_error_new(analyser, error);
                return expression_analysis_result_make_error();
            }
            signature = var_type->child_type;
            call_instruction.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
            call_instruction.options.call.options.pointer_access = symbol->options.variable_access;
            break;
        }
        case Symbol_Type::FUNCTION: {
            if (symbol->options.function == analyser->program->entry_function) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::OTHERS_NO_CALLING_TO_MAIN;
                error.error_node_index = expression_index;
                semantic_analyser_log_error_new(analyser, error);
                return expression_analysis_result_make_error();
            }

            signature = symbol->options.function->function_type;
            call_instruction.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instruction.options.call.options.function = symbol->options.function;
            break;
        }
        case Symbol_Type::EXTERN_FUNCTION: {
            signature = symbol->options.extern_function.function_signature;
            call_instruction.options.call.call_type = IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL;
            call_instruction.options.call.options.extern_function = symbol->options.extern_function;
            break;
        }
        case Symbol_Type::HARDCODED_FUNCTION: {
            signature = symbol->options.hardcoded_function->signature;
            call_instruction.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            call_instruction.options.call.options.hardcoded = symbol->options.hardcoded_function;
            break;
        }
        case Symbol_Type::TYPE: {
            Semantic_Error error;
            error.type = Semantic_Error_Type::SYMBOL_EXPECTED_FUNCTION_OR_VARIABLE_ON_FUNCTION_CALL;
            error.identifier_node_index = expression_node->children[0];
            error.symbol_table = symbol_table;
            error.error_node_index = expression_node->children[0];
            semantic_analyser_log_error_new(analyser, error);
            return expression_analysis_result_make_error();
        }
        default: panic("HEY");
        }

        if (create_temporary_access) {
            *access = ir_data_access_create_intermediate(code_block, signature->return_type);
        }
        call_instruction.options.call.destination = *access;

        int arguments_node_index = expression_node->children[1];
        AST_Node* arguments_node = &analyser->compiler->parser.nodes[arguments_node_index];
        if (arguments_node->children.size != signature->parameter_types.size) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH;
            error.function_type = signature;
            error.invalid_argument_count.expected = signature->parameter_types.size;
            error.invalid_argument_count.given = arguments_node->children.size;
            error.error_node_index = expression_index;
            semantic_analyser_log_error_new(analyser, error);
            rollback_on_exit = true;
            return expression_analysis_result_make_success(signature->return_type, false, nullptr);
        }

        call_instruction.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(arguments_node->children.size);
        SCOPE_EXIT(
            if (rollback_on_exit) { dynamic_array_destroy(&call_instruction.options.call.arguments); }
            else { dynamic_array_push_back(&code_block->instructions, call_instruction); }
        );
        for (int i = 0; i < signature->parameter_types.size && i < arguments_node->children.size; i++)
        {
            IR_Data_Access argument_access;
            Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
                analyser, symbol_table, arguments_node->children[i], code_block, true, &argument_access
            );
            switch (expr_result.type)
            {
            case Analysis_Result_Type::DEPENDENCY: {
                rollback_on_exit = true;
                return expr_result;
            }
            case Analysis_Result_Type::ERROR_OCCURED: {
                break;
            }
            case Analysis_Result_Type::SUCCESS: {
                if (expr_result.options.success.result_type != signature->parameter_types[i])
                {
                    IR_Data_Access casted_argument = ir_data_access_create_intermediate(code_block, signature->parameter_types[i]);
                    if (semantic_analyser_cast_implicit_if_possible(analyser, code_block, argument_access, casted_argument)) {
                        argument_access = casted_argument;
                    }
                    else {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::INVALID_TYPE_ARGUMENT_TYPE_MISMATCH;
                        error.function_type = signature;
                        error.given_type = expr_result.options.success.result_type;
                        error.expected_type = signature->parameter_types[i];
                        error.invalid_argument_count.expected = signature->parameter_types.size;
                        error.invalid_argument_count.given = arguments_node->children.size;
                        error.error_node_index = arguments_node->children[i];
                        semantic_analyser_log_error_new(analyser, error);
                    }
                }
                dynamic_array_push_back(&call_instruction.options.call.arguments, argument_access);
                break;
            }
            default: {
                panic("What");
            }
            }
        }
        return expression_analysis_result_make_success(signature->return_type, false, nullptr);
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ:
    {
        Symbol* symbol;
        Identifier_Analysis_Result variable_identifier =
            semantic_analyser_analyse_identifier_node(analyser, symbol_table, &analyser->compiler->parser, expression_node->children[0], false);
        switch (variable_identifier.type) {
        case Analysis_Result_Type::SUCCESS:
            symbol = &variable_identifier.options.symbol;
            break;
        case Analysis_Result_Type::DEPENDENCY:
            return expression_analysis_result_make_dependency(variable_identifier.options.dependency);
        case Analysis_Result_Type::ERROR_OCCURED:
            return expression_analysis_result_make_error();
        }

        if (symbol->symbol_type == Symbol_Type::VARIABLE)
        {
            if (create_temporary_access) {
                *access = symbol->options.variable_access;
            }
            else {
                IR_Instruction move_instr;
                move_instr.type = IR_Instruction_Type::MOVE;
                move_instr.options.move.destination = *access;
                move_instr.options.move.source = symbol->options.variable_access;
                dynamic_array_push_back(&code_block->instructions, move_instr);
            }
            return expression_analysis_result_make_success(ir_data_access_get_type(&symbol->options.variable_access), true, nullptr);
        }
        else if (symbol->symbol_type == Symbol_Type::FUNCTION || symbol->symbol_type == Symbol_Type::EXTERN_FUNCTION)
        {
            IR_Instruction address_of_instr;
            address_of_instr.type = IR_Instruction_Type::ADDRESS_OF;
            Type_Signature* result_type;
            if (symbol->symbol_type == Symbol_Type::FUNCTION) {
                address_of_instr.options.address_of.type = IR_Instruction_Address_Of_Type::FUNCTION;
                address_of_instr.options.address_of.options.function = symbol->options.function;
                result_type = type_system_make_pointer(&analyser->compiler->type_system, symbol->options.function->function_type);
            }
            else {
                address_of_instr.options.address_of.type = IR_Instruction_Address_Of_Type::EXTERN_FUNCTION;
                address_of_instr.options.address_of.options.extern_function = symbol->options.extern_function;
                result_type = type_system_make_pointer(&analyser->compiler->type_system, symbol->options.extern_function.function_signature);
            }
            if (create_temporary_access) {
                *access = ir_data_access_create_intermediate(code_block, result_type);
            }
            address_of_instr.options.address_of.destination = *access;
            dynamic_array_push_back(&code_block->instructions, address_of_instr);
            // !! INFO: Here we return just the function as the type, not the function pointer
            return expression_analysis_result_make_success(result_type->child_type, false, nullptr);
        }
        else {
            Semantic_Error error;
            error.type = Semantic_Error_Type::SYMBOL_EXPECTED_VARIABLE_OR_FUNCTION_ON_VARIABLE_READ;
            error.identifier_node_index = expression_node->children[0];
            error.symbol_table = symbol_table;
            error.error_node_index = expression_index;
            semantic_analyser_log_error_new(analyser, error);
            rollback_on_exit = true;
        }
        return expression_analysis_result_make_error();
    }
    case AST_Node_Type::EXPRESSION_CAST:
    {
        Type_Analysis_Result cast_destination_result = semantic_analyser_analyse_type(analyser, symbol_table, expression_node->children[0]);
        if (cast_destination_result.type != Analysis_Result_Type::SUCCESS)
        {
            if (cast_destination_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                return expression_analysis_result_make_error();
            }
            if (cast_destination_result.type == Analysis_Result_Type::DEPENDENCY) {
                return expression_analysis_result_make_dependency(cast_destination_result.options.dependency);
            }
            panic("Should not happen");
        }

        Type_Signature* cast_destination_type = cast_destination_result.options.result_type;
        IR_Data_Access source_access;
        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[1], code_block, true, &source_access
        );
        if (expr_result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return expr_result;
        }

        Type_Signature* cast_source_type = expr_result.options.success.result_type;
        bool cast_valid = false;
        IR_Instruction_Cast_Type cast_type;
        {
            // Pointer casting
            if (cast_source_type->type == Signature_Type::POINTER && cast_destination_type->type == Signature_Type::POINTER) {
                cast_valid = true;
                cast_type = IR_Instruction_Cast_Type::POINTERS;
            }
            // U64 to Pointer
            if (cast_source_type == analyser->compiler->type_system.u64_type && cast_destination_type->type == Signature_Type::POINTER) {
                cast_valid = true;
                cast_type = IR_Instruction_Cast_Type::U64_TO_POINTER;
            }
            // Pointer to U64
            if (cast_source_type->type == Signature_Type::POINTER && cast_destination_type == analyser->compiler->type_system.u64_type) {
                cast_valid = true;
                cast_type = IR_Instruction_Cast_Type::POINTER_TO_U64;
            }
            // Primitive Casting:
            if (cast_source_type->type == Signature_Type::PRIMITIVE && cast_destination_type->type == Signature_Type::PRIMITIVE) {
                cast_valid = true;
                cast_type = IR_Instruction_Cast_Type::PRIMITIVE_TYPES;
                if (cast_source_type->primitive_type == Primitive_Type::BOOLEAN || cast_destination_type->primitive_type == Primitive_Type::BOOLEAN) {
                    cast_valid = false;
                }
            }
            // Array casting
            if (cast_source_type->type == Signature_Type::ARRAY_SIZED && cast_destination_type->type == Signature_Type::ARRAY_UNSIZED) {
                if (cast_source_type->child_type == cast_destination_type->child_type) {
                    cast_type = IR_Instruction_Cast_Type::ARRAY_SIZED_TO_UNSIZED;
                    cast_valid = true;
                }
            }
        }

        if (cast_valid)
        {
            IR_Instruction cast_instr;
            cast_instr.type = IR_Instruction_Type::CAST;
            cast_instr.options.cast.source = source_access;
            if (create_temporary_access) {
                *access = ir_data_access_create_intermediate(code_block, cast_destination_type);
            }
            cast_instr.options.cast.destination = *access;
            cast_instr.options.cast.type = cast_type;
            dynamic_array_push_back(&code_block->instructions, cast_instr);
        }
        else {
            Semantic_Error error;
            error.type = Semantic_Error_Type::EXPRESSION_INVALID_CAST;
            error.given_type = cast_source_type;
            error.expected_type = cast_destination_type;
            error.error_node_index = expression_index;
            semantic_analyser_log_error_new(analyser, error);
            rollback_on_exit = true;
        }
        return expression_analysis_result_make_success(cast_destination_type, false, nullptr);
    }
    case AST_Node_Type::EXPRESSION_LITERAL:
    {
        Token* token = &analyser->compiler->lexer.tokens[analyser->compiler->parser.token_mapping[expression_index].start_index];
        IR_Data_Access literal_access;
        Type_System* type_system = &analyser->compiler->type_system;
        void* value_ptr;
        if (token->type == Token_Type::BOOLEAN_LITERAL) {
            byte* value = new byte;
            value_ptr = value;
            *value = token->attribute.bool_value == 0 ? 0 : 1;
            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->bool_type, array_create_static<byte>(value, sizeof(bool)));
        }
        else if (token->type == Token_Type::INTEGER_LITERAL) {
            int* value = new int;
            value_ptr = value;
            *value = token->attribute.integer_value;
            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->i32_type, array_create_static<byte>((byte*)value, sizeof(int)));
        }
        else if (token->type == Token_Type::FLOAT_LITERAL) {
            float* value = new float;
            value_ptr = value;
            *value = token->attribute.float_value;
            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->f32_type, array_create_static<byte>((byte*)value, sizeof(float)));
        }
        else if (token->type == Token_Type::NULLPTR) {
            void** value = new void*;
            value_ptr = value;
            *value = nullptr;
            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->void_ptr_type, array_create_static<byte>((byte*)value, sizeof(void*)));
        }
        else if (token->type == Token_Type::STRING_LITERAL)
        {
            String string = identifier_pool_index_to_string(analyser->compiler->identifier_pool, token->attribute.identifier_number);
            Upp_String* upp_string = new Upp_String;
            upp_string->character_buffer_data = string.characters;
            upp_string->character_buffer_size = string.capacity;
            upp_string->size = string.size;
            value_ptr = upp_string;

            /*
            byte string_data[20];
            char** character_buffer_data_ptr = (char**)&string_data[0];
            int* character_buffer_size_ptr = (int*)&string_data[8];
            int* string_size_ptr = (int*)&string_data[16];
            *character_buffer_data_ptr = string.characters;
            *character_buffer_size_ptr = string.capacity;
            *string_size_ptr = string.size;
            */

            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->string_type, array_create_static<byte>((byte*)upp_string, sizeof(Upp_String)));
        }
        else {
            panic("Should not happen!");
        }

        if (create_temporary_access) {
            *access = literal_access;
        }
        else {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = *access;
            move_instr.options.move.source = literal_access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }

        return expression_analysis_result_make_success(ir_data_access_get_type(&literal_access), false, value_ptr);
    }
    case AST_Node_Type::EXPRESSION_NEW:
    {
        Type_Analysis_Result new_type_result = semantic_analyser_analyse_type(analyser, symbol_table, expression_node->children[0]);
        if (new_type_result.type != Analysis_Result_Type::SUCCESS)
        {
            if (new_type_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                return expression_analysis_result_make_error();
            }
            if (new_type_result.type == Analysis_Result_Type::DEPENDENCY) {
                return expression_analysis_result_make_dependency(new_type_result.options.dependency);
            }
            panic("Should not happen");
        }

        Type_Signature* new_type = new_type_result.options.result_type;
        if (new_type == analyser->compiler->type_system.void_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
            error.error_node_index = expression_node->children[0];
            semantic_analyser_log_error_new(analyser, error);
            return expression_analysis_result_make_error();
        }

        IR_Instruction instruction;
        instruction.type = IR_Instruction_Type::FUNCTION_CALL;
        instruction.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
        instruction.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
        dynamic_array_push_back(&instruction.options.call.arguments, ir_data_access_create_constant_i32(analyser, new_type->size_in_bytes));
        instruction.options.call.destination = ir_data_access_create_intermediate(code_block, analyser->compiler->type_system.void_ptr_type);
        instruction.options.call.options.hardcoded = analyser->program->hardcoded_functions[(int)IR_Hardcoded_Function_Type::MALLOC_SIZE_I32];
        dynamic_array_push_back(&code_block->instructions, instruction);

        Type_Signature* result_type = type_system_make_pointer(&analyser->compiler->type_system, new_type);
        // Cast to given type
        IR_Instruction cast_instr;
        cast_instr.type = IR_Instruction_Type::CAST;
        cast_instr.options.cast.type = IR_Instruction_Cast_Type::POINTERS;
        if (create_temporary_access) {
            *access = ir_data_access_create_intermediate(code_block, result_type);
        }
        cast_instr.options.cast.destination = *access;
        cast_instr.options.cast.source = instruction.options.call.destination;
        dynamic_array_push_back(&code_block->instructions, cast_instr);

        return expression_analysis_result_make_success(result_type, false, nullptr);
    }
    case AST_Node_Type::EXPRESSION_NEW_ARRAY:
    {
        Type_Analysis_Result element_type_result = semantic_analyser_analyse_type(analyser, symbol_table, expression_node->children[1]);
        if (element_type_result.type != Analysis_Result_Type::SUCCESS)
        {
            if (element_type_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                return expression_analysis_result_make_error();
            }
            if (element_type_result.type == Analysis_Result_Type::DEPENDENCY) {
                return expression_analysis_result_make_dependency(element_type_result.options.dependency);
            }
            panic("Should not happen");
        }

        Type_Signature* element_type = element_type_result.options.result_type;
        if (element_type == analyser->compiler->type_system.void_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
            error.error_node_index = expression_node->children[1];
            semantic_analyser_log_error_new(analyser, error);
            return expression_analysis_result_make_error();
        }
        Type_Signature* array_type = type_system_make_array_unsized(&analyser->compiler->type_system, element_type);

        if (create_temporary_access) {
            *access = ir_data_access_create_intermediate(code_block, array_type);
        }
        IR_Data_Access array_size_access = ir_data_access_create_intermediate(code_block,
            type_system_make_pointer(type_system, type_system->i32_type)
        );
        {
            IR_Instruction result_size_instr;
            result_size_instr.type = IR_Instruction_Type::ADDRESS_OF;
            result_size_instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
            result_size_instr.options.address_of.source = *access;
            result_size_instr.options.address_of.destination = array_size_access;
            result_size_instr.options.address_of.options.member.name_handle = analyser->token_index_size;
            result_size_instr.options.address_of.options.member.offset = 8;
            result_size_instr.options.address_of.options.member.type = type_system->i32_type;
            dynamic_array_push_back(&code_block->instructions, result_size_instr);
            array_size_access.is_memory_access = true;
        }

        Expression_Analysis_Result index_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, false, &array_size_access);
        if (index_result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return index_result;
        }
        if (index_result.options.success.result_type != analyser->compiler->type_system.i32_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE;
            error.given_type = index_result.options.success.result_type;
            error.expected_type = analyser->compiler->type_system.i32_type;
            error.error_node_index = expression_node->children[0];
            semantic_analyser_log_error_new(analyser, error);
            return expression_analysis_result_make_success(array_type, false, nullptr);
        }

        IR_Instruction size_calculation_instr;
        size_calculation_instr.type = IR_Instruction_Type::BINARY_OP;
        size_calculation_instr.options.binary_op.type = IR_Instruction_Binary_OP_Type::MULTIPLICATION;
        size_calculation_instr.options.binary_op.operand_left = array_size_access;
        int element_in_array_size = math_round_next_multiple(element_type->size_in_bytes, element_type->alignment_in_bytes);
        size_calculation_instr.options.binary_op.operand_right = ir_data_access_create_constant_i32(analyser, element_in_array_size);
        IR_Data_Access array_memory_size_access = ir_data_access_create_intermediate(code_block, analyser->compiler->type_system.i32_type);
        size_calculation_instr.options.binary_op.destination = array_memory_size_access;
        dynamic_array_push_back(&code_block->instructions, size_calculation_instr);

        IR_Data_Access array_data_access = ir_data_access_create_intermediate(code_block,
            type_system_make_pointer(type_system, type_system_make_pointer(type_system, element_type))
        );
        {
            IR_Instruction instr_pointer_access;
            instr_pointer_access.type = IR_Instruction_Type::ADDRESS_OF;
            instr_pointer_access.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
            instr_pointer_access.options.address_of.source = *access;
            instr_pointer_access.options.address_of.destination = array_data_access;
            instr_pointer_access.options.address_of.options.member.name_handle = analyser->token_index_data;
            instr_pointer_access.options.address_of.options.member.offset = 0;
            instr_pointer_access.options.address_of.options.member.type = type_system_make_pointer(type_system, element_type);
            dynamic_array_push_back(&code_block->instructions, instr_pointer_access);
            array_data_access.is_memory_access = true;
        }

        IR_Instruction instruction;
        instruction.type = IR_Instruction_Type::FUNCTION_CALL;
        instruction.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
        instruction.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
        dynamic_array_push_back(&instruction.options.call.arguments, array_memory_size_access);
        instruction.options.call.destination = ir_data_access_create_intermediate(code_block, analyser->compiler->type_system.void_ptr_type);
        instruction.options.call.options.hardcoded = analyser->program->hardcoded_functions[(int)IR_Hardcoded_Function_Type::MALLOC_SIZE_I32];
        dynamic_array_push_back(&code_block->instructions, instruction);

        // Cast to given type
        IR_Instruction cast_instr;
        cast_instr.type = IR_Instruction_Type::CAST;
        cast_instr.options.cast.type = IR_Instruction_Cast_Type::POINTERS;
        cast_instr.options.cast.destination = array_data_access;
        cast_instr.options.cast.source = instruction.options.call.destination;
        dynamic_array_push_back(&code_block->instructions, cast_instr);

        return expression_analysis_result_make_success(array_type, false, nullptr);
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
    {
        IR_Data_Access array_expr_access;
        Expression_Analysis_Result array_access_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &array_expr_access);
        if (array_access_expr_result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return array_access_expr_result;
        }
        Type_Signature* access_signature = array_access_expr_result.options.success.result_type;
        if (access_signature->type != Signature_Type::ARRAY_SIZED && access_signature->type != Signature_Type::ARRAY_UNSIZED) {
            Semantic_Error error;
            error.given_type = access_signature;
            error.type = Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS;
            error.error_node_index = expression_node->children[0];
            semantic_analyser_log_error_new(analyser, error);
            rollback_on_exit = true;
            return expression_analysis_result_make_error();
        }

        IR_Data_Access index_access;
        Expression_Analysis_Result index_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[1], code_block, true, &index_access);
        if (index_expr_result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return index_expr_result;
        }
        if (index_expr_result.options.success.result_type != analyser->compiler->type_system.i32_type) {
            Semantic_Error error;
            error.given_type = index_expr_result.options.success.result_type;
            error.expected_type = analyser->compiler->type_system.i32_type;
            error.type = Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX;
            error.error_node_index = expression_node->children[1];
            semantic_analyser_log_error_new(analyser, error);
            return expression_analysis_result_make_success(access_signature->child_type, true, nullptr);
        }

        IR_Instruction instruction;
        instruction.type = IR_Instruction_Type::ADDRESS_OF;
        instruction.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
        instruction.options.address_of.source = array_expr_access;
        instruction.options.address_of.options.index_access = index_access;
        instruction.options.address_of.destination = ir_data_access_create_intermediate(code_block,
            type_system_make_pointer(&analyser->compiler->type_system, access_signature->child_type)
        );
        dynamic_array_push_back(&code_block->instructions, instruction);

        if (create_temporary_access) {
            *access = instruction.options.address_of.destination;
            access->is_memory_access = true;
        }
        else {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = instruction.options.address_of.destination;
            move_instr.options.move.source.is_memory_access = true;
            move_instr.options.move.destination = *access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }

        return expression_analysis_result_make_success(access_signature->child_type, true, nullptr);
    }
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
    {
        IR_Data_Access expr_access;
        Expression_Analysis_Result access_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &expr_access
        );
        if (access_expr_result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return access_expr_result;
        }

        IR_Instruction access_instr;
        access_instr.type = IR_Instruction_Type::ADDRESS_OF;
        access_instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
        Type_Signature* type_signature = access_expr_result.options.success.result_type;
        if (type_signature->type == Signature_Type::POINTER) {
            if (type_signature->child_type->type == Signature_Type::STRUCT) {
                type_signature = type_signature->child_type;
                access_instr.options.address_of.source = ir_data_access_dereference_pointer(code_block, expr_access);
            }
        }
        else {
            access_instr.options.address_of.source = expr_access;
        }

        Type_Signature* member_type = 0;
        if (type_signature->type == Signature_Type::STRUCT)
        {
            Struct_Member* found = 0;
            for (int i = 0; i < type_signature->member_types.size; i++) {
                Struct_Member* member = &type_signature->member_types[i];
                if (member->name_handle == expression_node->name_id) {
                    found = member;
                }
            }
            if (found == 0) {
                if (type_signature->size_in_bytes == 0 && type_signature->alignment_in_bytes == 0) {
                    rollback_on_exit = true;
                    return expression_analysis_result_make_dependency(workload_dependency_make_type_size_unknown(type_signature, expression_index));
                }
                else {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND;
                    error.given_type = type_signature;
                    error.error_node_index = expression_index;
                    semantic_analyser_log_error_new(analyser, error);
                    return expression_analysis_result_make_error();
                }
            }

            access_instr.options.address_of.options.member = *found;
            member_type = found->type;
        }
        else if (type_signature->type == Signature_Type::ARRAY_SIZED || type_signature->type == Signature_Type::ARRAY_UNSIZED)
        {
            if (expression_node->name_id != analyser->token_index_size && expression_node->name_id != analyser->token_index_data) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND;
                error.given_type = type_signature;
                error.error_node_index = expression_index;
                semantic_analyser_log_error_new(analyser, error);
                rollback_on_exit = true;
                return expression_analysis_result_make_error();
            }
            if (type_signature->type == Signature_Type::ARRAY_UNSIZED)
            {
                if (expression_node->name_id == analyser->token_index_size) {
                    member_type = analyser->compiler->type_system.i32_type;
                    access_instr.options.address_of.options.member.name_handle = expression_node->name_id;
                    access_instr.options.address_of.options.member.offset = 8;
                    access_instr.options.address_of.options.member.type = member_type;
                }
                else {
                    member_type = type_system_make_pointer(&analyser->compiler->type_system, type_signature->child_type);
                    access_instr.options.address_of.options.member.name_handle = expression_node->name_id;
                    access_instr.options.address_of.options.member.offset = 0;
                    access_instr.options.address_of.options.member.type = member_type;
                }
            }
            else // Array_Sized
            {
                if (expression_node->name_id == analyser->token_index_size)
                {
                    IR_Instruction move_instr;
                    move_instr.type = IR_Instruction_Type::MOVE;
                    move_instr.options.move.source = ir_data_access_create_constant_i32(analyser, type_signature->array_element_count);
                    if (create_temporary_access) {
                        *access = ir_data_access_create_intermediate(code_block, analyser->compiler->type_system.i32_type);
                    }
                    move_instr.options.move.destination = *access;
                    dynamic_array_push_back(&code_block->instructions, move_instr);
                    return expression_analysis_result_make_success(analyser->compiler->type_system.i32_type, false, nullptr);
                }
                else
                {
                    Type_Signature* array_ptr_type = type_system_make_pointer(&analyser->compiler->type_system, type_signature);
                    IR_Instruction address_of_instr;
                    address_of_instr.type = IR_Instruction_Type::ADDRESS_OF;
                    address_of_instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
                    address_of_instr.options.address_of.source = access_instr.options.address_of.source;
                    address_of_instr.options.address_of.destination = ir_data_access_create_intermediate(code_block, array_ptr_type);
                    dynamic_array_push_back(&code_block->instructions, address_of_instr);

                    Type_Signature* base_ptr_type = type_system_make_pointer(&analyser->compiler->type_system, type_signature->child_type);
                    IR_Instruction cast_instr;
                    cast_instr.type = IR_Instruction_Type::CAST;
                    cast_instr.options.cast.source = address_of_instr.options.address_of.destination;
                    if (create_temporary_access) {
                        *access = ir_data_access_create_intermediate(code_block, base_ptr_type);
                    }
                    cast_instr.options.cast.destination = *access;
                    cast_instr.options.cast.type = IR_Instruction_Cast_Type::POINTERS;
                    dynamic_array_push_back(&code_block->instructions, cast_instr);

                    return expression_analysis_result_make_success(base_ptr_type, false, nullptr);
                }
            }
        }
        else
        {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS;
            error.given_type = type_signature;
            error.error_node_index = expression_index;
            semantic_analyser_log_error_new(analyser, error);
            rollback_on_exit = true;
            return expression_analysis_result_make_error();
        }

        access_instr.options.address_of.destination = ir_data_access_create_intermediate(code_block, type_system_make_pointer(type_system, member_type));
        dynamic_array_push_back(&code_block->instructions, access_instr);

        if (create_temporary_access) {
            *access = access_instr.options.address_of.destination;
            access->is_memory_access = true;
        }
        else {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = access_instr.options.address_of.destination;
            move_instr.options.move.source.is_memory_access = true;
            move_instr.options.move.destination = *access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }

        return expression_analysis_result_make_success(member_type, true, nullptr);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
    {
        IR_Data_Access operand_access;
        Expression_Analysis_Result operand_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &operand_access
        );
        if (operand_result.type != Analysis_Result_Type::SUCCESS) {
            if (operand_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                return expression_analysis_result_make_success(type_system->bool_type, false, nullptr);
            }
            if (operand_result.type == Analysis_Result_Type::DEPENDENCY) {
                return operand_result;
            }
            panic("Should not happen");
        }
        if (operand_result.options.success.result_type != type_system->bool_type) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR;
            error.given_type = operand_result.options.success.result_type;
            error.error_node_index = expression_index;
            semantic_analyser_log_error_new(analyser, error);
            rollback_on_exit = true;
            return expression_analysis_result_make_success(type_system->bool_type, false, nullptr);
        }

        IR_Instruction not_instr;
        not_instr.type = IR_Instruction_Type::UNARY_OP;
        not_instr.options.unary_op.source = operand_access;
        not_instr.options.unary_op.type = IR_Instruction_Unary_OP_Type::NOT;
        if (create_temporary_access) {
            *access = ir_data_access_create_intermediate(code_block, type_system->bool_type);
        }
        not_instr.options.unary_op.destination = *access;
        dynamic_array_push_back(&code_block->instructions, not_instr);
        return expression_analysis_result_make_success(type_system->bool_type, false, nullptr);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
    {
        IR_Data_Access operand_access;
        Expression_Analysis_Result operand_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &operand_access
        );
        if (operand_result.type != Analysis_Result_Type::SUCCESS) {
            return operand_result;
        }

        Type_Signature* operand_type = operand_result.options.success.result_type;
        if (operand_type->type != Signature_Type::PRIMITIVE) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR;
            error.given_type = operand_result.options.success.result_type;
            error.error_node_index = expression_index;
            semantic_analyser_log_error_new(analyser, error);
            rollback_on_exit = true;
            return expression_analysis_result_make_error();
        }
        if (!primitive_type_is_float(operand_type->primitive_type))
        {
            if (!primitive_type_is_integer(operand_type->primitive_type)) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR;
                error.given_type = operand_result.options.success.result_type;
                error.error_node_index = expression_index;
                semantic_analyser_log_error_new(analyser, error);
                rollback_on_exit = true;
                return expression_analysis_result_make_error();
            }
            else {
                if (!primitive_type_is_signed(operand_type->primitive_type)) {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR;
                    error.error_node_index = expression_index;
                    error.given_type = operand_result.options.success.result_type;
                    semantic_analyser_log_error_new(analyser, error);
                    rollback_on_exit = true;
                    return expression_analysis_result_make_error();
                }
            }
        }

        IR_Instruction negate_instr;
        negate_instr.type = IR_Instruction_Type::UNARY_OP;
        negate_instr.options.unary_op.source = operand_access;
        negate_instr.options.unary_op.type = IR_Instruction_Unary_OP_Type::NEGATE;
        if (create_temporary_access) {
            *access = ir_data_access_create_intermediate(code_block, operand_type);
        }
        negate_instr.options.unary_op.destination = *access;
        dynamic_array_push_back(&code_block->instructions, negate_instr);
        return expression_analysis_result_make_success(operand_type, false, nullptr);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
    {
        AST_Node* child_node = &analyser->compiler->parser.nodes[expression_node->children[0]];

        IR_Data_Access expr_access;
        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &expr_access
        );
        if (expr_result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return expr_result;
        }
        Type_Signature* pointer_type = type_system_make_pointer(type_system, expr_result.options.success.result_type);
        // Special Case, see Expression_Variable_Read how this works
        if (pointer_type->child_type->type == Signature_Type::FUNCTION)
        {
            if (create_temporary_access) {
                *access = expr_access;
            }
            else {
                // In this case a temporary access was already created, now i have to remove it
                IR_Instruction* function_access_instr = &code_block->instructions[code_block->instructions.size - 1];
                function_access_instr->options.address_of.destination = *access;
                dynamic_array_rollback_to_size(&code_block->registers, code_block->registers.size - 1);
            }
            return expression_analysis_result_make_success(pointer_type, false, nullptr);
        }

        if (!expr_result.options.success.has_memory_address) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::EXPRESSION_ADDRESS_OF_REQUIRES_MEMORY_ADDRESS;
            error.error_node_index = expression_index;
            semantic_analyser_log_error_new(analyser, error);
        }
        if (expr_access.is_memory_access)
        {
            if (create_temporary_access) {
                *access = expr_access;
                access->is_memory_access = false;
                return expression_analysis_result_make_success(pointer_type, false, nullptr);
            }
            else {
                IR_Instruction move_instr;
                move_instr.type = IR_Instruction_Type::MOVE;
                move_instr.options.move.source = expr_access;
                move_instr.options.move.source.is_memory_access = false;
                move_instr.options.move.destination = *access;
                dynamic_array_push_back(&code_block->instructions, move_instr);
                return expression_analysis_result_make_success(pointer_type, false, nullptr);
            }
        }

        IR_Instruction address_of_instr;
        address_of_instr.type = IR_Instruction_Type::ADDRESS_OF;
        address_of_instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
        address_of_instr.options.address_of.source = expr_access;
        if (create_temporary_access) {
            *access = ir_data_access_create_intermediate(code_block, pointer_type);
        }
        address_of_instr.options.address_of.destination = *access;
        dynamic_array_push_back(&code_block->instructions, address_of_instr);
        return expression_analysis_result_make_success(pointer_type, false, nullptr);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
    {
        IR_Data_Access pointer_access;
        Expression_Analysis_Result result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &pointer_access
        );
        if (result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return result;
        }

        Type_Signature* signature = result.options.success.result_type;
        if (signature->type != Signature_Type::POINTER) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR;
            error.given_type = signature;
            error.error_node_index = expression_index;
            semantic_analyser_log_error_new(analyser, error);
            rollback_on_exit = true;
            return expression_analysis_result_make_error();
        }

        IR_Data_Access result_access = ir_data_access_dereference_pointer(code_block, pointer_access);;
        if (create_temporary_access) {
            *access = result_access;
        }
        else {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = *access;
            move_instr.options.move.source = result_access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }

        return expression_analysis_result_make_success(signature->child_type, true, nullptr);
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::ADDITION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::SUBTRACTION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::DIVISION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::MULTIPLICATION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::GREATER_THAN;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::GREATER_EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::LESS_THAN;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::LESS_EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::MODULO;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::AND;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::OR;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::NOT_EQUAL;
        break;
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    if (is_binary_op)
    {
        IR_Data_Access left_access;
        IR_Data_Access right_access;
        Expression_Analysis_Result left_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &left_access
        );
        if (left_expr_result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return left_expr_result;
        }
        Expression_Analysis_Result right_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[1], code_block, true, &right_access
        );
        if (right_expr_result.type != Analysis_Result_Type::SUCCESS) {
            rollback_on_exit = true;
            return right_expr_result;
        }

        // Try implicit casting if types dont match
        Type_Signature* left_type = left_expr_result.options.success.result_type;
        Type_Signature* right_type = right_expr_result.options.success.result_type;
        Type_Signature* operand_type = left_type;
        if (left_type != right_type)
        {
            IR_Data_Access casted_access = ir_data_access_create_intermediate(code_block, right_type);
            bool left_to_right_worked = false;
            if (semantic_analyser_cast_implicit_if_possible(analyser, code_block, left_access, casted_access)) {
                operand_type = right_type;
                left_access = casted_access;
                left_to_right_worked = true;
            }
            bool right_to_left_worked = false;
            if (!left_to_right_worked)
            {
                code_block->registers[casted_access.index] = left_type;
                if (semantic_analyser_cast_implicit_if_possible(analyser, code_block, right_access, casted_access)) {
                    operand_type = left_type;
                    right_access = casted_access;
                    right_to_left_worked = true;
                }
            }
            if (!right_to_left_worked && !left_to_right_worked) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH;
                error.binary_op_left_type = left_type;
                error.binary_op_right_type = right_type;
                error.error_node_index = expression_index;
                semantic_analyser_log_error_new(analyser, error);
                rollback_on_exit = true;
                return expression_analysis_result_make_error();
            }
        }

        // Determine what operands are valid
        bool int_valid = false;
        bool float_valid = false;
        bool bool_valid = false;
        bool ptr_valid = false;
        Type_Signature* result_type = operand_type;
        switch (binary_op_type)
        {
        case IR_Instruction_Binary_OP_Type::ADDITION:
        case IR_Instruction_Binary_OP_Type::SUBTRACTION:
        case IR_Instruction_Binary_OP_Type::MULTIPLICATION:
        case IR_Instruction_Binary_OP_Type::DIVISION:
            result_type = operand_type;
            float_valid = true;
            int_valid = true;
            break;
        case IR_Instruction_Binary_OP_Type::GREATER_THAN:
        case IR_Instruction_Binary_OP_Type::GREATER_EQUAL:
        case IR_Instruction_Binary_OP_Type::LESS_THAN:
        case IR_Instruction_Binary_OP_Type::LESS_EQUAL:
            float_valid = true;
            int_valid = true;
            result_type = type_system->bool_type;
            break;
        case IR_Instruction_Binary_OP_Type::MODULO:
            int_valid = true;
            result_type = operand_type;
            break;
        case IR_Instruction_Binary_OP_Type::EQUAL:
        case IR_Instruction_Binary_OP_Type::NOT_EQUAL:
            float_valid = true;
            int_valid = true;
            bool_valid = true;
            ptr_valid = true;
            result_type = type_system->bool_type;
            break;
        case IR_Instruction_Binary_OP_Type::AND:
        case IR_Instruction_Binary_OP_Type::OR:
            bool_valid = true;
            result_type = type_system->bool_type;
            break;
        }

        if (operand_type->type == Signature_Type::POINTER)
        {
            if (!ptr_valid) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR;
                error.binary_op_left_type = left_type;
                error.binary_op_right_type = right_type;
                error.error_node_index = expression_index;
                semantic_analyser_log_error_new(analyser, error);
                rollback_on_exit = true;
                return expression_analysis_result_make_error();
            }
        }
        else
        {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR;
            error.binary_op_left_type = left_type;
            error.binary_op_right_type = right_type;
            error.error_node_index = expression_index;
            if (operand_type->type != Signature_Type::PRIMITIVE) {
                semantic_analyser_log_error_new(analyser, error);
                rollback_on_exit = true;
                return expression_analysis_result_make_error();
            }
            if (primitive_type_is_integer(operand_type->primitive_type) && !int_valid) {
                semantic_analyser_log_error_new(analyser, error);
                rollback_on_exit = true;
                return expression_analysis_result_make_error();
            }
            if (primitive_type_is_float(operand_type->primitive_type) && !float_valid) {
                semantic_analyser_log_error_new(analyser, error);
                rollback_on_exit = true;
                return expression_analysis_result_make_error();
            }
            if (operand_type->primitive_type == Primitive_Type::BOOLEAN && !bool_valid) {
                semantic_analyser_log_error_new(analyser, error);
                rollback_on_exit = true;
                return expression_analysis_result_make_error();
            }
        }

        IR_Instruction binary_op_instr;
        binary_op_instr.type = IR_Instruction_Type::BINARY_OP;
        binary_op_instr.options.binary_op.type = binary_op_type;
        binary_op_instr.options.binary_op.operand_left = left_access;
        binary_op_instr.options.binary_op.operand_right = right_access;
        if (create_temporary_access) {
            *access = ir_data_access_create_intermediate(code_block, result_type);
        }
        binary_op_instr.options.binary_op.destination = *access;
        dynamic_array_push_back(&code_block->instructions, binary_op_instr);

        return expression_analysis_result_make_success(result_type, false, nullptr);
    }

    panic("Should not happen");
    return expression_analysis_result_make_error();
}

Variable_Creation_Analysis_Result semantic_analyser_analyse_variable_creation_statements(
    Semantic_Analyser* analyser, Symbol_Table* symbol_table, int statement_index, IR_Code_Block* code_block, bool is_global)
{
    AST_Node* statement = &analyser->compiler->parser.nodes[statement_index];
    bool needs_expression_evaluation;
    bool type_is_given;
    int expression_index;
    int type_node_index;
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
    {
        needs_expression_evaluation = false;
        type_is_given = true;
        type_node_index = statement->children[0];
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
    {
        needs_expression_evaluation = true;
        expression_index = statement->children[1];
        type_is_given = true;
        type_node_index = statement->children[0];
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
    {
        needs_expression_evaluation = true;
        expression_index = statement->children[0];
        type_is_given = false;
        break;
        break;
    }
    default:
        panic("Should not happen!");
    }

    Type_Signature* definition_type = 0;
    if (type_is_given)
    {
        Type_Analysis_Result definition_result = semantic_analyser_analyse_type(analyser, symbol_table, type_node_index);
        switch (definition_result.type)
        {
        case Analysis_Result_Type::SUCCESS:
            definition_type = definition_result.options.result_type;
            if (definition_type == analyser->compiler->type_system.void_type) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
                error.error_node_index = type_node_index;
                semantic_analyser_log_error_new(analyser, error);
                definition_type = analyser->compiler->type_system.error_type;
            }
            break;
        case Analysis_Result_Type::ERROR_OCCURED:
            definition_type = analyser->compiler->type_system.error_type;
            break;
        case Analysis_Result_Type::DEPENDENCY: {
            Variable_Creation_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.dependency = definition_result.options.dependency;
            return result;
        }
        }
    }

    IR_Data_Access variable_access;
    int rollback_data_access_index;
    {
        variable_access.is_memory_access = false;
        if (is_global) {
            variable_access.type = IR_Data_Access_Type::GLOBAL_DATA;
            dynamic_array_push_back(&code_block->function->program->globals, analyser->compiler->type_system.void_type);
            variable_access.option.program = code_block->function->program;
            rollback_data_access_index = code_block->function->program->globals.size - 1;
        }
        else {
            variable_access.type = IR_Data_Access_Type::REGISTER;
            dynamic_array_push_back(&code_block->registers, analyser->compiler->type_system.void_type);
            variable_access.option.definition_block = code_block;
            rollback_data_access_index = code_block->registers.size - 1;
        }
        variable_access.index = rollback_data_access_index;
    }

    Type_Signature* infered_type = 0;
    if (needs_expression_evaluation)
    {
        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_index, code_block, false, &variable_access
        );
        switch (expr_result.type)
        {
        case Analysis_Result_Type::SUCCESS:
            infered_type = expr_result.options.success.result_type;
            if (infered_type == analyser->compiler->type_system.void_type) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
                error.error_node_index = expression_index;
                semantic_analyser_log_error_new(analyser, error);
                definition_type = analyser->compiler->type_system.error_type;
            }
            break;
        case Analysis_Result_Type::ERROR_OCCURED:
            infered_type = analyser->compiler->type_system.error_type;
            break;
        case Analysis_Result_Type::DEPENDENCY: {
            Variable_Creation_Analysis_Result result;
            result.type = Analysis_Result_Type::DEPENDENCY;
            result.dependency = expr_result.options.dependency;
            if (is_global) {
                dynamic_array_rollback_to_size(&code_block->function->program->globals, rollback_data_access_index);
            }
            else {
                dynamic_array_rollback_to_size(&code_block->registers, rollback_data_access_index);
            }
            return result;
        }
        }
    }

    // Change temporary variable type to correct type
    {
        Type_Signature* final_type;
        if (type_is_given) {
            final_type = definition_type;
            if (needs_expression_evaluation) {
                if (final_type != infered_type) {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT;
                    error.given_type = infered_type;
                    error.expected_type = final_type;
                    error.error_node_index = expression_index;
                    semantic_analyser_log_error_new(analyser, error);
                }
            }
        }
        else {
            final_type = infered_type;
        }
        if (is_global) {
            code_block->function->program->globals[rollback_data_access_index] = final_type;
        }
        else {
            code_block->registers[rollback_data_access_index] = final_type;
        }
    }

    Symbol var_symbol;
    var_symbol.symbol_type = Symbol_Type::VARIABLE;
    var_symbol.name_handle = statement->name_id;
    var_symbol.is_templated = false;
    var_symbol.definition_node_index = statement_index;
    var_symbol.options.variable_access = variable_access;
    symbol_table_define_symbol(symbol_table, analyser, var_symbol, true);

    Variable_Creation_Analysis_Result result;
    result.type = Analysis_Result_Type::SUCCESS;
    return result;
}

void semantic_analyser_find_workloads_recursively(Semantic_Analyser* analyser, Symbol_Table* parent_table, int node_index)
{
    AST_Node* node = &analyser->compiler->parser.nodes[node_index];

    if (node->type != AST_Node_Type::ROOT && node->type != AST_Node_Type::MODULE && node->type != AST_Node_Type::MODULE_TEMPLATED) {
        panic("Should not happen");
    }

    Symbol_Table* symbol_table = 0;
    AST_Node* definitions_node;
    bool inside_template = false;
    Dynamic_Array<int> template_parameter_names;
    {
        if (node->type == AST_Node_Type::ROOT) {
            symbol_table = analyser->root_table;
            definitions_node = &analyser->compiler->parser.nodes[node->children[0]];
        }
        else if (node->type == AST_Node_Type::MODULE)
        {
            if (hashtable_find_element(&parent_table->modules, node->name_id) != 0) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED;
                error.symbol_table = parent_table;
                error.name_id = node->name_id;
                error.error_node_index = node_index;
                semantic_analyser_log_error_new(analyser, error);
                return;
            }
            symbol_table = symbol_table_create(analyser, parent_table, node_index, true);
            Symbol_Table_Module table_module;
            table_module.is_templated = false;
            table_module.module_table = symbol_table;
            hashtable_insert_element(&parent_table->modules, node->name_id, table_module);
            definitions_node = &analyser->compiler->parser.nodes[node->children[0]];
        }
        else if (node->type == AST_Node_Type::MODULE_TEMPLATED)
        {
            inside_template = true;
            definitions_node = &analyser->compiler->parser.nodes[node->children[1]];
            if (hashtable_find_element(&parent_table->modules, node->name_id) != 0) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED;
                error.symbol_table = parent_table;
                error.name_id = node->name_id;
                error.error_node_index = node_index;
                semantic_analyser_log_error_new(analyser, error);
                return;
            }
            symbol_table = symbol_table_create(analyser, parent_table, node_index, true);

            AST_Node* template_parameter_node = &analyser->compiler->parser.nodes[node->children[0]];
            template_parameter_names = dynamic_array_create_empty<int>(template_parameter_node->children.size);
            for (int i = 0; i < template_parameter_node->children.size; i++)
            {
                AST_Node* identifier_node = &analyser->compiler->parser.nodes[template_parameter_node->children[i]];
                Symbol symbol;
                symbol.symbol_type = Symbol_Type::TYPE;
                symbol.name_handle = identifier_node->name_id;
                symbol.definition_node_index = node->children[i];
                symbol.is_templated = false;
                Type_Signature template_type;
                template_type.type = Signature_Type::TEMPLATE_TYPE;
                template_type.size_in_bytes = 1;
                template_type.alignment_in_bytes = 1;
                template_type.template_name = identifier_node->name_id;
                symbol.options.data_type = type_system_register_type(&analyser->compiler->type_system, template_type);
                dynamic_array_push_back(&template_parameter_names, symbol.name_handle);
                symbol_table_define_symbol(symbol_table, analyser, symbol, false);
            }

            Symbol_Table_Module table_module;
            table_module.is_templated = true;
            table_module.module_table = symbol_table;
            table_module.template_parameter_names = template_parameter_names;
            hashtable_insert_element(&parent_table->modules, node->name_id, table_module);
        }
        else {
            panic("Cannot happen");
            definitions_node = 0;
        }
    }

    assert(definitions_node->type == AST_Node_Type::DEFINITIONS, "HEY");
    for (int i = 0; i < definitions_node->children.size; i++)
    {
        int child_index = definitions_node->children[i];
        AST_Node* top_level_node = &analyser->compiler->parser.nodes[child_index];
        switch (top_level_node->type)
        {
        case AST_Node_Type::MODULE:
            if (inside_template) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES;
                error.error_node_index = child_index;
                semantic_analyser_log_error_new(analyser, error);
                continue;
            }
            semantic_analyser_find_workloads_recursively(analyser, symbol_table, child_index);
            break;
        case AST_Node_Type::MODULE_TEMPLATED:
            if (inside_template) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES;
                error.error_node_index = child_index;
                semantic_analyser_log_error_new(analyser, error);
                continue;
            }
            semantic_analyser_find_workloads_recursively(analyser, symbol_table, child_index);
            break;
        case AST_Node_Type::EXTERN_FUNCTION_DECLARATION:
        {
            if (inside_template) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES;
                error.error_node_index = child_index;
                semantic_analyser_log_error_new(analyser, error);
                break;
            }
            // Create Workload
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION;
            workload.node_index = child_index;
            workload.symbol_table = symbol_table;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case AST_Node_Type::EXTERN_HEADER_IMPORT:
        {
            if (inside_template) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES;
                error.error_node_index = child_index;
                semantic_analyser_log_error_new(analyser, error);
                break;
            }
            // Create Workload
            Analysis_Workload workload;
            workload.type = Analysis_Workload_Type::EXTERN_HEADER_IMPORT;
            workload.node_index = child_index;
            workload.symbol_table = symbol_table;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case AST_Node_Type::EXTERN_LIB_IMPORT: {
            if (inside_template) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES;
                error.error_node_index = child_index;
                semantic_analyser_log_error_new(analyser, error);
                break;
            }
            dynamic_array_push_back(&analyser->program->extern_program_sources.lib_files, top_level_node->name_id);
            break;
        }
        case AST_Node_Type::FUNCTION:
        {
            Analysis_Workload workload;
            workload.symbol_table = symbol_table;
            workload.type = Analysis_Workload_Type::FUNCTION_HEADER;
            workload.node_index = child_index;
            workload.options.function_header.type_lookup_table = symbol_table;
            workload.options.function_header.is_template_instance = false;
            workload.options.function_header.is_template_analysis = inside_template;
            if (inside_template) {
                workload.options.function_header.template_parameter_names = dynamic_array_create_copy(
                    template_parameter_names.data, template_parameter_names.size
                );
            }
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        case AST_Node_Type::STRUCT:
        {
            AST_Node* struct_node = top_level_node;
            Type_Signature* signature;
            {
                Type_Signature struct_sig;
                struct_sig.type = Signature_Type::STRUCT;
                struct_sig.member_types = dynamic_array_create_empty<Struct_Member>(struct_node->children.size);
                struct_sig.alignment_in_bytes = 0;
                struct_sig.size_in_bytes = 0;
                struct_sig.struct_name_handle = struct_node->name_id;
                signature = type_system_register_type(&analyser->compiler->type_system, struct_sig);
            }
            {
                Symbol s;
                s.symbol_type = Symbol_Type::TYPE;
                s.options.data_type = signature;
                s.name_handle = struct_node->name_id;
                s.definition_node_index = child_index;
                s.is_templated = inside_template;
                if (inside_template) {
                    s.template_instances = dynamic_array_create_empty<Symbol_Template_Instance>(2);
                    s.template_parameter_names = dynamic_array_create_copy(
                        template_parameter_names.data, template_parameter_names.size
                    );
                }
                symbol_table_define_symbol(symbol_table, analyser, s, false);
            }

            if (struct_node->children.size == 0) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::OTHERS_STRUCT_MUST_CONTAIN_MEMBER;
                error.error_node_index = child_index;
                semantic_analyser_log_error_new(analyser, error);
                break;
            }

            // Prepare struct body workload
            {
                Analysis_Workload body_workload;
                body_workload.node_index = child_index;
                body_workload.symbol_table = symbol_table;
                body_workload.type = Analysis_Workload_Type::STRUCT_BODY;
                body_workload.options.struct_body.struct_signature = signature;
                body_workload.options.struct_body.current_child_index = 0;
                body_workload.options.struct_body.type_lookup_table = symbol_table;
                body_workload.options.struct_body.offset = 0;
                body_workload.options.struct_body.alignment = 0;
                body_workload.options.struct_body.is_template_instance = false;
                dynamic_array_push_back(&analyser->active_workloads, body_workload);
            }
            break;
        }
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION: {
            if (inside_template) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_TEMPLATED_GLOBALS;
                error.error_node_index = node_index;
                semantic_analyser_log_error_new(analyser, error);
                continue;
            }
            Analysis_Workload workload;
            workload.symbol_table = symbol_table;
            workload.type = Analysis_Workload_Type::GLOBAL;
            workload.node_index = child_index;
            dynamic_array_push_back(&analyser->active_workloads, workload);
            break;
        }
        default: panic("HEy");
        }
    }
}

enum class Defer_Resolve_Depth
{
    WHOLE_FUNCTION,
    LOCAL_BLOCK,
    LOOP_EXIT
};

void workload_code_block_work_through_defers(Semantic_Analyser* analyser, Analysis_Workload* workload, Defer_Resolve_Depth resolve_depth)
{
    assert(workload->type == Analysis_Workload_Type::CODE_BLOCK, "Wrong type budyd");
    Analysis_Workload_Code_Block* block_workload = &workload->options.code_block;
    for (int i = block_workload->active_defer_statements.size - 1; i >= 0; i--)
    {
        bool end_loop = false;
        switch (resolve_depth)
        {
        case Defer_Resolve_Depth::WHOLE_FUNCTION:
            end_loop = false;
            break;
        case Defer_Resolve_Depth::LOCAL_BLOCK: {
            end_loop = i < block_workload->local_block_defer_depth;
            break;
        }
        case Defer_Resolve_Depth::LOOP_EXIT: {
            end_loop = i < block_workload->surrounding_loop_defer_depth;
            break;
        }
        default: panic("what");
        }
        if (end_loop) break;

        IR_Code_Block* defer_block = ir_code_block_create(block_workload->code_block->function);
        Analysis_Workload defer_workload;
        defer_workload.type = Analysis_Workload_Type::CODE_BLOCK;
        defer_workload.node_index = block_workload->active_defer_statements[i];
        defer_workload.symbol_table = symbol_table_create(analyser, workload->symbol_table, defer_workload.node_index, false);
        defer_workload.options.code_block.active_defer_statements = dynamic_array_create_empty<int>(4);
        defer_workload.options.code_block.code_block = defer_block;
        defer_workload.options.code_block.current_child_index = 0;
        defer_workload.options.code_block.inside_defer = true;
        defer_workload.options.code_block.local_block_defer_depth = 0;
        defer_workload.options.code_block.surrounding_loop_defer_depth = 0;
        defer_workload.options.code_block.inside_loop = false; // Defers cannot break out of loops, I guess
        defer_workload.options.code_block.requires_return = false;
        defer_workload.options.code_block.check_last_instruction_result = false;
        dynamic_array_push_back(&analyser->active_workloads, defer_workload);

        IR_Instruction block_instr;
        block_instr.type = IR_Instruction_Type::BLOCK;
        block_instr.options.block = defer_block;
        dynamic_array_push_back(&block_workload->code_block->instructions, block_instr);
    }
    dynamic_array_reset(&block_workload->active_defer_statements);
}

Analysis_Workload analysis_workload_make_code_block(Semantic_Analyser* analyser, int block_index, IR_Code_Block* code_block, Analysis_Workload* current_work)
{
    assert(current_work->type == Analysis_Workload_Type::CODE_BLOCK, "HEY");
    Analysis_Workload_Code_Block* block_workload = &current_work->options.code_block;
    Analysis_Workload new_workload;
    new_workload.node_index = block_index;
    new_workload.symbol_table = symbol_table_create(analyser, current_work->symbol_table, block_index, !block_workload->inside_defer);
    new_workload.type = Analysis_Workload_Type::CODE_BLOCK;
    new_workload.options.code_block.active_defer_statements = dynamic_array_create_empty<int>(block_workload->active_defer_statements.size + 1);
    for (int i = 0; i < block_workload->active_defer_statements.size; i++) {
        dynamic_array_push_back(&new_workload.options.code_block.active_defer_statements, block_workload->active_defer_statements[i]);
    }
    new_workload.options.code_block.code_block = code_block;
    new_workload.options.code_block.current_child_index = 0;
    new_workload.options.code_block.inside_defer = block_workload->inside_defer;
    new_workload.options.code_block.inside_loop = block_workload->inside_loop;
    new_workload.options.code_block.local_block_defer_depth = block_workload->active_defer_statements.size;
    new_workload.options.code_block.surrounding_loop_defer_depth = current_work->options.code_block.surrounding_loop_defer_depth;
    new_workload.options.code_block.requires_return = false;
    new_workload.options.code_block.check_last_instruction_result = false;
    return new_workload;
}

void analysis_workload_destroy(Analysis_Workload* workload)
{
    switch (workload->type)
    {
    case Analysis_Workload_Type::STRUCT_BODY:
    case Analysis_Workload_Type::GLOBAL:
    case Analysis_Workload_Type::SIZED_ARRAY_SIZE:
    case Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION:
    case Analysis_Workload_Type::EXTERN_HEADER_IMPORT:
        break;
    case Analysis_Workload_Type::FUNCTION_HEADER:
        if (workload->options.function_header.is_template_analysis) {
            if (workload->options.function_header.template_parameter_names.data != 0) {
                dynamic_array_destroy(&workload->options.function_header.template_parameter_names);
            }
        }
        break;
    case Analysis_Workload_Type::CODE_BLOCK:
        dynamic_array_destroy(&workload->options.code_block.active_defer_statements);
        break;
    default: panic("Hey");
    }
}

void workload_dependency_destroy(Workload_Dependency* dependency)
{
    switch (dependency->type)
    {
    case Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED:
    case Workload_Dependency_Type::TEMPLATE_INSTANCE_NOT_FINISHED:
    case Workload_Dependency_Type::TYPE_SIZE_UNKNOWN:
        break;
    case Workload_Dependency_Type::IDENTIFER_NOT_FOUND:
        dynamic_array_destroy(&dependency->options.identifier_not_found.template_parameter_names);
        break;
    default: panic("What");
    }
}

void analysis_workload_append_to_string(Analysis_Workload* workload, String* string, Semantic_Analyser* analyser)
{
    switch (workload->type)
    {
    case Analysis_Workload_Type::CODE_BLOCK: {
        string_append_formated(string, "Code_Block");
        break;
    }
    case Analysis_Workload_Type::FUNCTION_HEADER: {
        string_append_formated(string, "Function Header, name: %s",
            identifier_pool_index_to_string(analyser->compiler->identifier_pool, analyser->compiler->parser.nodes[workload->node_index].name_id).characters
        );
        if (workload->options.function_header.is_template_instance) {
            string_append_formated(string, "<");
            Symbol* symbol = symbol_table_find_symbol(workload->symbol_table, workload->options.function_header.symbol_name_id, false);
            Symbol_Template_Instance* instance = &symbol->template_instances[workload->options.function_header.symbol_instance_index];
            for (int i = 0; i < instance->template_arguments.size; i++) {
                type_signature_append_to_string(string, instance->template_arguments[i], analyser);
                if (i != instance->template_arguments.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
            string_append_formated(string, ">");
        }

        break;
    }
    case Analysis_Workload_Type::GLOBAL: {
        string_append_formated(string, "Global Variable, name: %s",
            identifier_pool_index_to_string(analyser->compiler->identifier_pool, analyser->compiler->parser.nodes[workload->node_index].name_id).characters
        );
        break;
    }
    case Analysis_Workload_Type::SIZED_ARRAY_SIZE: {
        string_append_formated(string, "Sized Array");
        break;
    }
    case Analysis_Workload_Type::EXTERN_HEADER_IMPORT: {
        string_append_formated(string, "Extern header import, name: %s",
            identifier_pool_index_to_string(analyser->compiler->identifier_pool, analyser->compiler->parser.nodes[workload->node_index].name_id).characters
        );
        break;
    }
    case Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION: {
        string_append_formated(string, "Extern function declaration, name: %s",
            identifier_pool_index_to_string(analyser->compiler->identifier_pool, analyser->compiler->parser.nodes[workload->node_index].name_id).characters
        );
        break;
    }
    case Analysis_Workload_Type::STRUCT_BODY: {
        string_append_formated(string, "Struct Body, name: %s",
            identifier_pool_index_to_string(analyser->compiler->identifier_pool, workload->options.struct_body.struct_signature->struct_name_handle).characters
        );
        if (workload->options.struct_body.is_template_instance) {
            string_append_formated(string, "<");
            Symbol* symbol = symbol_table_find_symbol(workload->symbol_table, workload->options.struct_body.symbol_name_id, false);
            Symbol_Template_Instance* instance = &symbol->template_instances[workload->options.struct_body.symbol_instance_index];
            for (int i = 0; i < instance->template_arguments.size; i++) {
                type_signature_append_to_string(string, instance->template_arguments[i], analyser);
                if (i != instance->template_arguments.size - 1) {
                    string_append_formated(string, ", ");
                }
            }
            string_append_formated(string, ">");
        }
        break;
    }
    default: panic("Hey");
    }
}

void semantic_error_get_error_location(Semantic_Analyser* analyser, Semantic_Error error, Dynamic_Array<Token_Range>* locations)
{
    Dynamic_Array<Token_Range> token_mapping = analyser->compiler->parser.token_mapping;
    switch (error.type)
    {
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT: 
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE: {
        AST_Node* identifier_node = &analyser->compiler->parser.nodes[error.identifier_node_index];
        assert(identifier_node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED || identifier_node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED, "What");
        Token_Range unnamed_block_range = analyser->compiler->parser.token_mapping[identifier_node->children[0]];
        dynamic_array_push_back(locations, token_range_make(unnamed_block_range.start_index, unnamed_block_range.start_index + 1));
        dynamic_array_push_back(locations, token_range_make(unnamed_block_range.end_index-1, unnamed_block_range.end_index));
        break;
    }
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED: {
        dynamic_array_push_back(locations, token_mapping[error.identifier_node_index]);
        break;
    }
    case Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED:  {
        Token_Range extern_header_node = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(extern_header_node.start_index + 1, extern_header_node.start_index + 2));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_VOID_USAGE: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL_EXPECTED_FUNCTION_POINTER: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARGUMENT_TYPE_MISMATCH: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS: {
        AST_Node* member_access_node = &analyser->compiler->parser.nodes[error.error_node_index];
        assert(member_access_node->type == AST_Node_Type::EXPRESSION_MEMBER_ACCESS, "What");
        Token_Range range = token_mapping[member_access_node->children[0]];
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index+1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_IF_CONDITION: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR: {
        AST_Node* binary_op_node = &analyser->compiler->parser.nodes[error.error_node_index];
        assert(ast_node_type_is_binary_expression(binary_op_node->type), "HEY");
        Token_Range range = token_mapping[binary_op_node->children[0]];
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index+1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT: {
        AST_Node* assign_node = &analyser->compiler->parser.nodes[error.error_node_index];
        assert(assign_node->type == AST_Node_Type::STATEMENT_ASSIGNMENT || assign_node->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN, "hey");
        Token_Range range = token_mapping[assign_node->children[0]];
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index+1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_RETURN: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::INVALID_TYPE_DELETE: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::SYMBOL_EXPECTED_FUNCTION_OR_VARIABLE_ON_FUNCTION_CALL: {
        dynamic_array_push_back(locations, token_mapping[error.identifier_node_index]);
        break;
    }
    case Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER: {
        dynamic_array_push_back(locations, token_mapping[error.identifier_node_index]);
        break;
    }
    case Semantic_Error_Type::SYMBOL_EXPECTED_VARIABLE_OR_FUNCTION_ON_VARIABLE_READ:{
        dynamic_array_push_back(locations, token_mapping[error.identifier_node_index]);
        break;
    }
    case Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL:{
        dynamic_array_push_back(locations, token_mapping[error.identifier_node_index]);
        break;
    }
    case Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH: {
        AST_Node* expression_node = &analyser->compiler->parser.nodes[error.error_node_index];
        assert(expression_node->type == AST_Node_Type::EXPRESSION_FUNCTION_CALL, "What");
        Token_Range arguments_range = analyser->compiler->parser.token_mapping[expression_node->children[1]];
        dynamic_array_push_back(locations, token_range_make(arguments_range.start_index, arguments_range.start_index + 1));
        dynamic_array_push_back(locations, token_range_make(arguments_range.end_index-1, arguments_range.end_index));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_INVALID_CAST: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND: {
        AST_Node* member_access_node = &analyser->compiler->parser.nodes[error.error_node_index];
        assert(member_access_node->type == AST_Node_Type::EXPRESSION_MEMBER_ACCESS, "What");
        Token_Range range = token_mapping[member_access_node->children[0]];
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index+1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_ADDRESS_OF_REQUIRES_MEMORY_ADDRESS: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH: {
        AST_Node* binary_op_node = &analyser->compiler->parser.nodes[error.error_node_index];
        assert(ast_node_type_is_binary_expression(binary_op_node->type), "HEY");
        Token_Range range = token_mapping[binary_op_node->children[0]];
        dynamic_array_push_back(locations, token_range_make(range.end_index, range.end_index+1));
        break;
    }
    case Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::OTHERS_STRUCT_MUST_CONTAIN_MEMBER: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE:
    case Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS:
    case Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE: {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED:  {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::OTHERS_BREAK_NOT_INSIDE_LOOP: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::OTHERS_CONTINUE_NOT_INSIDE_LOOP: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT:  {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.end_index-1, range.end_index));
        break;
    }
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TEMPLATE_INSTANCE:  {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK:  {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE:  {
        dynamic_array_push_back(locations, token_mapping[error.error_node_index]);
        break;
    }
    case Semantic_Error_Type::OTHERS_MAIN_CANNOT_BE_TEMPLATED: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::OTHERS_MAIN_NOT_DEFINED: {
        break;
    }
    case Semantic_Error_Type::OTHERS_NO_CALLING_TO_MAIN:  {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS:  {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_TEMPLATED_GLOBALS: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_NON_INTEGER_ARRAY_SIZE_EVALUATION: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, range);
        break;
    }
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS: {
        Token_Range range = token_mapping[error.error_node_index];
        dynamic_array_push_back(locations, token_range_make(range.start_index, range.start_index+1));
        break;
    }
    default: panic("hey");
    }
}

void semantic_error_append_to_string(Semantic_Analyser* analyser, Semantic_Error e, String* string)
{
    bool print_symbol_by_name_id = false;
    bool print_symbol_by_identifier_node = false;
    bool print_given_type = false;
    bool print_expected_type = false;
    bool print_function_type = false;
    bool print_binary_type = false;
    bool print_required_argument_count = false;
    bool print_name_id = false;
    bool print_struct_members = false;
    bool print_identifier_node = false;
    bool print_member_access_name_id = false;

    int rollback_index = analyser->errors.size;
    SCOPE_EXIT(dynamic_array_rollback_to_size(&analyser->errors, rollback_index));

    switch (e.type)
    {
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT:
        string_append_formated(string, "Invalid Template Argument count");
        print_required_argument_count = true;
        print_symbol_by_identifier_node = true;
        break;
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE:
        string_append_formated(string, "Template arguments invalid, symbol is not templated");
        print_symbol_by_identifier_node = true;
        break;
    case Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED:
        string_append_formated(string, "Symbol is templated, requires template arguments");
        print_symbol_by_identifier_node = true;
        break;
    case Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL:
        string_append_formated(string, "Extern header does not contain this symbol");
        print_name_id = true;
        break;
    case Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED:
        string_append_formated(string, "Parsing extern header failed");
        break;
    case Semantic_Error_Type::INVALID_TYPE_VOID_USAGE:
        string_append_formated(string, "Invalid use of void type");
        break;
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL_EXPECTED_FUNCTION_POINTER:
        string_append_formated(string, "Expected function pointer type on function call");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER:
        string_append_formated(string, "Expected function type on function import");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARGUMENT_TYPE_MISMATCH:
        string_append_formated(string, "Argument type does not match function parameter type");
        print_given_type = true;
        print_expected_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS:
        string_append_formated(string, "Array access only works on array types");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX:
        string_append_formated(string, "Array access index must be of type i32");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE:
        string_append_formated(string, "Array allocation size must be of type i32");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE:
        string_append_formated(string, "Array size must be of type i32");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS:
        string_append_formated(string, "Member access only valid on struct/array or pointer to struct/array types");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_IF_CONDITION:
        string_append_formated(string, "If condition must be boolean");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION:
        string_append_formated(string, "While condition must be boolean");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR:
        string_append_formated(string, "Unary operator type invalid");
        print_given_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR:
        string_append_formated(string, "Binary operator types invalid");
        print_binary_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT:
        string_append_formated(string, "Invalid assignment type");
        print_given_type = true;
        print_expected_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_RETURN:
        string_append_formated(string, "Invalid return type");
        print_given_type = true;
        print_expected_type = true;
        break;
    case Semantic_Error_Type::INVALID_TYPE_DELETE:
        string_append_formated(string, "Only pointer or unsized array types can be deleted");
        print_given_type = true;
        break;
    case Semantic_Error_Type::SYMBOL_EXPECTED_FUNCTION_OR_VARIABLE_ON_FUNCTION_CALL:
        string_append_formated(string, "Expected Variable or Function symbol for function call");
        print_symbol_by_identifier_node = true;
        break;
    case Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER:
        string_append_formated(string, "Expected Type symbol");
        print_symbol_by_identifier_node = true;
        break;
    case Semantic_Error_Type::SYMBOL_EXPECTED_VARIABLE_OR_FUNCTION_ON_VARIABLE_READ:
        string_append_formated(string, "Expected Variable or Function symbol for Variable read");
        print_symbol_by_identifier_node = true;
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL:
        string_append_formated(string, "Could not resolve symbol");
        print_identifier_node = true;
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED:
        string_append_formated(string, "Symbol already defined");
        print_symbol_by_name_id = true;
        break;
    case Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED:
        string_append_formated(string, "Module already defined");
        print_name_id = true;
        break;
    case Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH:
        string_append_formated(string, "Parameter count does not match argument count, expected: %d, given: %d",
            e.invalid_argument_count.expected, e.invalid_argument_count.given
        );
        print_required_argument_count = true;
        print_function_type = true;
        break;
    case Semantic_Error_Type::EXPRESSION_INVALID_CAST:
        string_append_formated(string, "Invalid cast");
        print_expected_type = true;
        print_given_type = true;
        break;
    case Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND:
        string_append_formated(string, "Struct/Array does not contain member");
        print_given_type = true;
        print_member_access_name_id = true;
        break;
    case Semantic_Error_Type::EXPRESSION_ADDRESS_OF_REQUIRES_MEMORY_ADDRESS:
        string_append_formated(string, "Cannot take address, expression does not have a memory address");
        break;
    case Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH:
        string_append_formated(string, "Binary op types do not match and cannot be implicitly casted");
        print_binary_type = true;
        break;
    case Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL:
        string_append_formated(string, "Expression does not do anything, must be function call");
        break;
    case Semantic_Error_Type::OTHERS_STRUCT_MUST_CONTAIN_MEMBER:
        string_append_formated(string, "Struct must contain at least one member");
        break;
    case Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED:
        string_append_formated(string, "Struct member is already defined");
        print_name_id = true;
        break;
    case Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE:
        string_append_formated(string, "While loop always exits, never runs more than once");
        break;
    case Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS:
        string_append_formated(string, "While loop always returns, never runs more than once");
        break;
    case Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS:
        string_append_formated(string, "While loop always continues, never stops");
        break;
    case Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE:
        string_append_formated(string, "Unreachable statement");
        break;
    case Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED:
        string_append_formated(string, "No returns allowed inside of defer");
        break;
    case Semantic_Error_Type::OTHERS_BREAK_NOT_INSIDE_LOOP:
        string_append_formated(string, "Break not inside a loop");
        break;
    case Semantic_Error_Type::OTHERS_CONTINUE_NOT_INSIDE_LOOP:
        string_append_formated(string, "Continue not inside a loop");
        break;
    case Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT:
        string_append_formated(string, "Function is missing a return statement");
        break;
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TEMPLATE_INSTANCE:
        string_append_formated(string, "Unfinished workload template instance");
        break;
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK:
        string_append_formated(string, "Unfinished workload code block");
        break;
    case Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE:
        string_append_formated(string, "Unfinished workload type size");
        break;
    case Semantic_Error_Type::OTHERS_MAIN_CANNOT_BE_TEMPLATED:
        string_append_formated(string, "Main function cannot be templated");
        break;
    case Semantic_Error_Type::OTHERS_MAIN_NOT_DEFINED:
        string_append_formated(string, "Main function not found");
        break;
    case Semantic_Error_Type::OTHERS_NO_CALLING_TO_MAIN:
        string_append_formated(string, "Cannot call main function again");
        break;
    case Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS:
        string_append_formated(string, "Left side of assignment does not have a memory address");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_TEMPLATED_GLOBALS:
        string_append_formated(string, "Templated globals not implemented yet");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_NON_INTEGER_ARRAY_SIZE_EVALUATION:
        string_append_formated(string, "Non integer array size not implemented yet");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES:
        string_append_formated(string, "Nested template modules not implemented yet");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES:
        string_append_formated(string, "Extern imports inside templates not allowed");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT:
        string_append_formated(string, "Extern global variable import not implemented yet");
        break;
    case Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS:
        string_append_formated(string, "Nested defers not implemented yet");
        break;
    default: panic("ERROR");
    }

    if (print_symbol_by_name_id)
    {
        Symbol* symbol = symbol_table_find_symbol(e.symbol_table, e.name_id, false);
        if (symbol != 0) {
            string_append_formated(string, "\n  Symbol: ");
            symbol_append_to_string(symbol, string, analyser);
        }
    }
    if (print_symbol_by_identifier_node)
    {
        Identifier_Analysis_Result result = semantic_analyser_analyse_identifier_node(analyser, e.symbol_table, &analyser->compiler->parser, e.identifier_node_index, false);
        if (result.type == Analysis_Result_Type::SUCCESS) {
            string_append_formated(string, "\n  Symbol: ");
            symbol_append_to_string(&result.options.symbol, string, analyser);
        }
    }
    if (print_given_type) {
        string_append_formated(string, "\n  Given Type:    ");
        type_signature_append_to_string(string, e.given_type, analyser);
    }
    if (print_expected_type) {
        string_append_formated(string, "\n  Expected Type: ");
        type_signature_append_to_string(string, e.expected_type, analyser);
    }
    if (print_function_type) {
        string_append_formated(string, "\n  Function Type: ");
        type_signature_append_to_string(string, e.function_type, analyser);
    }
    if (print_binary_type) {
        string_append_formated(string, "\n  Left Operand type:  ");
        type_signature_append_to_string(string, e.binary_op_left_type, analyser);
        string_append_formated(string, "\n  Right Operand type: ");
        type_signature_append_to_string(string, e.binary_op_right_type, analyser);
    }
    if (print_required_argument_count) {
        string_append_formated(string, "\n  Given argument count: %d, required: %d", e.invalid_argument_count.given, e.invalid_argument_count.expected);
    }
    if (print_name_id) {
        string_append_formated(string, "\n  Name: %s", identifier_pool_index_to_string(analyser->compiler->identifier_pool, e.name_id).characters);
    }
    if (print_member_access_name_id) {
        AST_Node* node = &analyser->compiler->parser.nodes[e.error_node_index];
        assert(node->type == AST_Node_Type::EXPRESSION_MEMBER_ACCESS, "BAllern");
        string_append_formated(string, "\n  Accessed member name: %s",
            identifier_pool_index_to_string(analyser->compiler->identifier_pool, node->name_id).characters
        );
    }
    if (print_struct_members)
    {
        string_append_formated(string, "\n  Available struct members: %s");
        assert(e.given_type->type == Signature_Type::STRUCT, "HEY");
        for (int i = 0; i < e.given_type->member_types.size; i++) {
            Struct_Member* member = &e.given_type->member_types[i];
            string_append_formated(string, "\n\t\t%s",
                identifier_pool_index_to_string(analyser->compiler->identifier_pool, member->name_handle).characters
            );
        }
    }
    if (print_identifier_node)
    {
        string_append_formated(string, "\n  Identifier node: ");
        AST_Node* node = &analyser->compiler->parser.nodes[e.identifier_node_index];
        while (node->parent >= 0) {
            AST_Node* parent = &analyser->compiler->parser.nodes[node->parent];
            if (ast_node_type_is_identifier_node(parent->type)) {
                node = parent;
            }
            else {
                break;
            }
        }
        while (node != 0)
        {
            string_append_formated(string, identifier_pool_index_to_string(analyser->compiler->identifier_pool, node->name_id).characters);
            if (node->children.size != 0) {
                node = &analyser->compiler->parser.nodes[node->children[0]];
            }
            else {
                break;
            }
        }
    }
}

void identifer_or_path_append_to_string(int node_index, Semantic_Analyser* analyser, String* string)
{
    AST_Node* node = &analyser->compiler->parser.nodes[node_index];
    assert(node->type == AST_Node_Type::IDENTIFIER_NAME ||
        node->type == AST_Node_Type::IDENTIFIER_PATH ||
        node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED ||
        node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED, "hEY");
    while (node->type != AST_Node_Type::IDENTIFIER_NAME && node->type != AST_Node_Type::IDENTIFIER_NAME_TEMPLATED)
    {
        string_append_formated(string, "%s::", identifier_pool_index_to_string(analyser->compiler->identifier_pool, node->name_id).characters);
        if (node->type == AST_Node_Type::IDENTIFIER_PATH) {
            node = &analyser->compiler->parser.nodes[node->children[0]];
        }
        else {
            node = &analyser->compiler->parser.nodes[node->children[1]];
        }
    }
    if (node->type == AST_Node_Type::IDENTIFIER_NAME) {
        string_append_formated(string, "%s", identifier_pool_index_to_string(analyser->compiler->identifier_pool, node->name_id).characters);
    }
}

void workload_dependency_append_to_string(Workload_Dependency* dependency, String* string, Semantic_Analyser* analyser)
{
    switch (dependency->type)
    {
    case Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED:
        string_append_formated(string, "Code not finished");
        break;
    case Workload_Dependency_Type::IDENTIFER_NOT_FOUND:
        string_append_formated(string, "Identifier not found \"");
        identifer_or_path_append_to_string(dependency->node_index, analyser, string);
        string_append_formated(string, "\"");
        break;
    case Workload_Dependency_Type::TYPE_SIZE_UNKNOWN:
        string_append_formated(string, "Type size unknown ");
        type_signature_append_to_string(string, dependency->options.type_signature, analyser);
        break;
    case Workload_Dependency_Type::TEMPLATE_INSTANCE_NOT_FINISHED: {
        string_append_formated(string, "Template Instance not finished: ");
        Symbol* s = symbol_table_find_symbol(dependency->options.template_not_finished.symbol_table, dependency->options.template_not_finished.symbol_name_id, false);
        assert(s != 0, "HEY");
        assert(s->is_templated, "HEY");
        assert(s->symbol_type == Symbol_Type::FUNCTION || s->symbol_type == Symbol_Type::TYPE, "HEY");
        Symbol_Template_Instance* instance = &s->template_instances[dependency->options.template_not_finished.instance_index];
        string_append_formated(string, identifier_pool_index_to_string(analyser->compiler->identifier_pool, s->name_handle).characters);
        string_append_formated(string, "<");
        for (int i = 0; i < instance->template_arguments.size; i++) {
            type_signature_append_to_string(string, instance->template_arguments[i], analyser);
            if (i != instance->template_arguments.size - 1) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, ">");
        break;
    }
    default: panic("hey");
    }
}

Type_Signature* import_c_type(Semantic_Analyser* analyser, C_Import_Type* type, Hashtable<C_Import_Type*, Type_Signature*>* type_conversions)
{
    {
        Type_Signature** converted = hashtable_find_element(type_conversions, type);
        if (converted != 0) {
            return *converted;
        }
    }
    Type_Signature signature;
    signature.size_in_bytes = type->byte_size;
    signature.alignment_in_bytes = type->alignment;
    Type_Signature* result_type = 0;
    switch (type->type)
    {
    case C_Import_Type_Type::ARRAY: {
        signature.type = Signature_Type::ARRAY_SIZED;
        signature.array_element_count = type->array.array_size;
        signature.child_type = import_c_type(analyser, type->array.element_type, type_conversions);
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    case C_Import_Type_Type::POINTER: {
        signature.type = Signature_Type::POINTER;
        signature.child_type = import_c_type(analyser, type->array.element_type, type_conversions);
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    case C_Import_Type_Type::PRIMITIVE: {
        switch (type->primitive)
        {
        case C_Import_Primitive::VOID_TYPE:
            result_type = analyser->compiler->type_system.void_type;
            break;
        case C_Import_Primitive::BOOL:
            result_type = analyser->compiler->type_system.bool_type;
            break;
        case C_Import_Primitive::CHAR: {
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u8_type;
            }
            else {
                result_type = analyser->compiler->type_system.i8_type;
            }
            break;
        }
        case C_Import_Primitive::DOUBLE:
            result_type = analyser->compiler->type_system.f64_type;
            break;
        case C_Import_Primitive::FLOAT:
            result_type = analyser->compiler->type_system.f32_type;
            break;
        case C_Import_Primitive::INT:
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u32_type;
            }
            else {
                result_type = analyser->compiler->type_system.i32_type;
            }
            break;
        case C_Import_Primitive::LONG:
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u32_type;
            }
            else {
                result_type = analyser->compiler->type_system.i32_type;
            }
            break;
        case C_Import_Primitive::LONG_DOUBLE:
            result_type = analyser->compiler->type_system.f64_type;
            break;
        case C_Import_Primitive::LONG_LONG:
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u64_type;
            }
            else {
                result_type = analyser->compiler->type_system.i64_type;
            }
            break;
        case C_Import_Primitive::SHORT:
            if (((u8)type->qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
                result_type = analyser->compiler->type_system.u16_type;
            }
            else {
                result_type = analyser->compiler->type_system.i16_type;
            }
            break;
        default: panic("WHAT");
        }
        break;
    }
    case C_Import_Type_Type::ENUM: {
        result_type = analyser->compiler->type_system.i32_type;
        break;
    }
    case C_Import_Type_Type::ERROR_TYPE: {
        signature.type = Signature_Type::ARRAY_SIZED;
        signature.child_type = analyser->compiler->type_system.u8_type;
        signature.array_element_count = type->byte_size;
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    case C_Import_Type_Type::STRUCTURE:
    {
        signature.type = Signature_Type::STRUCT;
        if (type->structure.is_anonymous) {
            signature.struct_name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("__c_anon"));
        }
        else {
            signature.struct_name_handle = type->structure.name_id;
        }
        signature.member_types = dynamic_array_create_empty<Struct_Member>(type->structure.members.size);
        if (!type->structure.contains_bitfield)
        {
            for (int i = 0; i < type->structure.members.size; i++) {
                C_Import_Structure_Member* mem = &type->structure.members[i];
                Struct_Member member;
                member.name_handle = mem->name_id;
                member.offset = mem->offset;
                member.type = import_c_type(analyser, mem->type, type_conversions);
                dynamic_array_push_back(&signature.member_types, member);
            }
        }
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    case C_Import_Type_Type::FUNCTION_SIGNATURE:
    {
        signature.type = Signature_Type::FUNCTION;
        signature.return_type = import_c_type(analyser, type->function_signature.return_type, type_conversions);
        signature.parameter_types = dynamic_array_create_empty<Type_Signature*>(type->function_signature.parameters.size);
        for (int i = 0; i < type->function_signature.parameters.size; i++) {
            dynamic_array_push_back(&signature.parameter_types, import_c_type(analyser, type->function_signature.parameters[i].type, type_conversions));
        }
        result_type = type_system_register_type(&analyser->compiler->type_system, signature);
        break;
    }
    default: panic("WHAT");
    }

    assert(result_type != 0, "HEY");
    hashtable_insert_element(type_conversions, type, result_type);
    return result_type;
}

void semantic_analyser_analyse(Semantic_Analyser* analyser, Compiler* compiler)
{
    if (PRINT_DEPENDENCIES) {
        logg("SEMANTIC_ANALYSER_DEPENDECIES:\n-----------------------------\n");
    }
    SCOPE_EXIT(if (PRINT_DEPENDENCIES) logg("------------------------------------\n"););

    // Reset analyser data
    {
        analyser->compiler = compiler;
        type_system_reset_all(&analyser->compiler->type_system, &analyser->compiler->lexer);
        for (int i = 0; i < analyser->symbol_tables.size; i++) {
            symbol_table_destroy(analyser->symbol_tables[i]);
        }
        dynamic_array_reset(&analyser->symbol_tables);
        for (int i = 0; i < analyser->known_expression_values.size; i++) {
            delete analyser->known_expression_values[i];
        }
        dynamic_array_reset(&analyser->known_expression_values);
        dynamic_array_reset(&analyser->errors);
        dynamic_array_reset(&analyser->active_workloads);
        dynamic_array_reset(&analyser->waiting_workload);

        hashtable_reset(&analyser->finished_code_blocks);
        hashtable_reset(&analyser->ast_to_symbol_table);

        analyser->root_table = symbol_table_create(analyser, nullptr, 0, true);
        if (analyser->program != 0) {
            ir_program_destroy(analyser->program);
        }
        analyser->program = ir_program_create(&analyser->compiler->type_system);
        analyser->global_init_function = ir_function_create(analyser->program,
            type_system_make_function(
                &analyser->compiler->type_system,
                dynamic_array_create_empty<Type_Signature*>(1),
                analyser->compiler->type_system.void_type
            )
        );
    }

    // Add symbols for basic datatypes
    {
        int int_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("int"));
        int bool_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("bool"));
        int float_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("float"));
        int u8_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("u8"));
        int u16_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("u16"));
        int u32_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("u32"));
        int u64_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("u64"));
        int i8_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("i8"));
        int i16_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("i16"));
        int i32_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("i32"));
        int i64_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("i64"));
        int f64_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("f64"));
        int f32_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("f32"));
        int byte_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("byte"));
        int void_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("void"));
        int string_token_index = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("String"));

        semantic_analyser_define_type_symbol(analyser, analyser->root_table, int_token_index, analyser->compiler->type_system.i32_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, bool_token_index, analyser->compiler->type_system.bool_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, float_token_index, analyser->compiler->type_system.f32_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, f32_token_index, analyser->compiler->type_system.f32_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, f64_token_index, analyser->compiler->type_system.f64_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, u8_token_index, analyser->compiler->type_system.u8_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, byte_token_index, analyser->compiler->type_system.u8_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, u16_token_index, analyser->compiler->type_system.u16_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, u32_token_index, analyser->compiler->type_system.u32_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, u64_token_index, analyser->compiler->type_system.u64_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, i8_token_index, analyser->compiler->type_system.i8_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, i16_token_index, analyser->compiler->type_system.i16_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, i32_token_index, analyser->compiler->type_system.i32_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, i64_token_index, analyser->compiler->type_system.i64_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, void_token_index, analyser->compiler->type_system.void_type, -1);
        semantic_analyser_define_type_symbol(analyser, analyser->root_table, string_token_index, analyser->compiler->type_system.string_type, -1);

        analyser->token_index_size = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("size"));
        analyser->token_index_data = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("data"));
        analyser->token_index_main = identifier_pool_add_or_find_identifier_by_string(compiler->identifier_pool, string_create_static("main"));
    }

    // Initialize hardcoded_function types and symbols
    for (int i = 0; i < analyser->program->hardcoded_functions.size; i++)
    {
        IR_Hardcoded_Function* hardcoded = analyser->program->hardcoded_functions[i];
        Symbol symbol;
        symbol.definition_node_index = -1;
        symbol.is_templated = false;
        symbol.options.hardcoded_function = hardcoded;
        symbol.symbol_type = Symbol_Type::HARDCODED_FUNCTION;
        switch (hardcoded->type)
        {
        case IR_Hardcoded_Function_Type::PRINT_I32: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("print_i32"));
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_F32: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("print_f32"));
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_BOOL: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("print_bool"));
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_STRING: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("print_string"));
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_LINE: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("print_line"));
            break;
        }
        case IR_Hardcoded_Function_Type::READ_I32: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("read_i32"));
            break;
        }
        case IR_Hardcoded_Function_Type::READ_F32: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("read_f32"));
            break;
        }
        case IR_Hardcoded_Function_Type::READ_BOOL: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("read_bool"));
            break;
        }
        case IR_Hardcoded_Function_Type::RANDOM_I32: {
            symbol.name_handle = identifier_pool_add_or_find_identifier_by_string(analyser->compiler->identifier_pool, string_create_static("random_i32"));
            break;
        }
        case IR_Hardcoded_Function_Type::MALLOC_SIZE_I32:
        case IR_Hardcoded_Function_Type::FREE_POINTER:
            continue;
        default:
            panic("What");
        }
        symbol_table_define_symbol(analyser->root_table, analyser, symbol, false);
    }

    // Find all workloads: TODO: Maybe create Workload-Type for this task
    semantic_analyser_find_workloads_recursively(analyser, analyser->root_table, 0);

    // Execute all Workloads
    Dynamic_Array<AST_Node>* nodes = &analyser->compiler->parser.nodes;
    while (analyser->active_workloads.size != 0)
    {
        Analysis_Workload workload = analyser->active_workloads[analyser->active_workloads.size - 1];
        dynamic_array_swap_remove(&analyser->active_workloads, analyser->active_workloads.size - 1);

        if (PRINT_DEPENDENCIES)
        {
            String output = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&output));
            string_append_formated(&output, "WORKING ON: ");
            analysis_workload_append_to_string(&workload, &output, analyser);
            string_append_formated(&output, "\n");
            logg(output.characters);
        }

        bool found_workload_dependency = false;
        Workload_Dependency found_dependency;

        // Execute Workload
        switch (workload.type)
        {
        case Analysis_Workload_Type::SIZED_ARRAY_SIZE:
        {
            Type_Signature* array_sig = workload.options.sized_array_type;
            if (array_sig->child_type->size_in_bytes == 0 || array_sig->child_type->alignment_in_bytes == 0) {
                panic("Hey, at this point this should be resolved!");
            }
            array_sig->alignment_in_bytes = array_sig->child_type->alignment_in_bytes;
            array_sig->size_in_bytes = math_round_next_multiple(array_sig->child_type->size_in_bytes, array_sig->child_type->alignment_in_bytes) * array_sig->array_element_count;
            break;
        }
        case Analysis_Workload_Type::FUNCTION_HEADER:
        {
            int function_name = analyser->compiler->parser.nodes[workload.node_index].name_id;
            AST_Node* function_node = &(*nodes)[workload.node_index];
            AST_Node* signature_node = &(*nodes)[function_node->children[0]];
            AST_Node* parameter_block = &(*nodes)[signature_node->children[0]];

            // Create function signature
            Type_Signature* function_type;
            {
                Type_Signature* return_type;
                if (signature_node->children.size == 2)
                {
                    Type_Analysis_Result return_type_result = semantic_analyser_analyse_type(
                        analyser, workload.options.function_header.type_lookup_table, signature_node->children[1]
                    );
                    if (return_type_result.type == Analysis_Result_Type::SUCCESS) {
                        return_type = return_type_result.options.result_type;
                    }
                    else if (return_type_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                        return_type = analyser->compiler->type_system.error_type;
                    }
                    else {
                        found_workload_dependency = true;
                        found_dependency = return_type_result.options.dependency;
                        break;
                    }
                }
                else {
                    return_type = analyser->compiler->type_system.void_type;
                }

                Dynamic_Array<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(parameter_block->children.size);
                for (int i = 0; i < parameter_block->children.size; i++)
                {
                    int parameter_index = parameter_block->children[i];
                    AST_Node* parameter = &analyser->compiler->parser.nodes[parameter_index];
                    Type_Analysis_Result param_type_result = semantic_analyser_analyse_type(
                        analyser, workload.options.function_header.type_lookup_table, parameter->children[0]
                    );
                    if (param_type_result.type == Analysis_Result_Type::SUCCESS) {
                        dynamic_array_push_back(&parameter_types, param_type_result.options.result_type);
                    }
                    else if (param_type_result.type == Analysis_Result_Type::ERROR_OCCURED) {
                        dynamic_array_push_back(&parameter_types, analyser->compiler->type_system.error_type);
                    }
                    else {
                        found_workload_dependency = true;
                        found_dependency = param_type_result.options.dependency;
                        dynamic_array_destroy(&parameter_types);
                        break;
                    }
                }
                if (found_workload_dependency) {
                    break;
                }
                function_type = type_system_make_function(&analyser->compiler->type_system, parameter_types, return_type);
            }

            // Create function
            IR_Function* function = ir_function_create(analyser->program, function_type);
            Symbol_Table* function_table = symbol_table_create(
                analyser, workload.options.function_header.type_lookup_table, workload.node_index, !workload.options.function_header.is_template_instance
            );
            {
                if (workload.options.function_header.is_template_instance)
                {
                    Symbol* symbol = symbol_table_find_symbol(workload.symbol_table, workload.options.function_header.symbol_name_id, true);
                    assert(symbol != 0, "HEy");
                    Symbol_Template_Instance* instance = &symbol->template_instances[workload.options.function_header.symbol_instance_index];
                    instance->instanciated = true;
                    instance->options.function = function;

                    if (function_node->name_id == analyser->token_index_main) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_MAIN_CANNOT_BE_TEMPLATED;
                        error.error_node_index = workload.node_index;
                        semantic_analyser_log_error_new(analyser, error);
                        break;
                    }
                }
                else
                {
                    Symbol function_symbol;
                    function_symbol.definition_node_index = workload.node_index;
                    function_symbol.name_handle = function_node->name_id;
                    function_symbol.options.function = function;
                    if (workload.options.function_header.is_template_analysis) {
                        function_symbol.is_templated = true;
                        function_symbol.template_parameter_names = workload.options.function_header.template_parameter_names;
                        workload.options.function_header.template_parameter_names.data = 0;
                        function_symbol.template_instances = dynamic_array_create_empty<Symbol_Template_Instance>(2);
                    }
                    else {
                        function_symbol.is_templated = false;
                    }
                    function_symbol.symbol_type = Symbol_Type::FUNCTION;
                    symbol_table_define_symbol(workload.symbol_table, analyser, function_symbol, false);
                    if (function_node->name_id == analyser->token_index_main) {
                        analyser->program->entry_function = function;
                        IR_Instruction call_global_init_instr;
                        call_global_init_instr.type = IR_Instruction_Type::FUNCTION_CALL;
                        call_global_init_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
                        call_global_init_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                        call_global_init_instr.options.call.options.function = analyser->global_init_function;
                        dynamic_array_push_back(&function->code->instructions, call_global_init_instr);
                    }
                }

                // Define parameters
                for (int i = 0; i < parameter_block->children.size; i++)
                {
                    int parameter_index = parameter_block->children[i];
                    AST_Node* parameter = &analyser->compiler->parser.nodes[parameter_index];

                    Symbol symbol;
                    symbol.definition_node_index = parameter_index;
                    symbol.is_templated = false;
                    symbol.name_handle = parameter->name_id;
                    symbol.symbol_type = Symbol_Type::VARIABLE;
                    symbol.options.variable_access.index = i;
                    symbol.options.variable_access.type = IR_Data_Access_Type::PARAMETER;
                    symbol.options.variable_access.is_memory_access = false;
                    symbol.options.variable_access.option.function = function;
                    symbol_table_define_symbol(function_table, analyser, symbol, true);
                }
            }

            // Create new workload for Function body
            {
                Analysis_Workload body_workload;
                body_workload.type = Analysis_Workload_Type::CODE_BLOCK;
                body_workload.node_index = function_node->children[1];
                body_workload.symbol_table = function_table;
                body_workload.options.code_block.code_block = function->code;
                body_workload.options.code_block.current_child_index = 0;
                body_workload.options.code_block.active_defer_statements = dynamic_array_create_empty<int>(4);
                body_workload.options.code_block.inside_defer = false;
                body_workload.options.code_block.local_block_defer_depth = 0;
                body_workload.options.code_block.surrounding_loop_defer_depth = 0;
                body_workload.options.code_block.inside_loop = false;
                body_workload.options.code_block.requires_return = true;
                body_workload.options.code_block.check_last_instruction_result = false;
                dynamic_array_push_back(&analyser->active_workloads, body_workload);
            }

            break;
        }
        case Analysis_Workload_Type::GLOBAL: {
            Variable_Creation_Analysis_Result result = semantic_analyser_analyse_variable_creation_statements(analyser,
                workload.symbol_table, workload.node_index, analyser->global_init_function->code, true
            );
            if (result.type == Analysis_Result_Type::DEPENDENCY) {
                found_workload_dependency = true;
                found_dependency = result.dependency;
            }
            break;
        }
        case Analysis_Workload_Type::EXTERN_HEADER_IMPORT:
        {
            AST_Node* extern_node = &(*nodes)[workload.node_index];
            int header_name_id = extern_node->name_id;
            String header_name = identifier_pool_index_to_string(analyser->compiler->identifier_pool, header_name_id);
            Optional<C_Import_Package> package = c_importer_import_header(&analyser->compiler->c_importer, header_name);
            if (package.available)
            {
                // TODO: Import all identifiers given by node_children!
                logg("Importing header successfull: %s\n", header_name.characters);
                dynamic_array_push_back(&analyser->program->extern_program_sources.headers_to_include, header_name_id);
                Hashtable<C_Import_Type*, Type_Signature*> type_conversion_table = hashtable_create_pointer_empty<C_Import_Type*, Type_Signature*>(256);
                SCOPE_EXIT(hashtable_destroy(&type_conversion_table));
                for (int i = 0; i < extern_node->children.size; i++)
                {
                    int import_id = (*nodes)[extern_node->children[i]].name_id;
                    C_Import_Symbol* import_symbol = hashtable_find_element(&package.value.symbol_table.symbols, import_id);
                    if (import_symbol == 0) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL;
                        error.name_id = import_id;
                        error.error_node_index = extern_node->children[i];
                        semantic_analyser_log_error_new(analyser, error);
                        continue;
                    }

                    Symbol sym;
                    sym.definition_node_index = extern_node->children[i];
                    sym.is_templated = false;
                    sym.name_handle = import_id;
                    switch (import_symbol->type)
                    {
                    case C_Import_Symbol_Type::TYPE:
                    {
                        sym.symbol_type = Symbol_Type::TYPE;
                        sym.options.data_type = import_c_type(analyser, import_symbol->data_type, &type_conversion_table);
                        symbol_table_define_symbol(workload.symbol_table, analyser, sym, false);
                        if (sym.options.data_type->type == Signature_Type::STRUCT) {
                            hashtable_insert_element(&analyser->program->extern_program_sources.extern_type_signatures, sym.options.data_type, sym.name_handle);
                        }
                        break;
                    }
                    case C_Import_Symbol_Type::FUNCTION: {
                        Extern_Function_Identifier extern_fn;
                        extern_fn.name_id = import_id;
                        extern_fn.function_signature = import_c_type(analyser, import_symbol->data_type, &type_conversion_table);
                        assert(extern_fn.function_signature->type == Signature_Type::FUNCTION, "HEY");
                        sym.symbol_type = Symbol_Type::EXTERN_FUNCTION;
                        sym.options.extern_function = extern_fn;
                        symbol_table_define_symbol(workload.symbol_table, analyser, sym, false);
                        break;
                    }
                    case C_Import_Symbol_Type::GLOBAL_VARIABLE: {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT;
                        error.error_node_index = workload.node_index;
                        semantic_analyser_log_error_new(analyser, error);
                        break;
                    }
                    default: panic("hey");
                    }
                }

                // Import all used type names
                auto iter = hashtable_iterator_create(&package.value.symbol_table.symbols);
                while (hashtable_iterator_has_next(&iter))
                {
                    int name_id = *iter.key;
                    if (symbol_table_find_symbol(workload.symbol_table, name_id, true) != 0) {
                        hashtable_iterator_next(&iter);
                        continue;
                    }
                    C_Import_Symbol* import_sym = iter.value;
                    if (import_sym->type == C_Import_Symbol_Type::TYPE)
                    {
                        Type_Signature** signature = hashtable_find_element(&type_conversion_table, import_sym->data_type);
                        if (signature != 0)
                        {
                            Symbol sym;
                            sym.symbol_type = Symbol_Type::TYPE;
                            sym.is_templated = false;
                            sym.name_handle = name_id;
                            sym.options.data_type = *signature;
                            sym.definition_node_index = workload.node_index;
                            symbol_table_define_symbol(workload.symbol_table, analyser, sym, false);
                            if (sym.options.data_type->type == Signature_Type::STRUCT) {
                                hashtable_insert_element(&analyser->program->extern_program_sources.extern_type_signatures, sym.options.data_type, sym.name_handle);
                            }
                        }
                    }
                    hashtable_iterator_next(&iter);
                }
            }
            else {
                Semantic_Error error;
                error.type = Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED;
                error.error_node_index = workload.node_index;
                semantic_analyser_log_error_new(analyser, error);
            }
            break;
        }
        case Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION:
        {
            AST_Node* extern_node = &(*nodes)[workload.node_index];
            int name_id = extern_node->name_id;
            Type_Analysis_Result result = semantic_analyser_analyse_type(analyser, workload.symbol_table, extern_node->children[0]);
            switch (result.type)
            {
            case Analysis_Result_Type::SUCCESS:
            {
                if (result.options.result_type->type != Signature_Type::POINTER && result.options.result_type->child_type->type != Signature_Type::FUNCTION) {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER;
                    error.error_node_index = extern_node->children[0];
                    semantic_analyser_log_error_new(analyser, error);
                    break;
                }
                Extern_Function_Identifier extern_fn;
                extern_fn.name_id = extern_node->name_id;
                extern_fn.function_signature = result.options.result_type->child_type;
                dynamic_array_push_back(&analyser->program->extern_program_sources.extern_functions, extern_fn);
                Symbol sym;
                sym.symbol_type = Symbol_Type::EXTERN_FUNCTION;
                sym.name_handle = extern_fn.name_id;
                sym.is_templated = false;
                sym.options.extern_function = extern_fn;
                sym.definition_node_index = workload.node_index;
                symbol_table_define_symbol(workload.symbol_table, analyser, sym, false);
                break;
            }
            case Analysis_Result_Type::DEPENDENCY:
                found_workload_dependency = true;
                found_dependency = result.options.dependency;
                break;
            case Analysis_Result_Type::ERROR_OCCURED:
                break;
            }
            break;
        }
        case Analysis_Workload_Type::CODE_BLOCK:
        {
            Analysis_Workload_Code_Block* block_workload = &workload.options.code_block;
            AST_Node* statement_block_node = &(*nodes)[workload.node_index];
            Statement_Analysis_Result statement_result = Statement_Analysis_Result::NO_RETURN;

            // Check last block finish result
            if (block_workload->check_last_instruction_result)
            {
                IR_Instruction* last_instruction = &block_workload->code_block->instructions[block_workload->code_block->instructions.size - 1];
                if (last_instruction->type == IR_Instruction_Type::BLOCK)
                {
                    Statement_Analysis_Result* result_optional = hashtable_find_element(&analyser->finished_code_blocks, last_instruction->options.block);
                    if (result_optional == 0) {
                        panic("I dont think this should happen");
                    }
                    statement_result = *result_optional;
                }
                else if (last_instruction->type == IR_Instruction_Type::IF)
                {
                    if (last_instruction->options.if_instr.false_branch->instructions.size != 0)
                    {
                        Statement_Analysis_Result* true_branch_opt = hashtable_find_element(&analyser->finished_code_blocks, last_instruction->options.if_instr.true_branch);
                        Statement_Analysis_Result* false_branch_opt = hashtable_find_element(&analyser->finished_code_blocks, last_instruction->options.if_instr.false_branch);
                        if (true_branch_opt == 0 || false_branch_opt == 0) {
                            panic("This should not happen!");
                        }
                        if (*true_branch_opt == *false_branch_opt) {
                            statement_result = *false_branch_opt;
                        }
                    }
                }
                else if (last_instruction->type == IR_Instruction_Type::WHILE)
                {
                    Statement_Analysis_Result* body_result = hashtable_find_element(&analyser->finished_code_blocks, last_instruction->options.while_instr.code);
                    assert(body_result != 0, "Should not happen");
                    if (*body_result == Statement_Analysis_Result::RETURN) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS;
                        error.error_node_index = statement_block_node->children[block_workload->current_child_index - 1];
                        semantic_analyser_log_error_new(analyser, error);
                    }
                    else if (*body_result == Statement_Analysis_Result::CONTINUE) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS;
                        error.error_node_index = statement_block_node->children[block_workload->current_child_index - 1];
                        semantic_analyser_log_error_new(analyser, error);
                    }
                    else if (*body_result == Statement_Analysis_Result::BREAK) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE;
                        error.error_node_index = statement_block_node->children[block_workload->current_child_index - 1];
                        semantic_analyser_log_error_new(analyser, error);
                    }
                }
                else {
                    panic("Hey, should not happen!");
                }
            }
            block_workload->check_last_instruction_result = false;

            // Analyse Block
            for (int i = block_workload->current_child_index; i < statement_block_node->children.size && !found_workload_dependency; i++)
            {
                block_workload->current_child_index = i;
                int statement_index = statement_block_node->children[i];
                if (statement_result != Statement_Analysis_Result::NO_RETURN) {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE;
                    error.error_node_index = statement_index;
                    semantic_analyser_log_error_new(analyser, error);
                    continue;
                }

                AST_Node* statement_node = &(*nodes)[statement_index];
                switch (statement_node->type)
                {
                case AST_Node_Type::STATEMENT_RETURN:
                {
                    statement_result = Statement_Analysis_Result::RETURN;
                    IR_Instruction return_instr;
                    return_instr.type = IR_Instruction_Type::RETURN;
                    Type_Signature* return_type = 0;

                    // Determine return type
                    if (block_workload->code_block->function == analyser->program->entry_function) {
                        return_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                        return_instr.options.return_instr.options.exit_code = IR_Exit_Code::SUCCESS;
                        return_type = analyser->compiler->type_system.void_type;
                    }
                    else
                    {
                        if (statement_node->children.size == 0) {
                            return_type = analyser->compiler->type_system.void_type;
                            return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
                        }
                        else
                        {
                            return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
                            Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
                                analyser, workload.symbol_table, statement_node->children[0],
                                block_workload->code_block, true, &return_instr.options.return_instr.options.return_value
                            );
                            switch (expr_result.type)
                            {
                            case Analysis_Result_Type::SUCCESS: {
                                return_type = expr_result.options.success.result_type;
                                break;
                            }
                            case Analysis_Result_Type::DEPENDENCY:
                                found_dependency = expr_result.options.dependency;
                                found_workload_dependency = true;
                                break;
                            case Analysis_Result_Type::ERROR_OCCURED:
                                continue;
                            }
                        }
                    }
                    if (found_workload_dependency) break;

                    if (return_type != block_workload->code_block->function->function_type->return_type) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::INVALID_TYPE_RETURN;
                        error.error_node_index = statement_index;
                        semantic_analyser_log_error_new(analyser, error);
                    }

                    if (block_workload->inside_defer) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED;
                        error.error_node_index = statement_index;
                        semantic_analyser_log_error_new(analyser, error);
                    }
                    else {
                        if (workload.options.code_block.active_defer_statements.size != 0 && statement_node->children.size != 0) {
                            // Return value needs to be saved in another register before being returned, because defers could otherwise change the values afterwards
                            IR_Data_Access tmp = ir_data_access_create_intermediate(
                                block_workload->code_block, ir_data_access_get_type(&return_instr.options.return_instr.options.return_value)
                            );
                            IR_Instruction move_instr;
                            move_instr.type = IR_Instruction_Type::MOVE;
                            move_instr.options.move.destination = tmp;
                            move_instr.options.move.source = return_instr.options.return_instr.options.return_value;
                            dynamic_array_push_back(&workload.options.code_block.code_block->instructions, move_instr);
                            return_instr.options.return_instr.options.return_value = tmp;
                        }
                        workload_code_block_work_through_defers(analyser, &workload, Defer_Resolve_Depth::WHOLE_FUNCTION);
                    }

                    dynamic_array_push_back(&block_workload->code_block->instructions, return_instr);
                    break;
                }
                case AST_Node_Type::STATEMENT_BREAK:
                {
                    if (block_workload->inside_loop) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_BREAK_NOT_INSIDE_LOOP;
                        error.error_node_index = statement_index;
                        semantic_analyser_log_error_new(analyser, error);
                    }
                    if (!block_workload->inside_defer) {
                        workload_code_block_work_through_defers(analyser, &workload, Defer_Resolve_Depth::LOOP_EXIT);
                    }

                    IR_Instruction break_instr;
                    break_instr.type = IR_Instruction_Type::BREAK;
                    dynamic_array_push_back(&block_workload->code_block->instructions, break_instr);
                    statement_result = Statement_Analysis_Result::BREAK;
                    break;
                }
                case AST_Node_Type::STATEMENT_CONTINUE:
                {
                    if (block_workload->inside_loop) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_CONTINUE_NOT_INSIDE_LOOP;
                        error.error_node_index = statement_index;
                        semantic_analyser_log_error_new(analyser, error);
                    }
                    if (!block_workload->inside_defer) {
                        workload_code_block_work_through_defers(analyser, &workload, Defer_Resolve_Depth::LOOP_EXIT);
                    }

                    IR_Instruction continue_instr;
                    continue_instr.type = IR_Instruction_Type::CONTINUE;
                    dynamic_array_push_back(&block_workload->code_block->instructions, continue_instr);
                    statement_result = Statement_Analysis_Result::CONTINUE;
                    break;
                }
                case AST_Node_Type::STATEMENT_DEFER:
                {
                    if (block_workload->inside_defer) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS;
                        error.error_node_index = statement_index;
                        semantic_analyser_log_error_new(analyser, error);
                    }
                    else {
                        dynamic_array_push_back(&block_workload->active_defer_statements, statement_node->children[0]);
                    }
                    break;
                }
                case AST_Node_Type::STATEMENT_EXPRESSION:
                {
                    AST_Node* expression_node = &analyser->compiler->parser.nodes[statement_node->children[0]];
                    if (expression_node->type != AST_Node_Type::EXPRESSION_FUNCTION_CALL) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL;
                        error.error_node_index = statement_index;
                        semantic_analyser_log_error_new(analyser, error);
                        break;
                    }
                    IR_Data_Access temp;
                    Expression_Analysis_Result result = semantic_analyser_analyse_expression(
                        analyser, workload.symbol_table, statement_node->children[0], block_workload->code_block, true, &temp
                    );
                    if (result.type == Analysis_Result_Type::DEPENDENCY) {
                        found_workload_dependency = true;
                        found_dependency = result.options.dependency;
                    }
                    break;
                }
                case AST_Node_Type::STATEMENT_BLOCK:
                {
                    IR_Instruction block_instruction;
                    block_instruction.type = IR_Instruction_Type::BLOCK;
                    block_instruction.options.block = ir_code_block_create(block_workload->code_block->function);
                    dynamic_array_push_back(&block_workload->code_block->instructions, block_instruction);

                    Analysis_Workload new_workload = analysis_workload_make_code_block(analyser, statement_index, block_instruction.options.block, &workload);
                    dynamic_array_push_back(&analyser->active_workloads, new_workload);
                    block_workload->check_last_instruction_result = true;
                    found_workload_dependency = true;
                    found_dependency = workload_dependency_make_code_block_finished(block_instruction.options.block, statement_index);
                    block_workload->current_child_index++;
                    break;
                }
                case AST_Node_Type::STATEMENT_IF:
                {
                    IR_Instruction if_instruction;
                    if_instruction.type = IR_Instruction_Type::IF;
                    Expression_Analysis_Result expression_result = semantic_analyser_analyse_expression(
                        analyser, workload.symbol_table, statement_node->children[0], block_workload->code_block, true, &if_instruction.options.if_instr.condition
                    );
                    switch (expression_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        if (expression_result.options.success.result_type != analyser->compiler->type_system.bool_type) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_IF_CONDITION;
                            error.error_node_index = statement_node->children[0];
                            semantic_analyser_log_error_new(analyser, error);
                        }
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = expression_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency) {
                        break;
                    }

                    if_instruction.options.if_instr.true_branch = ir_code_block_create(block_workload->code_block->function);
                    if_instruction.options.if_instr.false_branch = ir_code_block_create(block_workload->code_block->function);
                    dynamic_array_push_back(&block_workload->code_block->instructions, if_instruction);
                    Analysis_Workload if_branch_work = analysis_workload_make_code_block(analyser, statement_node->children[1],
                        if_instruction.options.if_instr.true_branch, &workload
                    );
                    dynamic_array_push_back(&analyser->active_workloads, if_branch_work);
                    break;
                }
                case AST_Node_Type::STATEMENT_IF_ELSE:
                {
                    IR_Instruction if_instruction;
                    if_instruction.type = IR_Instruction_Type::IF;
                    Expression_Analysis_Result expression_result = semantic_analyser_analyse_expression(
                        analyser, workload.symbol_table, statement_node->children[0], block_workload->code_block, true, &if_instruction.options.if_instr.condition
                    );
                    switch (expression_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        if (expression_result.options.success.result_type != analyser->compiler->type_system.bool_type) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_IF_CONDITION;
                            error.error_node_index = statement_node->children[0];
                            semantic_analyser_log_error_new(analyser, error);
                        }
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = expression_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency) {
                        break;
                    }

                    if_instruction.options.if_instr.true_branch = ir_code_block_create(block_workload->code_block->function);
                    if_instruction.options.if_instr.false_branch = ir_code_block_create(block_workload->code_block->function);
                    dynamic_array_push_back(&block_workload->code_block->instructions, if_instruction);

                    Analysis_Workload if_branch_work = analysis_workload_make_code_block(analyser, statement_node->children[1],
                        if_instruction.options.if_instr.true_branch, &workload
                    );
                    dynamic_array_push_back(&analyser->active_workloads, if_branch_work);
                    Waiting_Workload else_waiting;
                    else_waiting.dependency = workload_dependency_make_code_block_finished(if_instruction.options.if_instr.true_branch, statement_node->children[1]);
                    else_waiting.workload = analysis_workload_make_code_block(analyser, statement_node->children[2],
                        if_instruction.options.if_instr.false_branch, &workload
                    );
                    dynamic_array_push_back(&analyser->waiting_workload, else_waiting);

                    found_workload_dependency = true;
                    found_dependency = workload_dependency_make_code_block_finished(if_instruction.options.if_instr.false_branch, statement_node->children[2]);
                    block_workload->check_last_instruction_result = true;
                    block_workload->current_child_index++;
                    break;
                }
                case AST_Node_Type::STATEMENT_WHILE:
                {
                    IR_Instruction while_instruction;
                    while_instruction.type = IR_Instruction_Type::WHILE;
                    while_instruction.options.while_instr.condition_code = ir_code_block_create(block_workload->code_block->function);
                    Expression_Analysis_Result expression_result = semantic_analyser_analyse_expression(
                        analyser, workload.symbol_table, statement_node->children[0], while_instruction.options.while_instr.condition_code,
                        true, &while_instruction.options.while_instr.condition_access
                    );
                    switch (expression_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        if (expression_result.options.success.result_type != analyser->compiler->type_system.bool_type) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION;
                            error.error_node_index = statement_node->children[0];
                            semantic_analyser_log_error_new(analyser, error);
                        }
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = expression_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency) {
                        ir_code_block_destroy(while_instruction.options.while_instr.condition_code);
                        break;
                    }
                    while_instruction.options.while_instr.code = ir_code_block_create(block_workload->code_block->function);
                    dynamic_array_push_back(&block_workload->code_block->instructions, while_instruction);

                    Analysis_Workload while_body_workload = analysis_workload_make_code_block(analyser, statement_node->children[1],
                        while_instruction.options.while_instr.code, &workload
                    );
                    while_body_workload.options.code_block.surrounding_loop_defer_depth = block_workload->active_defer_statements.size;
                    dynamic_array_push_back(&analyser->active_workloads, while_body_workload);

                    found_workload_dependency = true;
                    found_dependency = workload_dependency_make_code_block_finished(while_instruction.options.while_instr.code, statement_node->children[1]);
                    block_workload->check_last_instruction_result = true;
                    block_workload->current_child_index++;
                    break;
                }
                case AST_Node_Type::STATEMENT_DELETE:
                {
                    IR_Data_Access delete_access;
                    Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
                        analyser, workload.symbol_table, statement_node->children[0], block_workload->code_block, true, &delete_access
                    );

                    bool error_occured = false;
                    Type_Signature* delete_type = expr_result.options.success.result_type;
                    switch (expr_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                    {
                        delete_type = expr_result.options.success.result_type;
                        if (delete_type->type != Signature_Type::POINTER && delete_type->type != Signature_Type::ARRAY_UNSIZED) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_DELETE;
                            error.error_node_index = statement_index;
                            semantic_analyser_log_error_new(analyser, error);
                            error_occured = true;
                        }
                        break;
                    }
                    break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = expr_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        error_occured = true;
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency || error_occured) {
                        break;
                    }

                    IR_Instruction delete_instr;
                    delete_instr.type = IR_Instruction_Type::FUNCTION_CALL;
                    delete_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
                    delete_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
                    delete_instr.options.call.destination = {};
                    delete_instr.options.call.options.hardcoded = analyser->program->hardcoded_functions[(int)IR_Hardcoded_Function_Type::FREE_POINTER];
                    if (delete_type->type == Signature_Type::ARRAY_UNSIZED)
                    {
                        IR_Instruction address_instr;
                        address_instr.type = IR_Instruction_Type::ADDRESS_OF;
                        address_instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
                        address_instr.options.address_of.source = delete_access;
                        Type_Signature* pointer_sig = type_system_make_pointer(&analyser->compiler->type_system, delete_type->child_type);
                        IR_Data_Access array_data_access = ir_data_access_create_intermediate(block_workload->code_block,
                            type_system_make_pointer(&analyser->compiler->type_system, pointer_sig)
                        );
                        address_instr.options.address_of.destination = array_data_access;
                        address_instr.options.address_of.options.member.name_handle = analyser->token_index_data;
                        address_instr.options.address_of.options.member.offset = 0;
                        address_instr.options.address_of.options.member.type = pointer_sig;
                        dynamic_array_push_back(&block_workload->code_block->instructions, address_instr);
                        array_data_access.is_memory_access = true;
                        dynamic_array_push_back(&delete_instr.options.call.arguments, array_data_access);
                    }
                    else {
                        dynamic_array_push_back(&delete_instr.options.call.arguments, delete_access);
                    }
                    dynamic_array_push_back(&block_workload->code_block->instructions, delete_instr);
                    break;
                }
                case AST_Node_Type::STATEMENT_ASSIGNMENT:
                {
                    IR_Data_Access left_access;
                    int rollback_instruction_index = block_workload->code_block->instructions.size;
                    int rollback_register_index = block_workload->code_block->registers.size;
                    bool error_occured = false;
                    Expression_Analysis_Result left_result = semantic_analyser_analyse_expression(
                        analyser, workload.symbol_table, statement_node->children[0], block_workload->code_block, true, &left_access
                    );
                    Type_Signature* left_type;
                    switch (left_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        left_type = left_result.options.success.result_type;
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = left_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        error_occured = true;
                        break;
                    default: panic("what");
                    }
                    if (found_workload_dependency) {
                        break;
                    }

                    IR_Data_Access right_access;
                    Expression_Analysis_Result right_result = semantic_analyser_analyse_expression(
                        analyser, workload.symbol_table, statement_node->children[1], block_workload->code_block, true, &right_access
                    );
                    Type_Signature* right_type = 0;
                    switch (right_result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        right_type = right_result.options.success.result_type;
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        found_workload_dependency = true;
                        found_dependency = right_result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        error_occured = true;
                        break;
                    default: panic("what");
                    }
                    if (error_occured || found_workload_dependency) {
                        break;
                    }

                    if (right_type == analyser->compiler->type_system.void_type) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::INVALID_TYPE_VOID_USAGE;
                        error.error_node_index = statement_node->children[0];
                        semantic_analyser_log_error_new(analyser, error);
                        break;
                    }
                    if (!left_result.options.success.has_memory_address) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS;
                        error.error_node_index = statement_index;
                        semantic_analyser_log_error_new(analyser, error);
                        break;
                    }
                    if (left_type != right_type) {
                        if (!semantic_analyser_cast_implicit_if_possible(analyser, block_workload->code_block, right_access, left_access)) {
                            Semantic_Error error;
                            error.type = Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT;
                            error.error_node_index = statement_index;
                            error.given_type = right_type;
                            error.expected_type = left_type;
                            semantic_analyser_log_error_new(analyser, error);
                        }
                    }
                    else {
                        IR_Instruction move_instr;
                        move_instr.type = IR_Instruction_Type::MOVE;
                        move_instr.options.move.source = right_access;
                        move_instr.options.move.destination = left_access;
                        dynamic_array_push_back(&block_workload->code_block->instructions, move_instr);
                    }
                    break;
                }
                case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
                case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
                case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: {
                    Variable_Creation_Analysis_Result result = semantic_analyser_analyse_variable_creation_statements(
                        analyser, workload.symbol_table, statement_index, block_workload->code_block, false
                    );
                    if (result.type == Analysis_Result_Type::DEPENDENCY) {
                        found_workload_dependency = true;
                        found_dependency = result.dependency;
                    }
                    break;
                }
                default: {
                    panic("Should be covered!\n");
                    break;
                }
                }
            }

            if (found_workload_dependency) {
                break; // Will be added to waiting queue outside this thing
            }

            // Check if block ending is correct
            if (block_workload->requires_return && statement_result == Statement_Analysis_Result::NO_RETURN)
            {
                if (block_workload->code_block->function->function_type->return_type == analyser->compiler->type_system.void_type)
                {
                    workload_code_block_work_through_defers(analyser, &workload, Defer_Resolve_Depth::WHOLE_FUNCTION);
                    IR_Instruction return_instr;
                    return_instr.type = IR_Instruction_Type::RETURN;
                    if (block_workload->code_block->function == analyser->program->entry_function) {
                        return_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                        return_instr.options.return_instr.options.exit_code = IR_Exit_Code::SUCCESS;
                    }
                    else {
                        return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
                    }
                    dynamic_array_push_back(&block_workload->code_block->instructions, return_instr);
                }
                else {
                    Semantic_Error error;
                    error.type = Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT;
                    error.error_node_index = workload.node_index;
                    semantic_analyser_log_error_new(analyser, error);
                }
            }
            workload_code_block_work_through_defers(analyser, &workload, Defer_Resolve_Depth::LOCAL_BLOCK);
            hashtable_insert_element(&analyser->finished_code_blocks, block_workload->code_block, statement_result);
            break;
        }
        case Analysis_Workload_Type::STRUCT_BODY:
        {
            AST_Node* struct_node = &nodes->data[workload.node_index];
            Type_Signature* struct_signature = workload.options.struct_body.struct_signature;
            if (struct_signature->size_in_bytes != 0 || struct_signature->alignment_in_bytes != 0) {
                panic("Already analysed!");
            }

            for (int i = workload.options.struct_body.current_child_index; i < struct_node->children.size; i++)
            {
                AST_Node* member_definition_node = &(*nodes)[struct_node->children[i]]; // 
                Type_Analysis_Result member_result = semantic_analyser_analyse_type(
                    analyser, workload.options.struct_body.type_lookup_table, member_definition_node->children[0]
                );
                Type_Signature* member_type = 0;
                switch (member_result.type)
                {
                case Analysis_Result_Type::SUCCESS:
                    member_type = member_result.options.result_type;
                    if (member_type->alignment_in_bytes == 0 && member_type->size_in_bytes == 0) {
                        found_workload_dependency = true;
                        found_dependency = workload_dependency_make_type_size_unknown(member_type, struct_node->children[i]);
                    }
                    break;
                case Analysis_Result_Type::DEPENDENCY:
                    found_workload_dependency = true;
                    found_dependency = member_result.options.dependency;
                    break;
                case Analysis_Result_Type::ERROR_OCCURED:
                    member_type = analyser->compiler->type_system.error_type;
                    break;
                default: panic("HEY");
                }
                if (found_workload_dependency) {
                    workload.options.struct_body.current_child_index = i;
                    break;
                }
                workload.options.struct_body.alignment = math_maximum(workload.options.struct_body.alignment, member_type->alignment_in_bytes);
                workload.options.struct_body.offset = math_round_next_multiple(workload.options.struct_body.offset, member_type->alignment_in_bytes);

                for (int j = 0; j < struct_signature->member_types.size; j++) {
                    if (struct_signature->member_types[j].name_handle == member_definition_node->name_id) {
                        Semantic_Error error;
                        error.type = Semantic_Error_Type::OTHERS_STRUCT_MEMBER_ALREADY_DEFINED;
                        error.name_id = member_definition_node->name_id;
                        error.error_node_index = struct_node->children[i];
                        semantic_analyser_log_error_new(analyser, error);
                    }
                }
                Struct_Member member;
                member.name_handle = member_definition_node->name_id;
                member.offset = workload.options.struct_body.offset;
                member.type = member_type;
                dynamic_array_push_back(&struct_signature->member_types, member);

                workload.options.struct_body.offset += member_type->size_in_bytes;
            }

            if (found_workload_dependency) {
                break;
            }

            struct_signature->size_in_bytes = workload.options.struct_body.offset;
            struct_signature->alignment_in_bytes = workload.options.struct_body.alignment;
            if (workload.options.struct_body.is_template_instance) {
                Symbol* struct_symbol = symbol_table_find_symbol(workload.symbol_table, workload.options.struct_body.symbol_name_id, true);
                assert(struct_symbol != 0, "hey");
                struct_symbol->template_instances[workload.options.struct_body.symbol_instance_index].instanciated = true;
                struct_symbol->template_instances[workload.options.struct_body.symbol_instance_index].options.data_type = workload.options.struct_body.struct_signature;
            }

            break;
        }
        default: panic("Hey");
        }

        // Finish Workload
        if (found_workload_dependency)
        {
            Waiting_Workload waiting;
            waiting.workload = workload;
            waiting.dependency = found_dependency;
            dynamic_array_push_back(&analyser->waiting_workload, waiting);

            if (PRINT_DEPENDENCIES)
            {
                String output = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&output));
                string_append_formated(&output, "DEPENDENCY: ");
                workload_dependency_append_to_string(&waiting.dependency, &output, analyser);
                string_append_formated(&output, "   |||   Workload: ");
                analysis_workload_append_to_string(&workload, &output, analyser);
                string_append_formated(&output, "\n");
                logg(output.characters);
            }
        }
        else
        {
            // Workload finished
            if (PRINT_DEPENDENCIES)
            {
                String output = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&output));
                string_append_formated(&output, "FINISHED: ");
                analysis_workload_append_to_string(&workload, &output, analyser);
                string_append_formated(&output, "\n");
                logg(output.characters);
            }

            // Destroy Workload
            analysis_workload_destroy(&workload);
        }

        // Check if dependencies have been resolved
        if (analyser->active_workloads.size == 0)
        {
            // TODO: We would probably want a message system at some point, which resolves these things automatically
            for (int i = 0; i < analyser->waiting_workload.size; i++)
            {
                Waiting_Workload* waiting = &analyser->waiting_workload[i];
                bool dependency_resolved = false;
                bool error_occured = false;
                // Check if dependency is resolved
                switch (waiting->dependency.type)
                {
                case Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED: {
                    Statement_Analysis_Result* result = hashtable_find_element(&analyser->finished_code_blocks, waiting->dependency.options.code_block);
                    dependency_resolved = result != 0;
                    break;
                }
                case Workload_Dependency_Type::IDENTIFER_NOT_FOUND:
                {
                    Identifier_Analysis_Result result = semantic_analyser_analyse_identifier_node_with_template_arguments(
                        analyser,
                        waiting->dependency.options.identifier_not_found.symbol_table,
                        &analyser->compiler->parser, waiting->dependency.node_index, waiting->dependency.options.identifier_not_found.current_scope_only,
                        waiting->dependency.options.identifier_not_found.template_parameter_names
                    );
                    switch (result.type)
                    {
                    case Analysis_Result_Type::SUCCESS:
                        dependency_resolved = true;
                        break;
                    case Analysis_Result_Type::DEPENDENCY:
                        waiting->dependency = result.options.dependency;
                        break;
                    case Analysis_Result_Type::ERROR_OCCURED:
                        dependency_resolved = true;
                        error_occured = true;
                        break;
                    }
                    break;
                }
                case Workload_Dependency_Type::TYPE_SIZE_UNKNOWN: {
                    if (waiting->dependency.options.type_signature->alignment_in_bytes != 0 &&
                        waiting->dependency.options.type_signature->size_in_bytes != 0) {
                        dependency_resolved = true;
                    }
                    break;
                }
                case Workload_Dependency_Type::TEMPLATE_INSTANCE_NOT_FINISHED: {
                    Symbol* symbol = symbol_table_find_symbol(waiting->dependency.options.template_not_finished.symbol_table,
                        waiting->dependency.options.template_not_finished.symbol_name_id, false);
                    assert(symbol != 0, "Hey");
                    Symbol_Template_Instance* instance = &symbol->template_instances[waiting->dependency.options.template_not_finished.instance_index];
                    dependency_resolved = instance->instanciated;
                    break;
                }
                default: panic("Hey"); break;
                }

                if (dependency_resolved)
                {
                    if (PRINT_DEPENDENCIES && !error_occured)
                    {
                        String output = string_create_empty(256);
                        SCOPE_EXIT(string_destroy(&output));
                        string_append_formated(&output, "RESOLVED: ");
                        workload_dependency_append_to_string(&waiting->dependency, &output, analyser);
                        string_append_formated(&output, "   |||   Workload: ");
                        analysis_workload_append_to_string(&waiting->workload, &output, analyser);
                        string_append_formated(&output, "\n");
                        logg(output.characters);
                    }

                    if (!error_occured)
                    {
                        dynamic_array_push_back(&analyser->active_workloads, waiting->workload);
                        workload_dependency_destroy(&waiting->dependency);
                    }
                    dynamic_array_swap_remove(&analyser->waiting_workload, i);
                    i = i - 1;
                }
            }
        }
    }

    // Add return for global init function
    {
        IR_Instruction return_instr;
        return_instr.type = IR_Instruction_Type::RETURN;
        return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
        dynamic_array_push_back(&analyser->global_init_function->code->instructions, return_instr);
        if (analyser->program->entry_function == 0) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::OTHERS_MAIN_NOT_DEFINED;
            error.error_node_index = 0;
            semantic_analyser_log_error_new(analyser, error);
        }
    }


    // Log unresolved dependency errors
    if (analyser->errors.size == 0 && analyser->waiting_workload.size != 0)
    {
        for (int i = 0; i < analyser->waiting_workload.size; i++)
        {
            Workload_Dependency* dependency = &analyser->waiting_workload[i].dependency;
            Semantic_Error error;
            error.error_node_index = dependency->node_index;
            switch (dependency->type)
            {
            case Workload_Dependency_Type::TEMPLATE_INSTANCE_NOT_FINISHED: {
                error.type = Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TEMPLATE_INSTANCE;
                break;
            }
            case Workload_Dependency_Type::CODE_BLOCK_NOT_FINISHED: {
                error.type = Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK;
                break;
            }
            case Workload_Dependency_Type::IDENTIFER_NOT_FOUND:
                error.type = Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL;
                error.error_node_index = dependency->node_index;
                error.identifier_node_index = dependency->node_index;
                break;
            case Workload_Dependency_Type::TYPE_SIZE_UNKNOWN:
                error.type = Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE;
                break;
            default: panic("Hey"); break;
            }
            semantic_analyser_log_error_new(analyser, error);
            analysis_workload_destroy(&analyser->waiting_workload[i].workload);
            workload_dependency_destroy(&analyser->waiting_workload[i].dependency);
        }
        dynamic_array_reset(&analyser->waiting_workload);
        dynamic_array_reset(&analyser->active_workloads);
    }
}

Semantic_Analyser semantic_analyser_create()
{
    Semantic_Analyser result;
    result.symbol_tables = dynamic_array_create_empty<Symbol_Table*>(64);
    result.active_workloads = dynamic_array_create_empty<Analysis_Workload>(64);
    result.waiting_workload = dynamic_array_create_empty<Waiting_Workload>(64);
    result.errors = dynamic_array_create_empty<Semantic_Error>(64);
    result.known_expression_values = dynamic_array_create_empty<void*>(32);
    result.finished_code_blocks = hashtable_create_pointer_empty<IR_Code_Block*, Statement_Analysis_Result>(64);
    result.ast_to_symbol_table = hashtable_create_empty<int, Symbol_Table*>(256, &hash_i32, &equals_i32);
    result.program = 0;
    return result;
}

void semantic_analyser_destroy(Semantic_Analyser* analyser)
{
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        symbol_table_destroy(analyser->symbol_tables[i]);
    }
    dynamic_array_destroy(&analyser->symbol_tables);
    for (int i = 0; i < analyser->known_expression_values.size; i++) {
        delete analyser->known_expression_values[i];
    }
    dynamic_array_destroy(&analyser->known_expression_values);
    dynamic_array_destroy(&analyser->errors);
    dynamic_array_destroy(&analyser->active_workloads);
    dynamic_array_destroy(&analyser->waiting_workload);
    hashtable_destroy(&analyser->ast_to_symbol_table);
    hashtable_destroy(&analyser->finished_code_blocks);
}

