#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"

struct Identifier_Pool;

enum class Primitive_Type
{
    BOOLEAN,
    INTEGER,
    FLOAT
};

enum class Signature_Type
{
    VOID_TYPE,
    PRIMITIVE,
    POINTER,
    FUNCTION,
    STRUCT,
    ARRAY, // Array with compile-time known size, like [5]int
    SLICE, // Array with dynamic size, []int
    TEMPLATE_TYPE,
    ERROR_TYPE,
    // Future: Enum and Unions
};

struct Type_Signature;
struct Struct_Member
{
    Type_Signature* type;
    int offset;
    int name_handle;
};

struct Type_Signature
{
    Signature_Type type;
    int size;
    int alignment;
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
            int name;
        } structure;
        int template_name;
    } options;
};
struct Semantic_Analyser;
void type_signature_append_to_string(String* string, Type_Signature* signature, Identifier_Pool* identifier_pool);
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

struct Type_System
{
    Dynamic_Array<Type_Signature*> types;

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

    int id_data;
    int id_size;
};

Type_System type_system_create();
void type_system_destroy(Type_System* system);
void type_system_add_primitives(Type_System* system, Identifier_Pool* pool);
void type_system_reset(Type_System* system);

Type_Signature* type_system_register_type(Type_System* system, Type_Signature signature);
Type_Signature* type_system_make_pointer(Type_System* system, Type_Signature* child_type);
Type_Signature* type_system_make_slice(Type_System* system, Type_Signature* element_type);
Type_Signature* type_system_make_function(Type_System* system, Dynamic_Array<Type_Signature*> parameter_types, Type_Signature* return_type);
void type_system_print(Type_System* system, Identifier_Pool* pool);