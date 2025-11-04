#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "../../win32/process.hpp"
#include "../../win32/thread.hpp"
#include "constant_pool.hpp"

struct Datatype;
struct String;
struct Type_System;
struct Module_Progress;
struct ModTree_Function;
struct Datatype_Struct;
struct Datatype_Primitive;
struct Datatype_Struct_Pattern;
struct Datatype_Slice;
struct Poly_Header;
struct Function_Progress;
struct Source_Code;
struct Analysis_Pass;
struct Workload_Structure_Polymorphic;
struct Datatype_Function_Pointer;
struct Struct_Content;
struct Call_Signature;
namespace AST {
	struct Module;
	struct Expression;
	struct Call_Node;
}

// Note:
// Cast type records all cast where 'an actual operation' happens, e.g. new values are created
// The type of the result may still change, even if there is no cast involved,
// which happens with auto-dereference, subtype-changes, to/from optional pointer and const-changes
// To get a full picture of all performed operations the Expression_Cast_Info struct is required
enum class Cast_Type
{
    INTEGERS,
    FLOATS,
    FLOAT_TO_INT,
    INT_TO_FLOAT,
    POINTERS,
    POINTER_TO_ADDRESS,
    ADDRESS_TO_POINTER,
    ENUM_TO_INT,
    INT_TO_ENUM,
    ARRAY_TO_SLICE, 
    TO_ANY,
    FROM_ANY,
    TO_OPTIONAL,
    CUSTOM_CAST, 

    NO_CAST, // No cast needed, source-type == destination-type
    UNKNOWN, // Either source or destination type are/contain error/unknown type
    INVALID, // Cast is not valid
};
const char* cast_type_to_string(Cast_Type type);

enum class Cast_Mode
{
    NONE = 1,
    EXPLICIT, // cast{u64} i
    INFERRED, // cast i
    POINTER_EXPLICIT, // cast_pointer{*int} ip
    POINTER_INFERRED, // cast_pointer ip
    IMPLICIT, // x: u32 = i
    
    MAX_ENUM_VALUE
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

	POINTER_TO_POINTER, // Includes casts between pointer, function-pointer and address (But only with cast_pointer)
	TO_BYTE_POINTER, // Does not affect cast_pointer
	FROM_BYTE_POINTER, // Does not affect cast_pointer

	TO_ANY,
	FROM_ANY,
	ENUM_TO_INT,
	INT_TO_ENUM,
	ARRAY_TO_SLICE,
	TO_SUBTYPE,
	TO_OPTIONAL,

	MAX_ENUM_VALUE
};

enum class Context_Change_Type
{
	BINARY_OPERATOR,
	UNARY_OPERATOR,
	ARRAY_ACCESS,
	CAST,
	DOT_CALL,
	ITERATOR,
	CAST_OPTION,
	IMPORT,
	INVALID, // Not a valid context change
	MAX_ENUM_VALUE
};

enum class Timing_Task
{
	LEXING,
	PARSING,
	ANALYSIS,
	CODE_GEN,
	RESET,
	CODE_EXEC,
	OUTPUT,
	FINISH,
};
const char* timing_task_to_string(Timing_Task task);

enum class Extern_Compiler_Setting
{
	LIBRARY,           // .lib filename
	LIBRARY_DIRECTORY, // Search path for lib files
	HEADER_FILE,       // Header file to include (should contain extern function + extern struct definitions)
	INCLUDE_DIRECTORY, // Directory for C-Compiler to search for header files
	SOURCE_FILE,       // .cpp file for compiler
	DEFINITION,        // Definitions to use before any header includes (e.g. #define _DEBUG)

	MAX_ENUM_VALUE
};

enum class Hardcoded_Type
{
	TYPE_OF,
	TYPE_INFO,
	ASSERT_FN,
	SIZE_OF,
	ALIGN_OF,
	PANIC_FN,
	RETURN_TYPE,
	STRUCT_TAG,

	MEMORY_COPY,
	MEMORY_ZERO,
	MEMORY_COMPARE,

	SYSTEM_ALLOC,
	SYSTEM_FREE,

	BITWISE_NOT,
	BITWISE_AND,
	BITWISE_OR,
	BITWISE_XOR,
	BITWISE_SHIFT_LEFT,
	BITWISE_SHIFT_RIGHT,

	PRINT_I32,
	PRINT_F32,
	PRINT_BOOL,
	PRINT_LINE,
	PRINT_STRING,
	READ_I32,
	READ_F32,
	READ_BOOL,
	RANDOM_I32,

	MAX_ENUM_VALUE
};
void hardcoded_type_append_to_string(String* string, Hardcoded_Type hardcoded);

enum class Member_Access_Type
{
	STRUCT_MEMBER_ACCESS, // Includes subtype and tag access
	STRUCT_POLYMORHPIC_PARAMETER_ACCESS,
	ENUM_MEMBER_ACCESS,
	DOT_CALL_AS_MEMBER,
	DOT_CALL,
	STRUCT_SUBTYPE, // Generates a type, e.g. x: Node.Expression
	STRUCT_UP_OR_DOWNCAST, // a: Node, a.Expression.something --> The .Expression is a downcast
};



enum class Exit_Code_Type
{
	SUCCESS,
	COMPILATION_FAILED, // Code did not because there were compile errors
	CODE_ERROR,
	EXECUTION_ERROR, // Stack overflow, return value overflow, instruction limit reached
	INSTRUCTION_LIMIT_REACHED,
	TYPE_INFO_WAITING_FOR_TYPE_FINISHED,

	MAX_ENUM_VALUE
};
const char* exit_code_type_as_string(Exit_Code_Type type);

struct Exit_Code
{
	Exit_Code_Type type;
	const char* error_msg; // May be null
};

Exit_Code exit_code_make(Exit_Code_Type type, const char* error_msg = 0);
void exit_code_append_to_string(String* string, Exit_Code code);

struct Poly_Function
{
	Poly_Header* poly_header;
	Function_Progress* base_progress;
};





// CALLABLES AND PARAMETERS
struct Call_Parameter
{
    String* name;
    Datatype* datatype;
    bool required;                      // Default values and implicit parameters don't require values
    bool requires_named_addressing;     // Implicit arguments and #instanciate, #get_overload?
    bool must_not_be_set;               // #instanciate must not set specific normal arguments

    // Polymorphic infos
    int comptime_variable_index;              // -1, otherwise this is a comptime parameter
	int partial_pattern_index;                // Parameters with datatype->pattern_contains_missing_parameter are indexed
	Dynamic_Array<int> dependencies;          // Indices to pattern-variables
    bool contains_pattern_variable_definition;

    // Polymorphic dependency infos
    // Note: this will change once we have smarter stuff here
    // If the default value does not exist, boolean is set to false and the others are nullptr
    // If it exists, the value_expr or value_pass may still be null (In polymorphic function/on error)
    bool default_value_exists;
    AST::Expression* default_value_expr;
    Analysis_Pass* default_value_pass;
};

struct Call_Signature
{
    Dynamic_Array<Call_Parameter> parameters;
    // Return type of functions/poly-functions is stored as one of the parameters
    //  or -1 if no return type exists
    int return_type_index; 

    bool is_registered; // For debugging/deduplication

    Optional<Datatype*> return_type();
};

// Note:
// Callable_Type is mostly used for convenience when generating code, e.g. in IR-Generator.
// This is why we store struct/union/slice initializer seperately, even though the result-type could be used to 
// find out which one is currently used
enum class Callable_Type
{
    FUNCTION,
    POLY_FUNCTION,
    POLY_STRUCT,
    STRUCT_INITIALIZER,
    UNION_INITIALIZER,
	SLICE_INITIALIZER, // Struct-initializer syntax for slices
	HARDCODED,
    FUNCTION_POINTER,
	DOT_CALL_POLYMORPHIC,
	DOT_CALL_NORMAL,
	CONTEXT_CHANGE,
	ERROR_OCCURED, // In case of non-resolvable overloads or invalid symbol
};

struct Callable
{
	Call_Signature* signature;
	Callable_Type type;
	union
	{
		ModTree_Function* function;
		Poly_Function poly_function;
		Workload_Structure_Polymorphic* poly_struct;
		Hardcoded_Type hardcoded;
		Datatype_Slice* slice_type;
		Datatype_Function_Pointer* function_pointer;
		Context_Change_Type context_change_type;
		Struct_Content* struct_content;
	} options;
};

Callable callable_make(Call_Signature* signature, Callable_Type type);




// Extern Sources
struct Extern_Sources
{
	Dynamic_Array<ModTree_Function*> extern_functions;
	Dynamic_Array<String*> compiler_settings[(int)Extern_Compiler_Setting::MAX_ENUM_VALUE];
};

Extern_Sources extern_sources_create();
void extern_sources_destroy(Extern_Sources* sources);



// Identifier Pool
struct Predefined_IDs
{
	// Other
	String* main;
	String* id_struct;
	String* empty_string;
	String* invalid_symbol_name;
	String* cast_mode;
	String* cast_mode_none;
	String* cast_mode_explicit;
	String* cast_mode_inferred;
	String* cast_mode_implicit;
	String* cast_mode_pointer_explicit;
	String* cast_mode_pointer_inferred;
	String* byte;
	String* value;
	String* is_available;
	String* uninitialized_token; // _
	String* return_type_name; // !return_type

	String* hashtag_instanciate;
	String* hashtag_bake;
	String* hashtag_get_overload;
	String* hashtag_get_overload_poly;

	String* cast_pointer;
	String* defer_restore;
	String* cast;
	String* defer;

	String* lambda_function;
	String* bake_function;

	String* function;
	String* create_fn;
	String* next_fn;
	String* has_next_fn;
	String* value_fn;
	String* name;
	String* as_member_access;
	String* commutative;
	String* binop;
	String* unop;
	String* global;
	String* option;
	String* lib;
	String* lib_dir;
	String* source;
	String* header;
	String* header_dir;
	String* definition;

	// Hardcoded functions
	String* type_of;
	String* type_info;

	// Members
	String* data;
	String* size;
	String* tag;
	String* anon_struct;
	String* anon_enum;
	String* c_string;
	String* allocator;
	String* bytes;

	// Context members 
	String* id_import;
	String* set_option;
	String* set_cast_option;
	String* add_binop;
	String* add_unop;
	String* add_cast;
	String* add_array_access;
	String* add_dot_call;
	String* add_iterator;

	String* cast_option;
	String* cast_option_enum_values[(int)Cast_Option::MAX_ENUM_VALUE];
};

struct Identifier_Pool;
struct Identifier_Pool_Lock
{
	Identifier_Pool* pool;
};

struct Identifier_Pool
{
	Hashtable<String, String*> identifier_lookup_table;
	Predefined_IDs predefined_ids;
	Semaphore add_identifier_semaphore;
};

Identifier_Pool identifier_pool_create();
void identifier_pool_destroy(Identifier_Pool* pool);

Identifier_Pool_Lock identifier_pool_lock_aquire(Identifier_Pool* pool);
void identifier_pool_lock_release(Identifier_Pool_Lock& lock);
String* identifier_pool_add(Identifier_Pool_Lock* lock, String identifier);
String* identifier_pool_lock_and_add(Identifier_Pool* pool, String identifier);

void identifier_pool_print(Identifier_Pool* pool);



// Fiber Pool
struct Fiber_Pool;
struct Fiber_Pool_Handle { // Handle to a fiber from a fiber pool
	Fiber_Pool* pool;
	int pool_index;
};

Fiber_Pool* fiber_pool_create();
void fiber_pool_destroy(Fiber_Pool* pool);
Fiber_Pool_Handle fiber_pool_get_handle(Fiber_Pool* pool, fiber_entry_fn entry_fn, void* userdata);
void fiber_pool_set_current_fiber_to_main(Fiber_Pool* pool);
bool fiber_pool_switch_to_handel(Fiber_Pool_Handle handle); // Returns true if fiber finished, or if fiber waits for more stuff to happen
void fiber_pool_switch_to_main_fiber(Fiber_Pool* pool);
void fiber_pool_check_all_handles_completed(Fiber_Pool* pool);
void fiber_pool_test(); // Just tests the fiber pool if everything works correctly
