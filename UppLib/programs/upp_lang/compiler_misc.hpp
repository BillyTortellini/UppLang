#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "../../win32/process.hpp"
#include "constant_pool.hpp"

struct Datatype;
struct String;
struct Poly_Header;
struct Upp_Function;
struct Call_Signature;

namespace AST
{
    struct Expression;
    struct Code_Block;
};

enum class Compile_Type
{
    ANALYSIS_ONLY,
    BUILD_CODE,
};

enum class Cast_Type
{
	// Primitive casts
	INTEGERS,
	FLOATS,
	ENUMS,
	FLOAT_TO_INT,
	INT_TO_FLOAT,
	ENUM_TO_INT,
	INT_TO_ENUM,

	// Pointer conversions
	POINTERS,
	POINTER_TO_ADDRESS,
	ADDRESS_TO_POINTER,

	TO_SUB_TYPE,
	TO_BASE_TYPE,

	// Operation casts
	DEREFERENCE,
	ADDRESS_OF,
	ARRAY_TO_SLICE,
	TO_ANY,
	FROM_ANY,
	CUSTOM_CAST,

	// Note: From here upwards the values are not accessible in the language anymore
	NO_CAST, // No cast needed, source-type == destination-type
	UNKNOWN, // Either source or destination type are/contain error/unknown type
	INVALID, // Cast is not valid
	MAX_ENUM_VALUE
};
const char* cast_type_to_string(Cast_Type type);

enum class Custom_Operator_Type
{
	CAST,
	BINOP,
	UNOP,
	ARRAY_ACCESS,
	ITERATOR,
	INVALID, // Invalid identifier given

	MAX_ENUM_VALUE
};

enum class Symbol_Access_Level
{
    GLOBAL = 0,      // Can be accessed everywhere (comptime definitions, functions, structs)
    POLYMORPHIC = 1, // Access level for polymorphic parameters (anonymous structs/lambdas/bake)
    INTERNAL = 2     // Access level for variables/parameters of functions, which only have meaningful values during execution
};

enum class Import_Type
{
	NONE,      // for lookups, if we don't want to query imports
    SYMBOLS,   // import Foo~*
    DOT_CALLS, // import dot_calls Foo
    OPERATORS, // import operators Foo
};

enum class Node_Section
{
    WHOLE,             // Every character, including child text
    WHOLE_NO_CHILDREN, // Every character without child text
    IDENTIFIER,        // Highlight Identifier if the node has any
    KEYWORD,           // Highlight first keyword if the node contains one
    ENCLOSURE,         // Highlight first-encountered enclosures, e.g. (), {}, []
    NONE,              // Not quite sure if this is usefull at all
    FIRST_TOKEN,       // To display error that isn't specific to all internal tokens
    END_TOKEN,         // To display that something is missing
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

	MAX_ENUM_VALUE
};
void hardcoded_type_append_to_string(String* string, Hardcoded_Type hardcoded);

enum class Member_Access_Type
{
	STRUCT_MEMBER_ACCESS, // Includes subtype and tag access
	STRUCT_POLYMORHPIC_PARAMETER_ACCESS,
	ENUM_MEMBER_ACCESS
};


struct Function_Body
{
    bool is_expression;
    union {
        AST::Expression* expr;
        AST::Code_Block* block;
    };
};



enum class Exit_Code_Type
{
	SUCCESS, // Program executed successfully
	RUNNING, // Bytecode-thread can continue, probably only used during stepping
	COMPILATION_FAILED, // Code did not because there were compile errors
	CODE_ERROR,
	EXECUTION_ERROR, // Stack overflow, return value overflow, instruction limit reached
	INSTRUCTION_LIMIT_REACHED,
	TYPE_INFO_WAITING_FOR_TYPE_FINISHED,
	CALL_TO_UNFINISHED_FUNCTION,

	MAX_ENUM_VALUE
};
const char* exit_code_type_as_string(Exit_Code_Type type);

struct Exit_Code
{
	Exit_Code_Type type;
	union 
	{
		Datatype* waiting_for_type_finish_type;
		Upp_Function* waiting_for_function;
		const char* error_msg; // May be null
	} options;
};

Exit_Code exit_code_make(Exit_Code_Type type, const char* error_msg = 0);
void exit_code_append_to_string(String* string, Exit_Code code);

struct Poly_Function
{
	Poly_Header* poly_header;
	Upp_Function* function;
};



// CALLABLES AND PARAMETERS
struct Call_Parameter
{
    String* name;
    Datatype* datatype;
    bool required;                      // Default values and implicit parameters don't require values
    bool requires_named_addressing;     // Implicit arguments and #instanciate, #get_overload?
    bool must_not_be_set;               // #instanciate must not set specific normal arguments

	int pattern_variable_index; // -1 if not comptime or implicit parameter
};

struct Call_Signature
{
    Dynamic_Array<Call_Parameter> parameters;
    // Return type of functions/poly-functions is stored as one of the parameters
    //  or -1 if no return type exists
    int return_type_index; 
    bool is_registered; // For debugging/deduplication

    Optional<Datatype*> return_type();
	int param_count(bool with_return = false);
};



// Extern Sources
struct Extern_Sources
{
	Dynamic_Array<Upp_Function*> extern_functions;
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
	String* root_module;
	String* invalid_symbol_name;
	String* byte;
	String* value;
	String* is_available;
	String* uninitialized_token; // _
	String* return_type_name; // !return_type
	String* operators;
	String* dot_calls;

	String* hashtag_instanciate;
	String* hashtag_bake;
	String* hashtag_get_overload;
	String* hashtag_get_overload_poly;

	String* add_binop;
	String* add_unop;
	String* add_cast;
	String* add_iterator;
	String* add_array_access;

	String* defer_restore;
	String* cast;
	String* defer;
	String* from;
	String* to;

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
	String* string;
	String* allocator;
	String* bytes;

	// Cast type
	String* cast_type;
	String* cast_type_enum_values[(int)Cast_Type::MAX_ENUM_VALUE];
};

struct Identifier_Pool
{
	Hashtable<String, String*> identifier_lookup_table;
	Predefined_IDs predefined_ids;
};

Identifier_Pool identifier_pool_create();
void identifier_pool_destroy(Identifier_Pool* pool);
String* identifier_pool_add(Identifier_Pool* pool, String identifier);
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
