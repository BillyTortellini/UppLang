#pragma once

#include "../../utility/datatypes.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"

struct Debugger;
struct Compilation_Unit;
struct Compiler_Analysis_Data;
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
    DEBUG,
    FLAGS,
    RIP,
    OTHER, // Something we currently don't keep track of (e.g. segment registers)
};

struct X64_Register_Value_Location
{
    X64_Register_Type type;
    int register_index; // Depending on type, the index of the register (e.g. 16 integer + 16 xmm registers)
    int size; // Access size, may be 0-128
    int offset; // Offset in register, for e.g. al and ah (high and low bits)
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

// struct X64_Register_State
// {
//     u64 integer_registers[(int)X64_Integer_Register::MAX_VALUE];
//     u64 mmx_registers[8];
//     u64 xmm_registers[16];
//     u64 rip;
//     u64 flags;
// };

struct Machine_Code_Address_To_Line_Result
{
    Compilation_Unit* unit;
    int line_index;
    int function_slot;
    AST::Statement* statement;
};

enum class Debugger_Reset_Type
{
    TERMINATE_PROCESS,
    DETACH_FROM_PROCESS,
    PROCESS_EXIT_RECEIVED
};

struct Stack_Frame
{
    u64 instruction_pointer;
    bool inside_upp_function;
    union {
        struct {
            int slot;
            Compilation_Unit* unit;
            int line_index;
        } upp_function;
        struct {
            String symbol_name; // may be empty
            String dll_name; // may also be empty
            u64 offset_from_symbol_start;
        } other;
    } options;
};

enum class Debugger_State
{
    RUNNING,
    BREAKPOINT_HIT_OR_HALTED, // Caused by breakpoints, stepping, __debugbreak
    EXCEPTION_OCCURED, // Hit an exception which wasn't caused by debugger (e.g. access violation)
    NO_ACTIVE_PROCESS
};

struct Source_Breakpoint
{
    int line_index;
    Compilation_Unit* compilation_unit;
    Dynamic_Array<u64> addresses; // May be empty
    int active_reference_count; // Active if greater 0
};

Debugger* debugger_create();
void debugger_destroy(Debugger* debugger);
void debugger_reset(Debugger* debugger, Debugger_Reset_Type reset_type = Debugger_Reset_Type::TERMINATE_PROCESS);

bool debugger_start_process(
    Debugger* debugger, const char* exe_filename, const char* pdb_filename, const char* main_obj_filepath, Compiler_Analysis_Data* analysis_data
);

Debugger_State debugger_get_state(Debugger* debugger);
Debugger_State debugger_continue_until_next_event(Debugger* debugger); // Returns true if process has closed
Debugger_State debugger_continue_until_next_breakpoint_or_exit(Debugger* debugger);
void debugger_wait_for_console_command(Debugger* debugger);

Source_Breakpoint* debugger_add_source_breakpoint(Debugger* debugger, int line_index, Compilation_Unit* unit);
void debugger_remove_source_breakpoint(Debugger* debugger, Source_Breakpoint* breakpoint);
Array<Stack_Frame> debugger_get_stack_frames(Debugger* debugger);

void debugger_print_line_translation(Debugger* debugger, Compilation_Unit* compilation_unit, int line_index, Compiler_Analysis_Data* analysis_data);






