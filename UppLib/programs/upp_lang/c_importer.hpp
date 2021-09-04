#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../utility/utils.hpp"

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
    VOID_TYPE,
    BOOL,
};

enum class C_Type_Qualifiers
{
    UNSIGNED = 1,
    SIGNED = 2,
    VOLATILE = 4,
    RESTRICT = 8,
    CONST_QUAL = 16,
    ATOMIC = 32
};

struct C_Import_Type;
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
    bool contains_bitfield;
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
    ERROR_TYPE,
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

struct Identifier_Pool;
struct C_Importer
{
    Identifier_Pool* identifier_pool;
    Hashtable<String, C_Import_Package> cache;
};

C_Importer c_importer_create(Identifier_Pool* pool);
void c_importer_destroy(C_Importer* importer);
Optional<C_Import_Package> c_importer_import_header(C_Importer* importer, String header_name);


