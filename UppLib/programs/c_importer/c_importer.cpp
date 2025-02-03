#include "c_importer.hpp"

#include "../../utility/file_io.hpp"
#include "c_lexer.hpp"
#include "../upp_lang/compiler_misc.hpp"
#include <iostream>
#include <cstdio>
#include <algorithm>
#include "../../win32/process.hpp"
#include "../../utility/binary_parser.hpp"
#include "../../win32/timing.hpp"

struct C_Variable_Instance
{
    String* id;
    C_Import_Type* type;
};

struct C_Variable_Definition
{
    C_Import_Type* base_type;
    Dynamic_Array<C_Variable_Instance> instances;
};

C_Import_Package c_import_package_create()
{
    C_Import_Package result;
    result.symbol_table.symbols = hashtable_create_pointer_empty<String*, C_Import_Symbol>(64);
    result.type_system.registered_types = dynamic_array_create<C_Import_Type*>(64);

    C_Import_Type* error_prototype = new C_Import_Type;
    error_prototype->type = C_Import_Type_Type::UNKNOWN_TYPE;
    error_prototype->byte_size = 1;
    error_prototype->alignment = 1;
    error_prototype->qualifiers = (C_Type_Qualifiers)0;
    dynamic_array_push_back(&result.type_system.registered_types, error_prototype);
    result.type_system.unknown_type = error_prototype;

    return result;
}

void c_import_package_destroy(C_Import_Package* package)
{
    hashtable_destroy(&package->symbol_table.symbols);
    for (int i = 0; i < package->type_system.registered_types.size; i++) {
        C_Import_Type* type = package->type_system.registered_types[i];
        switch (type->type)
        {
        case C_Import_Type_Type::ARRAY:
        case C_Import_Type_Type::POINTER:
        case C_Import_Type_Type::PRIMITIVE:
        case C_Import_Type_Type::UNKNOWN_TYPE:
            break;
        case C_Import_Type_Type::ENUM:
            dynamic_array_destroy(&type->enumeration.members);
            break;
        case C_Import_Type_Type::FUNCTION_SIGNATURE:
            dynamic_array_destroy(&type->function_signature.parameters);
            break;
        case C_Import_Type_Type::STRUCTURE:
            dynamic_array_destroy(&type->structure.members);
            break;
        default: panic("HEY");
        }
        delete type;
    }
    dynamic_array_destroy(&package->type_system.registered_types);
}

C_Import_Type* c_import_type_system_register_type(C_Import_Type_System* system, C_Import_Type type)
{
    bool break_loop = false;
    for (int i = 0; i < system->registered_types.size && !break_loop; i++)
    {
        C_Import_Type* cmp_type = system->registered_types[i];
        if (cmp_type->type != type.type) continue;
        if (cmp_type->qualifiers != type.qualifiers) continue;
        switch (type.type)
        {
        case C_Import_Type_Type::UNKNOWN_TYPE:
            return cmp_type;
        case C_Import_Type_Type::ENUM:
        case C_Import_Type_Type::STRUCTURE:
            break_loop = true;
            continue;
        case C_Import_Type_Type::ARRAY:
            if (type.array.element_type == cmp_type->array.element_type && type.array.array_size == cmp_type->array.array_size) return cmp_type;
            break;
        case C_Import_Type_Type::FUNCTION_SIGNATURE: {
            if (type.function_signature.return_type != cmp_type->function_signature.return_type ||
                type.function_signature.parameters.size != cmp_type->function_signature.parameters.size) {
                continue;
            }
            bool all_match = true;
            for (int j = 0; j < type.function_signature.parameters.size; j++) {
                if (type.function_signature.parameters[j].type != cmp_type->function_signature.parameters[j].type) {
                    all_match = false;
                    break;
                }
            }
            if (all_match)
            {
                dynamic_array_destroy(&type.function_signature.parameters);
                return cmp_type;
            }
            continue;
        }
        case C_Import_Type_Type::POINTER:
            if (type.pointer_child_type == cmp_type->pointer_child_type) return cmp_type;
            continue;
        case C_Import_Type_Type::PRIMITIVE:
            if (type.primitive == cmp_type->primitive) return cmp_type;
            continue;
        default: panic("HEY");
        }
    }

    C_Import_Type* registered_type = new C_Import_Type;
    *registered_type = type;
    dynamic_array_push_back(&system->registered_types, registered_type);
    return registered_type;
}

struct Header_Parser
{
    C_Import_Package result_package;
    C_Lexer* lexer;
    Dynamic_Array<C_Token> tokens;
    int index;
    String source_code;

    String* identifier_typedef;
    String* identifier_unaligned;
    String* identifier_inline;
    String* identifier_inline_alt;
    String* identifier_ptr32;
    String* identifier_ptr64;
    String* identifier_force_inline;
    String* identifier_static;
    String* identifier_enum;
    String* identifier_union;
    String* identifier_wchar_t;
    String* identifier_wchar_t_alt;
    String* identifier_int8;
    String* identifier_int16;
    String* identifier_int32;
    String* identifier_int64;
    String* identifier_bool;
    String* identifier_char;
    String* identifier_short;
    String* identifier_int;
    String* identifier_long;
    String* identifier_float;
    String* identifier_double;
    String* identifier_void;
    String* identifier_signed;
    String* identifier_unsigned;
    String* identifier_const;
    String* identifier_volatile;
    String* identifier_restrict;
    String* identifier_atomic;
    String* identifier_call_conv_cdecl;
    String* identifier_call_conv_clrcall;
    String* identifier_call_conv_stdcall;
    String* identifier_call_conv_fastcall;
    String* identifier_call_conv_thiscall;
    String* identifier_call_conv_vectorcall;
};

void print_tokens_till_newline(Dynamic_Array<C_Token> tokens, String source, int token_index);
Header_Parser header_parser_create(C_Lexer* lexer, String source_code)
{
    Header_Parser result;
    result.result_package = c_import_package_create();
    result.lexer = lexer;
    result.index = 0;
    result.source_code = source_code;
    result.identifier_typedef = identifier_pool_add(lexer->identifier_pool, string_create_static("typedef"));
    result.identifier_unaligned = identifier_pool_add(lexer->identifier_pool, string_create_static("__unaligned"));
    result.identifier_ptr32 = identifier_pool_add(lexer->identifier_pool, string_create_static("__ptr32"));
    result.identifier_ptr64 = identifier_pool_add(lexer->identifier_pool, string_create_static("__ptr64"));
    result.identifier_inline = identifier_pool_add(lexer->identifier_pool, string_create_static("inline"));
    result.identifier_inline_alt = identifier_pool_add(lexer->identifier_pool, string_create_static("__inline"));
    result.identifier_force_inline = identifier_pool_add(lexer->identifier_pool, string_create_static("__forceinline"));
    result.identifier_static = identifier_pool_add(lexer->identifier_pool, string_create_static("static"));
    result.identifier_enum = identifier_pool_add(lexer->identifier_pool, string_create_static("enum"));
    result.identifier_union = identifier_pool_add(lexer->identifier_pool, string_create_static("union"));
    result.identifier_char = identifier_pool_add(lexer->identifier_pool, string_create_static("char"));
    result.identifier_short = identifier_pool_add(lexer->identifier_pool, string_create_static("short"));
    result.identifier_int = identifier_pool_add(lexer->identifier_pool, string_create_static("int"));
    result.identifier_wchar_t = identifier_pool_add(lexer->identifier_pool, string_create_static("wchar_t"));
    result.identifier_wchar_t_alt = identifier_pool_add(lexer->identifier_pool, string_create_static("__wchar_t"));
    result.identifier_int8 = identifier_pool_add(lexer->identifier_pool, string_create_static("__int8"));
    result.identifier_int16 = identifier_pool_add(lexer->identifier_pool, string_create_static("__int16"));
    result.identifier_int32 = identifier_pool_add(lexer->identifier_pool, string_create_static("__int32"));
    result.identifier_int64 = identifier_pool_add(lexer->identifier_pool, string_create_static("__int64"));
    result.identifier_long = identifier_pool_add(lexer->identifier_pool, string_create_static("long"));
    result.identifier_float = identifier_pool_add(lexer->identifier_pool, string_create_static("float"));
    result.identifier_double = identifier_pool_add(lexer->identifier_pool, string_create_static("double"));
    result.identifier_signed = identifier_pool_add(lexer->identifier_pool, string_create_static("signed"));
    result.identifier_bool = identifier_pool_add(lexer->identifier_pool, string_create_static("bool"));
    result.identifier_void = identifier_pool_add(lexer->identifier_pool, string_create_static("void"));
    result.identifier_unsigned = identifier_pool_add(lexer->identifier_pool, string_create_static("unsigned"));
    result.identifier_const = identifier_pool_add(lexer->identifier_pool, string_create_static("const"));
    result.identifier_volatile = identifier_pool_add(lexer->identifier_pool, string_create_static("volatile"));
    result.identifier_restrict = identifier_pool_add(lexer->identifier_pool, string_create_static("restrict"));
    result.identifier_atomic = identifier_pool_add(lexer->identifier_pool, string_create_static("atomic"));

    result.identifier_call_conv_cdecl = identifier_pool_add(lexer->identifier_pool, string_create_static("__cdecl"));
    result.identifier_call_conv_clrcall = identifier_pool_add(lexer->identifier_pool, string_create_static("__clrcall"));
    result.identifier_call_conv_fastcall = identifier_pool_add(lexer->identifier_pool, string_create_static("__stdcall"));
    result.identifier_call_conv_stdcall = identifier_pool_add(lexer->identifier_pool, string_create_static("__fastcall"));
    result.identifier_call_conv_thiscall = identifier_pool_add(lexer->identifier_pool, string_create_static("__thiscall"));
    result.identifier_call_conv_vectorcall = identifier_pool_add(lexer->identifier_pool, string_create_static("__vectorcall"));

    // Create new tokens array, where lines starting with # are removed, and __pragma and __declspec compiler stuff is removed
    result.tokens = dynamic_array_create<C_Token>(lexer->tokens.size);

    String* identifier_pragma_underscore = identifier_pool_add(lexer->identifier_pool, string_create_static("__pragma"));
    String* identifier_declspec = identifier_pool_add(lexer->identifier_pool, string_create_static("__declspec"));
    String* identifier_static_assert = identifier_pool_add(lexer->identifier_pool, string_create_static("static_assert"));
    int last_line_index = -1;
    for (int i = 0; i < lexer->tokens.size; i++)
    {
        C_Token* token = &lexer->tokens[i];
        bool is_first_token_in_line = false;
        if (token->position.start.line_index != last_line_index) {
            last_line_index = token->position.start.line_index;
            is_first_token_in_line = true;
        }

        // Skip lines starting with a hashtag
        if (is_first_token_in_line && token->type == C_Token_Type::HASHTAG) {
            while (i < lexer->tokens.size && lexer->tokens[i].position.start.line_index == last_line_index) {
                i++;
            }
            i--;
            continue;
        }

        if (token->type == C_Token_Type::IDENTIFIER_NAME)
        {
            if (token->attribute.id == identifier_pragma_underscore || 
                token->attribute.id == identifier_declspec || 
                token->attribute.id == identifier_static_assert) 
            {
                // Skip everything afterwards if followed by a (
                i += 1;
                token = &lexer->tokens[i];
                if (token->type == C_Token_Type::OPEN_PARENTHESIS) {
                    i++;
                    int depth = 1;
                    while (i < lexer->tokens.size) {
                        token = &lexer->tokens[i];
                        if (token->type == C_Token_Type::OPEN_PARENTHESIS) {
                            depth++;
                        }
                        else if (token->type == C_Token_Type::CLOSED_PARENTHESIS) {
                            depth--;
                            if (depth == 0) {
                                break;
                            }
                        }
                        i++;
                    }
                }
                else {
                    panic("Hey, i think this is weird\n");
                }
                continue;
            }
            if (token->type == C_Token_Type::ERROR_TOKEN) {
                continue;
            }
            // Skip specific tokens
            if (token->attribute.id == result.identifier_call_conv_cdecl ||
                token->attribute.id == result.identifier_ptr32 ||
                token->attribute.id == result.identifier_ptr64 ||
                token->attribute.id == result.identifier_call_conv_clrcall ||
                token->attribute.id == result.identifier_call_conv_fastcall ||
                token->attribute.id == result.identifier_call_conv_stdcall ||
                token->attribute.id == result.identifier_call_conv_thiscall ||
                token->attribute.id == result.identifier_call_conv_vectorcall) {
                continue;
            }
        }
        dynamic_array_push_back(&result.tokens, *token);
    }

    return result;
}

void header_parser_destroy(Header_Parser* parser, bool destroy_package)
{
    dynamic_array_destroy(&parser->tokens);
    if (destroy_package) {
        c_import_package_destroy(&parser->result_package);
    }
}

bool header_parser_is_finished(Header_Parser* parser) {
    return parser->index >= parser->tokens.size;
}

void header_parser_goto_next_line(Header_Parser* parser) {
    if (!header_parser_is_finished(parser)) return;
    int line_index = parser->tokens[parser->index].position.start.line_index;
    parser->index++;
    while (header_parser_is_finished(parser) && parser->tokens[parser->index].position.start.line_index == line_index) {
        parser->index++;
    }
}

bool header_parser_test_next_token(Header_Parser* parser, C_Token_Type type) {
    if (parser->index >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == type;
}

bool header_parser_test_next_token_2(Header_Parser* parser, C_Token_Type t1, C_Token_Type t2) {
    if (parser->index + 1 >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == t1 &&
        parser->tokens[parser->index + 1].type == t2;
}

bool header_parser_test_next_token_3(Header_Parser* parser, C_Token_Type t1, C_Token_Type t2, C_Token_Type t3) {
    if (parser->index + 2 >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == t1 &&
        parser->tokens[parser->index + 1].type == t2 &&
        parser->tokens[parser->index + 2].type == t3;
}

bool header_parser_test_next_token_4(Header_Parser* parser, C_Token_Type t1, C_Token_Type t2, C_Token_Type t3, C_Token_Type t4) {
    if (parser->index + 3 >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == t1 &&
        parser->tokens[parser->index + 1].type == t2 &&
        parser->tokens[parser->index + 2].type == t3 &&
        parser->tokens[parser->index + 3].type == t4;
}

bool header_parser_test_next_token_5(Header_Parser* parser, C_Token_Type t1, C_Token_Type t2, C_Token_Type t3, C_Token_Type t4, C_Token_Type t5) {
    if (parser->index + 4 >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == t1 &&
        parser->tokens[parser->index + 1].type == t2 &&
        parser->tokens[parser->index + 2].type == t3 &&
        parser->tokens[parser->index + 3].type == t4 &&
        parser->tokens[parser->index + 4].type == t5;
}

bool header_parser_next_is_identifier(Header_Parser* parser, String* id)
{
    if (parser->index >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == C_Token_Type::IDENTIFIER_NAME && parser->tokens[parser->index].attribute.id == id;
}

void print_tokens_till_newline_token_style(Dynamic_Array<C_Token> tokens, String source, int token_index, C_Lexer* lexer)
{
    C_Token* start_tok = &tokens[token_index];
    String str = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&str));
    for (int i = token_index; i < tokens.size; i++) 
    {
        C_Token* token = &tokens[i];
        if (start_tok->position.start.line_index != token->position.start.line_index) {
            break;
        }
        switch (token->type)
        {
        case C_Token_Type::IDENTIFIER_NAME:
            string_append_formated(&str, token->attribute.id->characters);
            break;
        case C_Token_Type::STRING_LITERAL:
            string_append_formated(&str, "\"%s\"",  token->attribute.id->characters);
            break;
        case C_Token_Type::BOOLEAN_LITERAL:
            string_append_formated(&str, "%s", token->attribute.bool_value ? "TRUE": "FALSE");
            break;
        case C_Token_Type::FLOAT_LITERAL:
            string_append_formated(&str, "%3.2f", token->attribute.float_value);
            break;
        case C_Token_Type::INTEGER_LITERAL:
            string_append_formated(&str, "%d", token->attribute.integer_value);
            break;
        default:
            string_append_formated(&str, token_type_to_string(token->type));
        }
        string_append_formated(&str, " ");
    }
    logg(str.characters);
}

void print_tokens_till_newline(Dynamic_Array<C_Token> tokens, String source, int token_index)
{
    C_Token* token = &tokens[token_index];
    int end_pos = token->source_code_index;
    for (int i = token_index + 1; i < tokens.size; i++) {
        if (tokens[i].position.start.line_index != token->position.start.line_index) {
            end_pos = tokens[i].source_code_index;
            break;
        }
    }
    String t2_content = string_create_substring(&source, token->source_code_index, end_pos + 1);
    for (int i = 0; i < t2_content.size; i++) {
        if (t2_content.characters[i] == '\n' || t2_content.characters[i] == '\r') {
            t2_content.characters[i] = '\0';
            t2_content.size = i;
        }
    }
    SCOPE_EXIT(string_destroy(&t2_content));
    printf("%s", t2_content.characters);
}

void c_import_type_append_to_string(C_Import_Type* type, String* string, int indentation, bool print_array_members);

C_Type_Qualifiers header_parser_parse_type_qualifiers(Header_Parser* parser)
{
    u8 result = 0;
    while (parser->index < parser->tokens.size && parser->tokens[parser->index].type == C_Token_Type::IDENTIFIER_NAME) {
        String* id = parser->tokens[parser->index].attribute.id;
        if (id == parser->identifier_atomic) {
            result = result | (u8)C_Type_Qualifiers::ATOMIC;
        }
        else if (id == parser->identifier_const) {
            result = result | (u8)C_Type_Qualifiers::CONST_QUAL;
        }
        else if (id == parser->identifier_volatile) {
            result = result | (u8)C_Type_Qualifiers::VOLATILE;
        }
        else if (id == parser->identifier_restrict) {
            result = result | (u8)C_Type_Qualifiers::RESTRICT;
        }
        else if (id == parser->identifier_signed) {
            result = result | (u8)C_Type_Qualifiers::SIGNED;
        }
        else if (id == parser->identifier_unsigned) {
            result = result | (u8)C_Type_Qualifiers::UNSIGNED;
        }
        else {
            break;
        }
        parser->index++;
    }
    return (C_Type_Qualifiers)result;
}

Optional<C_Import_Type*> header_parser_parse_primitive_type(Header_Parser* parser, C_Type_Qualifiers qualifiers)
{
    bool success = false;
    if (!header_parser_test_next_token(parser, C_Token_Type::IDENTIFIER_NAME)) return optional_make_failure<C_Import_Type*>();

    C_Import_Type prototype;
    prototype.qualifiers = qualifiers;
    prototype.type = C_Import_Type_Type::PRIMITIVE;
    String* identifier = parser->tokens[parser->index].attribute.id;
    if (identifier == parser->identifier_long)
    {
        parser->index++;
        if (header_parser_next_is_identifier(parser, parser->identifier_long)) { // long long
            parser->index++;
            success = true;
            prototype.primitive = C_Import_Primitive::LONG_LONG;
            prototype.byte_size = sizeof(long long);
            prototype.alignment = alignof(long long);
        }
        else if (header_parser_next_is_identifier(parser, parser->identifier_int)) { // long int
            parser->index++;
            success = true;
            prototype.primitive = C_Import_Primitive::LONG;
            prototype.byte_size = sizeof(long);
            prototype.alignment = alignof(long);
        }
        else if (header_parser_next_is_identifier(parser, parser->identifier_double)) { // long double
            parser->index++;
            success = true;
            prototype.primitive = C_Import_Primitive::LONG_DOUBLE;
            prototype.byte_size = sizeof(long double);
            prototype.alignment = alignof(long double);
        }
        else {
            success = true;
            prototype.primitive = C_Import_Primitive::LONG;
            prototype.byte_size = sizeof(long);
            prototype.alignment = alignof(long);
        }
    }
    else if (identifier == parser->identifier_short || identifier == parser->identifier_wchar_t
        || identifier == parser->identifier_wchar_t_alt || identifier == parser->identifier_int16) {
        parser->index++;
        success = true;
        prototype.primitive = C_Import_Primitive::SHORT;
        prototype.byte_size = sizeof(short);
        prototype.alignment = alignof(short);
        if (header_parser_next_is_identifier(parser, parser->identifier_int)) { // short int
            parser->index++;
        }
    }
    else if (identifier == parser->identifier_char || identifier == parser->identifier_int8) {
        parser->index++;
        success = true;
        prototype.primitive = C_Import_Primitive::CHAR;
        prototype.byte_size = sizeof(char);
        prototype.alignment = alignof(char);
    }
    else if (identifier == parser->identifier_int || identifier == parser->identifier_int32) {
        parser->index++;
        success = true;
        prototype.primitive = C_Import_Primitive::INT;
        prototype.byte_size = sizeof(int);
        prototype.alignment = alignof(int);
    }
    else if (identifier == parser->identifier_int64) {
        parser->index++;
        success = true;
        prototype.primitive = C_Import_Primitive::LONG_LONG;
        prototype.byte_size = sizeof(long long);
        prototype.alignment = alignof(long long);
    }
    else if (identifier == parser->identifier_float) {
        parser->index++;
        success = true;
        prototype.primitive = C_Import_Primitive::FLOAT;
        prototype.byte_size = sizeof(float);
        prototype.alignment = alignof(float);
    }
    else if (identifier == parser->identifier_bool) {
        parser->index++;
        success = true;
        prototype.primitive = C_Import_Primitive::BOOL;
        prototype.byte_size = sizeof(bool);
        prototype.alignment = alignof(bool);
    }
    else if (identifier == parser->identifier_void) {
        parser->index++;
        success = true;
        prototype.primitive = C_Import_Primitive::VOID_TYPE;
        prototype.byte_size = 1;
        prototype.alignment = 1;
    }
    else if (identifier == parser->identifier_double) {
        parser->index++;
        success = true;
        prototype.primitive = C_Import_Primitive::DOUBLE;
        prototype.byte_size = sizeof(double);
        prototype.alignment = alignof(double);
    }

    if (success) {
        return optional_make_success(c_import_type_system_register_type(&parser->result_package.type_system, prototype));
    }
    return optional_make_failure<C_Import_Type*>();
}

struct Checkpoint
{
    Header_Parser* parser;
    int rewind_token_index;
};

Checkpoint checkpoint_make(Header_Parser* parser) {
    Checkpoint point;
    point.parser = parser;
    point.rewind_token_index = parser->index;
    return point;
}

void checkpoint_rewind(Checkpoint point) {
    point.parser->index = point.rewind_token_index;
}

void c_import_symbol_table_define_symbol(C_Import_Symbol_Table* table, C_Import_Symbol symbol, String* id)
{
    // The only time somy symbol should do shadowing if if its a typdef right after a definition, so the types should match
    C_Import_Symbol* old_sym = hashtable_find_element(&table->symbols, id);
    if (old_sym != 0)
    {
        if (old_sym->data_type != symbol.data_type) {
            panic("Changing type with shadowing!");
        }
        *old_sym = symbol;
    }
    else {
        hashtable_insert_element(&table->symbols, id, symbol);
    }
}

Optional<C_Variable_Definition> header_parser_parse_variable_definition(Header_Parser* parser, bool register_structure_tags);
Optional<C_Import_Type*> header_parser_parse_structure(Header_Parser* parser, C_Type_Qualifiers qualifiers, bool register_structure_tags)
{
    Checkpoint checkpoint = checkpoint_make(parser);

    C_Import_Type prototype;
    prototype.byte_size = 0;
    prototype.alignment = 0;
    prototype.qualifiers = qualifiers;

    if (header_parser_test_next_token(parser, C_Token_Type::STRUCT)) {
        prototype.type = C_Import_Type_Type::STRUCTURE;
        prototype.structure.is_union = false;
    }
    else if (header_parser_next_is_identifier(parser, parser->identifier_union)) {
        prototype.type = C_Import_Type_Type::STRUCTURE;
        prototype.structure.is_union = true;
    }
    else if (header_parser_next_is_identifier(parser, parser->identifier_enum)) {
        prototype.type = C_Import_Type_Type::ENUM;
    }
    else {
        checkpoint_rewind(checkpoint);
        return optional_make_failure<C_Import_Type*>();
    }
    parser->index++;

    // Check if we need to continue parsing
    C_Import_Type* structure_type = 0;
    {
        String* id;
        bool has_name = false;
        bool has_definition = false;
        if (header_parser_test_next_token(parser, C_Token_Type::IDENTIFIER_NAME))
        {
            has_name = true;
            id = parser->tokens[parser->index].attribute.id;
            parser->index++;
        }
        if (header_parser_test_next_token(parser, C_Token_Type::OPEN_BRACES))
        {
            has_definition = true;
            parser->index++;
        }

        if (!has_name && !has_definition) {
            checkpoint_rewind(checkpoint);
            return optional_make_failure<C_Import_Type*>();
        }

        if (prototype.type == C_Import_Type_Type::ENUM) {
            prototype.enumeration.is_anonymous = !has_name;
            prototype.enumeration.id = has_name ? id : nullptr;
            prototype.byte_size = 4;
            prototype.alignment = 4;
        }
        else {
            prototype.structure.is_anonymous = !has_name;
            prototype.structure.id = has_name ? id : nullptr;
            prototype.structure.contains_bitfield = false;
            prototype.byte_size = 0;
            prototype.alignment = 0;
        }

        if (has_name && register_structure_tags)
        {
            C_Import_Symbol* symbol = hashtable_find_element(&parser->result_package.symbol_table.symbols, id);
            if (symbol == 0)
            {
                if (prototype.type == C_Import_Type_Type::ENUM) {
                    prototype.enumeration.members = dynamic_array_create<C_Import_Enum_Member>(4);
                }
                else {
                    prototype.structure.members = dynamic_array_create<C_Import_Structure_Member>(4);
                }
                structure_type = c_import_type_system_register_type(&parser->result_package.type_system, prototype);

                C_Import_Symbol def_sym;
                def_sym.type = C_Import_Symbol_Type::TYPE;
                def_sym.data_type = structure_type;
                c_import_symbol_table_define_symbol(&parser->result_package.symbol_table, def_sym, id);

                /*
                {
                    String str = string_create_empty(256);
                    SCOPE_EXIT(string_destroy(&str));
                    string_append_formated(&str, "Structure def: ");
                    c_import_type_append_to_string(def_sym.data_type, &str, 0, parser, true);
                    logg("%s\n", str.characters);
                }
                */

            }
            else {
                assert(symbol->type == C_Import_Symbol_Type::TYPE, "HEY");
                structure_type = symbol->data_type;
            }
        }
        else {
            if (prototype.type == C_Import_Type_Type::ENUM) {
                prototype.enumeration.members = dynamic_array_create<C_Import_Enum_Member>(4);
            }
            else {
                prototype.structure.members = dynamic_array_create<C_Import_Structure_Member>(4);
            }
            structure_type = c_import_type_system_register_type(&parser->result_package.type_system, prototype);
        }

        if (!has_definition) {
            return optional_make_success(structure_type);
        }
    }

    // Parse type definition
    assert(structure_type->type == C_Import_Type_Type::ENUM || structure_type->type == C_Import_Type_Type::STRUCTURE, "HEY");
    if (structure_type->type == C_Import_Type_Type::STRUCTURE) {
        assert(structure_type->byte_size == 0 && structure_type->alignment == 0, "HEY");
    }

    bool success = true;
    int enum_counter = 0;
    while (true)
    {
        if (header_parser_test_next_token(parser, C_Token_Type::CLOSED_BRACES)) {
            parser->index++;
            break;
        }

        if (structure_type->type == C_Import_Type_Type::ENUM)
        {
            if (header_parser_test_next_token_2(parser, C_Token_Type::IDENTIFIER_NAME, C_Token_Type::OP_ASSIGNMENT))
            {
                C_Import_Enum_Member member;
                member.id = parser->tokens[parser->index].attribute.id;
                parser->index += 2;
                if (header_parser_test_next_token(parser, C_Token_Type::INTEGER_LITERAL))
                {
                    member.value = parser->tokens[parser->index].attribute.integer_value;
                    enum_counter = member.value + 1;
                    parser->index++;
                }
                else if (header_parser_test_next_token_2(parser, C_Token_Type::OP_MINUS, C_Token_Type::INTEGER_LITERAL))
                {
                    member.value = -parser->tokens[parser->index].attribute.integer_value;
                    enum_counter = member.value + 1;
                    parser->index += 2;
                }
                else if (header_parser_test_next_token(parser, C_Token_Type::IDENTIFIER_NAME))
                {
                    String* ref_name = parser->tokens[parser->index].attribute.id;
                    parser->index++;
                    bool found = false;
                    int found_value = 0;
                    for (int i = 0; i < structure_type->enumeration.members.size; i++) {
                        if (structure_type->enumeration.members[i].id == ref_name) {
                            found = true;
                            found_value = structure_type->enumeration.members[i].value;
                            break;
                        }
                    }
                    if (found)
                    {
                        member.value = found_value;
                        enum_counter = member.value + 1;
                    }
                    else {
                        success = false;
                        break;
                    }
                }
                else {
                    success = false;
                    break;
                }
                dynamic_array_push_back(&structure_type->enumeration.members, member);
            }
            else if (header_parser_test_next_token(parser, C_Token_Type::IDENTIFIER_NAME)) {
                C_Import_Enum_Member member;
                member.id = parser->tokens[parser->index].attribute.id;
                member.value = enum_counter;
                enum_counter++;
                dynamic_array_push_back(&structure_type->enumeration.members, member);
                parser->index += 1;
            }
            else {
                success = false;
                break;
            }

            if (header_parser_test_next_token(parser, C_Token_Type::COMMA)) {
                parser->index++;
            }
            else if (header_parser_test_next_token(parser, C_Token_Type::CLOSED_BRACES)) {
                parser->index++;
                break;
            }
            else {
                success = false;
                break;
            }
        }
        else
        {
            Optional<C_Variable_Definition> member_var = header_parser_parse_variable_definition(parser, false);
            if (!member_var.available) {
                success = false;
                break;
            }
            if (member_var.value.instances.size == 0)
            {
                if (member_var.value.base_type->type == C_Import_Type_Type::STRUCTURE && member_var.value.base_type->structure.is_anonymous)
                {
                    // Import members of structure into this structure, e.g. struct A { union {int x; int y;}};
                    structure_type->byte_size = math_round_next_multiple(structure_type->byte_size, member_var.value.base_type->alignment);
                    structure_type->alignment = math_maximum(structure_type->alignment, member_var.value.base_type->alignment);
                    for (int i = 0; i < member_var.value.base_type->structure.members.size; i++)
                    {
                        C_Import_Structure_Member* member = &member_var.value.base_type->structure.members[i];
                        C_Import_Structure_Member new_member;
                        new_member.offset = structure_type->byte_size + member->offset;
                        new_member.type = member->type;
                        new_member.id = member->id;
                        dynamic_array_push_back(&structure_type->structure.members, new_member);
                    }
                    structure_type->byte_size += member_var.value.base_type->byte_size;
                    structure_type->structure.contains_bitfield = member_var.value.base_type->structure.contains_bitfield;
                }
            }
            else
            {
                for (int i = 0; i < member_var.value.instances.size; i++)
                {
                    C_Variable_Instance* instance = &member_var.value.instances[i];
                    C_Import_Structure_Member member;
                    member.id = instance->id;
                    member.type = instance->type;
                    //assert(member.type->byte_size != 0 && member.type->alignment != 0, "Member type must be complete!");
                    if (member.type->byte_size != 0 && member.type->alignment != 0)
                    {
                        structure_type->byte_size = math_round_next_multiple(structure_type->byte_size, member.type->alignment);
                        structure_type->alignment = math_maximum(structure_type->alignment, member.type->alignment);
                        member.offset = structure_type->byte_size;
                        structure_type->byte_size += member.type->byte_size;
                    }
                    else {
                        member.offset = structure_type->byte_size;
                        structure_type->byte_size += 1;
                    }
                    dynamic_array_push_back(&structure_type->structure.members, member);
                }
            }

            if (header_parser_test_next_token_2(parser, C_Token_Type::COLON, C_Token_Type::INTEGER_LITERAL)) {
                structure_type->structure.contains_bitfield = true;
                parser->index += 2;
            }
            if (!header_parser_test_next_token(parser, C_Token_Type::SEMICOLON)) {
                success = false;
                break;
            }
            parser->index++;
        }
    }

    // Cleanup and return
    if (!success) {
        return optional_make_failure<C_Import_Type*>();
    }
    return optional_make_success(structure_type);
}

Optional<C_Import_Type*> header_parser_parse_type(Header_Parser* parser, bool register_structure_tags)
{
    Checkpoint checkpoint = checkpoint_make(parser);

    // Parse Qualifier flags
    C_Type_Qualifiers qualifiers = header_parser_parse_type_qualifiers(parser);
    Optional<C_Import_Type*> result = header_parser_parse_primitive_type(parser, qualifiers);
    if (!result.available)
    {
        result = header_parser_parse_structure(parser, qualifiers, register_structure_tags);
        if (!result.available) {
            if (header_parser_test_next_token(parser, C_Token_Type::IDENTIFIER_NAME))
            {
                String* id = parser->tokens[parser->index].attribute.id;
                parser->index++;
                C_Import_Symbol* symbol = hashtable_find_element(&parser->result_package.symbol_table.symbols, id);
                if (symbol == 0) {
                    // This should not happen
                    //print_tokens_till_newline(parser->tokens, parser->source_code, parser->index);
                    //print_tokens_till_newline_token_style(parser->tokens, parser->source_code, parser->index, parser->code_source);
                    return optional_make_success(parser->result_package.type_system.unknown_type);
                    //return optional_make_failure<C_Import_Type*>();
                     //panic("Check if this happens, otherwise return failure");
                }
                if (symbol->type != C_Import_Symbol_Type::TYPE) {
                    // This should also not happen
                    return optional_make_failure<C_Import_Type*>();
                    //return optional_make_success(parser->result_package.type_system.error_type);
                }
                //assert(symbol->type == C_Import_Symbol_Type::TYPE, "HEY");
                result.available = true;
                result.value = symbol->data_type;
            }
        }
    }

    if (!result.available) {
        checkpoint_rewind(checkpoint);
        return result;
    }

    // Parse type qualifiers again, since this is also allowed short: "short unsigned volatile i; int const _C"
    C_Import_Type* type = result.value;
    qualifiers = header_parser_parse_type_qualifiers(parser);
    if (qualifiers == (C_Type_Qualifiers)0) {
        return result;
    }
    if (((u8)type->qualifiers ^ (u8)qualifiers) == 0) {
        return result;
    }

    C_Import_Type prototype;
    prototype = *type;
    switch (type->type)
    {
    case C_Import_Type_Type::STRUCTURE: {
        prototype.structure.members = dynamic_array_create_copy(type->structure.members.data, type->structure.members.size);
        break;
    }
    case C_Import_Type_Type::ENUM: {
        prototype.enumeration.members = dynamic_array_create_copy(type->enumeration.members.data, type->enumeration.members.size);
        break;
    }
    case C_Import_Type_Type::FUNCTION_SIGNATURE: {
        prototype.function_signature.parameters = dynamic_array_create_copy(type->function_signature.parameters.data, type->function_signature.parameters.size);
        break;
    }
    case C_Import_Type_Type::ARRAY:
    case C_Import_Type_Type::POINTER:
    case C_Import_Type_Type::PRIMITIVE:
    case C_Import_Type_Type::UNKNOWN_TYPE:
        break;
    default: panic("WHAT");
    }
    prototype.qualifiers = (C_Type_Qualifiers)((u8)prototype.qualifiers & (u8)qualifiers);
    C_Import_Type* changed_type = c_import_type_system_register_type(&parser->result_package.type_system, prototype);
    return optional_make_success(changed_type);
}

void header_parser_skip_parenthesis(Header_Parser* parser, C_Token_Type open_type, C_Token_Type close_type)
{
    if (!header_parser_test_next_token(parser, open_type)) {
        panic("What");
        return;
    }
    parser->index++;
    int depth = 1;
    C_Token* last_token = 0;
    while (depth != 0 && parser->index < parser->tokens.size)
    {
        C_Token* token = &parser->tokens[parser->index];
        last_token = token;
        switch (token->type)
        {
        case C_Token_Type::OPEN_BRACES:
        case C_Token_Type::OPEN_PARENTHESIS:
        case C_Token_Type::OPEN_BRACKETS:
            depth++;
            break;
        case C_Token_Type::CLOSED_BRACES:
        case C_Token_Type::CLOSED_PARENTHESIS:
        case C_Token_Type::CLOSED_BRACKETS:
            depth--;
            break;
        }
        parser->index++;
    }
    //assert(last_token->type == close_type, "HEY");
}

C_Import_Type* header_parser_parse_array_suffix(Header_Parser* parser, C_Import_Type* base_type)
{
    bool is_array = false;
    bool has_size = false;
    int size = 0;
    while (parser->index < parser->tokens.size)
    {
        if (header_parser_test_next_token_2(parser, C_Token_Type::OPEN_BRACKETS, C_Token_Type::CLOSED_BRACKETS))
        {
            is_array = true;
            has_size = false;
            parser->index += 2;
            break;
        }
        else if (header_parser_test_next_token_3(parser, C_Token_Type::OPEN_BRACKETS, C_Token_Type::INTEGER_LITERAL, C_Token_Type::CLOSED_BRACKETS))
        {
            is_array = true;
            has_size = true;
            size = parser->tokens[parser->index + 1].attribute.integer_value;
            parser->index += 3;
        }
        else if (header_parser_test_next_token(parser, C_Token_Type::OPEN_BRACKETS))
        {
            is_array = true;
            has_size = true;
            size = 1;
            header_parser_skip_parenthesis(parser, C_Token_Type::OPEN_BRACKETS, C_Token_Type::CLOSED_BRACKETS);
        }
        else {
            break;
        }
    }

    C_Import_Type* refined_type = base_type;
    if (is_array)
    {
        if (has_size)
        {
            C_Import_Type array_type;
            array_type.type = C_Import_Type_Type::ARRAY;
            array_type.array.element_type = refined_type;
            array_type.array.array_size = size;
            array_type.byte_size = math_maximum(math_round_next_multiple(refined_type->byte_size, refined_type->alignment) * size, 1);
            array_type.alignment = refined_type->alignment;
            array_type.qualifiers = (C_Type_Qualifiers)0;
            refined_type = c_import_type_system_register_type(&parser->result_package.type_system, array_type);
        }
        else
        {
            C_Import_Type pointer_type;
            pointer_type.type = C_Import_Type_Type::POINTER;
            pointer_type.byte_size = 8;
            pointer_type.alignment = 8;
            pointer_type.pointer_child_type = refined_type;
            pointer_type.qualifiers = (C_Type_Qualifiers)0;
            refined_type = c_import_type_system_register_type(&parser->result_package.type_system, pointer_type);
        }
    }
    return refined_type;
}


C_Import_Type* header_parser_parse_pointer_suffix(Header_Parser* parser, C_Import_Type* base_type)
{
    C_Import_Type* refined_type = base_type;
    while (true)
    {
        if (header_parser_next_is_identifier(parser, parser->identifier_unaligned)) {
            parser->index++;
        }
        if (header_parser_test_next_token(parser, C_Token_Type::OP_STAR))
        {
            parser->index++;
            C_Type_Qualifiers qualifers = (C_Type_Qualifiers)0;
            if (header_parser_next_is_identifier(parser, parser->identifier_const)) {
                parser->index++;
                qualifers = (C_Type_Qualifiers)((u8)qualifers | (u8)C_Type_Qualifiers::CONST_QUAL);
            }
            C_Import_Type pointer_type;
            pointer_type.type = C_Import_Type_Type::POINTER;
            pointer_type.byte_size = 8;
            pointer_type.alignment = 8;
            pointer_type.pointer_child_type = refined_type;
            pointer_type.qualifiers = qualifers;
            refined_type = c_import_type_system_register_type(&parser->result_package.type_system, pointer_type);
        }
        else {
            break;
        }
    }
    return refined_type;
}

Optional<Dynamic_Array<C_Import_Parameter>> header_parser_parse_parameters(Header_Parser* parser)
{
    Checkpoint checkpoint = checkpoint_make(parser);
    if (!header_parser_test_next_token(parser, C_Token_Type::OPEN_PARENTHESIS)) {
        return optional_make_failure<Dynamic_Array<C_Import_Parameter>>();
    }
    parser->index += 1;

    Dynamic_Array<C_Import_Parameter> parameters = dynamic_array_create<C_Import_Parameter>(2);
    bool success = true;
    while (true)
    {
        if (header_parser_test_next_token(parser, C_Token_Type::CLOSED_PARENTHESIS)) {
            parser->index++;
            break;
        }

        Optional<C_Import_Type*> opt_type = header_parser_parse_type(parser, false);
        if (!opt_type.available) {
            success = false;
            break;
        }
        C_Import_Type* type = opt_type.value;

        type = header_parser_parse_pointer_suffix(parser, type);
        C_Import_Parameter param;
        param.has_name = false;
        if (header_parser_test_next_token(parser, C_Token_Type::IDENTIFIER_NAME)) {
            param.has_name = true;
            param.id = parser->tokens[parser->index].attribute.id;
            parser->index++;
        }
        type = header_parser_parse_array_suffix(parser, type);
        param.type = type;
        if (param.type->type == C_Import_Type_Type::PRIMITIVE && param.type->primitive == C_Import_Primitive::VOID_TYPE) {
            assert(!param.has_name, "HEY");
        }
        else {
            dynamic_array_push_back(&parameters, param);
        }

        if (header_parser_test_next_token(parser, C_Token_Type::CLOSED_PARENTHESIS)) {
            parser->index++;
            break;
        }
        else if (header_parser_test_next_token(parser, C_Token_Type::COMMA)) {
            parser->index++;
            continue;
        }
        else {
            // TODO: Default arguments maybe
            success = false;
            break;
        }
    }

    if (!success) {
        checkpoint_rewind(checkpoint);
        dynamic_array_destroy(&parameters);
        return optional_make_failure<Dynamic_Array<C_Import_Parameter>>();
    }
    return optional_make_success(parameters);
}

Optional<C_Variable_Definition> header_parser_parse_variable_definition(Header_Parser* parser, bool register_structure_tags)
{
    Checkpoint checkpoint = checkpoint_make(parser);
    Optional<C_Import_Type*> opt_base_type = header_parser_parse_type(parser, register_structure_tags);
    if (!opt_base_type.available) {
        checkpoint_rewind(checkpoint);
        return optional_make_failure<C_Variable_Definition>();
    }
    C_Import_Type* base_type = opt_base_type.value;
    C_Import_Type* first_type = header_parser_parse_pointer_suffix(parser, base_type);
    first_type = header_parser_parse_array_suffix(parser, first_type);

    C_Variable_Definition result;
    result.base_type = base_type;
    result.instances = dynamic_array_create<C_Variable_Instance>(2);
    bool success = true;
    SCOPE_EXIT(if (!success) { checkpoint_rewind(checkpoint); dynamic_array_destroy(&result.instances); });
    // Differentiate Function pointer definition from Variable definition
    if (header_parser_test_next_token_2(parser, C_Token_Type::OPEN_PARENTHESIS, C_Token_Type::OP_STAR) ||
        header_parser_test_next_token_3(parser, C_Token_Type::OPEN_PARENTHESIS, C_Token_Type::IDENTIFIER_NAME, C_Token_Type::OP_STAR))
    {
        if (parser->tokens[parser->index + 1].type == C_Token_Type::IDENTIFIER_NAME) {
            parser->index += 3;
        }
        else {
            parser->index += 2;
        }
        if (!header_parser_test_next_token_2(parser, C_Token_Type::IDENTIFIER_NAME, C_Token_Type::CLOSED_PARENTHESIS)) {
            success = false;
            return optional_make_failure<C_Variable_Definition>();
        }
        String* id = parser->tokens[parser->index].attribute.id;
        parser->index += 2;

        Optional<Dynamic_Array<C_Import_Parameter>> params = header_parser_parse_parameters(parser);
        if (!params.available) {
            success = false;
            return optional_make_failure<C_Variable_Definition>();
        }

        C_Import_Type function_prototype;
        function_prototype.type = C_Import_Type_Type::FUNCTION_SIGNATURE;
        function_prototype.byte_size = 1;
        function_prototype.alignment = 1;
        function_prototype.qualifiers = (C_Type_Qualifiers)0;
        function_prototype.function_signature.return_type = first_type;
        function_prototype.function_signature.parameters = params.value;
        C_Import_Type* function_type = c_import_type_system_register_type(&parser->result_package.type_system, function_prototype);

        C_Import_Type ptr_prototype;
        ptr_prototype.type = C_Import_Type_Type::POINTER;
        ptr_prototype.byte_size = 8;
        ptr_prototype.alignment = 8;
        ptr_prototype.qualifiers = (C_Type_Qualifiers)0;
        ptr_prototype.pointer_child_type = function_type;
        C_Import_Type* ptr_type = c_import_type_system_register_type(&parser->result_package.type_system, ptr_prototype);

        C_Variable_Instance instance;
        instance.id = id;
        instance.type = ptr_type;
        dynamic_array_push_back(&result.instances, instance);
        return optional_make_success(result);
    }
    else
    {
        bool first = true;
        while (true)
        {
            C_Variable_Instance instance;
            if (first) {
                instance.type = first_type;
                first = false;
            }
            else {
                instance.type = header_parser_parse_pointer_suffix(parser, base_type);
            }

            // Parse instance base_name
            if (header_parser_test_next_token(parser, C_Token_Type::IDENTIFIER_NAME))
            {
                instance.id = parser->tokens[parser->index].attribute.id;
                parser->index++;
            }
            else {
                break;
            }

            instance.type = header_parser_parse_array_suffix(parser, instance.type);
            dynamic_array_push_back(&result.instances, instance);
            // Continue if necessary, TODO: Skip default initialization (struct X { int a = 5, b = 7;}
            if (!header_parser_test_next_token(parser, C_Token_Type::COMMA)) {
                break;
            }
            parser->index++;
        }
    }

    if (success)
    {
        return optional_make_success(result);
    }
    return optional_make_failure<C_Variable_Definition>();
}

void string_indent(String* string, int lvl) {
    for (int i = 0; i < lvl; i++) {
        string_append_formated(string, "    ");
    }
}

void c_type_qualifier_append_to_string(String* string, C_Type_Qualifiers qualifiers)
{
    if (((u8)qualifiers & (u8)C_Type_Qualifiers::ATOMIC) != 0) {
        string_append_formated(string, "atomic ");
    }
    if (((u8)qualifiers & (u8)C_Type_Qualifiers::CONST_QUAL) != 0) {
        string_append_formated(string, "const ");
    }
    if (((u8)qualifiers & (u8)C_Type_Qualifiers::RESTRICT) != 0) {
        string_append_formated(string, "restrict ");
    }
    if (((u8)qualifiers & (u8)C_Type_Qualifiers::SIGNED) != 0) {
        string_append_formated(string, "signed ");
    }
    if (((u8)qualifiers & (u8)C_Type_Qualifiers::UNSIGNED) != 0) {
        string_append_formated(string, "unsigned ");
    }
    if (((u8)qualifiers & (u8)C_Type_Qualifiers::VOLATILE) != 0) {
        string_append_formated(string, "volatile ");
    }
}

void c_import_type_append_to_string(C_Import_Type* type, String* string, int indentation, bool print_array_members)
{
    string_indent(string, indentation);
    if (type->type != C_Import_Type_Type::POINTER && type->type != C_Import_Type_Type::ARRAY) {
        c_type_qualifier_append_to_string(string, type->qualifiers);
    }
    switch (type->type)
    {
    case C_Import_Type_Type::PRIMITIVE: {
        switch (type->primitive)
        {
        case C_Import_Primitive::VOID_TYPE:
            string_append_formated(string, "VOID_TYPE");
            break;
        case C_Import_Primitive::BOOL:
            string_append_formated(string, "BOOL");
            break;
        case C_Import_Primitive::CHAR:
            string_append_formated(string, "CHAR");
            break;
        case C_Import_Primitive::SHORT:
            string_append_formated(string, "SHORT");
            break;
        case C_Import_Primitive::INT:
            string_append_formated(string, "INT");
            break;
        case C_Import_Primitive::LONG:
            string_append_formated(string, "LONG");
            break;
        case C_Import_Primitive::LONG_LONG:
            string_append_formated(string, "LONG_LONG");
            break;
        case C_Import_Primitive::FLOAT:
            string_append_formated(string, "FLOAT");
            break;
        case C_Import_Primitive::DOUBLE:
            string_append_formated(string, "DOUBLE");
            break;
        case C_Import_Primitive::LONG_DOUBLE:
            string_append_formated(string, "LONG_DOUBLE");
            break;
        default: panic("HEY");
        }
        break;
    }
    case C_Import_Type_Type::UNKNOWN_TYPE: {
        string_append_formated(string, "ERROR_TYPE");
        break;
    }
    case C_Import_Type_Type::ARRAY: {
        c_import_type_append_to_string(type->array.element_type, string, indentation, print_array_members);
        string_append_formated(string, "[%d]", type->array.array_size);
        break;
    }
    case C_Import_Type_Type::POINTER: {
        c_import_type_append_to_string(type->pointer_child_type, string, indentation, print_array_members);
        c_type_qualifier_append_to_string(string, type->qualifiers);
        string_append_formated(string, "*");
        break;
    }
    case C_Import_Type_Type::STRUCTURE:
    {
        if (type->structure.is_union) {
            string_append_formated(string, "UNION");
        }
        else {
            string_append_formated(string, "STRUCT");
        }
        if (!type->structure.is_anonymous) {
            string_append_formated(string, " %s", type->structure.id->characters);
        }
        if (print_array_members)
        {
            string_append_formated(string, "\n");
            string_indent(string, indentation);
            string_append_formated(string, "{\n");
            string_indent(string, indentation + 1);
            for (int i = 0; i < type->structure.members.size; i++)
            {
                C_Import_Structure_Member* member = &type->structure.members[i];
                c_import_type_append_to_string(member->type, string, indentation + 1, false);
                string_append_formated(string, "%s\n",  member->id->characters);
            }
            string_indent(string, indentation);
            string_append_formated(string, "}");
        }
        break;
    }
    case C_Import_Type_Type::ENUM: {
        string_append_formated(string, "ENUM ");
        if (!type->enumeration.is_anonymous) {
            string_append_formated(string, " %s", type->enumeration.id->characters);
        }
        string_append_formated(string, " {");
        for (int i = 0; i < type->enumeration.members.size; i++) {
            C_Import_Enum_Member* member = &type->enumeration.members[i];
            string_append_formated(string, "%s = %d, ", member->id->characters, member->value);
        }
        string_append_formated(string, "}");
        break;
    }
    case C_Import_Type_Type::FUNCTION_SIGNATURE: {
        string_append_formated(string, "Function ");
        c_import_type_append_to_string(type->function_signature.return_type, string, 0, false);
        string_append_formated(string, "(");
        for (int i = 0; i < type->function_signature.parameters.size; i++) {
            C_Import_Parameter* parameter = &type->function_signature.parameters[i];
            c_import_type_append_to_string(parameter->type, string, 0, false);
            if (parameter->has_name) {
                string_append_formated(string, " %s", parameter->id->characters);
            }
            if (i != type->function_signature.parameters.size - 1) {
                string_append(string, ", ");
            }
        }
        string_append_formated(string, ")");
        break;
    }
    default: panic("HEY");
    }
    string_append_formated(string, " ");
}

enum class C_Definition_Modifiers
{
    STATIC = 1,
    INLINE = 2,
    EXTERN = 4,
    TYPEDEF = 8,
};

C_Definition_Modifiers header_parser_parse_definition_modifiers(Header_Parser* parser)
{
    C_Definition_Modifiers modifiers = (C_Definition_Modifiers)0;
    while (true)
    {
        if (header_parser_next_is_identifier(parser, parser->identifier_typedef)) {
            parser->index++;
            modifiers = (C_Definition_Modifiers)((u8)modifiers | (u8)C_Definition_Modifiers::TYPEDEF);
            continue;
        }
        else if (header_parser_next_is_identifier(parser, parser->identifier_inline) ||
            header_parser_next_is_identifier(parser, parser->identifier_inline_alt) ||
            header_parser_next_is_identifier(parser, parser->identifier_force_inline)) {
            parser->index++;
            modifiers = (C_Definition_Modifiers)((u8)modifiers | (u8)C_Definition_Modifiers::INLINE);
            continue;
        }
        else if (header_parser_next_is_identifier(parser, parser->identifier_static)) {
            parser->index++;
            modifiers = (C_Definition_Modifiers)((u8)modifiers | (u8)C_Definition_Modifiers::STATIC);
            continue;
        }
        else if (header_parser_test_next_token(parser, C_Token_Type::EXTERN)) {
            parser->index++;
            modifiers = (C_Definition_Modifiers)((u8)modifiers | (u8)C_Definition_Modifiers::EXTERN);
            continue;
        }
        break;
    }
    return modifiers;
}

bool header_parser_parse_known_structure(Header_Parser* parser)
{
    /*
        I can possibly encounter:
            - enums                         X
            - Struct forward declarations   X
            - Struct definitions            X
            - Union forward declarations    X
            - Union definitions             X
            - Function prototypes           X
            - Function pointers             X
            - Inline function definitions   Supported, but should not be available in bytecode

        What should I do when I find a structure prototype/a struct including itself?
        On structure prototype, I probably only want to create the symbol...
    */

    /*
    logg("\nParse known structure: #%d\n", parser->index);
    print_tokens_till_newline(parser->tokens, parser->source_code, parser->index);
    logg("\n");
    */

    C_Definition_Modifiers modifiers = header_parser_parse_definition_modifiers(parser);
    bool is_typedef = ((u8)modifiers & (u8)C_Definition_Modifiers::TYPEDEF) != 0;
    bool is_extern = ((u8)modifiers & (u8)C_Definition_Modifiers::EXTERN) != 0;
    bool is_inline = ((u8)modifiers & (u8)C_Definition_Modifiers::INLINE) != 0;
    if (is_inline) {
        //logg("Parsed structure: False");
        return false;
    }

    Optional<C_Variable_Definition> var_def_opt = header_parser_parse_variable_definition(parser, true);
    if (!var_def_opt.available) {
        //logg("Parsed structure: False\n");
        return false;
    }
    C_Variable_Definition var_def = var_def_opt.value;
    SCOPE_EXIT(dynamic_array_destroy(&var_def.instances););

    if (!is_extern && var_def.instances.size == 1 &&
        !(var_def.instances[0].type->type == C_Import_Type_Type::POINTER &&
            var_def.instances[0].type->pointer_child_type->type == C_Import_Type_Type::FUNCTION_SIGNATURE
            ) && header_parser_test_next_token(parser, C_Token_Type::OPEN_PARENTHESIS))
    {
        Optional<Dynamic_Array<C_Import_Parameter>> params = header_parser_parse_parameters(parser);
        if (params.available)
        {
            if (header_parser_test_next_token(parser, C_Token_Type::OPEN_BRACES)) {
                dynamic_array_destroy(&params.value);
                //logg("Parsed structure: False\n");
                return false;
            }
            else if (header_parser_test_next_token(parser, C_Token_Type::SEMICOLON)) {
                parser->index++;
            }
            else {
                dynamic_array_destroy(&params.value);
                //logg("Parsed structure: False\n");
                return false;
            }
            C_Import_Type function_sig;
            function_sig.byte_size = 1;
            function_sig.alignment = 1;
            function_sig.type = C_Import_Type_Type::FUNCTION_SIGNATURE;
            function_sig.qualifiers = (C_Type_Qualifiers)0;
            function_sig.function_signature.parameters = params.value;
            function_sig.function_signature.return_type = var_def.instances[0].type;
            C_Import_Type* registered_function = c_import_type_system_register_type(&parser->result_package.type_system, function_sig);

            C_Import_Symbol symbol;
            if (is_typedef) {
                symbol.type = C_Import_Symbol_Type::TYPE;
            }
            else {
                symbol.type = C_Import_Symbol_Type::FUNCTION;
            }
            symbol.data_type = registered_function;
            c_import_symbol_table_define_symbol(&parser->result_package.symbol_table, symbol, var_def.instances[0].id);

            {
                /*
                String str = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&str));
                string_append_formated(&str, "%s: ", is_typedef ? "typedef " : "function ");
                string_append_formated(&str, "%s = ", identifier_pool_index_to_string(parser->code_source, var_def.instances[0].id).characters);
                c_import_type_append_to_string(registered_function, &str, 0, parser, true);
                logg("%s\n", str.characters);
                */
            }

            //logg("Parsed structure: TRUE\n");
            return true;
        }
    }
    else if (header_parser_test_next_token(parser, C_Token_Type::SEMICOLON))
    {
        parser->index++;
        for (int i = 0; i < var_def.instances.size; i++)
        {
            C_Variable_Instance* instance = &var_def.instances[i];
            C_Import_Symbol symbol;
            if (is_typedef) {
                symbol.type = C_Import_Symbol_Type::TYPE;
            }
            else {
                symbol.type = C_Import_Symbol_Type::GLOBAL_VARIABLE;
            }
            symbol.data_type = instance->type;
            c_import_symbol_table_define_symbol(&parser->result_package.symbol_table, symbol, instance->id);

            {
                /*
                String str = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&str));
                string_append_formated(&str, "%s: ", is_typedef ? "typedef " : "global: ");
                string_append_formated(&str, "%s = ", identifier_pool_index_to_string(parser->code_source, instance->id).characters);
                c_import_type_append_to_string(symbol.data_type, &str, 0, parser, true);
                logg("%s\n", str.characters);
                */
            }


        }

        //logg("Parsed structure: TRUE\n");
        return true;
    }

    //logg("Parsed structure: FALSE\n");
    return false;
}

void header_parser_parse(Header_Parser* parser)
{
    /*
    printf("Header_parser_parse: ");
    print_tokens_till_newline(parser, parser->index);
    printf("\n");
    */

    String* identifier_extern_c = identifier_pool_add(parser->lexer->identifier_pool, string_create_static("C"));
    String* identifier_extern_cpp = identifier_pool_add(parser->lexer->identifier_pool, string_create_static("C++"));
    while (parser->index + 2 < parser->tokens.size)
    {
        C_Token* t1 = &parser->tokens[parser->index];
        C_Token* t2 = &parser->tokens[parser->index + 1];
        if (t1->type == C_Token_Type::EXTERN && t2->type == C_Token_Type::STRING_LITERAL && t2->attribute.id == identifier_extern_cpp)
        {
            /*
            logg("Henlo %d \n", parser->index);
            print_tokens_till_newline(parser->tokens, parser->source_code, parser->index);
            logg("\n");
            if (parser->index == 5018) {
                logg("Shit time");
            }
            */
        }
        else {
            int rewind_index = parser->index;
            if (header_parser_parse_known_structure(parser)) {
                continue;
            }
            parser->index = rewind_index;
        }

        int current_line = parser->tokens[parser->index].position.start.line_index;
        int depth = 0;
        bool depth_was_nonzero = false;
        while (parser->index + 2 < parser->tokens.size)
        {
            C_Token* t1 = &parser->tokens[parser->index];
            C_Token* t2 = &parser->tokens[parser->index + 1];
            C_Token* t3 = &parser->tokens[parser->index + 2];

            switch (t1->type)
            {
            case C_Token_Type::OPEN_BRACES:
                //case C_Token_Type::OPEN_PARENTHESIS:
                //case C_Token_Type::OPEN_BRACKETS:
                depth++;
                depth_was_nonzero = true;
                break;
            case C_Token_Type::CLOSED_BRACES:
                //case C_Token_Type::CLOSED_PARENTHESIS:
                //case C_Token_Type::CLOSED_BRACKETS:
                depth--;
                break;
            }
            if (depth < 0) {
                parser->index++;
                return;
            }
            if (depth == 0)
            {
                if (depth_was_nonzero) {
                    parser->index++;
                    break;
                }
                else {
                    if (t1->type == C_Token_Type::SEMICOLON) {
                        parser->index++;
                        break;
                    }
                    //if (t1->position.start.line_index != current_line) break;
                }
            }
            if (t1->type == C_Token_Type::EXTERN && t2->type == C_Token_Type::STRING_LITERAL && t2->attribute.id == identifier_extern_c)
            {
                if (t3->type == C_Token_Type::OPEN_BRACES) {
                    parser->index += 3;
                    header_parser_parse(parser);
                    /*logg("Stepping out of parse thing: \n\t\"");
                    print_tokens_till_newline(parser->tokens, parser->source_code, parser->index);
                    logg("\"\n");
                    */
                    break;
                }
                else {
                    parser->index += 2;
                    int rewind_index = parser->index;
                    header_parser_parse_known_structure(parser);
                    parser->index = rewind_index;
                }
            }

            parser->index++;
        }
    }
}

struct Print_Destination
{
    bool is_sizeof;
    bool is_alignof;
    bool is_member;
    C_Import_Symbol* symbol;
    C_Import_Structure_Member* member;
};

Print_Destination print_destination_make(bool is_sizeof, bool is_alignof, bool is_member, C_Import_Symbol* symbol, C_Import_Structure_Member* member)
{
    Print_Destination dest;
    dest.is_alignof = is_alignof;
    dest.is_sizeof = is_sizeof;
    dest.is_member = is_member;
    dest.symbol = symbol;
    dest.member = member;
    return dest;
}

Optional<C_Import_Package> c_importer_parse_header(
    const char* file_name, Identifier_Pool* pool, Dynamic_Array<String> include_dirs, Dynamic_Array<String> defines
)
{
    logg("Parsing header file: %s\n---------------------\n", file_name);
    // Run preprocessor on file_name
    {
        String command = string_create("cl /P /EP");
        string_append_formated(&command, " backend/c_importer/empty.cpp /Fibackend/c_importer/preprocessed.txt");
        SCOPE_EXIT(string_destroy(&command));
        for (int i = 0; i < include_dirs.size; i++) {
            String str = include_dirs[i];
            if (str.size > 0 && str.characters[0] == '\"') {
                string_append_formated(&command, " /I%s", str.characters);
            }
            else {
                string_append_formated(&command, " /I\"%s\"", str.characters);
            }
        }
        for (int i = 0; i < defines.size; i++) {
            String str = defines[i];
            string_append_formated(&command, " /D%s", str.characters);
        }
        string_append(&command, " /FI");
        string_append_formated(&command, file_name);
        logg("Compiling with %s\n", command.characters);

        Optional<Process_Result> result = process_start(command);
        SCOPE_EXIT(process_result_destroy(&result));
        if (!result.available) {
            return optional_make_failure<C_Import_Package>();
        }
        if (result.value.exit_code != 0) {
            logg("Error: %s\n", result.value.output.characters);
            return optional_make_failure<C_Import_Package>();
        }
    }

    // Load preprocessed file
    Optional<String> text_file_opt = file_io_load_text_file("backend/c_importer/preprocessed.txt");
    SCOPE_EXIT(file_io_unload_text_file(&text_file_opt));
    if (!text_file_opt.available) {
        return optional_make_failure<C_Import_Package>();
    }
    String source_code = text_file_opt.value;

    // Run lexer over file
    C_Lexer lexer = c_lexer_create();
    SCOPE_EXIT(c_lexer_destroy(&lexer));
    c_lexer_lex(&lexer, &source_code, pool);

    //logg("Lexing finished, Stats:\nIdentifier Count: #%d\nToken Count: #%d\n Whitespace-Token Count: %d\n",
        //code_source.identifiers.size, code_source.tokens.size, code_source.tokens_with_decoration.size - code_source.tokens.size);

    C_Import_Package package;
    {
        Header_Parser header_parser = header_parser_create(&lexer, source_code);
        SCOPE_EXIT(header_parser_destroy(&header_parser, false));
        header_parser_parse(&header_parser);
        package = header_parser.result_package;
    }


    // Get alignment and size of each file
    {
        String found_symbols = string_create_empty(4096);
        String output_program = string_create_empty(4096);
        SCOPE_EXIT(string_destroy(&found_symbols));
        SCOPE_EXIT(string_destroy(&output_program));

        string_append_formated(&output_program, "#include <cstdio>\n#include <%s>\n#define myoffsetof(s,m) ((size_t)&(((s*)0)->m))\n\nint main(int argc, char** argv) {\n", file_name);
        auto iter = hashtable_iterator_create(&package.symbol_table.symbols);
        int count = 0;
        double last_time = timer_current_time_in_seconds();
        Dynamic_Array<Print_Destination> destinations = dynamic_array_create<Print_Destination>(256);
        SCOPE_EXIT(dynamic_array_destroy(&destinations));
        while (hashtable_iterator_has_next(&iter))
        {
            count++;
            if (count % 2000 == 0) {
                double now = timer_current_time_in_seconds();
                logg("%d/%d %3.2fs\n", count, package.symbol_table.symbols.element_count, (float)(now - last_time));
                last_time = now;
            }
            C_Import_Symbol* symbol = iter.value;
            String* symbol_name = *iter.key;
            if (symbol->type == C_Import_Symbol_Type::TYPE) {
                if (symbol->data_type->type == C_Import_Type_Type::ENUM || symbol->data_type->type == C_Import_Type_Type::STRUCTURE) {
                    if (symbol->data_type->byte_size != 0 || symbol->data_type->alignment != 0) {
                        string_append_formated(
                            &output_program,
                            "    printf(\"%%zd\\n%%zd\\n\", sizeof(%s), alignof(%s));\n",
                            symbol_name->characters, symbol_name->characters
                        );
                        dynamic_array_push_back(&destinations, print_destination_make(true, false, false, symbol, 0));
                        dynamic_array_push_back(&destinations, print_destination_make(false, true, false, symbol, 0));
                    }
                    if (symbol->data_type->type == C_Import_Type_Type::STRUCTURE && !symbol->data_type->structure.contains_bitfield)
                    {
                        for (int i = 0; i < symbol->data_type->structure.members.size; i++)
                        {
                            C_Import_Structure_Member* member = &symbol->data_type->structure.members[i];
                            string_append_formated(
                                &output_program,
                                "    printf(\"%%zd\\n\", myoffsetof(%s, %s));\n",
                                symbol_name->characters,
                                member->id->characters
                            );
                            dynamic_array_push_back(&destinations, print_destination_make(false, false, true, 0, member));
                        }
                    }
                }
            }
            else
            {
                if (symbol->type == C_Import_Symbol_Type::FUNCTION) {
                    string_append_formated(&found_symbols, "Function: ");
                }
                else {
                    string_append_formated(&found_symbols, "Global: ");
                }
                string_append_formated(&found_symbols, " %s\n", (*iter.key)->characters);
            }
            hashtable_iterator_next(&iter);
        }
        string_append_formated(&output_program, "\n    return 0;\n}\n");

        file_io_write_file("backend/c_importer/sizeof_program.cpp", array_create_static((byte*)output_program.characters, output_program.size));
        file_io_write_file("backend/c_importer/found_symbols.txt", array_create_static((byte*)found_symbols.characters, found_symbols.size));

        
        String command = string_create("cl");
        string_append(&command, " backend/c_importer/sizeof_program.cpp");
        SCOPE_EXIT(string_destroy(&command));
        for (int i = 0; i < include_dirs.size; i++) {
            String str = include_dirs[i];
            if (str.size > 0 && str.characters[0] == '\"') {
                string_append_formated(&command, " /I %s", str.characters);
            }
            else {
                string_append_formated(&command, " /I \"%s\"", str.characters);
            }
        }
        for (int i = 0; i < defines.size; i++) {
            String str = defines[i];
            string_append_formated(&command, " /D%s", str.characters);
        }
        string_append(&command, " /link /OUT:backend/c_importer/sizeof_program.exe");
        logg("Size-of Programm Command: %s\n", command.characters);
        Optional<Process_Result> sizeof_comp = process_start(command);
        SCOPE_EXIT(process_result_destroy(&sizeof_comp));

        if (!sizeof_comp.available) {
            c_import_package_destroy(&package);
            return optional_make_failure<C_Import_Package>();
        }
        if (sizeof_comp.value.exit_code != 0) {
            logg("Sizeof program compilation failed\n");
            logg("C-Compiler output:\n%s\n", sizeof_comp.value.output.characters);
            c_import_package_destroy(&package);
            return optional_make_failure<C_Import_Package>();
        }
        Optional<Process_Result> sizeof_res = process_start(string_create_static("backend/c_importer/sizeof_program.exe"));
        SCOPE_EXIT(process_result_destroy(&sizeof_res));
        if (!sizeof_res.available || sizeof_res.value.exit_code != 0) {
            c_import_package_destroy(&package);
            return optional_make_failure<C_Import_Package>();
        }
        if (sizeof_res.value.exit_code != 0) {
            logg("Sizeof program execution failed, output:\n%s\n", sizeof_res.value.output.characters);
            c_import_package_destroy(&package);
            return optional_make_failure<C_Import_Package>();
        }

        Dynamic_Array<int> sizes = dynamic_array_create<int>(destinations.size);
        SCOPE_EXIT(dynamic_array_destroy(&sizes));
        int index = 0;
        while (index < sizeof_res.value.output.size)
        {
            char* parse = &sizeof_res.value.output.characters[index];
            char* end_ptr;
            int size = strtol(parse, &end_ptr, 10);
            if (end_ptr == parse) {
                break;
            }
            dynamic_array_push_back(&sizes, size);
            index = end_ptr - sizeof_res.value.output.characters;
        }

        if (sizes.size != destinations.size) {
            panic("Should not happen!");
        }
        for (int i = 0; i < destinations.size; i++)
        {
            Print_Destination dst = destinations.data[i];
            if (dst.is_alignof) {
                dst.symbol->data_type->alignment = sizes[i];
            }
            else if (dst.is_member) {
                dst.member->offset = sizes[i];
            }
            else if (dst.is_sizeof) {
                dst.symbol->data_type->byte_size = sizes[i];
            }
            else {
                panic("What");
            }
        }

    }

    return optional_make_success(package);
}

Optional<C_Import_Package> c_importer_import_header(
    C_Importer* importer, String header_name, Identifier_Pool* identifier_pool, 
    Dynamic_Array<String> include_directories, Dynamic_Array<String> defines
)
{
    importer->identifier_pool = identifier_pool;

    // Look in cache for file (Not supported anymore because the include-gui doesn't need it)
    // auto cache_elem = hashtable_find_element(&importer->cache, header_name);
    // if (cache_elem != 0) {
    //     return optional_make_success(*cache_elem);
    // }

    // Parse header if not in cache
    Optional<C_Import_Package> parsed_package = c_importer_parse_header(header_name.characters, importer->identifier_pool, include_directories, defines);
    if (parsed_package.available)
    {
        String cache_file_name = string_create(header_name.characters);
        hashtable_insert_element(&importer->cache, cache_file_name, parsed_package.value);
        return optional_make_success(parsed_package.value);
    }
    else {
        return optional_make_failure<C_Import_Package>();
    }
}

C_Importer c_importer_create()
{
    C_Importer importer;
    importer.cache = hashtable_create_empty<String, C_Import_Package>(64, hash_string, string_equals);
    return importer;

    /*
    if (!file_io_check_if_file_exists(cache_file)) {
        return importer;
    }

    Optional<BinaryParser> parser_opt = binary_parser_create_from_file(cache_file);
    if (!parser_opt.available) {
        logg("Couldnt load file!\n");
        return importer;
    }
    BinaryParser parser = parser_opt.value;
    SCOPE_EXIT(binary_parser_destroy(&parser));
    */

}

void c_package_cache_destroy(String* key, C_Import_Package* package)
{
    c_import_package_destroy(package);
}

void c_importer_destroy(C_Importer* importer)
{
    // TODO: Write cache file
    /*
    BinaryParser parser = binary_parser_create_empty(2048);
    SCOPE_EXIT(binary_parser_destroy(&parser));

    Hashset<void*> pointer_set = hashset_create_pointer_empty<void*>(256);
    hashset_insert_element(&pointer_set, importer->cache_file.)
    */

    hashtable_for_each(&importer->cache, c_package_cache_destroy);
    hashtable_destroy(&importer->cache);
}

