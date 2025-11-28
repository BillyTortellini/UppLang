#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include <initializer_list>
#include "ast.hpp"
#include "constant_pool.hpp"
#include "memory_source.hpp"

struct Symbol;
struct Datatype;
struct Datatype_Struct;
struct Datatype_Subtype;
struct String;
struct Workload_Structure_Body;
struct Workload_Structure_Polymorphic;
struct Function_Progress;
struct Pattern_Variable_State;
struct Datatype_Pattern_Variable;
struct Datatype_Struct;
struct Analysis_Pass;
struct Pattern_Variable;
struct Poly_Instance;
struct Call_Signature;

namespace Rich_Text
{
    struct Rich_Text;
};

namespace AST
{
    struct Expression;
};


// Helpers
struct Struct_Member
{
    Datatype* type;
    String* id;
    int offset; // Offset from base struct
    Datatype_Struct* structure; // In which struct-content this member is defined, may be null for slices or other things
    AST::Node* definition_node; // May be null, used for goto-definition in editor
};

Struct_Member struct_member_make(Datatype* type, String* id, Datatype_Struct* structure, int offset, AST::Node* definition_node);

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
    FUNCTION_POINTER,
    UNKNOWN_TYPE, // For error propagation
    // Unlike unknown, invalid will always lead to errors 
    //   Created by expressions that don't really have a type, like Poly_Struct/Poly_Function/Nothing...
    INVALID_TYPE,

    // Modifier-Types
    POINTER,
    CONSTANT,
    OPTIONAL_TYPE,

    // Types for polymorphism
    PATTERN_VARIABLE,
    STRUCT_PATTERN
};

struct Datatype_Memory_Info
{
    u64 size;
    int alignment;
    bool contains_padding_bytes;
    bool contains_reference;
    bool contains_function_pointer;
};

struct Datatype
{
    Datatype_Type type;
    Upp_Type_Handle type_handle;

    // For some types (e.g. structs, arrays, etc), the memory info isn't always available after the type has been created
    Optional<Datatype_Memory_Info> memory_info;
    Workload_Structure_Body* memory_info_workload;

    // Some cached values so we don't have to always walk the type tree
    bool contains_pattern;
    bool contains_partial_pattern; // Patterns where variables are missing, e.g. Node(_)
    bool contains_pattern_variable_definition; // Pattern with $T, not just reference
};

enum class Primitive_Class
{
    INTEGER = 1,
    FLOAT,
    BOOLEAN,
    ADDRESS,
    TYPE_HANDLE,
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
    usize element_count;

    Datatype_Pattern_Variable* count_variable_type; // May be null if it doesn't exist
};

struct Datatype_Slice {
    Datatype base;
    Datatype* element_type;
    Struct_Member data_member; // This may be problematic, as struct member doesn't have pointer to struct-content
    Struct_Member size_member;
    Call_Signature* slice_initializer_signature_cached; // Is null until used
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

struct Datatype_Function_Pointer
{
    Datatype base;
    Call_Signature* signature;
    bool is_optional;
};

// Note: Datatype_Optional is seperate from optional-pointers, as pointer types have a is_optional boolean
struct Datatype_Optional
{
    Datatype base;
    Datatype* child_type;

    Struct_Member value_member;
    Struct_Member is_available_member;
};

struct Datatype_Struct 
{
    Datatype base;
    Dynamic_Array<Struct_Member> members;
    bool is_union;

    String* name; // For base struct this is the struct-name, otherwise subtype names/ids
    AST::Node* definition_node; // Null for pre-defined structs, used for Goto-Definition in Editor
    Call_Signature* initializer_signature_cached;

    Datatype_Struct* parent_struct;
    Dynamic_Array<Datatype_Struct*> subtypes;
    Struct_Member tag_member; // Only valid if subtypes aren't empty
    int subtype_index; // Index in parent struct, without the + 1 for tag...

    Workload_Structure_Body* workload; // May be null if it's a predefined struct
    Dynamic_Array<Datatype*> types_waiting_for_size_finish; // May contain arrays, constant types or struct_subtypes
    bool is_extern_struct; // C-Generator needs to know if the type is already defined in a header
};

struct Datatype_Enum
{
    Datatype base;
    Dynamic_Array<Enum_Member> members;
    String* name;
    AST::Node* definition_node;

    bool values_are_sequential;
    int sequence_start_value; // Usually 1
};

struct Datatype_Pattern_Variable
{
    Datatype base;
    Pattern_Variable* variable; // May be null if no variable is associated with the pattern

    bool is_reference;
    Datatype_Pattern_Variable* mirrored_type; // Pointer to either the reference type or the "base" variable
};

struct Datatype_Struct_Pattern
{
    Datatype base;
    Poly_Instance* instance;
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

struct Datatype_Value_Format
{
    bool single_line; // If structs etc should use lines and indentation, or comma seperated values
    bool show_datatype; // E.g. Node{15, 12} vs {15, 12}, or Color.RED vs .RED (enums)
    Datatype_Format datatype_format;
    bool show_member_names; // E.g. Node{value = 15, alive = 10) vs Node{15, 10}
    int max_array_display_size; // -1 to disable, limits arrays like int.[#20, 10, 20, 30, ...]
    bool follow_pointers; // 
    int max_indentation_before_single_line; // -1 to disable, otherwise we format as single line at a specific indentation
    int max_indentation; // Should always be used, as we could have recursive datastructures
    int indentation_spaces;
};
Datatype_Value_Format datatype_value_format_multi_line(int max_array_values, int max_indentation_before_single_line);
Datatype_Value_Format datatype_value_format_single_line();

// We have both local and pointer memory sources, to facilitate all use-cases
void datatype_append_value_to_string(
    Datatype* type, Type_System* type_system, byte* value_ptr, String* string, Datatype_Value_Format format,
    int indentation, Memory_Source local_memory, Memory_Source pointer_memory
);



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
    bool has_return_type; // If true, then it's the last type
};

struct Internal_Type_Struct_Member
{
    Upp_C_String name;
    Upp_Type_Handle type;
    int offset;
};

struct Internal_Type_Struct
{
    Upp_Slice<Internal_Type_Struct_Member> members;
    Upp_C_String name;
    bool is_union;

    Upp_Slice<Upp_Type_Handle> subtypes;
    Upp_Type_Handle parent_struct;
    Internal_Type_Struct_Member tag_member;
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
    } options;
    Datatype_Type tag;
};



enum class Type_Deduplication_Type
{
    POINTER,
    CONSTANT,
    SLICE,
    ARRAY,
    FUNCTION_POINTER,
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
            Datatype* element_type;
            bool size_known;
            int element_count;
            Datatype_Pattern_Variable* count_variable_type; // May be null if it doesn't exist
        } array_type;
        struct {
            Call_Signature* signature;
            bool is_optional;
        } function_pointer;
    } options;
};

struct Type_Modifier_Info
{
	Datatype* initial_type; // Queried type
	Datatype* base_type; // Dereferenced/optional base type
	Datatype_Struct* struct_subtype; // If it's a subtype or a struct at the core
	int pointer_level;
	u32 const_flags;
	u32 optional_flags;
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
    Datatype_Struct* bytes;
    Datatype* unknown_type;
    Datatype* invalid_type;
    Datatype_Struct* any_type;
    Datatype_Struct* type_information_type;
    Datatype_Struct* internal_struct_info_type;
    Datatype_Struct* internal_member_info_type;
    Datatype_Struct* internal_enum_member_info_type;

    Datatype_Struct* empty_struct_type; // Required for now 
    Datatype* empty_pattern_variable;

    Datatype_Enum* cast_type_enum;
    Datatype_Enum* primitive_type_enum;

    Datatype_Struct* allocator;
    Datatype_Function_Pointer* allocate_function;
    Datatype_Function_Pointer* free_function;
    Datatype_Function_Pointer* resize_function;
};

// TYPE SYSTEM
struct Type_System
{
    double register_time;
    Predefined_Types predefined_types;

    Hashtable<Type_Deduplication, Datatype*> deduplication_table;
    Dynamic_Array<Datatype*> types;
    Dynamic_Array<Internal_Type_Information*> internal_type_infos;
};

Type_System type_system_create();
void type_system_destroy(Type_System* system);
void type_system_reset(Type_System* system);
void type_system_print(Type_System* system);
void type_system_add_predefined_types(Type_System* system);

Datatype_Pattern_Variable* type_system_make_pattern_variable_type(Pattern_Variable* pattern_variable);
Datatype_Struct_Pattern* type_system_make_struct_pattern(
    Poly_Instance* instance, bool is_partial_pattern, bool contains_pattern_variable_definition);
Datatype_Pointer* type_system_make_pointer(Datatype* child_type, bool is_optional = false, Type_System* type_system = nullptr);
Datatype_Slice* type_system_make_slice(Datatype* element_type);
// If the element_type is constant, the array type + the element_type will be const
Datatype* type_system_make_array(Datatype* element_type, bool count_known, int element_count, Datatype_Pattern_Variable* count_variable_type = 0);
Datatype* type_system_make_constant(Datatype* datatype, Type_System* type_system = nullptr);
Datatype_Optional* type_system_make_optional(Datatype* datatype);
// Creating a subtype of a constant creates a constant subtype
Datatype_Function_Pointer* type_system_make_function_pointer(Call_Signature* signature, bool is_optional, Type_System* type_system = nullptr);

// Note: empty types need to be finished before they are used!
Datatype_Enum* type_system_make_enum_empty(String* name, AST::Node* definition_node = 0);
Datatype_Struct* type_system_make_struct_empty(String* name, bool is_union = false, Datatype_Struct* parent = 0, Workload_Structure_Body* workload = 0);
void struct_add_member(Datatype_Struct* structure, String* id, Datatype* member_type, AST::Node* definition_node = nullptr);
void type_system_finish_struct(Datatype_Struct* structure);
void type_system_finish_enum(Datatype_Enum* enum_type);
void type_system_finish_array(Datatype_Array* array);



bool types_are_equal(Datatype* a, Datatype* b);
bool datatype_is_unknown(Datatype* a);
bool type_size_is_unfinished(Datatype* a);
Optional<Enum_Member> enum_type_find_member_by_value(Datatype_Enum* enum_type, int value);
Datatype* datatype_get_non_const_type(Datatype* datatype, bool* out_was_const = nullptr);
Datatype* datatype_get_undecorated(
    Datatype* datatype, bool remove_pointer = true, bool remove_subtype = true, bool remove_const = true, 
    bool remove_optional_pointer = false, bool remove_optional = false
);
bool datatype_is_pointer(Datatype* datatype, bool* out_is_optional = nullptr);
Type_Modifier_Info datatype_get_modifier_info(Datatype* datatype);
Upp_C_String upp_c_string_from_id(String* id);
Upp_C_String upp_c_string_empty();



// Casting functions
inline Datatype* upcast(Datatype* value)           { return value; }
inline Datatype* upcast(Datatype_Optional* value)  { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Function_Pointer* value)  { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Struct* value)    { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Enum* value)      { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Array* value)     { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Slice* value)     { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Primitive* value) { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Pointer* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Pattern_Variable* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Struct_Pattern* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Constant* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Subtype* value)   { return (Datatype*)value; }

inline Datatype_Type get_datatype_type(Datatype_Optional* unused) { return Datatype_Type::OPTIONAL_TYPE; }
inline Datatype_Type get_datatype_type(Datatype_Struct* unused) { return Datatype_Type::STRUCT; }
inline Datatype_Type get_datatype_type(Datatype_Function_Pointer* unused) { return Datatype_Type::FUNCTION_POINTER; }
inline Datatype_Type get_datatype_type(Datatype_Enum* unused) { return Datatype_Type::ENUM; }
inline Datatype_Type get_datatype_type(Datatype_Array* unused) { return Datatype_Type::ARRAY; }
inline Datatype_Type get_datatype_type(Datatype_Slice* unused) { return Datatype_Type::SLICE; }
inline Datatype_Type get_datatype_type(Datatype_Primitive* unused) { return Datatype_Type::PRIMITIVE; }
inline Datatype_Type get_datatype_type(Datatype_Pointer* unused) { return Datatype_Type::POINTER; }
inline Datatype_Type get_datatype_type(Datatype_Pattern_Variable* unused) { return Datatype_Type::PATTERN_VARIABLE; }
inline Datatype_Type get_datatype_type(Datatype_Struct_Pattern* base) { return Datatype_Type::STRUCT_PATTERN; }
inline Datatype_Type get_datatype_type(Datatype_Constant* base) { return Datatype_Type::CONSTANT; }
inline Datatype_Type get_datatype_type(Datatype* base) { return base->type; }

template<typename T>
T* downcast(Datatype* base) { 
    T empty;
    assert(get_datatype_type(&empty) == base->type, "Downcast failed!");
    return (T*)base;
}

