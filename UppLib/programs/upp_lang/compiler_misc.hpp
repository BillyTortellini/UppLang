#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "../../win32/process.hpp"

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

enum class Custom_Operator_Type
{
	AUTO_CAST,
	ARRAY_ACCESS,
	ITERATOR,
	DEFAULT_VALUE,

	BINOP_ADDITION,
	BINOP_SUBTRACTION,
	BINOP_MULTIPLY,
	BINOP_DIVIDE,
	BINOP_MODULO,

	BINOP_EQUAL,
	BINOP_NOT_EQUAL,
	BINOP_LESS,
	BINOP_LESS_EQUAL,
	BINOP_GREATER,
	BINOP_GREATER_EQUAL,

	UNOP_NOT,
	UNOP_NEGATE,

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
	ENUM_VALUE_AS_STRING,
	ENUM_TYPE_MIN_VALUE,
	ENUM_TYPE_MAX_VALUE,
	ENUM_TYPE_IS_CONTINOUS,

	CAST_PRIMITIVE,
	CAST_POINTER,

	MEMORY_COPY,
	MEMORY_COPY_NO_OVERLAP,
	MEMORY_ZERO,
	MEMORY_COMPARE,

	SYSTEM_ALLOC,
	SYSTEM_FREE,

	// Hardcoded IO
	PRINT_I32,
	PRINT_F32,
	PRINT_BOOL,
	PRINT_LINE,
	PRINT_STRING,
	READ_I32,
	READ_F32,
	READ_BOOL,

	BITWISE_NOT,
	BITWISE_AND,
	BITWISE_OR,
	BITWISE_XOR,
	BITWISE_SHIFT_LEFT,
	BITWISE_SHIFT_RIGHT,

	// Floating point math starts here
	F32_ABSOLUTE,
	F32_MODULO,
	F32_REMAINDER,
	F32_CEIL,     // Rounds towards infinity
	F32_FLOOR,    // Rounds towards negative infinity
	F32_TRUNCATE, // Rounds towards 0
	F32_ROUND_NEAREST, // Rounds towards nearest int, away from 0 in halfway cases

	F32_EXP,
	F32_LN, // base e
	F32_LOG10,
	F32_LOG2,
	F32_POW,
	F32_SQUARE_ROOT,
	F32_CUBE_ROOT,

	F32_SIN,
	F32_COS,
	F32_TAN,
	F32_ASIN,
	F32_ACOS,
	F32_ATAN,
	F32_ATAN2,

	F32_SINH,
	F32_COSH,
	F32_TANH,
	F32_ASINH,
	F32_ACOSH,
	F32_ATANH,

	F32_IS_NAN,
	F32_IS_FINITE,
	F32_IS_INFINITE,

	F64_ABSOLUTE,
	F64_MODULO,
	F64_REMAINDER,
	F64_CEIL,     // Rounds towards infinity
	F64_FLOOR,    // Rounds towards negative infinity
	F64_TRUNCATE, // Rounds towards 0
	F64_ROUND_NEAREST, // Rounds towards nearest int, away from 0 in halfway cases

	F64_EXP,
	F64_LN, // base e
	F64_LOG10,
	F64_LOG2,
	F64_POW,
	F64_SQUARE_ROOT,
	F64_CUBE_ROOT,

	F64_SIN,
	F64_COS,
	F64_TAN,
	F64_ASIN,
	F64_ACOS,
	F64_ATAN,
	F64_ATAN2,

	F64_SINH,
	F64_COSH,
	F64_TANH,
	F64_ASINH,
	F64_ACOSH,
	F64_ATANH,

	F64_IS_NAN,
	F64_IS_FINITE,
	F64_IS_INFINITE,

	MAX_ENUM_VALUE
};

enum class Hardcoded_Type_Class
{
	UTILITY,
	INPUT,
	OUTPUT,
	BITWISE_OPERATION,
	F32_UNARY,
	F32_BINARY,
	F32_PREDICATE,
	F64_UNARY,
	F64_BINARY,
	F64_PREDICATE,
};

struct Hardcoded_Type_Info
{
	Hardcoded_Type_Class type_class;
	const char* cstring;
	const char* symbol_name;
	const char* c_impl_name;
};
Hardcoded_Type_Info hardcoded_type_get_info(Hardcoded_Type type);

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


// C++ TYPE REPRESENTATIONS
template<typename T>
struct Upp_Slice
{
    T* data;
    usize size;
};

struct Upp_Slice_Base
{
    void* data;
    usize size;
};

// The 'primitive' string type as it is currently defined in the upp-language
// There is no null-terminator after data, and the size is just the number of bytes, not the character count
struct Upp_String {
    void* data;
    usize size;
};

struct Upp_Type_Handle
{
    u32 index;
};

struct Upp_Any
{
    void* data;
    Upp_Type_Handle type;
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

	String* custom_operator_function_names[(int)Custom_Operator_Type::MAX_ENUM_VALUE];

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
	String* type; // Any.type
	String* data;
	String* size;
	String* tag;
	String* anon_struct;
	String* anon_enum;
	String* c_string;
	String* string;
	String* allocator;
	String* bytes;
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
