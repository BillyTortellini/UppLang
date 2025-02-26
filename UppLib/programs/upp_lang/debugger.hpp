#pragma once

#include "../../utility/datatypes.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"

struct Debugger;
struct Compilation_Unit;
struct Compiler_Analysis_Data;
struct IR_Code_Block;
struct IR_Function;
struct Datatype;
namespace AST {
    struct Statement;
}


// Note: These are in the same order as the Unwind-Registers-Indices of PE-Exception records!
enum class X64_Integer_Register
{
    RAX = 0,
    RCX,
    RDX,
    RBX,
    RSP,
    RBP,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    MAX_VALUE
};

enum class X64_Debug_Register
{
    // 4 Hardware breakpoint registers
    DR0,
    DR1,
    DR2,
    DR3,
    // Note: DR4 and DR5 don't exist!
    DR6, // Status registers, shows which breakpoint conditions were hit
    DR7, // Debug control registers, enabling/disabling breakpoints 0-4
    MAX_VALUE
};

enum class X64_Register_Type
{
    INTEGER, // 64 bit integer registers
    MMX, // 64 bit float registers (not in use)
    XMM, // 128 bit media registers
    DEBUG_REG, // Debug registers (6)
    FLAGS,
    RIP,
    OTHER, // Something we currently don't keep track of (e.g. segment registers)
};

struct X64_Register_Value_Location
{
    X64_Register_Type type;
    int register_index; // Depending on type, the index of the register (e.g. 16 integer + 16 xmm registers)
    int offset;  // Offset from bottom of register, used for ah and al, and also some xmm high/low bytes
    int size;    // Access size, between 1-16 bytes
};

enum class X64_Flags
{
    CARRY             = 0x1 << 0,
    PARITY            = 0x1 << 2,
    AUX_CARRY         = 0x1 << 4,
    ZERO_FLAG         = 0x1 << 6,
    SIGN_FLAG         = 0x1 << 7,
    TRAP              = 0x1 << 8,
    INTERRUPT_ENABLED = 0x1 << 9,
    DIRECTION         = 0x1 << 10,
    OVERFLOW_FLAG     = 0x1 << 11,
    RESUME            = 0x1 << 16
};

struct X64_XMM_Value
{
    u64 low_bytes;
    u64 high_bytes;
};

struct X64_Register_State
{
    u64 integer_registers[(int)X64_Integer_Register::MAX_VALUE];
    X64_XMM_Value xmm_registers[16];
    u64 rip;
    u32 flags;
};

struct Closest_Symbol_Info
{
    int pe_index;
    int section_index;
    String section_name;
    String pe_name;

    int exception_handling_index; // -1 if not found
    bool found_symbol;
    u64 distance;
    String symbol_name; // Name from pdb or PE export table
};

struct Assembly_Source_Information
{
    IR_Function* ir_function; // From which upp-function this assembly comes from
    u64 function_start_address;
    u64 function_end_address;

    int c_line_index;

    IR_Code_Block* ir_block;
    int ir_instruction_index;

    AST::Statement* statement;
    Compilation_Unit* unit;
    int upp_line_index;
};

struct Stack_Frame
{
    u64 instruction_pointer;
    u64 stack_frame_start_address; // Start of stack-frame, at this address the return address is stored
    X64_Register_State register_state;
};

enum class Halt_Type
{
    DEBUG_EVENT_RECEIVED,     // Thread create/destroy, process create/destroy, others
    STEPPING,                 // Step instruction hit
    BREAKPOINT_HIT,           // One of our own breakpoints was hit
    DEBUG_BREAK_HIT,          // Breakpoint was hit that wasn't our own (e.g. debug_break)
    EXCEPTION_OCCURED,        // Exception occurred, which leads to program termination
};

enum class Debug_Process_State
{
    RUNNING,
    HALTED,
    NO_ACTIVE_PROCESS
};

struct Debugger_State
{
    Debug_Process_State process_state;
    Halt_Type halt_type; // Following are only valid in halted state
    const char* exception_name; // Only valid when exception occured
};

struct Source_Breakpoint
{
    int line_index;
    Compilation_Unit* compilation_unit;
    Dynamic_Array<u64> addresses; // May be empty
    int active_reference_count; // Active if greater 0
};

struct Debugger_Value_Read
{
    bool success;
    Datatype* result_type;
    const char* error_msg;
};

Debugger* debugger_create();
void debugger_destroy(Debugger* debugger);
void debugger_reset(Debugger* debugger);

bool debugger_start_process(
    Debugger* debugger, const char* exe_filename, const char* pdb_filename, const char* main_obj_filepath, Compiler_Analysis_Data* analysis_data
);

void debugger_resume_until_next_halt_or_exit(Debugger* debugger);
void debugger_step_over_statement(Debugger* debugger, bool step_into);
void debugger_step_out(Debugger* debugger);

Debugger_State debugger_get_state(Debugger* debugger);
Assembly_Source_Information debugger_get_assembly_source_information(Debugger* debugger, u64 virtual_address);
Closest_Symbol_Info debugger_find_closest_symbol_name(Debugger* debugger, u64 address);
void closest_symbol_info_append_to_string(Debugger* debugger, Closest_Symbol_Info symbol_info, String* string);
void debugger_wait_for_console_command(Debugger* debugger);
Debugger_Value_Read debugger_read_variable_value(
    Debugger* debugger, String variable_name, Dynamic_Array<u8>* value_buffer, int stack_frame_start, int max_frame_depth);

Source_Breakpoint* debugger_add_source_breakpoint(Debugger* debugger, int line_index, Compilation_Unit* unit);
void debugger_remove_source_breakpoint(Debugger* debugger, Source_Breakpoint* breakpoint);
Array<Stack_Frame> debugger_get_stack_frames(Debugger* debugger);

void debugger_print_line_translation(Debugger* debugger, Compilation_Unit* compilation_unit, int line_index, Compiler_Analysis_Data* analysis_data);






