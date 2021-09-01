#include "programs/upp_lang/upp_lang.hpp"
#include "programs/proc_city/proc_city.hpp"

#include "utility/file_io.hpp"
#include "programs/upp_lang/compiler.hpp"
#include <iostream>
#include <cstdio>
#include <algorithm>
#include "datastructures/string.hpp"

enum class C_Import_Primitive
{
    CHAR,
    SHORT,
    INT,
    LONG,
    LONG_LONG,
    FLOAT,
    DOUBLE,
    LONG_DOUBLE,
    VOID,
    BOOL,
};

enum class C_Type_Qualifiers
{
    UNSIGNED = 1,
    SIGNED = 2,
    VOLATILE = 4,
    RESTRICT = 8,
    CONST = 16,
    ATOMIC = 32
};

struct C_Import_Type;
struct C_Variable_Instance
{
    int name_id;
    C_Import_Type* type;
};

struct C_Variable_Definition
{
    C_Import_Type* base_type;
    Dynamic_Array<C_Variable_Instance> instances;
};

struct C_Import_Type_Array
{
    C_Import_Type* element_type;
    int array_size;
};

struct C_Import_Structure_Member
{
    int name_id;
    int offset;
    C_Import_Type* type;
};

struct C_Import_Type_Structure
{
    bool is_union;
    bool is_anonymous;
    int name_id;
    Dynamic_Array<C_Import_Structure_Member> members;
};

struct C_Import_Enum_Member
{
    int name_id;
    int value;
};

struct C_Import_Type_Enum
{
    bool is_anonymous;
    int name_id;
    Dynamic_Array<C_Import_Enum_Member> members;
};

struct C_Import_Parameter
{
    C_Import_Type* type;
    bool has_name;
    int name_id;
};

struct C_Import_Type_Function_Signature
{
    Dynamic_Array<C_Import_Parameter> parameters;
    C_Import_Type* return_type;
};

enum class C_Import_Type_Type
{
    PRIMITIVE,
    POINTER,
    ARRAY,
    STRUCTURE,
    ENUM,
    FUNCTION_SIGNATURE,
    ERROR,
};

struct C_Import_Type
{
    C_Import_Type_Type type;
    int byte_size;
    int alignment;
    C_Type_Qualifiers qualifiers;
    union
    {
        C_Import_Primitive primitive;
        C_Import_Type* pointer_child_type;
        C_Import_Type_Array array;
        C_Import_Type_Structure structure;
        C_Import_Type_Enum enumeration;
        C_Import_Type_Function_Signature function_signature;
    };
};

struct C_Import_Type_System
{
    Dynamic_Array<C_Import_Type*> registered_types;
    C_Import_Type* error_type;
};

enum class C_Import_Symbol_Type
{
    GLOBAL_VARIABLE,
    FUNCTION,
    TYPE,
};

struct C_Import_Symbol
{
    C_Import_Symbol_Type type;
    C_Import_Type* data_type;
};

struct C_Import_Symbol_Table
{
    Hashtable<int, C_Import_Symbol> symbols;
};

struct C_Import_Package
{
    C_Import_Symbol_Table symbol_table;
    C_Import_Type_System type_system;
};

C_Import_Package c_import_package_create()
{
    C_Import_Package result;
    result.symbol_table.symbols = hashtable_create_empty<int, C_Import_Symbol>(64, hash_i32, equals_i32);
    result.type_system.registered_types = dynamic_array_create_empty<C_Import_Type*>(64);

    C_Import_Type* error_prototype = new C_Import_Type;
    error_prototype->type = C_Import_Type_Type::ERROR;
    error_prototype->byte_size = 1;
    error_prototype->alignment = 1;
    error_prototype->qualifiers = (C_Type_Qualifiers)0;
    dynamic_array_push_back(&result.type_system.registered_types, error_prototype);
    result.type_system.error_type = error_prototype;

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
        case C_Import_Type_Type::ERROR:
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
        case C_Import_Type_Type::ERROR:
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
    Lexer* lexer;
    Dynamic_Array<Token> tokens;
    int index;
    String source_code;

    int identifier_typedef;
    int identifier_unaligned;
    int identifier_inline;
    int identifier_inline_alt;
    int identifier_force_inline;
    int identifier_static;
    int identifier_enum;
    int identifier_union;
    int identifier_wchar_t;
    int identifier_wchar_t_alt;
    int identifier_int8;
    int identifier_int16;
    int identifier_int32;
    int identifier_int64;
    int identifier_bool;
    int identifier_char;
    int identifier_short;
    int identifier_int;
    int identifier_long;
    int identifier_float;
    int identifier_double;
    int identifier_void;
    int identifier_signed;
    int identifier_unsigned;
    int identifier_const;
    int identifier_volatile;
    int identifier_restrict;
    int identifier_atomic;
    int identifier_call_conv_cdecl;
    int identifier_call_conv_clrcall;
    int identifier_call_conv_stdcall;
    int identifier_call_conv_fastcall;
    int identifier_call_conv_thiscall;
    int identifier_call_conv_vectorcall;
};

Header_Parser header_parser_create(Lexer* lexer, String source_code)
{
    Header_Parser result;
    result.result_package = c_import_package_create();
    result.lexer = lexer;
    result.index = 0;
    result.source_code = source_code;
    result.identifier_typedef = lexer_add_or_find_identifier_by_string(lexer, string_create_static("typedef"));
    result.identifier_unaligned = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__unaligned"));
    result.identifier_inline = lexer_add_or_find_identifier_by_string(lexer, string_create_static("inline"));
    result.identifier_inline_alt = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__inline"));
    result.identifier_force_inline = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__forceinline"));
    result.identifier_static = lexer_add_or_find_identifier_by_string(lexer, string_create_static("static"));
    result.identifier_enum = lexer_add_or_find_identifier_by_string(lexer, string_create_static("enum"));
    result.identifier_union = lexer_add_or_find_identifier_by_string(lexer, string_create_static("union"));
    result.identifier_char = lexer_add_or_find_identifier_by_string(lexer, string_create_static("char"));
    result.identifier_short = lexer_add_or_find_identifier_by_string(lexer, string_create_static("short"));
    result.identifier_int = lexer_add_or_find_identifier_by_string(lexer, string_create_static("int"));
    result.identifier_wchar_t = lexer_add_or_find_identifier_by_string(lexer, string_create_static("wchar_t"));
    result.identifier_wchar_t_alt = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__wchar_t"));
    result.identifier_int8 = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__int8"));
    result.identifier_int16 = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__int16"));
    result.identifier_int32 = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__int32"));
    result.identifier_int64 = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__int64"));
    result.identifier_long = lexer_add_or_find_identifier_by_string(lexer, string_create_static("long"));
    result.identifier_float = lexer_add_or_find_identifier_by_string(lexer, string_create_static("float"));
    result.identifier_double = lexer_add_or_find_identifier_by_string(lexer, string_create_static("double"));
    result.identifier_signed = lexer_add_or_find_identifier_by_string(lexer, string_create_static("signed"));
    result.identifier_bool = lexer_add_or_find_identifier_by_string(lexer, string_create_static("bool"));
    result.identifier_void = lexer_add_or_find_identifier_by_string(lexer, string_create_static("void"));
    result.identifier_unsigned = lexer_add_or_find_identifier_by_string(lexer, string_create_static("unsigned"));
    result.identifier_const = lexer_add_or_find_identifier_by_string(lexer, string_create_static("const"));
    result.identifier_volatile = lexer_add_or_find_identifier_by_string(lexer, string_create_static("volatile"));
    result.identifier_restrict = lexer_add_or_find_identifier_by_string(lexer, string_create_static("restrict"));
    result.identifier_atomic = lexer_add_or_find_identifier_by_string(lexer, string_create_static("atomic"));

    result.identifier_call_conv_cdecl = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__cdecl"));
    result.identifier_call_conv_clrcall = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__clrcall"));
    result.identifier_call_conv_fastcall = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__stdcall"));
    result.identifier_call_conv_stdcall = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__fastcall"));
    result.identifier_call_conv_thiscall = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__thiscall"));
    result.identifier_call_conv_vectorcall = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__vectorcall"));

    // Create new tokens array, where lines starting with # are removed, and __pragma and __declspec compiler stuff is removed
    result.tokens = dynamic_array_create_empty<Token>(lexer->tokens.size);

    int identifier_pragma_underscore = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__pragma"));
    int identifier_declspec = lexer_add_or_find_identifier_by_string(lexer, string_create_static("__declspec"));
    int identifier_static_assert = lexer_add_or_find_identifier_by_string(lexer, string_create_static("static_assert"));
    int last_line_index = -1;
    for (int i = 0; i < lexer->tokens.size; i++)
    {
        Token* token = &lexer->tokens[i];
        bool is_first_token_in_line = false;
        if (token->position.start.line != last_line_index) {
            last_line_index = token->position.start.line;
            is_first_token_in_line = true;
        }

        // Skip lines starting with a hashtag
        if (is_first_token_in_line && token->type == Token_Type::HASHTAG) {
            while (i < lexer->tokens.size && lexer->tokens[i].position.start.line == last_line_index) {
                i++;
            }
            i--;
            continue;
        }

        if (token->type == Token_Type::IDENTIFIER_NAME)
        {
            if (token->attribute.identifier_number == identifier_pragma_underscore || 
                token->attribute.identifier_number == identifier_declspec || 
                token->attribute.identifier_number == identifier_static_assert) {
                // Skip everything afterwards if followed by a (
                i += 1;
                token = &lexer->tokens[i];
                if (token->type == Token_Type::OPEN_PARENTHESIS) {
                    i++;
                    int depth = 1;
                    while (i < lexer->tokens.size) {
                        token = &lexer->tokens[i];
                        if (token->type == Token_Type::OPEN_PARENTHESIS) {
                            depth++;
                        }
                        else if (token->type == Token_Type::CLOSED_PARENTHESIS) {
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
            if (token->attribute.identifier_number == result.identifier_call_conv_cdecl ||
                token->attribute.identifier_number == result.identifier_call_conv_clrcall ||
                token->attribute.identifier_number == result.identifier_call_conv_fastcall ||
                token->attribute.identifier_number == result.identifier_call_conv_stdcall ||
                token->attribute.identifier_number == result.identifier_call_conv_thiscall ||
                token->attribute.identifier_number == result.identifier_call_conv_vectorcall) {
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
    int line = parser->tokens[parser->index].position.start.line;
    parser->index++;
    while (header_parser_is_finished(parser) && parser->tokens[parser->index].position.start.line == line) {
        parser->index++;
    }
}

bool header_parser_test_next_token(Header_Parser* parser, Token_Type type) {
    if (parser->index >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == type;
}

bool header_parser_test_next_token_2(Header_Parser* parser, Token_Type t1, Token_Type t2) {
    if (parser->index + 1 >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == t1 &&
        parser->tokens[parser->index + 1].type == t2;
}

bool header_parser_test_next_token_3(Header_Parser* parser, Token_Type t1, Token_Type t2, Token_Type t3) {
    if (parser->index + 2 >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == t1 &&
        parser->tokens[parser->index + 1].type == t2 &&
        parser->tokens[parser->index + 2].type == t3;
}

bool header_parser_test_next_token_4(Header_Parser* parser, Token_Type t1, Token_Type t2, Token_Type t3, Token_Type t4) {
    if (parser->index + 3 >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == t1 &&
        parser->tokens[parser->index + 1].type == t2 &&
        parser->tokens[parser->index + 2].type == t3 &&
        parser->tokens[parser->index + 3].type == t4;
}

bool header_parser_test_next_token_5(Header_Parser* parser, Token_Type t1, Token_Type t2, Token_Type t3, Token_Type t4, Token_Type t5) {
    if (parser->index + 4 >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == t1 &&
        parser->tokens[parser->index + 1].type == t2 &&
        parser->tokens[parser->index + 2].type == t3 &&
        parser->tokens[parser->index + 3].type == t4 &&
        parser->tokens[parser->index + 4].type == t5;
}

bool header_parser_next_is_identifier(Header_Parser* parser, int identifier_index)
{
    if (parser->index >= parser->tokens.size) return false;
    return parser->tokens[parser->index].type == Token_Type::IDENTIFIER_NAME && parser->tokens[parser->index].attribute.identifier_number == identifier_index;
}

void print_tokens_till_newline(Header_Parser* parser, int token_index)
{
    Token* token = &parser->tokens[token_index];
    String t2_content = string_create_substring(&parser->source_code, token->source_code_index, token->source_code_index + 64);
    for (int i = 0; i < t2_content.size; i++) {
        if (t2_content.characters[i] == '\n' || t2_content.characters[i] == '\r') {
            t2_content.characters[i] = '\0';
            t2_content.size = i;
        }
    }
    SCOPE_EXIT(string_destroy(&t2_content));
    printf("%s", t2_content.characters);
}

void c_import_type_append_to_string(C_Import_Type* type, String* string, int indentation, Header_Parser* parser, bool print_array_members);

C_Type_Qualifiers header_parser_parse_type_qualifiers(Header_Parser* parser)
{
    u8 result = 0;
    while (parser->index < parser->tokens.size && parser->tokens[parser->index].type == Token_Type::IDENTIFIER_NAME) {
        int id = parser->tokens[parser->index].attribute.identifier_number;
        if (id == parser->identifier_atomic) {
            result = result | (u8)C_Type_Qualifiers::ATOMIC;
        }
        else if (id == parser->identifier_const) {
            result = result | (u8)C_Type_Qualifiers::CONST;
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
    if (!header_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) return optional_make_failure<C_Import_Type*>();

    C_Import_Type prototype;
    prototype.qualifiers = qualifiers;
    prototype.type = C_Import_Type_Type::PRIMITIVE;
    int identifier = parser->tokens[parser->index].attribute.identifier_number;
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
        prototype.primitive = C_Import_Primitive::VOID;
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

void c_import_symbol_table_define_symbol(C_Import_Symbol_Table* table, C_Import_Symbol symbol, int name_id)
{
    C_Import_Symbol* old_sym = hashtable_find_element(&table->symbols, name_id);
    if (old_sym != 0) 
    {
        logg("Shadowing type!\n");
        *old_sym = symbol;
    }
    else {
        hashtable_insert_element(&table->symbols, name_id, symbol);
    }
}

Optional<C_Variable_Definition> header_parser_parse_variable_definition(Header_Parser* parser);
Optional<C_Import_Type*> header_parser_parse_structure(Header_Parser* parser, C_Type_Qualifiers qualifiers)
{
    Checkpoint checkpoint = checkpoint_make(parser);

    C_Import_Type prototype;
    prototype.byte_size = 0;
    prototype.alignment = 0;
    prototype.qualifiers = qualifiers;

    if (header_parser_test_next_token(parser, Token_Type::STRUCT)) {
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
        int name_id;
        bool has_name = false;
        bool has_definition = false;
        if (header_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME))
        {
            has_name = true;
            name_id = parser->tokens[parser->index].attribute.identifier_number;
            parser->index++;
        }
        if (header_parser_test_next_token(parser, Token_Type::OPEN_BRACES))
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
            prototype.enumeration.name_id = has_name ? name_id : -1;
            prototype.byte_size = 4;
            prototype.alignment = 4;
        }
        else {
            prototype.structure.is_anonymous = !has_name;
            prototype.structure.name_id = has_name ? name_id : -1;
            prototype.byte_size = 0;
            prototype.alignment = 0;
        }

        if (has_name)
        {
            C_Import_Symbol* symbol = hashtable_find_element(&parser->result_package.symbol_table.symbols, name_id);
            if (symbol == 0)
            {
                if (prototype.type == C_Import_Type_Type::ENUM) {
                    prototype.enumeration.members = dynamic_array_create_empty<C_Import_Enum_Member>(4);
                }
                else {
                    prototype.structure.members = dynamic_array_create_empty<C_Import_Structure_Member>(4);
                }
                structure_type = c_import_type_system_register_type(&parser->result_package.type_system, prototype);

                C_Import_Symbol def_sym;
                def_sym.type = C_Import_Symbol_Type::TYPE;
                def_sym.data_type = structure_type;
                c_import_symbol_table_define_symbol(&parser->result_package.symbol_table, def_sym, name_id);

                {
                    String str = string_create_empty(256);
                    SCOPE_EXIT(string_destroy(&str));
                    string_append_formated(&str, "Structure def: ");
                    c_import_type_append_to_string(def_sym.data_type, &str, 0, parser, true);
                    logg("%s\n", str.characters);
                }

            }
            else {
                assert(symbol->type == C_Import_Symbol_Type::TYPE, "HEY");
                structure_type = symbol->data_type;
            }
        }
        else {
            if (prototype.type == C_Import_Type_Type::ENUM) {
                prototype.enumeration.members = dynamic_array_create_empty<C_Import_Enum_Member>(4);
            }
            else {
                prototype.structure.members = dynamic_array_create_empty<C_Import_Structure_Member>(4);
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
        if (header_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
            parser->index++;
            break;
        }

        if (structure_type->type == C_Import_Type_Type::ENUM)
        {
            if (header_parser_test_next_token_3(parser, Token_Type::IDENTIFIER_NAME, Token_Type::OP_ASSIGNMENT, Token_Type::INTEGER_LITERAL))
            {
                C_Import_Enum_Member member;
                member.name_id = parser->tokens[parser->index].attribute.identifier_number;
                member.value = parser->tokens[parser->index + 2].attribute.integer_value;
                enum_counter = member.value + 1;
                dynamic_array_push_back(&structure_type->enumeration.members, member);
                parser->index += 3;
            }
            else if (header_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
                C_Import_Enum_Member member;
                member.name_id = parser->tokens[parser->index].attribute.identifier_number;
                member.value = enum_counter;
                enum_counter++;
                dynamic_array_push_back(&structure_type->enumeration.members, member);
                parser->index += 1;
            }
            else {
                success = false;
                break;
            }

            if (header_parser_test_next_token(parser, Token_Type::COMMA)) {
                parser->index++;
            }
            else if (header_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
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
            Optional<C_Variable_Definition> member_var = header_parser_parse_variable_definition(parser);
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
                        new_member.name_id = member->name_id;
                        dynamic_array_push_back(&structure_type->structure.members, new_member);
                    }
                    structure_type->byte_size += member_var.value.base_type->byte_size;
                }
            }
            else
            {
                for (int i = 0; i < member_var.value.instances.size; i++)
                {
                    C_Variable_Instance* instance = &member_var.value.instances[i];
                    C_Import_Structure_Member member;
                    member.name_id = instance->name_id;
                    member.type = instance->type;
                    assert(member.type->byte_size != 0 && member.type->alignment != 0, "Member type must be complete!");
                    structure_type->byte_size = math_round_next_multiple(structure_type->byte_size, member.type->alignment);
                    structure_type->alignment = math_maximum(structure_type->alignment, member.type->alignment);
                    member.offset = structure_type->byte_size;
                    structure_type->byte_size += member.type->byte_size;
                    dynamic_array_push_back(&structure_type->structure.members, member);
                }
            }

            if (header_parser_test_next_token_2(parser, Token_Type::COLON, Token_Type::INTEGER_LITERAL)) {
                parser->index += 2;
            }
            if (!header_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
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

Optional<C_Import_Type*> header_parser_parse_type(Header_Parser* parser)
{
    Checkpoint checkpoint = checkpoint_make(parser);

    // Parse Qualifier flags
    C_Type_Qualifiers qualifiers = header_parser_parse_type_qualifiers(parser);
    Optional<C_Import_Type*> result = header_parser_parse_primitive_type(parser, qualifiers);
    if (!result.available)
    {
        result = header_parser_parse_structure(parser, qualifiers);
        if (!result.available) {
            if (header_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME))
            {
                int name_id = parser->tokens[parser->index].attribute.identifier_number;
                parser->index++;
                C_Import_Symbol* symbol = hashtable_find_element(&parser->result_package.symbol_table.symbols, name_id);
                if (symbol == 0) {
                   return optional_make_success(parser->result_package.type_system.error_type);
                   //return optional_make_failure<C_Import_Type*>();
                    //panic("Check if this happens, otherwise return failure");
                }
                if (symbol->type != C_Import_Symbol_Type::TYPE) {
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
    case C_Import_Type_Type::ERROR:
        break;
    default: panic("WHAT");
    }
    prototype.qualifiers = (C_Type_Qualifiers)((u8)prototype.qualifiers & (u8)qualifiers);
    C_Import_Type* changed_type = c_import_type_system_register_type(&parser->result_package.type_system, prototype);
    return optional_make_success(changed_type);
}

C_Import_Type* header_parser_parse_array_suffix(Header_Parser* parser, C_Import_Type* base_type)
{
    bool is_array = false;
    bool has_size = false;
    int size = 0;
    if (header_parser_test_next_token_2(parser, Token_Type::OPEN_BRACKETS, Token_Type::CLOSED_BRACKETS))
    {
        is_array = true;
        has_size = false;
        parser->index += 2;
    }
    else
    {
        has_size = true;
        size = 1;
        while (header_parser_test_next_token_3(parser, Token_Type::OPEN_BRACKETS, Token_Type::INTEGER_LITERAL, Token_Type::CLOSED_BRACKETS)) {
            is_array = true;
            size = size * parser->tokens[parser->index + 1].attribute.integer_value;
            parser->index += 3;
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
        if (header_parser_test_next_token(parser, Token_Type::OP_STAR))
        {
            parser->index++;
            C_Type_Qualifiers qualifers = (C_Type_Qualifiers)0;
            if (header_parser_next_is_identifier(parser, parser->identifier_const)) {
                parser->index++;
                qualifers = (C_Type_Qualifiers)((u8)qualifers | (u8)C_Type_Qualifiers::CONST);
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
    if (!header_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
        return optional_make_failure<Dynamic_Array<C_Import_Parameter>>();
    }
    parser->index += 1;

    Dynamic_Array<C_Import_Parameter> parameters = dynamic_array_create_empty<C_Import_Parameter>(2);
    bool success = true;
    while (true)
    {
        if (header_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            parser->index++;
            break;
        }

        Optional<C_Import_Type*> opt_type = header_parser_parse_type(parser);
        if (!opt_type.available) {
            success = false;
            break;
        }
        C_Import_Type* type = opt_type.value;

        type = header_parser_parse_pointer_suffix(parser, type);
        C_Import_Parameter param;
        param.has_name = false;
        if (header_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
            param.has_name = true;
            param.name_id = parser->tokens[parser->index].attribute.identifier_number;
            parser->index++;
        }
        type = header_parser_parse_array_suffix(parser, type);
        param.type = type;
        if (param.type->type == C_Import_Type_Type::PRIMITIVE && param.type->primitive == C_Import_Primitive::VOID) {
            assert(!param.has_name, "HEY");
        }
        else {
            dynamic_array_push_back(&parameters, param);
        }

        if (header_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            parser->index++;
            break;
        }
        else if (header_parser_test_next_token(parser, Token_Type::COMMA)) {
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

Optional<C_Variable_Definition> header_parser_parse_variable_definition(Header_Parser* parser)
{
    Checkpoint checkpoint = checkpoint_make(parser);
    Optional<C_Import_Type*> opt_base_type = header_parser_parse_type(parser);
    if (!opt_base_type.available) {
        checkpoint_rewind(checkpoint);
        return optional_make_failure<C_Variable_Definition>();
    }
    C_Import_Type* base_type = opt_base_type.value;
    C_Import_Type* first_type = header_parser_parse_pointer_suffix(parser, base_type);
    first_type = header_parser_parse_array_suffix(parser, first_type);

    C_Variable_Definition result;
    result.base_type = base_type;
    result.instances = dynamic_array_create_empty<C_Variable_Instance>(2);
    bool success = true;
    SCOPE_EXIT(if (!success) { checkpoint_rewind(checkpoint); dynamic_array_destroy(&result.instances); });
    // Differentiate Function pointer definition from Variable definition
    if (header_parser_test_next_token_2(parser, Token_Type::OPEN_PARENTHESIS, Token_Type::OP_STAR) ||
        header_parser_test_next_token_3(parser, Token_Type::OPEN_PARENTHESIS, Token_Type::IDENTIFIER_NAME, Token_Type::OP_STAR))
    {
        if (parser->tokens[parser->index + 1].type == Token_Type::IDENTIFIER_NAME) {
            parser->index += 3;
        }
        else {
            parser->index += 2;
        }
        if (!header_parser_test_next_token_2(parser, Token_Type::IDENTIFIER_NAME, Token_Type::CLOSED_PARENTHESIS)) {
            success = false;
            return optional_make_failure<C_Variable_Definition>();
        }
        int name_id = parser->tokens[parser->index].attribute.identifier_number;
        parser->index++;

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
        instance.name_id = name_id;
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

            // Parse instance name
            if (header_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME))
            {
                instance.name_id = parser->tokens[parser->index].attribute.identifier_number;
                parser->index++;
            }
            else {
                break;
            }

            instance.type = header_parser_parse_array_suffix(parser, instance.type);
            dynamic_array_push_back(&result.instances, instance);
            // Continue if necessary, TODO: Skip default initialization (struct X { int a = 5, b = 7;}
            if (!header_parser_test_next_token(parser, Token_Type::COMMA)) {
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
    if (((u8)qualifiers & (u8)C_Type_Qualifiers::CONST) != 0) {
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

void c_import_type_append_to_string(C_Import_Type* type, String* string, int indentation, Header_Parser* parser, bool print_array_members)
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
        case C_Import_Primitive::VOID:
            string_append_formated(string, "VOID");
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
    case C_Import_Type_Type::ERROR: {
        string_append_formated(string, "ERROR_TYPE");
        break;
    }
    case C_Import_Type_Type::ARRAY: {
        c_import_type_append_to_string(type->array.element_type, string, indentation, parser, print_array_members);
        string_append_formated(string, "[%d]", type->array.array_size);
        break;
    }
    case C_Import_Type_Type::POINTER: {
        c_import_type_append_to_string(type->pointer_child_type, string, indentation, parser, print_array_members);
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
            string_append_formated(string, " %s", lexer_identifer_to_string(parser->lexer, type->structure.name_id).characters);
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
                c_import_type_append_to_string(member->type, string, indentation + 1, parser, false);
                string_append_formated(string, "%s\n", lexer_identifer_to_string(parser->lexer, member->name_id).characters);
            }
            string_indent(string, indentation);
            string_append_formated(string, "}");
        }
        break;
    }
    case C_Import_Type_Type::ENUM: {
        string_append_formated(string, "ENUM ");
        if (!type->enumeration.is_anonymous) {
            string_append_formated(string, " %s", lexer_identifer_to_string(parser->lexer, type->enumeration.name_id).characters);
        }
        string_append_formated(string, " {");
        for (int i = 0; i < type->enumeration.members.size; i++) {
            C_Import_Enum_Member* member = &type->enumeration.members[i];
            string_append_formated(string, "%s = %d, ",
                lexer_identifer_to_string(parser->lexer, member->name_id).characters,
                member->value
            );
        }
        string_append_formated(string, "}");
        break;
    }
    case C_Import_Type_Type::FUNCTION_SIGNATURE: {
        string_append_formated(string, "Function \n");
        c_import_type_append_to_string(type->function_signature.return_type, string, indentation + 1, parser, print_array_members);
        string_append_formated(string, "(");
        for (int i = 0; i < type->function_signature.parameters.size; i++) {
            C_Import_Parameter* parameter = &type->function_signature.parameters[i];
            if (parameter->has_name) {
                string_append_formated(string, "%s: ", lexer_identifer_to_string(parser->lexer, parameter->name_id).characters);
            }
            c_import_type_append_to_string(parameter->type, string, indentation + 2, parser, print_array_members);
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
        else if (header_parser_test_next_token(parser, Token_Type::EXTERN)) {
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
    C_Definition_Modifiers modifiers = header_parser_parse_definition_modifiers(parser);
    bool is_typedef = ((u8)modifiers & (u8)C_Definition_Modifiers::TYPEDEF) != 0;
    bool is_extern = ((u8)modifiers & (u8)C_Definition_Modifiers::EXTERN) != 0;

    Optional<C_Variable_Definition> var_def_opt = header_parser_parse_variable_definition(parser);
    if (!var_def_opt.available) {
        return false;
    }
    C_Variable_Definition var_def = var_def_opt.value;
    SCOPE_EXIT(dynamic_array_destroy(&var_def.instances););

    if (!is_extern && var_def.instances.size == 1 &&
        !(var_def.instances[0].type->type == C_Import_Type_Type::POINTER &&
            var_def.instances[0].type->pointer_child_type->type == C_Import_Type_Type::FUNCTION_SIGNATURE
            ) && header_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS))
    {
        Optional<Dynamic_Array<C_Import_Parameter>> params = header_parser_parse_parameters(parser);
        if (params.available)
        {
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
            c_import_symbol_table_define_symbol(&parser->result_package.symbol_table, symbol, var_def.instances[0].name_id);

            {
                String str = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&str));
                string_append_formated(&str, "%s: ", is_typedef ? "typedef " : "function ");
                string_append_formated(&str, "%s = ", lexer_identifer_to_string(parser->lexer, var_def.instances[0].name_id).characters);
                c_import_type_append_to_string(registered_function, &str, 0, parser, true);
                logg("%s\n", str.characters);
            }

            return true;
        }
    }
    else if (header_parser_test_next_token(parser, Token_Type::SEMICOLON))
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
            c_import_symbol_table_define_symbol(&parser->result_package.symbol_table, symbol, instance->name_id);

            {
                String str = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&str));
                string_append_formated(&str, "%s: ", is_typedef ? "typedef " : "global: ");
                string_append_formated(&str, "%s = ", lexer_identifer_to_string(parser->lexer, instance->name_id).characters);
                c_import_type_append_to_string(symbol.data_type, &str, 0, parser, true);
                logg("%s\n", str.characters);
            }


        }

        return true;
    }

    return false;
}

void header_parser_parse(Header_Parser* parser)
{
    /*
    printf("Header_parser_parse: ");
    print_tokens_till_newline(parser, parser->index);
    printf("\n");
    */

    int identifier_extern_c = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("C"));
    while (parser->index + 2 < parser->tokens.size)
    {
        /*
        printf("New parsing test line: ");
        print_tokens_till_newline(parser, parser->index);
        printf("\n");

        if (header_parser_test_next_token_2(parser, Token_Type::IDENTIFIER_NAME, Token_Type::STRUCT)) {
            if (parser->tokens[parser->index].attribute.identifier_number = parser->identifier_typedef) {
                logg("Here is where the fun begins");
            }
        }
        */

        int rewind_index = parser->index;
        header_parser_parse_known_structure(parser);
        parser->index = rewind_index;

        int current_line = parser->tokens[parser->index].position.start.line;
        int depth = 0;
        bool depth_was_nonzero = false;
        while (parser->index + 2 < parser->tokens.size)
        {
            Token* t1 = &parser->tokens[parser->index];
            Token* t2 = &parser->tokens[parser->index + 1];
            Token* t3 = &parser->tokens[parser->index + 2];

            switch (t1->type)
            {
            case Token_Type::OPEN_BRACES:
            case Token_Type::OPEN_PARENTHESIS:
            case Token_Type::OPEN_BRACKETS:
                depth++;
                depth_was_nonzero = true;
                break;
            case Token_Type::CLOSED_BRACES:
            case Token_Type::CLOSED_PARENTHESIS:
            case Token_Type::CLOSED_BRACKETS:
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
                    if (t1->type == Token_Type::SEMICOLON) {
                        parser->index++;
                        break;
                    }
                    //if (t1->position.start.line != current_line) break;
                }
            }
            if (t1->type == Token_Type::EXTERN && t2->type == Token_Type::STRING_LITERAL && t2->attribute.identifier_number == identifier_extern_c)
            {
                if (t3->type == Token_Type::OPEN_BRACES) {
                    parser->index += 3;
                    header_parser_parse(parser);
                    logg("Stepping out of parse thing: \n\t\"");
                    print_tokens_till_newline(parser, parser->index);
                    logg("\"\n");
                    break;
                }
                else {
                    parser->index += 2;
                    rewind_index = parser->index;
                    //if (!header_parser_parse_known_structure(parser)) {
                        //parser->index = rewind_index;
                    //}
                    header_parser_parse_known_structure(parser);
                    parser->index = rewind_index;
                    //else {
                        //break;
                    //}
                }
            }

            parser->index++;
        }
    }
}

void try_analyse_header_file()
{
    /*
        What information do i need:
            * Function prototypes
            * Typedefs
            * Structs
            * Unions
            * enums

        What do i know that could help me with parsing
            - I know that the program has to have correct Parenthesis indentation -> I could make an 'AST' using that
            - The stuff I need must either be at no scope, or in a scope that defines extern 'C'
            - I can look up exactly how a CC--function prototype may look
            - I can assume that the informations I need all start at a new line

        The plan:
         1. Create 'AST' like thing using correct Parenthesis
         2. Analyse this AST and extract the parts that may contain the things we need (Top-Level nodes and extern 'C' nodes)
         3. Find all typedefs (Probably the easiest to parse, since it requires the typedef keyword at the start)
         4. ... (Further planning required

         Notes:
           - Extern 'C' may appear in line before globals and possibly other stuff
           - #pragma may appear before an extern C, and possible at other places 2
           - __pragma also exists
           - Names starting with _ should probably not be accessible from the outside
           - typedefs may appear on the same line as struct declarations :(

        I think that parsing typedefs and structs could be made easier by just looking for the
        struct keyword, which needs to appear after an ; or at the start of the file.
        Typedefs are even simpler, since a typedef statement always starts with typedef and they dont appear anywhere else.

        I probably also want to remove compiler stuff, like #pragma, __pragma() and __declspec()
        I feel like I want a parser that just goes over everything, does something when finding extern "C" and other stuff
    */

    // Load preprocessed file
    Optional<String> text_file_opt = file_io_load_text_file("backend/tmp/after_pre.txt");
    SCOPE_EXIT(file_io_unload_text_file(&text_file_opt));
    if (!text_file_opt.available) {
        logg("Could not load file");
        std::cin.ignore();
        return;
    }
    String source_code = text_file_opt.value;

    // Run lexer over file
    Lexer lexer = lexer_create();
    SCOPE_EXIT(lexer_destroy(&lexer));
    lexer_parse_string(&lexer, &source_code);

    logg("Lexing finished, Stats:\nIdentifier Count: #%d\nToken Count: #%d\n Whitespace-Token Count: %d\n",
        lexer.identifiers.size, lexer.tokens.size, lexer.tokens_with_whitespaces.size - lexer.tokens.size);

    Header_Parser header_parser = header_parser_create(&lexer, source_code);
    SCOPE_EXIT(header_parser_destroy(&header_parser, true));
    header_parser_parse(&header_parser);
    /*

    // DEBUG OUTPUT
    for (int i = 0; i < lexer.tokens.size; i++) {
        printf("%s ", token_type_to_string(lexer.tokens[i].type));
    }
    for (int i = 0; i < lexer.identifiers.size; i++) {
        String identifier = lexer.identifiers[i];
        printf("%s\n", identifier.characters);
    }
    */

    /*
    // Analyse Parenthesis
    enum class Parenthesis_Type
    {
        BRACES, // {}
        PARENTHESIS, // ()
        BRACKETS // []
    };
    struct Parenthesis_Area
    {
        Parenthesis_Type type;
        int start;
        int end;
    };
    Dynamic_Array<Parenthesis_Area> areas = dynamic_array_create_empty<Parenthesis_Area>(256);
    Dynamic_Array<int> area_queue = dynamic_array_create_empty<int>(256);
    SCOPE_EXIT(dynamic_array_destroy(&areas));
    SCOPE_EXIT(dynamic_array_destroy(&area_queue));
    {
        Parenthesis_Area global_area;
        global_area.start = -1;
        global_area.end = lexer.tokens.size;
        global_area.type = Parenthesis_Type::BRACES;
        dynamic_array_push_back(&areas, global_area);
    }
    for (int i = 0; i < lexer.tokens.size; i++)
    {
        Token* token = &lexer.tokens[i];
        Parenthesis_Type parenthesis_type;
        bool is_open = false;
        switch (token->type)
        {
        case Token_Type::OPEN_PARENTHESIS: {
            parenthesis_type = Parenthesis_Type::PARENTHESIS;
            is_open = true;
            break;
        }
        case Token_Type::CLOSED_PARENTHESIS: {
            parenthesis_type = Parenthesis_Type::PARENTHESIS;
            is_open = false;
            break;
        }
        case Token_Type::OPEN_BRACKETS: {
            parenthesis_type = Parenthesis_Type::BRACKETS;
            is_open = true;
            break;
        }
        case Token_Type::CLOSED_BRACKETS: {
            parenthesis_type = Parenthesis_Type::BRACKETS;
            is_open = false;
            break;
        }
        case Token_Type::OPEN_BRACES: {
            parenthesis_type = Parenthesis_Type::BRACES;
            is_open = true;
            break;

        }
        case Token_Type::CLOSED_BRACES: {
            parenthesis_type = Parenthesis_Type::BRACES;
            is_open = false;
            break;
        }
        default: continue;
        }

        if (is_open)
        {
            Parenthesis_Area area;
            area.start = i;
            area.type = parenthesis_type;
            dynamic_array_push_back(&areas, area);
            dynamic_array_push_back(&area_queue, areas.size - 1);
        }
        else {
            int last_index = area_queue[area_queue.size - 1];
            dynamic_array_rollback_to_size(&area_queue, area_queue.size - 1);
            Parenthesis_Area* area = &areas[last_index];
            if (area->type != parenthesis_type) {
                panic("Hey, should not happen in valid header file!\n");
                break;
            }

            area->end = i;
        }
    }

    assert(area_queue.size == 0, "HEY");
    logg("Area count: #%d\n", areas.size);

    // Find areas that need to be checked (The two previous tokens are extern "C")
    int identifier_string_c = lexer_add_or_find_identifier_by_string(&lexer, string_create_static("C"));
    Dynamic_Array<int> extern_c_areas = dynamic_array_create_empty<int>(areas.size);
    SCOPE_EXIT(dynamic_array_destroy(&extern_c_areas));
    dynamic_array_push_back(&extern_c_areas, 0);
    for (int i = 0; i < areas.size; i++)
    {
        Parenthesis_Area* area = &areas[i];
        if (area->start < 1) continue;
        if (area->type != Parenthesis_Type::BRACES) continue;
        Token* t1 = &lexer.tokens[area->start - 2];
        Token* t2 = &lexer.tokens[area->start - 1];
        if (t1->type == Token_Type::EXTERN && t2->type == Token_Type::STRING_LITERAL)
        {
            if (t2->attribute.identifier_number == identifier_string_c) {
                dynamic_array_push_back(&extern_c_areas, i);
            }
        }
    }
    logg("Extern C-Area count: #%d\n", extern_c_areas.size);

    // Enumerate interesting line starts
    Dynamic_Array<int> interesting_line_starts = dynamic_array_create_empty<int>(256);
    SCOPE_EXIT(dynamic_array_destroy(&interesting_line_starts));
    for (int i = 0; i < extern_c_areas.size; i++)
    {
        Parenthesis_Area* area = &areas[extern_c_areas[i]];
        int next_inner_area_index = extern_c_areas[i] + 1;
        int last_line_index = -1;
        for (int j = area->start + 1; j < area->end; j++)
        {
            // Skip inner parenthesised areas
            if (next_inner_area_index < areas.size) {
                Parenthesis_Area* next_inner_area = &areas[next_inner_area_index];
                while (j >= next_inner_area->end) {
                    next_inner_area_index++;
                    next_inner_area = &areas[next_inner_area_index];
                }
                if (j >= next_inner_area->start) {
                    j = next_inner_area->end;
                    continue;
                }
            }

            // Skip if we already checked this line
            Token* token = &lexer.tokens[j];
            if (token->position.start.line == last_line_index) {
                continue;
            }
            else {
                last_line_index = token->position.start.line;
                dynamic_array_push_back(&interesting_line_starts, j);
            }
        }
    }
    std::sort(&interesting_line_starts.data[0], &interesting_line_starts.data[interesting_line_starts.size - 1]);
    logg("Intersting lines: #%d\n", interesting_line_starts.size);
    */

    /*
        Now lets look at each of those lines and look for patterns
    */
    /*
        int identifier_typedef = lexer_add_or_find_identifier_by_string(&lexer, string_create_static("typedef"));
        // Analyse interesting lines
        int count = 0;
        for (int i = 0; i < interesting_line_starts.size; i++)
        {
            int token_index = interesting_line_starts[i];
            Token* token = &lexer.tokens[token_index];
            //if (token->type == Token_Type::IDENTIFIER_NAME && token->attribute.identifier_number == identifier_typedef)
            if (token->type == Token_Type::STRUCT)
            {
                // Print line for now
                int k = token_index;
                while (lexer.tokens[k].position.start.line == token->position.start.line) {
                    k++;
                }
                k = k - 1;

                int str_len = lexer.tokens[k].position.end.character - token->position.start.character;
                assert(str_len > 0, "HEY");
                String t2_content = string_create_substring(&source_code, token->source_code_index, token->source_code_index + str_len);
                SCOPE_EXIT(string_destroy(&t2_content));
                printf("\t#%d: Found typedef: %s\n", count, t2_content.characters);
                count++;
            }
        }


        /*
        Code for token to string conversion:
                int str_len = t2->position.end.character - t2->position.start.character;
                assert(str_len > 0, "HEY");
                String t2_content = string_create_substring(&source_code, t2->source_code_index, t2->source_code_index + str_len);
                SCOPE_EXIT(string_destroy(&t2_content));
                printf("\t#%d: Found extern area: %s\n", count, t2_content.characters);
                count++;
        */

        /*
        Token_Parser parser;
        parser.tokens = lexer.tokens;
        parser.index = 0;
        parser.lexer = &lexer;

        int last_line = -1;
        int last_count = 0;
        int double_id_open_count = 0;
        while (parser_has_tokens(&parser))
        {
            if (parser.index > last_count) {
                last_count = last_count + 2000;
                logg("Already scanned #%d\n", parser.index);
            }
            if (token_parser_test_next_token_3(&parser, Token_Type::IDENTIFIER_NAME, Token_Type::IDENTIFIER_NAME, Token_Type::OPEN_PARENTHESIS))
            {
                double_id_open_count++;
                int start = parser.index;
                parser.index += 3;
                parser_print_next_n_tokens(&parser, 7);
                while (true)
                {
                    if (token_parser_test_next_token_2(&parser, Token_Type::CLOSED_PARENTHESIS, Token_Type::SEMICOLON))
                    {
                        logg("Found one!\n");
                        parser.index += 2;
                        break;
                    }
                    if (token_parser_test_next_token_2(&parser, Token_Type::IDENTIFIER_NAME, Token_Type::IDENTIFIER_NAME))
                    {
                        parser.index += 2;
                        while (token_parser_test_next_token(&parser, Token_Type::COLON)) {
                            parser.index += 1;
                        }
                        continue;
                    }
                    // Error, did not find any
                    break;
                }
            }
            parser_goto_next_line(&parser);
        }
        //lexer_print(&lexer);
        printf("\n Double id open count: #%d\n", double_id_open_count);
        */
    std::cin.ignore();
}

int main(int argc, char** argv)
{
    try_analyse_header_file();
    //upp_lang_main();
    //proc_city_main();

    return 0;
}