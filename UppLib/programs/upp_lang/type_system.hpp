#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include <initializer_list>
#include "ast.hpp"

struct Identifier_Pool;
struct Symbol;
struct Struct_Progress;
struct Predefined_Symbols;
struct Timer;
namespace AST
{
    struct Definition;
}



// TYPE SIGNATURES
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
    UNKNOWN_TYPE, // When errors occure we contine the analysis with unknown
};

struct Type_Signature;
struct Struct_Member
{
    Type_Signature* type;
    int offset;
    String* id;
};

struct Enum_Item
{
    String* id;
    int value;
};

struct Function_Parameter
{
    Optional<String*> name;
    Type_Signature* type;
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
            Dynamic_Array<Function_Parameter> parameters;
            Type_Signature* return_type; // If this is void type, then function has no return value
        } function;
        Type_Signature* pointer_child;
        struct {
            Type_Signature* element_type;
            bool count_known; // False in case of polymorphism(Comptime values) or when Errors occured
            int element_count;
        } array;
        struct {
            Type_Signature* element_type;
            Struct_Member data_member;
            Struct_Member size_member;
        } slice;
        struct {
            Dynamic_Array<Struct_Member> members;
            AST::Structure_Type struct_type;
            Struct_Member tag_member;

            Symbol* symbol; // May be null
            Struct_Progress* progress; // May be null
        } structure;
        struct {
            Dynamic_Array<Enum_Item> members;
            String* id;
        } enum_type;
        String* template_id;
    } options;
};
void type_signature_append_to_string(String* string, Type_Signature* signature);
void type_signature_append_value_to_string(Type_Signature* type, byte* value_ptr, String* string);


// C++ TYPE REPRESENTATIONS

// An array as it is currently defined in the upp-language
template<typename T>
struct Upp_Slice
{
    T* data_ptr;
    i32 size;
};

struct Upp_Slice_Base
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

struct Upp_Any
{
    void* data;
    u64 type;
};



// TYPE-INFORMATION STRUCTS (Usable in Upp)
struct Internal_Type_Primitive
{
    bool is_signed;
    Primitive_Type tag;
};

struct Internal_Type_Function
{
    Upp_Slice<u64> parameters;
    u64 return_type;
};

struct Internal_Type_Struct_Member
{
    Upp_String name;
    u64 type;
    int offset;
};

struct Internal_Structure_Type
{
    int tag_member_index;
    AST::Structure_Type tag;
};

struct Internal_Type_Struct
{
    Upp_Slice<Internal_Type_Struct_Member> members;
    Upp_String name;
    Internal_Structure_Type type;
};

struct Internal_Type_Enum_Member
{
    Upp_String name;
    int value;
};

struct Internal_Type_Array
{
    u64 element_type;
    int size;
};

struct Internal_Type_Slice
{
    u64 element_type;
};

struct Internal_Type_Enum
{
    Upp_Slice<Internal_Type_Enum_Member> members;
    Upp_String name;
};

enum class Internal_Type_Options_Tag
{
    VOID_TYPE = 1,
    TYPE_TYPE,
    POINTER,
    ARRAY,
    SLICE,
    PRIMITIVE,
    FUNCTION,
    STRUCTURE,
    ENUMERATION,
};
Internal_Type_Options_Tag signature_type_to_internal_type(Signature_Type type);

struct Internal_Type_Info_Options
{
    union {
        struct {} void_type;
        struct {} type_type;
        u64 pointer;
        Internal_Type_Array array;
        Internal_Type_Slice slice;
        Internal_Type_Primitive primitive;
        Internal_Type_Function function;
        Internal_Type_Struct structure;
        Internal_Type_Enum enumeration;
    };
    Internal_Type_Options_Tag tag;
};

struct Internal_Type_Information
{
    u64 type;
    int size;
    int alignment;
    Internal_Type_Info_Options options;
};




struct Predefined_Types
{
    // Primitive types
    Type_Signature* unknown_type;
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

    // Prebuilt structs/types used by compiler
    Type_Signature* void_ptr_type;
    Type_Signature* string_type;
    Type_Signature* empty_struct_type;
    Type_Signature* type_type;
    Type_Signature* type_information_type;
    Type_Signature* any_type;

    // Types for built-in/hardcoded functions
    Type_Signature* type_assert;
    Type_Signature* type_free;
    Type_Signature* type_malloc;
    Type_Signature* type_type_of;
    Type_Signature* type_type_info;
    Type_Signature* type_print_bool;
    Type_Signature* type_print_i32;
    Type_Signature* type_print_f32;
    Type_Signature* type_print_line;
    Type_Signature* type_print_string;
    Type_Signature* type_read_i32;
    Type_Signature* type_read_f32;
    Type_Signature* type_read_bool;
    Type_Signature* type_random_i32;
};

// TYPE SYSTEM
struct Type_System
{
    Timer* timer;
    double register_time;

    Dynamic_Array<Type_Signature*> types;
    Dynamic_Array<Internal_Type_Information> internal_type_infos;
    u64 next_internal_index;
    Predefined_Types predefined_types;
};

Type_System type_system_create(Timer* timer);
void type_system_destroy(Type_System* system);
void type_system_reset(Type_System* system);
void type_system_add_predefined_types(Type_System* system);

Type_Signature* type_system_register_type(Type_System* system, Type_Signature signature);
Type_Signature* type_system_make_primitive(Type_System* system, Primitive_Type type, int size, bool is_signed);
Type_Signature* type_system_make_pointer(Type_System* system, Type_Signature* child_type);
Type_Signature* type_system_make_slice(Type_System* system, Type_Signature* element_type);
Type_Signature* type_system_make_array(Type_System* system, Type_Signature* element_type, bool count_known, int element_count);
Type_Signature* type_system_make_template(Type_System* system, String* id);
// Note: Takes ownership of parameter_types!
Type_Signature* type_system_make_function(Type_System* system, std::initializer_list<Function_Parameter> parameter_types, Type_Signature* return_type);
Type_Signature type_system_make_function_empty(Type_System* system);
void empty_function_add_parameter(Type_Signature* function_signature, String* name, Type_Signature* type);
Type_Signature* empty_function_finish(Type_System* system, Type_Signature function_signature, Type_Signature* return_type);
// Note: empty types need to be finished before they are used!
Type_Signature* type_system_make_enum_empty(Type_System* system, String* id);
Type_Signature* type_system_make_struct_empty(Type_System* system, Symbol* symbol, AST::Structure_Type struct_type, Struct_Progress* progress);
void struct_add_member(Type_Signature* struct_sig, String* id, Type_Signature* member_type);
void type_system_finish_type(Type_System* system, Type_Signature* type);

bool type_signature_equals(Type_Signature* a, Type_Signature* b);
void type_system_print(Type_System* system);
Optional<Enum_Item> enum_type_find_member_by_value(Type_Signature* enum_type, int value);
Optional<Struct_Member> type_signature_find_member_by_id(Type_Signature* type, String* id); // Valid for both structs, unions and slices
