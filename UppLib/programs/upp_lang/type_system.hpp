#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

struct Identifier_Pool;
struct AST_Node;

enum class Primitive_Type
{
    INTEGER = 1,
    FLOAT = 2,
    BOOLEAN = 3
};

enum class Signature_Type
{
    VOID_TYPE,
    PRIMITIVE,
    POINTER,
    FUNCTION,
    STRUCT,
    ENUM,
    ARRAY, // Array with compile-time known size, like [5]int
    SLICE, // Array with dynamic size, []int
    TEMPLATE_TYPE,
    TYPE_TYPE,
    ERROR_TYPE,
    // Future: Enum and Unions
};

struct Type_Signature;
struct Struct_Member
{
    Type_Signature* type;
    int offset;
    String* id;
};

struct Enum_Member
{
    String* id;
    int value;
    AST_Node* definition_node;
};

enum class Structure_Type
{
    STRUCT,
    C_UNION,
    UNION
};

struct Type_Signature
{
    Signature_Type type;
    int size;
    int alignment;
    u64 internal_index;
    union
    {
        struct {
            Primitive_Type type;
            bool is_signed;
        } primitive;
        struct {
            Dynamic_Array<Type_Signature*> parameter_types;
            Type_Signature* return_type; // If this is void type, then function has no return value
        } function;
        Type_Signature* pointer_child;
        struct {
            Type_Signature* element_type;
            int element_count;
        } array;
        struct {
            Type_Signature* element_type;
            Struct_Member data_member;
            Struct_Member size_member;
        } slice;
        struct {
            Dynamic_Array<Struct_Member> members;
            String* id;
            Structure_Type struct_type;
            Struct_Member tag_member;
        } structure;
        struct {
            Dynamic_Array<Enum_Member> members;
            String* id;
        } enum_type;
        String* template_id;
    } options;
};
void type_signature_append_to_string(String* string, Type_Signature* signature);
void type_signature_append_value_to_string(Type_Signature* type, byte* value_ptr, String* string);

// An array as it is currently defiend in the upp-language
struct Upp_Slice
{
    void* data_ptr;
    i32 size;
};

// A string as it is currently defined in the upp-language
struct Upp_String
{
    char* character_buffer_data;
    i32 character_buffer_size;
    i32 _padding;
    i32 size;
};

struct Internal_Type_Information
{
    u64 type;
    int size;
    int alignment;
    bool is_primitive_type;
    i32 primitive_type;
};

struct Type_System
{
    Dynamic_Array<Type_Signature*> types;
    Dynamic_Array<Internal_Type_Information> internal_type_infos;
    u64 next_internal_index;

    Type_Signature* error_type;
    Type_Signature* bool_type;
    Type_Signature* i8_type;
    Type_Signature* i16_type;
    Type_Signature* i32_type;
    Type_Signature* i64_type;
    Type_Signature* u8_type;
    Type_Signature* u16_type;
    Type_Signature* u32_type;
    Type_Signature* u64_type;
    Type_Signature* f32_type;
    Type_Signature* f64_type;
    Type_Signature* void_type;
    Type_Signature* void_ptr_type;
    Type_Signature* string_type;
    Type_Signature* empty_struct_type;
    Type_Signature* type_type;
    Type_Signature* type_information_type;
    Type_Signature* type_primitive_type_enum;

    String* id_data;
    String* id_size;
    String* id_tag;
};

Type_System type_system_create();
void type_system_destroy(Type_System* system);
void type_system_add_primitives(Type_System* system, Identifier_Pool* pool);
void type_system_reset(Type_System* system);

Type_Signature* type_system_register_type(Type_System* system, Type_Signature signature);
void type_system_finish_type(Type_System* system, Type_Signature* type);

Type_Signature* type_system_make_primitive(Type_System* system, Primitive_Type type, int size, bool is_signed);
Type_Signature* type_system_make_pointer(Type_System* system, Type_Signature* child_type);
Type_Signature* type_system_make_slice(Type_System* system, Type_Signature* element_type);
Type_Signature* type_system_make_array_finished(Type_System* system, Type_Signature* element_type, int element_count);
Type_Signature* type_system_make_function(Type_System* system, Dynamic_Array<Type_Signature*> parameter_types, Type_Signature* return_type);
Type_Signature* type_system_make_template(Type_System* system, String* id);
struct AST_Node;
Type_Signature* type_system_make_struct_empty(Type_System* system, AST_Node* struct_node);
Type_Signature* type_system_make_enum_empty_from_node(Type_System* system, String* id);
Type_Signature* type_system_make_struct_empty_full(Type_System* system, String* id, Structure_Type struct_type);
void type_system_print(Type_System* system);
Optional<Enum_Member> enum_type_find_member_by_value(Type_Signature* enum_type, int value);