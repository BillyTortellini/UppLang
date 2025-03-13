#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include <initializer_list>
#include "ast.hpp"
#include "constant_pool.hpp"
#include "../../utility/rich_text.hpp"

struct Symbol;
struct Datatype;
struct Datatype_Struct;
struct Datatype_Subtype;
struct String;
struct Workload_Structure_Body;
struct Workload_Structure_Polymorphic;
struct Function_Progress;
struct Poly_Value;
struct Datatype_Template;
struct Struct_Content;
struct Analysis_Pass;

namespace AST
{
    struct Expression;
};


// Helpers
struct Function_Parameter
{
    String* name;
    Datatype* type;

    // If the default value does not exist, boolean is set to false and the others are nullptr
    // If it exists, the value_expr or value_pass may still be null (In polymorphic function/on error)
    bool default_value_exists;
    AST::Expression* value_expr;
    Analysis_Pass* value_pass;
};
Function_Parameter function_parameter_make_empty();

struct Struct_Member
{
    Datatype* type;
    String* id;
    int offset; // Offset from base struct
    Struct_Content* content; // In which struct-content this member is defined
};

struct Enum_Member
{
    String* name;
    int value;
};




// TYPE SIGNATURES
struct Upp_Type_Handle
{
    u32 index;
};

// NOTE: This enum has to be in synch with type_info options, see add_predefined_types
enum class Datatype_Type
{
    PRIMITIVE = 1, // Int, float, bool, type_handle, address, isize, usize
    ARRAY, // Array with compile-time known size, like [5]int
    SLICE, // Pointer + size
    STRUCT,
    ENUM,
    FUNCTION,
    UNKNOWN_TYPE, // For error propagation

    // Modifier-Types
    POINTER,
    CONSTANT,
    OPTIONAL_TYPE,
    SUBTYPE,

    // Types for polymorphism
    TEMPLATE_TYPE,
    STRUCT_INSTANCE_TEMPLATE
};

struct Datatype_Memory_Info
{
    u64 size;
    int alignment;
    bool contains_padding_bytes;
    bool contains_reference;
    bool contains_function_pointer;
};

struct Named_Index
{
    String* name;
    int index;
};

struct Subtype_Index {
    Dynamic_Array<Named_Index> indices;
};

struct Type_Mods
{
    int pointer_level;
    u32 constant_flags; // Of pointers
    u32 optional_flags; // Of pointers
    bool is_constant;
    Subtype_Index* subtype_index;
};

struct Datatype
{
    Datatype_Type type;
    Upp_Type_Handle type_handle;

    // For some types (e.g. structs, arrays, etc), the memory info isn't always available after the type has been created
    Optional<Datatype_Memory_Info> memory_info;
    Workload_Structure_Body* memory_info_workload;
    bool contains_template;

    // Some cached values so we don't have to always walk the type tree
    Datatype* base_type;
    Type_Mods mods; // These are the modifiers which when applied to the base_type gets us this type
};

enum class Primitive_Class
{
    INTEGER = 1,
    FLOAT,
    BOOLEAN,
    ADDRESS,
    TYPE_HANDLE
};

enum class Primitive_Type
{
    // Basic integers
    I8 = 1,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,

    // 'Integer-Aliases'
    ADDRESS,
    ISIZE,
    USIZE,
    TYPE_HANDLE, // 32-bit unsigned int
    C_CHAR,      // Exists for c-strings? Because c differentiates 3! different char types, signed, unsigned and other

    // Floats
    F32,
    F64,

    // Boolean
    BOOLEAN
};

struct Datatype_Primitive
{
    Datatype base;
    // Note: Size of primitive (e.g. 16 or 32 bit) is determined by type size, e.g. base.size
    Primitive_Type primitive_type;
    Primitive_Class primitive_class;
    bool is_signed; // True for all non-unsigned integers, on c_char this is set to false
};

struct Datatype_Array
{
    Datatype base;
    Datatype* element_type;

    bool count_known; // False in case of polymorphism (Comptime values) or when Errors occured
    u64 element_count;

    Datatype_Template* polymorphic_count_variable; // May be null if it doesn't exist
};

struct Datatype_Slice {
    Datatype base;
    Datatype* element_type;
    Struct_Member data_member; // This may be problematic, as struct member doesn't have pointer to constant
    Struct_Member size_member;
};

struct Datatype_Pointer 
{
    Datatype base;
    Datatype* element_type;
    bool is_optional;
};

struct Datatype_Constant
{
    Datatype base;
    Datatype* element_type;
};

struct Datatype_Function
{
    Datatype base;
    Dynamic_Array<Function_Parameter> parameters;
    Optional<Datatype*> return_type;
    bool is_optional;
    Datatype_Function* non_optional_type;
};

struct Datatype_Subtype
{
    Datatype base;
    Datatype* base_type;
    String* subtype_name;
    int subtype_index;
};

struct Datatype_Optional
{
    Datatype base;
    Datatype* child_type;
    Struct_Member value_member;
    Struct_Member is_available_member;
};

struct Struct_Content
{
    // Info
    Datatype_Struct* structure;
    Subtype_Index* index; // Contains name + index in parent
    String* name; // For base struct this is the struct-name, otherwise subtype names/ids

    // Content
    Dynamic_Array<Struct_Member> members;
    Dynamic_Array<Struct_Content*> subtypes;
    Struct_Member tag_member; // Only valid if subtypes aren't empty
    int max_alignment; // Largest alignment of all members/subtype-members
};

struct Datatype_Struct 
{
    Datatype base;
    AST::Structure_Type struct_type;
    Struct_Content content;

    Workload_Structure_Body* workload; // May be null if it's a predefined struct
    Dynamic_Array<Datatype*> types_waiting_for_size_finish; // May contain arrays, constant types or struct_subtypes
    bool is_extern_struct; // C-Generator needs to know if the type is already defined in a header
};

struct Datatype_Enum
{
    Datatype base;
    Dynamic_Array<Enum_Member> members;
    String* name;

    bool values_are_sequential;
    int sequence_start_value; // Usually 1
};

struct Datatype_Template
{
    Datatype base;
    Symbol* symbol;
    int value_access_index;
    int defined_in_parameter_index;

    bool is_reference;
    Datatype_Template* mirrored_type; // Pointer to either the reference type or the "base" polymorphic-type
};

struct Datatype_Struct_Instance_Template
{
    Datatype base;
    Workload_Structure_Polymorphic* struct_base;
    Array<Poly_Value> instance_values; // These need to be stored somewhere else now...
};

struct Datatype_Format
{
    int highlight_parameter_index; // -1 If invalid
    vec3 highlight_color;
    bool remove_const_from_function_params;
    bool append_struct_poly_parameter_values;
};
Datatype_Format datatype_format_make_default();

void datatype_append_to_rich_text(Datatype* type, Type_System* type_system, Rich_Text::Rich_Text* text, Datatype_Format format = datatype_format_make_default());
void datatype_append_to_string(String* string, Type_System* type_system, Datatype* signature, Datatype_Format format = datatype_format_make_default());
void datatype_append_value_to_string(Datatype* type, Type_System* type_system, byte* value_ptr, String* string);



// C++ TYPE REPRESENTATIONS
// An array as it is currently defined in the upp-language
template<typename T>
struct Upp_Slice
{
    T* data;
    u64 size;
};

struct Upp_Slice_Base
{
    void* data;
    u64 size;
};

// A c_string as it is currently defined in the upp-language
// The size of the bytes don't include the null-terminator, which is still expected
struct Upp_C_String {
    Upp_Slice<const u8> bytes;
};

struct Upp_Any
{
    void* data;
    Upp_Type_Handle type;
};

struct Upp_Allocator
{
    i64 allocate_fn_index_plus_one;
    i64 free_fn_index_plus_one;
    i64 resize_fn_index_plus_one;
};


// TYPE-INFORMATION STRUCTS (Usable in Upp)
struct Internal_Type_Primitive
{
    Primitive_Type type;
};

struct Internal_Type_Function
{
    Upp_Slice<Upp_Type_Handle> parameters;
    Upp_Type_Handle return_type;
    bool has_return_type;
};

struct Internal_Type_Struct_Member
{
    Upp_C_String name;
    Upp_Type_Handle type;
    int offset;
};

struct Internal_Type_Subtype
{
    Upp_Type_Handle type;
    Upp_C_String subtype_name;
    int subtype_index;
};

struct Internal_Type_Struct_Content
{
    Upp_Slice<Internal_Type_Struct_Member> members;
    Upp_Slice<Internal_Type_Struct_Content> subtypes;
    Internal_Type_Struct_Member tag_member;
    Upp_C_String name;
};

struct Internal_Type_Struct
{
    Internal_Type_Struct_Content content;
    bool is_union;
};

struct Internal_Type_Enum_Member
{
    Upp_C_String name;
    int value;
};

struct Internal_Type_Array
{
    Upp_Type_Handle element_type;
    int size;
};

struct Internal_Type_Slice
{
    Upp_Type_Handle element_type;
};

struct Internal_Type_Enum
{
    Upp_Slice<Internal_Type_Enum_Member> members;
    Upp_C_String name;
};

struct Internal_Type_Pointer
{
    Upp_Type_Handle child_type;
    bool is_optional;
};

struct Internal_Type_Optional
{
    Upp_Type_Handle child_type;
    int available_offset;
};

struct Internal_Type_Information
{
    Upp_Type_Handle type_handle;
    int size;
    int alignment;

    union {
        Internal_Type_Pointer pointer;
        Internal_Type_Optional optional;
        Internal_Type_Array array;
        Upp_Type_Handle constant;
        Internal_Type_Slice slice;
        Internal_Type_Primitive primitive;
        Internal_Type_Function function;
        Internal_Type_Struct structure;
        Internal_Type_Enum enumeration;
        Internal_Type_Subtype struct_subtype;
    } options;
    Datatype_Type tag;
};



enum class Type_Deduplication_Type
{
    POINTER,
    CONSTANT,
    SLICE,
    ARRAY,
    FUNCTION,
    SUBTYPE,
    OPTIONAL
};

struct Type_Deduplication
{
    Type_Deduplication_Type type;
    union
    {
        Datatype* non_constant_type;
        Datatype* slice_element_type;
        Datatype* optional_child_type;
        struct {
            Datatype* child_type;
            bool is_optional;
        } pointer;
        struct {
            Datatype* base_type;
            String* name;
            int index;
        } subtype;
        struct {
            Datatype* element_type;
            bool size_known;
            int element_count;
            Datatype_Template* polymorphic_count_variable; // May be null if it doesn't exist
        } array_type;
        struct {
            Dynamic_Array<Function_Parameter> parameters;
            Datatype* return_type;
        } function;
    } options;
};

struct Predefined_Types
{
    // Primitive types
    Datatype_Primitive* i8_type;
    Datatype_Primitive* i16_type;
    Datatype_Primitive* i32_type;
    Datatype_Primitive* i64_type;
    Datatype_Primitive* u8_type;
    Datatype_Primitive* u16_type;
    Datatype_Primitive* u32_type;
    Datatype_Primitive* u64_type;

    Datatype_Primitive* f32_type;
    Datatype_Primitive* f64_type;

    Datatype_Primitive* c_char;
    Datatype_Primitive* address;
    Datatype_Primitive* isize;
    Datatype_Primitive* usize;
    Datatype_Primitive* bool_type;
    Datatype*           type_handle;

    // Prebuilt structs/types used by compiler
    Datatype* c_string;
    Datatype* unknown_type;
    Datatype_Struct* any_type;
    Datatype_Struct* type_information_type;
    Datatype_Struct* internal_struct_content_type;
    Datatype_Struct* internal_member_info_type;
    Datatype_Struct* empty_struct_type; // Required for now 

    Datatype_Enum* cast_mode;
    Datatype_Enum* cast_option;
    Datatype_Enum* primitive_type_enum;

    Datatype_Struct* allocator;
    Datatype_Function* allocate_function;
    Datatype_Function* free_function;
    Datatype_Function* resize_function;

    Datatype_Function* hardcoded_system_alloc;
    Datatype_Function* hardcoded_system_free;

    Datatype_Function* type_memory_copy;
    Datatype_Function* type_memory_zero;
    Datatype_Function* type_memory_compare;

    Datatype_Function* type_assert;
    Datatype_Function* type_type_of;
    Datatype_Function* type_type_info;
    Datatype_Function* type_size_of;
    Datatype_Function* type_align_of;
    Datatype_Function* type_panic;

    Datatype_Function* type_print_bool;
    Datatype_Function* type_print_i32;
    Datatype_Function* type_print_f32;
    Datatype_Function* type_print_line;
    Datatype_Function* type_print_string;
    Datatype_Function* type_read_i32;
    Datatype_Function* type_read_f32;
    Datatype_Function* type_read_bool;
    Datatype_Function* type_random_i32;

    Datatype_Function* type_bitwise_unop;
    Datatype_Function* type_bitwise_binop;

    Datatype_Function* type_set_cast_option;
    Datatype_Function* type_add_binop;
    Datatype_Function* type_add_unop;
    Datatype_Function* type_add_cast;
    Datatype_Function* type_add_array_access;
    Datatype_Function* type_add_dotcall;
    Datatype_Function* type_add_iterator;
};

// TYPE SYSTEM
struct Type_System
{
    double register_time;
    Predefined_Types predefined_types;

    Hashtable<Type_Deduplication, Datatype*> deduplication_table;
    Hashset<Subtype_Index*> subtype_index_deduplication;
    Dynamic_Array<Datatype*> types;
    Dynamic_Array<Internal_Type_Information*> internal_type_infos;

    Subtype_Index subtype_base_index;
};

Type_System type_system_create();
void type_system_destroy(Type_System* system);
void type_system_reset(Type_System* system);
void type_system_print(Type_System* system);
void type_system_add_predefined_types(Type_System* system);

Datatype_Template* type_system_make_template_type(Symbol* symbol, int value_access_index, int defined_in_parameter_index);
Datatype_Struct_Instance_Template* type_system_make_struct_instance_template(Workload_Structure_Polymorphic* base, Array<Poly_Value> instance_values);
Datatype_Pointer* type_system_make_pointer(Datatype* child_type, bool is_optional = false);
Datatype_Slice* type_system_make_slice(Datatype* element_type);
// If the element_type is constant, the array type + the element_type will be const
Datatype* type_system_make_array(Datatype* element_type, bool count_known, int element_count, Datatype_Template* polymorphic_count_variable = 0);
Datatype* type_system_make_constant(Datatype* datatype);
Datatype_Optional* type_system_make_optional(Datatype* datatype);
Datatype* type_system_make_subtype(Datatype* datatype, String* subtype_name, int subtype_index); // Creating a subtype of a constant creates a constant subtype
Datatype* type_system_make_type_with_mods(Datatype* base_type, Type_Mods mods);

// Note: Takes ownership of parameters (Or deletes them if type deduplication kicked in)
Datatype_Function* type_system_make_function(Dynamic_Array<Function_Parameter> parameters, Datatype* return_type = 0); 
Datatype_Function* type_system_make_function(std::initializer_list<Function_Parameter> parameter_types, Datatype* return_type = 0);
Datatype_Function* type_system_make_function_optional(Datatype_Function* function);

// Note: empty types need to be finished before they are used!
Datatype_Enum* type_system_make_enum_empty(String* name);
Datatype_Struct* type_system_make_struct_empty(AST::Structure_Type struct_type, String* name, Workload_Structure_Body* workload = 0);
void struct_add_member(Struct_Content* content, String* id, Datatype* member_type);
Struct_Content* struct_add_subtype(Struct_Content* content, String* id);
Struct_Content* struct_content_get_parent(Struct_Content* content); // Returns 0 if it's base-content
void type_system_finish_struct(Datatype_Struct* structure);
void type_system_finish_enum(Datatype_Enum* enum_type);
void type_system_finish_array(Datatype_Array* array);



bool types_are_equal(Datatype* a, Datatype* b);
bool datatype_is_unknown(Datatype* a);
bool type_size_is_unfinished(Datatype* a);
Optional<Enum_Member> enum_type_find_member_by_value(Datatype_Enum* enum_type, int value);
Datatype* datatype_get_non_const_type(Datatype* datatype);
bool type_mods_pointer_is_constant(Type_Mods mods, int pointer_level);
bool type_mods_pointer_is_optional(Type_Mods mods, int pointer_level);
bool type_mods_are_equal(const Type_Mods& a, const Type_Mods& b);
Struct_Content* type_mods_get_subtype(Datatype_Struct* structure, Type_Mods mods, int max_level = -1);
Subtype_Index* subtype_index_make(Dynamic_Array<Named_Index> indices); // Takes ownership of indices
Subtype_Index* subtype_index_make_subtype(Subtype_Index* base_index, String* name, int subtype_index);
Type_Mods type_mods_make(bool is_constant, int pointer_level, u32 const_flags, u32 optional_flags, Subtype_Index* subtype = 0);
bool datatype_is_pointer(Datatype* datatype, bool* out_is_optional = nullptr);
Upp_C_String upp_c_string_from_id(String* id);
Upp_C_String upp_c_string_empty();


// Casting functions
inline Datatype* upcast(Datatype* value)           { return value; }
inline Datatype* upcast(Datatype_Optional* value)  { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Function* value)  { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Struct* value)    { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Enum* value)      { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Array* value)     { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Slice* value)     { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Primitive* value) { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Pointer* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Template* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Struct_Instance_Template* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Constant* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Subtype* value)   { return (Datatype*)value; }

inline Datatype_Type get_datatype_type(Datatype_Optional* unused) { return Datatype_Type::OPTIONAL_TYPE; }
inline Datatype_Type get_datatype_type(Datatype_Struct* unused) { return Datatype_Type::STRUCT; }
inline Datatype_Type get_datatype_type(Datatype_Function* unused) { return Datatype_Type::FUNCTION; }
inline Datatype_Type get_datatype_type(Datatype_Enum* unused) { return Datatype_Type::ENUM; }
inline Datatype_Type get_datatype_type(Datatype_Array* unused) { return Datatype_Type::ARRAY; }
inline Datatype_Type get_datatype_type(Datatype_Slice* unused) { return Datatype_Type::SLICE; }
inline Datatype_Type get_datatype_type(Datatype_Primitive* unused) { return Datatype_Type::PRIMITIVE; }
inline Datatype_Type get_datatype_type(Datatype_Pointer* unused) { return Datatype_Type::POINTER; }
inline Datatype_Type get_datatype_type(Datatype_Template* unused) { return Datatype_Type::TEMPLATE_TYPE; }
inline Datatype_Type get_datatype_type(Datatype_Struct_Instance_Template* base) { return Datatype_Type::STRUCT_INSTANCE_TEMPLATE; }
inline Datatype_Type get_datatype_type(Datatype_Constant* base) { return Datatype_Type::CONSTANT; }
inline Datatype_Type get_datatype_type(Datatype_Subtype* base) { return Datatype_Type::SUBTYPE; }
inline Datatype_Type get_datatype_type(Datatype* base) { return base->type; }

template<typename T>
T* downcast(Datatype* base) { 
    T empty;
    assert(get_datatype_type(&empty) == base->type, "Downcast failed!");
    return (T*)base;
}

