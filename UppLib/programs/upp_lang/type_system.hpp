#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include <initializer_list>
#include "ast.hpp"

struct Symbol;
struct Timer;
struct Type_Base;
struct String;
struct Workload_Structure_Body;
struct Workload_Structure_Polymorphic;
struct Workload_Function_Parameter;
struct Function_Progress;

// Helpers
enum class Parameter_Type
{
    NORMAL,
    POLYMORPHIC
};

struct Function_Parameter
{
    Optional<String*> name;
    Type_Base* type;
    Workload_Function_Parameter* workload; // May be null for extern/hardcoded functions
    Symbol* symbol; // May be null for extern/hardcoded functions
    int index; // Index in parameter array of function signature

    // Note: It can be the case that a default value exists, but the upp_constant is not available because there was an error,
    //       and we need to be able to seperate these cases.
    bool has_default_value;
    Optional<Upp_Constant> default_value;

    // Polymorphic infos
    Parameter_Type parameter_type;
    bool has_dependencies_on_other_parameters;
    union {
        struct {
            int index_in_non_polymorphic_signature;
        } normal;
        int polymorphic_index;
    };
};
Function_Parameter function_parameter_make_empty(Symbol* symbol = 0, Workload_Function_Parameter* workload = 0);

struct Struct_Member
{
    Type_Base* type;
    int offset;
    String* id;
};

struct Enum_Item
{
    String* name;
    int value;
};




// TYPE SIGNATURES
struct Type_Handle
{
    u32 index;
};

enum class Type_Type
{
    VOID_POINTER = 1, // Note: Void type does not exist, but void pointers are here for interoperability with C
    TYPE_HANDLE,
    ERROR_TYPE,
    PRIMITIVE,
    POINTER,
    FUNCTION,
    STRUCT,
    ENUM,
    ARRAY, // Array with compile-time known size, like [5]int
    SLICE, // Array with dynamic size, []int
    POLYMORPHIC,
    STRUCT_INSTANCE_TEMPLATE
};

struct Type_Base
{
    Type_Type type;
    int size; // in byte
    int alignment; // in byte
    Type_Handle type_handle;
    bool contains_polymorphic_type; // Means the type-tree contains at least one polymorphic-type node...
};

struct Type_Polymorphic
{
    Type_Base base;
    Symbol* symbol;
    Workload_Function_Parameter* parameter_workload;
    int index;
    Type_Base* datatype; 

    bool is_reference;
    Type_Polymorphic* mirrored_type; // Pointer to either the reference type or the "base" polymorphic-type
};

enum class Matchable_Argument_Type
{
    POLYMORPHIC_SYMBOL,
    TYPE_CONTAINING_POLYMORPHIC,
    CONSTANT_VALUE,
};

struct Matchable_Argument
{
    // A matchable argument is either a type or a value
    Matchable_Argument_Type type;
    union {
        Type_Polymorphic* polymorphic_symbol;
        Type_Base* polymorphic_type;
        Upp_Constant constant;
    } options;
};

struct Type_Struct_Instance_Template
{
    Type_Base base;
    Workload_Structure_Polymorphic* struct_base;
    Array<Matchable_Argument> matchable_arguments;
};

enum class Primitive_Type
{
    INTEGER = 1,
    FLOAT = 2,
    BOOLEAN = 3
};

struct Type_Primitive
{
    Type_Base base;
    // Note: Size of primitive (e.g. 16 or 32 bit) is determined by type size, e.g. base.size
    Primitive_Type primitive_type;
    bool is_signed; // Only valid for integers
};

struct Type_Pointer
{
    Type_Base base;
    Type_Base* points_to_type;
};

struct Type_Array
{
    Type_Base base;
    Type_Base* element_type;
    bool count_known; // False in case of polymorphism(Comptime values) or when Errors occured
    int element_count;

    Type_Polymorphic* polymorphic_count_variable; // May be null if it doesn't exist
};

struct Type_Function
{
    Type_Base base;
    Dynamic_Array<Function_Parameter> parameters;
    Optional<Type_Base*> return_type;

    int parameters_with_default_value_count;
};

struct Type_Slice {
    Type_Base base;
    Type_Base* element_type;
    Struct_Member data_member;
    Struct_Member size_member;
};

struct Type_Struct {
    Type_Base base;
    Dynamic_Array<Struct_Member> members;
    AST::Structure_Type struct_type;
    Struct_Member tag_member; // Only valid for unions

    Optional<String*> name;
    Workload_Structure_Body* workload; // May be null if it's a predefined struct
};

struct Type_Enum
{
    Type_Base base;
    Dynamic_Array<Enum_Item> members;
    String* name;
};

void type_append_to_string(String* string, Type_Base* type);
void type_append_value_to_string(Type_Base* type, byte* value_ptr, String* string);


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
    void* data;
    i32 size;
};

// A string as it is currently defined in the upp-language
struct Upp_String
{
    Upp_Slice_Base character_buffer;
    i32 size;
};

struct Upp_Any
{
    void* data;
    Type_Handle type;
};



// TYPE-INFORMATION STRUCTS (Usable in Upp)
struct Internal_Type_Primitive
{
    bool is_signed;
    Primitive_Type tag;
};

struct Internal_Type_Function
{
    Upp_Slice<Type_Handle> parameters;
    Type_Handle return_type;
    bool has_return_type;
};

struct Internal_Type_Struct_Member
{
    Upp_String name;
    Type_Handle type;
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
    Type_Handle element_type;
    int size;
};

struct Internal_Type_Slice
{
    Type_Handle element_type;
};

struct Internal_Type_Enum
{
    Upp_Slice<Internal_Type_Enum_Member> members;
    Upp_String name;
};

struct Internal_Type_Info_Options
{
    union {
        struct {} type_type;
        Type_Handle pointer;
        Internal_Type_Array array;
        Internal_Type_Slice slice;
        Internal_Type_Primitive primitive;
        Internal_Type_Function function;
        Internal_Type_Struct structure;
        Internal_Type_Enum enumeration;
    };
    Type_Type tag;
};

struct Internal_Type_Information
{
    Type_Handle type_handle;
    int size;
    int alignment;
    Internal_Type_Info_Options options;
};




struct Predefined_Types
{
    Type_Base* void_pointer_type;
    Type_Base* error_type;
    Type_Base* type_handle;

    // Primitive types
    Type_Primitive* bool_type;
    Type_Primitive* i8_type;
    Type_Primitive* i16_type;
    Type_Primitive* i32_type;
    Type_Primitive* i64_type;
    Type_Primitive* u8_type;
    Type_Primitive* u16_type;
    Type_Primitive* u32_type;
    Type_Primitive* u64_type;
    Type_Primitive* f32_type;
    Type_Primitive* f64_type;

    // Prebuilt structs/types used by compiler
    Type_Struct* string_type;
    Type_Struct* empty_struct_type;
    Type_Struct* type_information_type;
    Type_Struct* any_type;

    // Types for built-in/hardcoded functions
    Type_Function* type_assert;
    Type_Function* type_free;
    Type_Function* type_malloc;
    Type_Function* type_type_of;
    Type_Function* type_type_info;
    Type_Function* type_print_bool;
    Type_Function* type_print_i32;
    Type_Function* type_print_f32;
    Type_Function* type_print_line;
    Type_Function* type_print_string;
    Type_Function* type_read_i32;
    Type_Function* type_read_f32;
    Type_Function* type_read_bool;
    Type_Function* type_random_i32;
};

// TYPE SYSTEM
struct Type_System
{
    Timer* timer;
    double register_time;
    Predefined_Types predefined_types;

    // Note: Both registered_types and types array contain the same types, I just keep the types array for convenience
    Hashset<Type_Base*> registered_types;
    Dynamic_Array<Type_Base*> types;
    Dynamic_Array<Internal_Type_Information*> internal_type_infos;
    u64 next_internal_index;
};

Type_System type_system_create(Timer* timer);
void type_system_destroy(Type_System* system);
void type_system_reset(Type_System* system);
void type_system_print(Type_System* system);
void type_system_add_predefined_types(Type_System* system);

Type_Polymorphic* type_system_make_polymorphic(Symbol* symbol, Workload_Function_Parameter* parameter_workload, int index);
Type_Struct_Instance_Template* type_system_make_struct_instance_template(
    Workload_Structure_Polymorphic* base, Array<Matchable_Argument> arguments);
Type_Pointer* type_system_make_pointer(Type_Base* child_type);
Type_Slice* type_system_make_slice(Type_Base* element_type);
Type_Array* type_system_make_array(Type_Base* element_type, bool count_known, int element_count);

// Note: Takes ownership of parameter_types!
Type_Function* type_system_make_function(Dynamic_Array<Function_Parameter> parameters, Type_Base* return_type = 0); // Takes ownership of parameters!
Type_Function* type_system_make_function(std::initializer_list<Function_Parameter> parameter_types, Type_Base* return_type = 0);
Type_Function type_system_make_function_empty();
Type_Function* type_system_finish_function(Type_Function function, Type_Base* return_type = 0);

// Note: empty types need to be finished before they are used!
Type_Enum* type_system_make_enum_empty(String* name);
Type_Struct* type_system_make_struct_empty(AST::Structure_Type struct_type, String* name = 0, Workload_Structure_Body* workload = 0);
void struct_add_member(Type_Struct* structure, String* id, Type_Base* member_type);
void type_system_finish_struct(Type_Struct* structure);
void type_system_finish_enum(Type_Enum* enum_type);
void type_system_finish_array(Type_Array* array);

bool types_are_equal(Type_Base* a, Type_Base* b);
bool type_is_unknown(Type_Base* a);
bool type_size_is_unfinished(Type_Base* a);
Optional<Enum_Item> enum_type_find_member_by_value(Type_Enum* enum_type, int value);
Optional<Struct_Member> type_signature_find_member_by_id(Type_Base* type, String* id);  // Valid for both structs, unions and slices



// Casting functions
inline Type_Base* upcast(Type_Base* value)      { return value; }
inline Type_Base* upcast(Type_Function* value)  { return (Type_Base*)value; }
inline Type_Base* upcast(Type_Struct* value)    { return (Type_Base*)value; }
inline Type_Base* upcast(Type_Enum* value)      { return (Type_Base*)value; }
inline Type_Base* upcast(Type_Array* value)     { return (Type_Base*)value; }
inline Type_Base* upcast(Type_Slice* value)     { return (Type_Base*)value; }
inline Type_Base* upcast(Type_Primitive* value) { return (Type_Base*)value; }
inline Type_Base* upcast(Type_Pointer* value)   { return (Type_Base*)value; }
inline Type_Base* upcast(Type_Polymorphic* value)   { return (Type_Base*)value; }
inline Type_Base* upcast(Type_Struct_Instance_Template* value)   { return (Type_Base*)value; }

inline Type_Type get_type_type(Type_Struct* unused) { return Type_Type::STRUCT; }
inline Type_Type get_type_type(Type_Function* unused) { return Type_Type::FUNCTION; }
inline Type_Type get_type_type(Type_Enum* unused) { return Type_Type::ENUM; }
inline Type_Type get_type_type(Type_Array* unused) { return Type_Type::ARRAY; }
inline Type_Type get_type_type(Type_Slice* unused) { return Type_Type::SLICE; }
inline Type_Type get_type_type(Type_Primitive* unused) { return Type_Type::PRIMITIVE; }
inline Type_Type get_type_type(Type_Pointer* unused) { return Type_Type::POINTER; }
inline Type_Type get_type_type(Type_Polymorphic* unused) { return Type_Type::POLYMORPHIC; }
inline Type_Type get_type_type(Type_Struct_Instance_Template* base) { return Type_Type::STRUCT_INSTANCE_TEMPLATE; }
inline Type_Type get_type_type(Type_Base* base) { return base->type; }

template<typename T>
T* downcast(Type_Base* base) { 
    T empty;
    assert(get_type_type(&empty) == base->type, "Downcast failed!");
    return (T*)base;
}

