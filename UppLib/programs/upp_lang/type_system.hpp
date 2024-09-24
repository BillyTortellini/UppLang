#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include <initializer_list>
#include "ast.hpp"
#include "constant_pool.hpp"

struct Symbol;
struct Timer;
struct Datatype;
struct Datatype_Struct;
struct Datatype_Subtype;
struct String;
struct Workload_Structure_Body;
struct Workload_Structure_Polymorphic;
struct Function_Progress;
struct Polymorphic_Value;
struct Datatype_Template_Parameter;
struct Struct_Content;

// Helpers
struct Function_Parameter
{
    String* name;
    Datatype* type;

    // A default value may or may not exist, and if it exists it may not be available
    bool default_value_exists;
    Optional<Upp_Constant> default_value_opt;
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
    PRIMITIVE = 1, // Int, float, bool
    ARRAY, // Array with compile-time known size, like [5]int
    SLICE, // Pointer + size
    POINTER,
    STRUCT,
    ENUM,
    FUNCTION,
    SUBTYPE,
    TYPE_HANDLE,
    BYTE_POINTER, // Same as void* in C++
    CONSTANT,
    UNKNOWN_TYPE, // For error propagation

    // Types for polymorphism
    TEMPLATE_PARAMETER,
    STRUCT_INSTANCE_TEMPLATE
};

struct Datatype_Memory_Info
{
    int size;
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
    u32 constant_flags;
    int pointer_level;
    Subtype_Index* subtype_index;
};

struct Datatype
{
    Datatype_Type type;
    Upp_Type_Handle type_handle;

    // For some types (e.g. structs, arrays, etc), the memory info isn't always available after the type has been created
    Optional<Datatype_Memory_Info> memory_info;
    Workload_Structure_Body* memory_info_workload;
    bool contains_type_template;

    // Some cached values so we don't have to always walk the type tree
    Datatype* base_type;
    Type_Mods mods; // These are the modifiers which when applied to the base_type gets us this type
};

enum class Primitive_Type
{
    INTEGER = 1,
    FLOAT = 2,
    BOOLEAN = 3
};

struct Datatype_Primitive
{
    Datatype base;
    // Note: Size of primitive (e.g. 16 or 32 bit) is determined by type size, e.g. base.size
    Primitive_Type primitive_type;
    bool is_signed; // Only valid for integers
};

struct Datatype_Array
{
    Datatype base;
    Datatype* element_type;

    bool count_known; // False in case of polymorphism (Comptime values) or when Errors occured
    int element_count;

    Datatype_Template_Parameter* polymorphic_count_variable; // May be null if it doesn't exist
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
};

struct Datatype_Constant
{
    Datatype base;
    Datatype* element_type;
};

struct Datatype_Function
{
    Datatype base;
    // If the datatype is constant, then the parameters are non-owning (See datatype_base_destroy)
    Dynamic_Array<Function_Parameter> parameters;
    Optional<Datatype*> return_type;

    int parameters_with_default_value_count;
};

struct Datatype_Subtype
{
    Datatype base;
    Datatype* base_type;
    String* subtype_name;
    int subtype_index;
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
};

struct Datatype_Enum
{
    Datatype base;
    Dynamic_Array<Enum_Member> members;
    String* name;

    bool values_are_sequential;
    int sequence_start_value; // Usually 1
};

struct Datatype_Template_Parameter
{
    Datatype base;
    Symbol* symbol;
    int value_access_index;
    int defined_in_parameter_index;

    bool is_reference;
    Datatype_Template_Parameter* mirrored_type; // Pointer to either the reference type or the "base" polymorphic-type
};

struct Datatype_Struct_Instance_Template
{
    Datatype base;
    Workload_Structure_Polymorphic* struct_base;
    Array<Polymorphic_Value> instance_values; // These need to be stored somewhere else now...
};

void datatype_append_to_string(String* string, Datatype* type);
void datatype_append_value_to_string(Datatype* type, byte* value_ptr, String* string);



// C++ TYPE REPRESENTATIONS
// An array as it is currently defined in the upp-language
template<typename T>
struct Upp_Slice
{
    T* data;
    i32 size;
};

struct Upp_Slice_Base
{
    void* data;
    i32 size;
};

// A string as it is currently defined in the upp-language
// Note: The size of the slice includes the 0 character, so it should be always > 0
//       This is done because delete on slices should be able to work with the size of the slice (e.g. memory size should match)
struct Upp_String {
    Upp_Slice<const u8> bytes;
};

struct Upp_Any
{
    void* data;
    Upp_Type_Handle type;
};

enum class Cast_Option
{
    INTEGER_SIZE_UPCAST = 1,
    INTEGER_SIZE_DOWNCAST,
    INTEGER_SIGNED_TO_UNSIGNED,
    INTEGER_UNSIGNED_TO_SIGNED,
    FLOAT_SIZE_UPCAST,
    FLOAT_SIZE_DOWNCAST,
    INT_TO_FLOAT,
    FLOAT_TO_INT,

    POINTER_TO_POINTER, // Includes casts between pointer, byte_pointer, function-pointer and u64 (But only with cast_pointer)
    TO_BYTE_POINTER, // Does not affect cast_pointer
    FROM_BYTE_POINTER, // Does not affect cast_pointer
    POINTER_NULL_CHECK,

    TO_ANY,
    FROM_ANY,
    ENUM_TO_INT,
    INT_TO_ENUM,
    ARRAY_TO_SLICE,
    TO_SUBTYPE,

    MAX_ENUM_VALUE
};


// TYPE-INFORMATION STRUCTS (Usable in Upp)
struct Internal_Type_Primitive
{
    bool is_signed;
    Primitive_Type tag;
};

struct Internal_Type_Function
{
    Upp_Slice<Upp_Type_Handle> parameters;
    Upp_Type_Handle return_type;
    bool has_return_type;
};

struct Internal_Type_Struct_Member
{
    Upp_String name;
    Upp_Type_Handle type;
    int offset;
};

struct Internal_Type_Subtype
{
    Upp_Type_Handle type;
    Upp_String subtype_name;
    int subtype_index;
};

struct Internal_Type_Struct_Content
{
    Upp_Slice<Internal_Type_Struct_Member> members;
    Upp_Slice<Internal_Type_Struct_Content> subtypes;
    Internal_Type_Struct_Member tag_member;
    Upp_String name;
};

struct Internal_Type_Struct
{
    Internal_Type_Struct_Content content;
    bool is_union;
};

struct Internal_Type_Enum_Member
{
    Upp_String name;
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
    Upp_String name;
};

struct Internal_Type_Information
{
    Upp_Type_Handle type_handle;
    int size;
    int alignment;

    union {
        Upp_Type_Handle pointer;
        Upp_Type_Handle constant;
        Internal_Type_Array array;
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
    SUBTYPE
};

struct Type_Deduplication
{
    Type_Deduplication_Type type;
    union
    {
        Datatype* non_constant_type;
        Datatype* pointer_element_type;
        Datatype* slice_element_type;
        struct {
            Datatype* base_type;
            String* name;
            int index;
        } subtype;
        struct {
            Datatype* element_type;
            bool size_known;
            int element_count;
            Datatype_Template_Parameter* polymorphic_count_variable; // May be null if it doesn't exist
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
    Datatype_Primitive* bool_type;
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

    // Prebuilt structs/types used by compiler
    Datatype* string;
    Datatype* unknown_type;
    Datatype* type_handle;
    Datatype* byte_pointer;
    Datatype_Struct* any_type;
    Datatype_Struct* type_information_type;
    Datatype_Struct* empty_struct_type; // Required for now 

    Datatype_Enum* cast_mode;
    Datatype_Enum* cast_option;

    // Types for built-in/hardcoded functions
    Datatype_Function* type_assert;
    Datatype_Function* type_free;
    Datatype_Function* type_malloc;
    Datatype_Function* type_type_of;
    Datatype_Function* type_type_info;
    Datatype_Function* type_print_bool;
    Datatype_Function* type_print_i32;
    Datatype_Function* type_print_f32;
    Datatype_Function* type_print_line;
    Datatype_Function* type_print_string;
    Datatype_Function* type_read_i32;
    Datatype_Function* type_read_f32;
    Datatype_Function* type_read_bool;
    Datatype_Function* type_random_i32;

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
    Timer* timer;
    double register_time;
    Predefined_Types predefined_types;

    Hashtable<Type_Deduplication, Datatype*> deduplication_table;
    Hashset<Subtype_Index*> subtype_index_deduplication;
    Dynamic_Array<Datatype*> types;
    Dynamic_Array<Internal_Type_Information*> internal_type_infos;

    Subtype_Index subtype_base_index;
};

Type_System type_system_create(Timer* timer);
void type_system_destroy(Type_System* system);
void type_system_reset(Type_System* system);
void type_system_print(Type_System* system);
void type_system_add_predefined_types(Type_System* system);

Datatype_Template_Parameter* type_system_make_template_parameter(Symbol* symbol, int value_access_index, int defined_in_parameter_index);
Datatype_Struct_Instance_Template* type_system_make_struct_instance_template(Workload_Structure_Polymorphic* base, Array<Polymorphic_Value> instance_values);
Datatype_Pointer* type_system_make_pointer(Datatype* child_type);
Datatype_Slice* type_system_make_slice(Datatype* element_type);
Datatype_Array* type_system_make_array(Datatype* element_type, bool count_known, int element_count, Datatype_Template_Parameter* polymorphic_count_variable = 0);
Datatype* type_system_make_constant(Datatype* datatype);
Datatype* type_system_make_subtype(Datatype* datatype, String* subtype_name, int subtype_index); // Creating a subtype of a constant creates a constant subtype
Datatype* type_system_make_type_with_mods(Datatype* base_type, Type_Mods mods);

// Note: Takes ownership of parameters (Or deletes them if type deduplication kicked in)
Datatype_Function* type_system_make_function(Dynamic_Array<Function_Parameter> parameters, Datatype* return_type = 0); 
Datatype_Function* type_system_make_function(std::initializer_list<Function_Parameter> parameter_types, Datatype* return_type = 0);

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
bool type_mods_is_constant(Type_Mods mods, int pointer_level);
Struct_Content* type_mods_get_subtype(Datatype_Struct* structure, Type_Mods mods, int max_level = -1);
Subtype_Index* subtype_index_make(Dynamic_Array<Named_Index> indices); // Takes ownership of indices
Subtype_Index* subtype_index_make_subtype(Subtype_Index* base_index, String* name, int subtype_index);
Type_Mods type_mods_make(int pointer_level, u32 const_flags, Subtype_Index* subtype = 0);
bool datatype_is_pointer(Datatype* datatype);


// Casting functions
inline Datatype* upcast(Datatype* value)      { return value; }
inline Datatype* upcast(Datatype_Function* value)  { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Struct* value)    { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Enum* value)      { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Array* value)     { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Slice* value)     { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Primitive* value) { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Pointer* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Template_Parameter* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Struct_Instance_Template* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Constant* value)   { return (Datatype*)value; }
inline Datatype* upcast(Datatype_Subtype* value)   { return (Datatype*)value; }

inline Datatype_Type get_datatype_type(Datatype_Struct* unused) { return Datatype_Type::STRUCT; }
inline Datatype_Type get_datatype_type(Datatype_Function* unused) { return Datatype_Type::FUNCTION; }
inline Datatype_Type get_datatype_type(Datatype_Enum* unused) { return Datatype_Type::ENUM; }
inline Datatype_Type get_datatype_type(Datatype_Array* unused) { return Datatype_Type::ARRAY; }
inline Datatype_Type get_datatype_type(Datatype_Slice* unused) { return Datatype_Type::SLICE; }
inline Datatype_Type get_datatype_type(Datatype_Primitive* unused) { return Datatype_Type::PRIMITIVE; }
inline Datatype_Type get_datatype_type(Datatype_Pointer* unused) { return Datatype_Type::POINTER; }
inline Datatype_Type get_datatype_type(Datatype_Template_Parameter* unused) { return Datatype_Type::TEMPLATE_PARAMETER; }
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

