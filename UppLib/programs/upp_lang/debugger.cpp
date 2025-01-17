#include "debugger.hpp"

#include <Windows.h>
#include "../../win32/windows_helper_functions.hpp"
#include <cstdio>
#include <iostream>
#include "../../utility/file_io.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include <bddisasm.h>
#include "compiler.hpp"
#include "c_backend.hpp"
#include "editor_analysis_info.hpp"
#include "ir_code.hpp"

#include <dia2.h>
#include <atlbase.h>

const bool DEBUG_OUTPUT_ENABLED = true;


// Prototypes
u64 debugger_find_address_of_function(Debugger* debugger, String name);
Machine_Code_Address_To_Line_Result source_mapping_machine_instruction_address_to_upp_line(Debugger* debugger, u64 virtual_address);



void wide_string_from_utf8(Dynamic_Array<wchar_t>* character_buffer, const char* string)
{
    size_t length = strlen(string);
    dynamic_array_reserve(character_buffer, (int)length * 4); // 4 byte for each char _must_ be enough for now (I don't know enough about UTF-16)
    dynamic_array_reset(character_buffer);

    // Early exit on length 0 as MultiByteToWodeChar uses 0 as error-code
    if (length == 0) {
        character_buffer->data[0] = '\0';
        character_buffer->size = 0;
        return;
    }

    int written_chars = MultiByteToWideChar(CP_UTF8, 0, string, (int)length + 1, character_buffer->data, character_buffer->capacity);
    if (written_chars == 0) {
        character_buffer->data[0] = '\0';
        character_buffer->size = 0;
        return;
    }
    character_buffer->size = written_chars;
}

void wide_string_to_utf8(const wchar_t* wide_string, String* string)
{
    string_reset(string);
    int character_count = lstrlenW(wide_string) + 1;
    string_reserve(string, character_count * 4); // Max 4 bytes per char in UTF-8
    int written_chars = WideCharToMultiByte(CP_UTF8, 0, wide_string, character_count, string->characters, string->capacity, 0, 0);
    if (written_chars == 0) {
        string->size = 0;
        string->characters[0] = '\0';
        return;
    }
    string->size = (int)strlen(string->characters);
}

const char* x64_integer_register_to_name(X64_Integer_Register reg)
{
    switch (reg)
    {
    case X64_Integer_Register::RAX: return "RAX";
    case X64_Integer_Register::RCX: return "RCX";
    case X64_Integer_Register::RDX: return "RDX";
    case X64_Integer_Register::RBX: return "RBX";
    case X64_Integer_Register::RSP: return "RSP";
    case X64_Integer_Register::RBP: return "RBP";
    case X64_Integer_Register::RSI: return "RSI";
    case X64_Integer_Register::RDI: return "RDI";
    case X64_Integer_Register::R8:  return "R8";
    case X64_Integer_Register::R9:  return "R9";
    case X64_Integer_Register::R10: return "R10";
    case X64_Integer_Register::R11: return "R11";
    case X64_Integer_Register::R12: return "R12";
    case X64_Integer_Register::R13: return "R13";
    case X64_Integer_Register::R14: return "R14";
    case X64_Integer_Register::R15: return "R15";
    }
    return "Unknown";
}



namespace PDB_Analysis
{
    enum class PDB_Constant_Type
    {
        SIGNED_INT,
        UNSIGNED_INT,
        FLOAT,
        BOOLEAN,
        NULL_VALUE,
        OTHER // Unknown/Not parsed from PDB currently
    };

    struct PDB_Constant_Value
    {
        PDB_Constant_Type type;
        u32 size;
        union {
            i64 int_value;
            f32 val_f32;
            f64 val_f64;
            bool val_bool;
        } options;
    };

    struct PDB_Location_Static
    {
        u64 offset;
        u32 section_index; // Should not be null I guess
    };

    enum class PDB_Location_Type
    {
        STATIC,
        INSIDE_REGISTER,
        REGISTER_RELATIVE, // Stored relative to some register
        IS_CONSTANT, // Some constant value, not sure if this can grow infinitly large...

        THREAD_LOCAL_STORAGE, // Also something I don't deal with for now
        UNKNOWN, // Either something I don't parse from pdb or optimized away
    };

    struct PDB_Location
    {
        PDB_Location_Type type;
        union
        {
            PDB_Location_Static static_loc;
            PDB_Location_Static thread_local_storage;
            X64_Register_Value_Location register_loc;
            struct {
                X64_Register_Value_Location reg;
                i64 offset;
            } register_relative;
            PDB_Constant_Value constant_value;
        } options;
    };

    struct PDB_Variable_Info
    {
        String name;
        PDB_Location location;
    };

    struct PDB_Code_Block_Info
    {
        PDB_Location_Static location;
        u64 length; // Size in bytes
        Dynamic_Array<PDB_Variable_Info> variables;
        int function_index;
    };

    struct PDB_Line_Info
    {
        PDB_Location_Static location;
        u64 length; // Length in bytes
        int source_file_id;
        int line_num; // Starts with 1!
    };

    // Further information for functions where we have access to the source-code
    struct PDB_Function_Source_Info
    {
        int function_index;
        Optional<PDB_Location_Static> debug_start_location;
        Optional<PDB_Location_Static> debug_end_location;

        Dynamic_Array<int> child_block_indices;
        Dynamic_Array<PDB_Variable_Info> parameter_infos;
        Dynamic_Array<PDB_Line_Info> line_infos;
    };

    struct PDB_Function
    {
        String name;
        PDB_Location_Static location;
        u64 length; // Size in bytes
        int source_info_index; // -1 if not available, which should be for most functions in imported libraries
    };

    struct PDB_Information
    {
        Dynamic_Array<PDB_Function_Source_Info> source_infos;
        Dynamic_Array<PDB_Code_Block_Info> block_infos;
        Dynamic_Array<PDB_Variable_Info> global_infos;
        Dynamic_Array<PDB_Function> functions;
        Hashtable<i32, String> source_file_paths;
    };

    PDB_Information* pdb_information_create()
    {
        PDB_Information* information = new PDB_Information;
        information->source_infos = dynamic_array_create<PDB_Function_Source_Info>();
        information->block_infos = dynamic_array_create<PDB_Code_Block_Info>();
        information->global_infos = dynamic_array_create<PDB_Variable_Info>();
        information->functions = dynamic_array_create<PDB_Function>();
        information->source_file_paths = hashtable_create_empty<i32, String>(3, hash_i32, equals_i32);
        return information;
    }

    void pdb_information_destroy(PDB_Information* information)
    {
        for (int i = 0; i < information->source_infos.size; i++) {
            auto& fn = information->source_infos[i];
            dynamic_array_destroy(&fn.line_infos);
            dynamic_array_destroy(&fn.child_block_indices);

            for (int j = 0; j < fn.parameter_infos.size; j++) {
                string_destroy(&fn.parameter_infos[j].name);
            }
            dynamic_array_destroy(&fn.parameter_infos);
        }
        dynamic_array_destroy(&information->source_infos);

        for (int i = 0; i < information->functions.size; i++) {
            auto& fn = information->functions[i];
            string_destroy(&fn.name);
        }
        dynamic_array_destroy(&information->functions);

        for (int i = 0; i < information->block_infos.size; i++) {
            auto& block = information->block_infos[i];
            for (int j = 0; j < block.variables.size; j++) {
                string_destroy(&block.variables[j].name);
            }
            dynamic_array_destroy(&block.variables);
        }
        dynamic_array_destroy(&information->block_infos);

        for (auto iter = hashtable_iterator_create(&information->source_file_paths); hashtable_iterator_has_next(&iter); hashtable_iterator_next(&iter)) {
            string_destroy(iter.value);
        }
        hashtable_destroy(&information->source_file_paths);

        for (int i = 0; i < information->global_infos.size; i++) {
            string_destroy(&information->global_infos[i].name);
        }
        dynamic_array_destroy(&information->global_infos);

        delete information;
    }

    PDB_Constant_Value variant_to_constant_value(VARIANT& variant)
    {
        PDB_Constant_Value value;
        value.type = PDB_Constant_Type::OTHER;
        value.size = 0;

        switch (variant.vt)
        {
        case VT_I1:  value.type = PDB_Constant_Type::SIGNED_INT; value.size = 1; value.options.int_value = (i8)variant.bVal; break;
        case VT_I2:  value.type = PDB_Constant_Type::SIGNED_INT; value.size = 2; value.options.int_value = (i16)variant.iVal; break;
        case VT_I4:  value.type = PDB_Constant_Type::SIGNED_INT; value.size = 4; value.options.int_value = (i32)variant.lVal; break;
        case VT_I8:  value.type = PDB_Constant_Type::SIGNED_INT; value.size = 8; value.options.int_value = (i64)variant.llVal; break;
        case VT_UI1: value.type = PDB_Constant_Type::UNSIGNED_INT; value.size = 1; value.options.int_value = (u8)variant.bVal; break;
        case VT_UI2: value.type = PDB_Constant_Type::UNSIGNED_INT; value.size = 2; value.options.int_value = (u16)variant.uiVal; break;
        case VT_UI4: value.type = PDB_Constant_Type::UNSIGNED_INT; value.size = 4; value.options.int_value = (u32)variant.ulVal; break;
        case VT_UI8: value.type = PDB_Constant_Type::UNSIGNED_INT; value.size = 8; value.options.int_value = (u64)variant.ullVal; break;

        case VT_R4: value.type = PDB_Constant_Type::FLOAT; value.size = 4; value.options.val_f32 = variant.fltVal; break;
        case VT_R8: value.type = PDB_Constant_Type::FLOAT; value.size = 8; value.options.val_f64 = variant.dblVal; break;

        case VT_BOOL: value.type = PDB_Constant_Type::BOOLEAN; value.size = 1; value.options.val_bool = variant.boolVal; break;
        case VT_NULL: value.type = PDB_Constant_Type::NULL_VALUE; value.size = 1; break; // We don't know the size of this null value here
        default: break;
        }

        return value;
    }

    X64_Register_Value_Location register_id_to_location(u32 reg_id)
    {
        X64_Register_Value_Location loc;
        loc.type = X64_Register_Type::OTHER;
        loc.register_index = 0;
        loc.size = 8;
        loc.offset = 0;
        switch (reg_id)
        {
        case CV_REG_NONE: break;

        case CV_AMD64_AL:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RAX; loc.size = 1; break;
        case CV_AMD64_CL:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RCX; loc.size = 1; break;
        case CV_AMD64_DL:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDX; loc.size = 1; break;
        case CV_AMD64_BL:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBX; loc.size = 1; break;

        case CV_AMD64_AH:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RAX; loc.size = 1; loc.offset = 1; break;
        case CV_AMD64_CH:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RCX; loc.size = 1; loc.offset = 1; break;
        case CV_AMD64_DH:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDX; loc.size = 1; loc.offset = 1; break;
        case CV_AMD64_BH:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBX; loc.size = 1; loc.offset = 1; break;

        case CV_AMD64_SIL: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RSI; loc.size = 1; break;
        case CV_AMD64_DIL: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDI; loc.size = 1; break;
        case CV_AMD64_BPL: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBP; loc.size = 1; break;
        case CV_AMD64_SPL: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RSP; loc.size = 1; break;

        case CV_AMD64_AX:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RAX; loc.size = 2; break;
        case CV_AMD64_CX:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RCX; loc.size = 2; break;
        case CV_AMD64_DX:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDX; loc.size = 2; break;
        case CV_AMD64_BX:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBX; loc.size = 2; break;
        case CV_AMD64_SP:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RSP; loc.size = 2; break;
        case CV_AMD64_BP:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBP; loc.size = 2; break;
        case CV_AMD64_SI:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RSI; loc.size = 2; break;
        case CV_AMD64_DI:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDI; loc.size = 2; break;

        case CV_AMD64_EAX: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RAX; loc.size = 4; break;
        case CV_AMD64_ECX: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RCX; loc.size = 4; break;
        case CV_AMD64_EDX: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDX; loc.size = 4; break;
        case CV_AMD64_EBX: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBX; loc.size = 4; break;
        case CV_AMD64_ESP: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RSP; loc.size = 4; break;
        case CV_AMD64_EBP: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBP; loc.size = 4; break;
        case CV_AMD64_ESI: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RSI; loc.size = 4; break;
        case CV_AMD64_EDI: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDI; loc.size = 4; break;

        case CV_AMD64_RIP: loc.type = X64_Register_Type::RIP; loc.size = 8; break;

        case CV_AMD64_RAX:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RAX; loc.size = 8; break;
        case CV_AMD64_RBX:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBX; loc.size = 8; break;
        case CV_AMD64_RCX:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RCX; loc.size = 8; break;
        case CV_AMD64_RDX:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDX; loc.size = 8; break;
        case CV_AMD64_RSI:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RSI; loc.size = 8; break;
        case CV_AMD64_RDI:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RDI; loc.size = 8; break;
        case CV_AMD64_RBP:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RBP; loc.size = 8; break;
        case CV_AMD64_RSP:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::RSP; loc.size = 8; break;

        case CV_AMD64_R8:   loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R8;  loc.size = 8; break;
        case CV_AMD64_R9:   loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R9;  loc.size = 8; break;
        case CV_AMD64_R10:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R10; loc.size = 8; break;
        case CV_AMD64_R11:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R11; loc.size = 8; break;
        case CV_AMD64_R12:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R12; loc.size = 8; break;
        case CV_AMD64_R13:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R13; loc.size = 8; break;
        case CV_AMD64_R14:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R14; loc.size = 8; break;
        case CV_AMD64_R15:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R15; loc.size = 8; break;

        case CV_AMD64_R8B:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R8;  loc.size = 1; break;
        case CV_AMD64_R9B:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R9;  loc.size = 1; break;
        case CV_AMD64_R10B: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R10; loc.size = 1; break;
        case CV_AMD64_R11B: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R11; loc.size = 1; break;
        case CV_AMD64_R12B: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R12; loc.size = 1; break;
        case CV_AMD64_R13B: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R13; loc.size = 1; break;
        case CV_AMD64_R14B: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R14; loc.size = 1; break;
        case CV_AMD64_R15B: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R15; loc.size = 1; break;

        case CV_AMD64_R8W:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R8;  loc.size = 2; break;
        case CV_AMD64_R9W:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R9;  loc.size = 2; break;
        case CV_AMD64_R10W: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R10; loc.size = 2; break;
        case CV_AMD64_R11W: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R11; loc.size = 2; break;
        case CV_AMD64_R12W: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R12; loc.size = 2; break;
        case CV_AMD64_R13W: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R13; loc.size = 2; break;
        case CV_AMD64_R14W: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R14; loc.size = 2; break;
        case CV_AMD64_R15W: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R15; loc.size = 2; break;

        case CV_AMD64_R8D:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R8;  loc.size = 4; break;
        case CV_AMD64_R9D:  loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R9;  loc.size = 4; break;
        case CV_AMD64_R10D: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R10; loc.size = 4; break;
        case CV_AMD64_R11D: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R11; loc.size = 4; break;
        case CV_AMD64_R12D: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R12; loc.size = 4; break;
        case CV_AMD64_R13D: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R13; loc.size = 4; break;
        case CV_AMD64_R14D: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R14; loc.size = 4; break;
        case CV_AMD64_R15D: loc.type = X64_Register_Type::INTEGER; loc.register_index = (int)X64_Integer_Register::R15; loc.size = 4; break;

        // Handle ranges seperately
        default:
        {
            // MM Registers (Float registers, actually 80 bit large)
            if (reg_id >= CV_AMD64_MM0 && reg_id <= CV_AMD64_MM7) {
                loc.type = X64_Register_Type::MMX;
                loc.register_index = reg_id - CV_AMD64_MM0;
                loc.size = 8;
                loc.offset = 0;
            }
            // MM Register sub-range
            else if (reg_id >= CV_AMD64_MM00 && reg_id <= CV_AMD64_MM71) {
                loc.type = X64_Register_Type::MMX;
                loc.size = 4;
                loc.register_index = (reg_id - CV_AMD64_MM00) / 2;
                loc.offset = (reg_id - CV_AMD64_MM00) % 2 == 0 ? 0 : 4;
            }
            // Full XMM Registers
            else if (reg_id >= CV_AMD64_XMM0 && reg_id <= CV_AMD64_XMM15) {
                loc.type = X64_Register_Type::XMM;
                loc.register_index = reg_id - CV_AMD64_XMM0;
                loc.size = 16;
            }
            // XMM float sub-ranges
            else if (reg_id >= CV_AMD64_XMM0_0 && reg_id <= CV_AMD64_XMM7_3) {
                loc.type = X64_Register_Type::XMM;
                loc.size = 4;
                loc.register_index = (reg_id - CV_AMD64_XMM0) / 4;
                loc.offset = ((reg_id - CV_AMD64_XMM0) % 4) * 4;
            }
            else if (reg_id >= CV_AMD64_XMM8_0 && reg_id <= CV_AMD64_XMM15_3) {
                loc.type = X64_Register_Type::XMM;
                loc.size = 4;
                loc.register_index = 7 + (reg_id - CV_AMD64_XMM7) / 4;
                loc.offset = ((reg_id - CV_AMD64_XMM7) % 4) * 4;
            }
            // XMM double sub-range
            else if (reg_id >= CV_AMD64_XMM0L && reg_id <= CV_AMD64_XMM7L) {
                loc.type = X64_Register_Type::XMM;
                loc.size = 8;
                loc.register_index = reg_id - CV_AMD64_XMM0L;
                loc.offset = 0;
            }
            else if (reg_id >= CV_AMD64_XMM0H && reg_id <= CV_AMD64_XMM7H) {
                loc.type = X64_Register_Type::XMM;
                loc.size = 8;
                loc.register_index = reg_id - CV_AMD64_XMM0H;
                loc.offset = 8;
            }
            else if (reg_id >= CV_AMD64_XMM8L && reg_id <= CV_AMD64_XMM15L) {
                loc.type = X64_Register_Type::XMM;
                loc.size = 8;
                loc.register_index = reg_id - CV_AMD64_XMM8L;
                loc.offset = 0;
            }
            else if (reg_id >= CV_AMD64_XMM8H && reg_id <= CV_AMD64_XMM15H) {
                loc.type = X64_Register_Type::XMM;
                loc.size = 8;
                loc.register_index = reg_id - CV_AMD64_XMM8H;
                loc.offset = 8;
            }
            break;
        }
        }

        return loc;
    }

    PDB_Location pdb_symbol_get_location(IDiaSymbol* symbol)
    {
        PDB_Location location;
        location.type = PDB_Location_Type::UNKNOWN;

        DWORD location_type;
        if (symbol->get_locationType(&location_type) != S_OK) {
            return location;
        }

        switch (location_type)
        {
        case LocIsStatic:
        {
            DWORD section = 0;
            DWORD offset = 0;
            if ((symbol->get_addressSection(&section) == S_OK) &&
                (symbol->get_addressOffset(&offset) == S_OK))
            {
                location.type = PDB_Location_Type::STATIC;
                location.options.static_loc.section_index = section;
                location.options.static_loc.offset = offset;
            }
            return location;
        }
        case LocIsTLS:
        {
            DWORD section = 0;
            DWORD offset = 0;
            if ((symbol->get_addressSection(&section) == S_OK) &&
                (symbol->get_addressOffset(&offset) == S_OK))
            {
                location.type = PDB_Location_Type::THREAD_LOCAL_STORAGE;
                location.options.thread_local_storage.section_index = section;
                location.options.thread_local_storage.offset = offset;
            }
            return location;
        }
        case LocIsEnregistered:
        {
            location.type = PDB_Location_Type::INSIDE_REGISTER;
            location.options.register_loc.type = X64_Register_Type::OTHER;
            location.options.register_loc.register_index = 0;
            location.options.register_loc.offset = 0;
            location.options.register_loc.size = 0;

            DWORD register_id = 0;
            if (symbol->get_registerId(&register_id) == S_OK) {
                location.options.register_loc = register_id_to_location(register_id);
            }
            return location;
        }
        case LocIsRegRel:
        {
            location.type = PDB_Location_Type::REGISTER_RELATIVE;
            location.options.register_relative.reg.type = X64_Register_Type::OTHER;
            location.options.register_relative.reg.register_index = 0;
            location.options.register_relative.reg.offset = 0;
            location.options.register_relative.reg.size = 0;
            location.options.register_relative.offset = 0;

            DWORD register_id = 0;
            LONG offset = 0;
            if ((symbol->get_registerId(&register_id) == S_OK) &&
                (symbol->get_offset(&offset) == S_OK))
            {
                location.options.register_relative.reg = register_id_to_location(register_id);
                location.options.register_relative.offset = offset;
            }
            return location;
        }
        case LocIsConstant:
        {
            location.type = PDB_Location_Type::IS_CONSTANT;
            location.options.constant_value.type = PDB_Constant_Type::OTHER;
            location.options.constant_value.size = 0;

            VARIANT variant;
            VariantInit(&variant);
            if (symbol->get_value(&variant) == S_OK) {
                location.options.constant_value = variant_to_constant_value(variant);
                VariantClear((VARIANTARG*)&variant);
            }
            return location;
        }
        // Handle these as other for now
        case LocInMetaData:
        case LocIsIlRel:
        case LocIsThisRel:
        case LocIsBitField:
        case LocIsSlot:
        case LocIsNull:
        default: break;
        }

        return location;
    }

    void pdb_symbol_get_name(IDiaSymbol* symbol, String* append_to)
    {
        string_reset(append_to);
        BSTR name = nullptr;
        if (symbol->get_name(&name) == S_OK) {
            wide_string_to_utf8(name, append_to);
            SysFreeString(name);
        }
    }

    void pdb_symbol_get_undecorated_name(IDiaSymbol* symbol, String* append_to)
    {
        string_reset(append_to);
        BSTR name = nullptr;
        if (symbol->get_undecoratedName(&name) == S_OK) {
            wide_string_to_utf8(name, append_to);
            SysFreeString(name);
        }
    }

    void pdb_symbol_analyse_recursive(
        IDiaSymbol* symbol, PDB_Information* info, bool is_main_compiland, int source_info_index, int block_index, IDiaSession* session)
    {
        DWORD tag = 0;
        if (symbol->get_symTag(&tag) != S_OK) {
            return;
        }

        switch (tag)
        {
            // Interesting Symbols:
        case SymTagFunction:
        {
            PDB_Location location = pdb_symbol_get_location(symbol);
            // Only handle functions at static locations (Not sure if anything else ever happens)
            if (location.type != PDB_Location_Type::STATIC) {
                break;
            }

            u64 length = 0;
            if (symbol->get_length(&length) != S_OK) {
                break;
            }

            // Add function info
            int function_index = -1;
            {
                PDB_Function fn;
                fn.length = length;
                fn.location = location.options.static_loc;
                fn.name = string_create();
                fn.source_info_index = -1;
                pdb_symbol_get_name(symbol, &fn.name);
                function_index = info->functions.size;
                dynamic_array_push_back(&info->functions, fn);
            }

            if (!is_main_compiland) {
                break;
            }

            // Query source-infos
            PDB_Function_Source_Info source_info;
            source_info.line_infos = dynamic_array_create<PDB_Line_Info>();
            source_info.parameter_infos = dynamic_array_create<PDB_Variable_Info>();
            source_info.child_block_indices = dynamic_array_create<int>();
            source_info.debug_start_location = optional_make_failure<PDB_Location_Static>();
            source_info.debug_end_location = optional_make_failure<PDB_Location_Static>();
            source_info.function_index = function_index;

            int added_source_info_index = info->source_infos.size;
            dynamic_array_push_back(&info->source_infos, source_info);

            // Query line-infos
            auto& function_info = info->functions[function_index];
            {
                CComPtr<IDiaEnumLineNumbers> line_iterator;
                bool worked = session->findLinesByAddr(
                    function_info.location.section_index,
                    function_info.location.offset,
                    function_info.length,
                    &line_iterator
                ) == S_OK;

                if (worked)
                {
                    IDiaLineNumber* line_number = NULL;
                    DWORD celt;
                    DWORD last_src_id = (DWORD)-1;
                    while (line_iterator->Next(1, &line_number, &celt) == S_OK && celt == 1)
                    {
                        // Assembly code information
                        DWORD rva;
                        DWORD seg;
                        DWORD offset;
                        DWORD length;

                        // Line origin
                        DWORD linenum;
                        DWORD src_id;
                        if (line_number->get_relativeVirtualAddress(&rva) != S_OK ||
                            line_number->get_addressSection(&seg) != S_OK ||
                            line_number->get_addressOffset(&offset) != S_OK ||
                            line_number->get_lineNumber(&linenum) != S_OK ||
                            line_number->get_sourceFileId(&src_id) != S_OK ||
                            line_number->get_length(&length) != S_OK)
                        {
                            line_number->Release();
                            continue;
                        }

                        // Add source-filename to filename-table if not already done
                        if (last_src_id != src_id)
                        {
                            int int_id = src_id;
                            String* str = hashtable_find_element(&info->source_file_paths, int_id);
                            if (str == 0)
                            {
                                CComPtr<IDiaSourceFile> source_file;
                                if (line_number->get_sourceFile(&source_file) == S_OK) {
                                    BSTR filename = NULL;
                                    if (source_file->get_fileName(&filename) == S_OK) {
                                        String source_filename = string_create();
                                        wide_string_to_utf8(filename, &source_filename);
                                        hashtable_insert_element(&info->source_file_paths, int_id, source_filename);
                                        SysFreeString(filename);
                                    }
                                }
                            }
                            last_src_id = src_id;
                        }

                        PDB_Line_Info line_info;
                        line_info.length = length;
                        line_info.location.section_index = seg;
                        line_info.location.offset = offset;
                        line_info.source_file_id = src_id;
                        line_info.line_num = linenum;
                        dynamic_array_push_back(&info->source_infos[added_source_info_index].line_infos, line_info);

                        line_number->Release();
                    }
                }
            }

            // Recursively call all child items
            IDiaEnumSymbols* child_iterator = NULL;
            if (SUCCEEDED(symbol->findChildren(SymTagNull, NULL, nsNone, &child_iterator)))
            {
                IDiaSymbol* child = NULL;
                ULONG celt = 0;
                while (SUCCEEDED(child_iterator->Next(1, &child, &celt)) && (celt == 1)) {
                    pdb_symbol_analyse_recursive(child, info, is_main_compiland, added_source_info_index, -1, session);
                    child->Release();
                }
                child_iterator->Release();
            }

            break;
        }
        case SymTagBlock:
        {
            // If not inside a function, return (Not sure if this happens)
            if (source_info_index == -1 || !is_main_compiland) {
                break;
            }
            PDB_Location location = pdb_symbol_get_location(symbol);
            // Only handle blocks at static locations (Not sure if anything else ever happens)
            if (location.type != PDB_Location_Type::STATIC) {
                break;
            }
            u64 length = 0;
            if (symbol->get_length(&length) != S_OK) {
                break;
            }

            PDB_Code_Block_Info block_info;
            block_info.variables = dynamic_array_create<PDB_Variable_Info>();
            block_info.function_index = source_info_index;
            block_info.length = source_info_index;
            block_info.location = location.options.static_loc;

            int added_block_index = info->block_infos.size;
            dynamic_array_push_back(&info->block_infos, block_info);

            // Recursively call all child items
            IDiaEnumSymbols* child_iterator = NULL;
            if (SUCCEEDED(symbol->findChildren(SymTagNull, NULL, nsNone, &child_iterator)))
            {
                IDiaSymbol* child = NULL;
                ULONG celt = 0;
                while (SUCCEEDED(child_iterator->Next(1, &child, &celt)) && (celt == 1)) {
                    pdb_symbol_analyse_recursive(child, info, is_main_compiland, source_info_index, added_block_index, session);
                    child->Release();
                }
                child_iterator->Release();
            }
        }
        case SymTagData: // Variables, parameters, globals
        {
            if (!is_main_compiland) { // Question if globals are stored per compiland or per exe...
                break;
            }

            DWORD data_kind;
            if (symbol->get_dataKind(&data_kind) != S_OK) {
                break;
            }
            PDB_Variable_Info variable_info;
            variable_info.location = pdb_symbol_get_location(symbol);
            switch (data_kind)
            {
            case DataIsLocal: {
                if (block_index == -1) {
                    break;
                }
                variable_info.name = string_create();
                pdb_symbol_get_name(symbol, &variable_info.name);
                auto& block = info->block_infos[block_index];
                dynamic_array_push_back(&block.variables, variable_info);
                break;
            }
            case DataIsParam:
            {
                if (source_info_index == -1) {
                    break;
                }
                variable_info.name = string_create();
                pdb_symbol_get_name(symbol, &variable_info.name);
                auto& source_info = info->source_infos[source_info_index];
                dynamic_array_push_back(&source_info.parameter_infos, variable_info);
                break;
            }

            case DataIsStaticLocal:
            case DataIsFileStatic: // File scoped global
            case DataIsGlobal: {
                variable_info.name = string_create();
                pdb_symbol_get_name(symbol, &variable_info.name);
                dynamic_array_push_back(&info->global_infos, variable_info);
                break;
            }

            case DataIsConstant:
            case DataIsUnknown:
            case DataIsObjectPtr: // this pointer
            case DataIsMember:
            case DataIsStaticMember:
            default: break;
            }
            break;
        }

        // Maybe compilands (obj files) can be hierarchical
        case SymTagCompiland:
        {
            // Recursively call all child items
            IDiaEnumSymbols* child_iterator = NULL;
            if (SUCCEEDED(symbol->findChildren(SymTagNull, NULL, nsNone, &child_iterator)))
            {
                IDiaSymbol* child = NULL;
                ULONG celt = 0;
                while (SUCCEEDED(child_iterator->Next(1, &child, &celt)) && (celt == 1)) {
                    pdb_symbol_analyse_recursive(child, info, is_main_compiland, -1, -1, session);
                    child->Release();
                }
                child_iterator->Release();
            }
            break;
        }

        // Function start and end addresses...
        case SymTagFuncDebugStart:
        {
            if (source_info_index == -1) {
                break;
            }
            auto& function = info->source_infos[source_info_index];
            if (function.debug_start_location.available) {
                break;
            }
            PDB_Location location = pdb_symbol_get_location(symbol);
            if (location.type != PDB_Location_Type::STATIC) {
                break;
            }
            function.debug_start_location = optional_make_success(location.options.static_loc);
            break;
        }
        case SymTagFuncDebugEnd:
        {
            if (source_info_index == -1) {
                break;
            }
            auto& function = info->source_infos[source_info_index];
            if (function.debug_end_location.available) {
                break;
            }
            PDB_Location location = pdb_symbol_get_location(symbol);
            if (location.type != PDB_Location_Type::STATIC) {
                break;
            }
            function.debug_end_location = optional_make_success(location.options.static_loc);
            break;
        }

        case SymTagLabel: break;
        case SymTagExe: break;
        case SymTagCompilandDetails: break;
        case SymTagCompilandEnv: break;

            // User defined types (structs, enums, classes)
        case SymTagUDT: break;
        case SymTagEnum: break;
        case SymTagTypedef: break;
        case SymTagBaseClass: break;

            // Types/Type-Modifierz
        case SymTagFunctionArgType: break;
        case SymTagFunctionType: break;
        case SymTagPointerType: break;
        case SymTagArrayType: break;
        case SymTagBaseType: break;
        case SymTagFriend: break;
        case SymTagCustomType: break;
        case SymTagManagedType: break;
        case SymTagVectorType: break;
        case SymTagMatrixType: break;

            // Maybe interesting
        case SymTagPublicSymbol: break;
        case SymTagNull: break;
        case SymTagCallSite: break;
        case SymTagInlineSite: break;

            // Not interesting
        case SymTagAnnotation: break;
        case SymTagUsingNamespace: break;
        case SymTagVTableShape: break;
        case SymTagVTable: break;
        case SymTagCustom: break;
        case SymTagThunk: break;
        case SymTagDimension: break;
        case SymTagBaseInterface: break;
        case SymTagHLSLType: break;
        case SymTagCaller: break;
        case SymTagCallee: break;
        case SymTagExport: break;
        case SymTagHeapAllocationSite: break;
        case SymTagCoffGroup: break;
        case SymTagInlinee: break;
        default: break;
        }
    }

    bool pdb_information_fill_from_file(PDB_Information* information, const char* filepath, const char* main_compiland_name)
    {
        Dynamic_Array<wchar_t> wide_string_buffer = dynamic_array_create<wchar_t>(64);
        SCOPE_EXIT(dynamic_array_destroy(&wide_string_buffer));
        String string_buffer = string_create(128);
        SCOPE_EXIT(string_destroy(&string_buffer));

        // Initialize COM
        HRESULT result = CoInitialize(NULL);
        if (FAILED(result)) {
            printf("CoInitialize failed!\n");
            return false;
        }
        SCOPE_EXIT(CoUninitialize());

        // Create DiaDataSource
        IDiaDataSource* data_source = NULL;
        result = CoCreateInstance(__uuidof(DiaSource), NULL, CLSCTX_INPROC_SERVER, __uuidof(IDiaDataSource), (void**)&data_source);
        if (FAILED(result)) {
            printf("CoCreateInstance failed!\n");
            return false;
        }
        SCOPE_EXIT(if (data_source != NULL) { data_source->Release(); data_source = NULL; });

        // Load PDB file
        wide_string_from_utf8(&wide_string_buffer, filepath);
        result = data_source->loadDataFromPdb(wide_string_buffer.data);
        if (FAILED(result)) {
            printf("LoadDataFromPdb failed!\n");
            return false;
        }

        // Open session for querying symbols
        IDiaSession* session = NULL;
        if (FAILED(data_source->openSession(&session))) {
            printf("OpenSession failed!\n");
            return false;
        }
        SCOPE_EXIT(if (session != NULL) { session->Release(); session = NULL; });

        IDiaSymbol* global_scope = NULL;
        if (FAILED(session->get_globalScope(&global_scope))) {
            printf("GetGlobalScope failed!\n");
            return false;
        }
        SCOPE_EXIT(if (global_scope != NULL) { global_scope->Release(); global_scope = NULL; });

        DWORD machine_type;
        if (global_scope->get_machineType(&machine_type) != S_OK) {
            printf("get_machine_type failed!\n");
            return false;
        }
        if (machine_type != IMAGE_FILE_MACHINE_AMD64) {
            printf("Machine type of pdb was not amd64! This is the only architecture currently supported\n");
            return false;
        }

        // Loop over all compilands
        IDiaEnumSymbols* compiland_iter = NULL;
        if (FAILED(global_scope->findChildren(SymTagCompiland, NULL, nsNone, &compiland_iter))) {
            printf("Enumerating compilands failed!\n");
            return false;
        }
        SCOPE_EXIT(if (compiland_iter != NULL) { compiland_iter->Release(); compiland_iter = NULL; });

        ULONG celt = 0;
        IDiaSymbol* compiland = NULL;
        while (compiland_iter->Next(1, &compiland, &celt) == S_OK && celt == 1)
        {
            bool is_main_compiland = false;
            BSTR wide_compiland_string;
            if (compiland->get_name(&wide_compiland_string) == S_OK)
            {
                wide_string_to_utf8(wide_compiland_string, &string_buffer);
                SysFreeString(wide_compiland_string);

                string_replace_character(&string_buffer, '\\', '/');
                //printf("Compiland name: \"%s\"\n", string_buffer.characters);
                if (string_equals_cstring(&string_buffer, main_compiland_name)) {
                    is_main_compiland = true;
                }
            }

            pdb_symbol_analyse_recursive(compiland, information, is_main_compiland, -1, -1, session);
            compiland->Release();
        }

        return true;
    }
}

namespace Process_Memory
{
    template<typename T>
    bool read_single_value(HANDLE process_handle, void* virtual_address, T* out_data) {
        if (virtual_address == nullptr || process_handle == nullptr) return false;
        u64 bytes_written = 0;
        if (!ReadProcessMemory(process_handle, virtual_address, (void*)out_data, sizeof(T), &bytes_written)) {
            return false;
        }
        return bytes_written == sizeof(T);
    }

    bool write_byte(HANDLE process_handle, void* virtual_address, u8 value) {
        if (virtual_address == nullptr || process_handle == nullptr) return false;
        u64 bytes_written = 0;
        BOOL success = WriteProcessMemory(process_handle, virtual_address, &value, 1, nullptr);
        return success != 0;
    }

    template<typename T>
    bool read_array(HANDLE process_handle, void* virtual_address, Dynamic_Array<T>* buffer, u64 expected_count) {
        dynamic_array_reset(buffer);
        if (virtual_address == nullptr || expected_count == 0 || process_handle == nullptr) return false;
        dynamic_array_reserve(buffer, expected_count);
        u64 bytes_written = 0;
        if (!ReadProcessMemory(process_handle, virtual_address, (void*)buffer->data, sizeof(T) * expected_count, &bytes_written)) {
            return false;
        }
        if (bytes_written == sizeof(T) * expected_count) {
            buffer->size = expected_count;
            return true;
        }
        buffer->size = 0;
        return false;
    }

    void read_as_much_as_possible(HANDLE process_handle, void* virtual_address, Dynamic_Array<u8>* out_bytes, u64 read_size)
    {
        dynamic_array_reset(out_bytes);
        dynamic_array_reserve(out_bytes, read_size);

        if (virtual_address == nullptr || read_size == 0 || process_handle == nullptr) {
            return;
        }

        // Check if reading whole read_size succeeds
        size_t bytes_written = 0;
        if (ReadProcessMemory(process_handle, virtual_address, out_bytes->data, read_size, &bytes_written) != 0) {
            out_bytes->size = read_size;
            return;
        }

        // Otherwise find largest read size with VirtualQueryEx
        {
            MEMORY_BASIC_INFORMATION memory_info;
            int written_bytes = VirtualQueryEx(process_handle, virtual_address, &memory_info, sizeof(memory_info));
            if (written_bytes == 0) { // Virtual query failed
                return;
            }

            if (memory_info.State != MEM_COMMIT) {
                return;
            }

            i64 max_read_length = (i64)memory_info.RegionSize - ((i64)virtual_address - (i64)memory_info.BaseAddress);
            if (max_read_length <= 0) {
                return;
            }
            read_size = math_minimum((u64)max_read_length, read_size);
        }

        // Try reading size again
        if (ReadProcessMemory(process_handle, virtual_address, out_bytes->data, read_size, &bytes_written) != 0) {
            return;
        }
        out_bytes->size = (int)bytes_written;
    }

    bool read_string(HANDLE process_handle, void* virtual_address, String* out_string, u64 max_size, bool is_wide_char, Dynamic_Array<u8>* byte_buffer)
    {
        string_reset(out_string);
        if (virtual_address == nullptr || max_size == 0 || process_handle == nullptr) {
            return false;
        }
        if (is_wide_char) {
            max_size = 2 * max_size + 1;
        }

        read_as_much_as_possible(process_handle, virtual_address, byte_buffer, max_size);
        if (byte_buffer->size == 0) {
            return false;
        }

        if (is_wide_char)
        {
            const wchar_t* char_ptr = (wchar_t*)byte_buffer->data;
            int max_length = max_size / 2;
            int wchar_count = -1;
            for (int i = 0; i < max_length; i++) {
                if (char_ptr[i] == 0) {
                    wchar_count = i;
                    break;
                }
            }

            // If string wasn't null-terminated, return false...
            if (wchar_count == -1) {
                return false;
            }

            wide_string_to_utf8((wchar_t*)byte_buffer->data, out_string);
            return out_string->size > 0;
        }
        else {
            string_reserve(out_string, byte_buffer->size + 1);
            memory_copy(out_string->characters, byte_buffer->data, byte_buffer->size);
            out_string->characters[byte_buffer->size] = '\0';
            out_string->size = (int)strlen(out_string->characters);
        }
        return true;
    }
}

namespace PE_Analysis
{
    // Export Infos
    struct Export_Symbol_Info
    {
        u32 rva; // Relative Virtual Address...
        Optional<String> name; // Symbols may not have a name if they are referenced by ordinal
        Optional<String> forwarder_name; // If the symbol is a forward to another dll
    };

    // Section Infos
    struct Section_Info
    {
        String name;
        int section_index;
        u64 rva;
        u64 size;

        bool flag_read;
        bool flag_write;
        bool flag_execute;
    };

    enum class Unwind_Code_Type
    {
        PUSH_REG,                   // Pushes register onto stack (e.g decreases RSP by 8)
        SAVE_REG,                   // Saves register to stack (offset from rsp)
        SET_FRAME_POINTER_REGISTER, // Set frame_pointer to offset from RSP
        SAVE_XMM_128,               // Saves all 128 Bits of a non-volatile XMM register on the stack
        ALLOC,                      // Moves stack pointer by specific size (Decrements RSP by alloc-size)
    };

    struct Unwind_Code
    {
        Unwind_Code_Type type;
        u8 instruction_offset; // Instruction offset from function start
        union {
            X64_Integer_Register push_reg;
            struct {
                X64_Integer_Register reg;
                u32 offset_from_rsp;
            } save_reg;
            struct {
                X64_Integer_Register fp_reg; // Usually rbp, but could also be other...
                u32 offset_from_rsp;
            } set_frame_pointer;
            struct {
                u32 xmm_number;
                u32 offset_from_rsp;
            } save_xmm_128;
            u32 alloc_size;
        } options;
    };

    struct Unwind_Block
    {
        u32 size_of_prolog;
        Dynamic_Array<Unwind_Code> unwind_codes;
        bool parsed_successfully; // If not, then we cannot do stack walking from this frame onwards
        int next_chained_unwind_block_index; // -1 if not available
    };

    struct Function_Unwind_Info
    {
        u64 start_rva;
        u64 end_rva;
        int unwind_block_index;
    };

    struct PE_Info
    {
        u64 base_address; // VirtualAddress where PE image is loaded
        String name; // Name of executable or dll
        String pdb_name;
        Dynamic_Array<Export_Symbol_Info> exported_symbols;
        Dynamic_Array<Section_Info> sections;
        Dynamic_Array<Function_Unwind_Info> function_unwind_infos;
        Dynamic_Array<Unwind_Block> unwind_blocks;
    };

    PE_Info pe_info_create()
    {
        PE_Info info;
        info.base_address = 0;
        info.name = string_create();
        info.pdb_name = string_create();
        info.exported_symbols = dynamic_array_create<Export_Symbol_Info>();
        info.sections = dynamic_array_create<Section_Info>();
        info.function_unwind_infos = dynamic_array_create<Function_Unwind_Info>();
        info.unwind_blocks = dynamic_array_create<Unwind_Block>();
        return info;
    }

    void pe_info_destroy(PE_Info* info)
    {
        string_destroy(&info->name);
        string_destroy(&info->pdb_name);

        for (int i = 0; i < info->exported_symbols.size; i++) {
            auto& symbol = info->exported_symbols[i];
            if (symbol.name.available) {
                string_destroy(&symbol.name.value);
            }
            if (symbol.forwarder_name.available) {
                string_destroy(&symbol.forwarder_name.value);
            }
        }
        dynamic_array_destroy(&info->exported_symbols);

        for (int i = 0; i < info->unwind_blocks.size; i++) {
            auto block = info->unwind_blocks[i];
            dynamic_array_destroy(&block.unwind_codes);
        }
        dynamic_array_destroy(&info->unwind_blocks);

        for (int i = 0; i < info->sections.size; i++) {
            string_destroy(&info->sections[i].name);
        }
        dynamic_array_destroy(&info->sections);

        dynamic_array_destroy(&info->function_unwind_infos);
    }

    struct PDB_INFO_DUMMY
    {
        u32 signature;
        GUID guid;
        u32 age;
    };

    const int MAX_STRING_LENGTH = 260;

    struct UNWIND_INFO
    {
        u8 Version : 3;
        u8 Flags : 5;
        u8 SizeOfProlog;
        u8 CountOfCodes;
        u8 FrameRegister : 4;
        u8 FrameOffset : 4;
    };

    union Unwind_Code_Slot {
        struct {
            u8 CodeOffset;
            u8 UnwindOp : 4;
            u8 OpInfo : 4;
        };
        u16 FrameOffset;
    };

    enum class UNWIND_OP_CODES {
        UWOP_PUSH_NONVOL = 0, /* info == register number */
        UWOP_ALLOC_LARGE,     /* no info, alloc size in next 2 slots */
        UWOP_ALLOC_SMALL,     /* info == size of allocation / 8 - 1 */
        UWOP_SET_FPREG,       /* no info, FP = RSP + UNWIND_INFO.FPRegOffset*16 */
        UWOP_SAVE_NONVOL,     /* info == register number, offset in next slot */
        UWOP_SAVE_NONVOL_FAR, /* info == register number, offset in next 2 slots */
        UWOP_SAVE_XMM128 = 8, /* info == XMM reg number, offset in next slot */
        UWOP_SAVE_XMM128_FAR, /* info == XMM reg number, offset in next 2 slots */
        UWOP_PUSH_MACHFRAME   /* info == 0: no error-code, 1: error-code */
    };

    u64 hash_function_unwind_info(Function_Unwind_Info* fn_info) {
        u64 hash = hash_u64(&fn_info->start_rva);
        hash = hash_combine(hash, hash_u64(&fn_info->end_rva));
        hash = hash_combine(hash, hash_i32(&fn_info->unwind_block_index));
        return hash;
    }

    bool equals_function_unwind_info(Function_Unwind_Info* a, Function_Unwind_Info* b) {
        return a->start_rva == b->start_rva && a->end_rva == b->end_rva && a->unwind_block_index == b->unwind_block_index;
    }

    void add_function_unwind_info(PE_Info* pe_info, Function_Unwind_Info info, Hashset<Function_Unwind_Info>* already_generated_infos)
    {
        if (hashset_insert_element(already_generated_infos, info)) {
            dynamic_array_push_back(&pe_info->function_unwind_infos, info);
        }
        else {
            //printf("found duplicated function_unwind_info\n");
        }
    }

    int add_unwind_block(
        PE_Info* pe_info, HANDLE process_handle, u64 unwind_data_rva, u64 base_virtual_address,
        Hashtable<u64, int>* already_generated_blocks, Hashset<Function_Unwind_Info>* already_generated_fn_infos,
        Dynamic_Array<Unwind_Code_Slot>& code_slot_buffer)
    {
        // Check if we have already generated this unwind_info
        {
            int* found_unwind_block_index = hashtable_find_element(already_generated_blocks, unwind_data_rva);
            if (found_unwind_block_index != nullptr) {
                //printf("found duplicated unwind block\n");
                return *found_unwind_block_index;
            }
        }

        // Analyse unwind info
        Unwind_Block unwind_block;
        unwind_block.next_chained_unwind_block_index = -1;
        unwind_block.parsed_successfully = true;
        unwind_block.size_of_prolog = 0;
        unwind_block.unwind_codes = dynamic_array_create<Unwind_Code>();

        // Load unwind data
        UNWIND_INFO unwind_info;
        bool success = Process_Memory::read_single_value(
            process_handle,
            (void*)(base_virtual_address + unwind_data_rva),
            &unwind_info
        );
        if (success) {
            unwind_block.size_of_prolog = unwind_info.SizeOfProlog;
            if (unwind_info.Version != 1) {
                //printf("    Found unwind_info with weird version value: %d\n", unwind_info.Version);
                success = false;
            }
        }
        else {
            printf("    Could not load unwind data\n");
        }

        // Read unwind_code slots (Immediately after unwind data in memory)
        if (success)
        {
            success = Process_Memory::read_array(
                process_handle,
                (void*)(base_virtual_address + unwind_data_rva + sizeof(UNWIND_INFO)),
                &code_slot_buffer,
                unwind_info.CountOfCodes
            );
        }

        // Parse unwind codes
        int slot_index = 0;
        while (slot_index < code_slot_buffer.size && success)
        {
            Unwind_Code_Slot slot = code_slot_buffer[slot_index];

            Unwind_Code code;
            code.instruction_offset = slot.CodeOffset;
            int slot_count = 1;

            auto parse_next_slot_as_value = [&](bool double_slot) -> u32
            {
                int used_slots = double_slot ? 2 : 1;
                slot_count = 1 + used_slots;
                if (slot_index + used_slots >= code_slot_buffer.size) {
                    printf("   Invalid unwind code, expected more slots\n");
                    success = false;
                    return 0;
                }

                u32 value = 0;
                if (double_slot) {
                    // Note: This depends on byte-order (little/big endian), but on AMD64 this shouldn't matter)
                    return code_slot_buffer[slot_index + 1].FrameOffset + ((u32)(code_slot_buffer[slot_index + 2].FrameOffset) << 16);
                }
                value = code_slot_buffer[slot_index + 1].FrameOffset;
                return value;
            };

            auto to_register_id = [&](u32 value) -> X64_Integer_Register {
                if (value >= (u32)X64_Integer_Register::MAX_VALUE) {
                    printf("Found invalid register_id in unwind code: %d\n", value);
                    success = false;
                    return X64_Integer_Register::RAX;
                }
                return (X64_Integer_Register)value;
            };

            switch ((UNWIND_OP_CODES)slot.UnwindOp)
            {
            case UNWIND_OP_CODES::UWOP_PUSH_NONVOL:
            {
                code.type = Unwind_Code_Type::PUSH_REG;
                code.options.push_reg = to_register_id(slot.OpInfo);
                break;
            }
            case UNWIND_OP_CODES::UWOP_ALLOC_LARGE:
            {
                if (slot.OpInfo != 0 && slot.OpInfo != 1) {
                    printf("   Invalid unwind code encountered\n");
                    success = false;
                    break;
                }
                code.type = Unwind_Code_Type::ALLOC;
                code.options.alloc_size = parse_next_slot_as_value(slot.OpInfo == 1);
                if (slot.OpInfo == 0) {
                    code.options.alloc_size *= 8;
                }
                break;
            }
            case UNWIND_OP_CODES::UWOP_ALLOC_SMALL:
            {
                code.type = Unwind_Code_Type::ALLOC;
                code.options.alloc_size = (u32)(slot.OpInfo) * 8 + 8;
                break;
            }
            case UNWIND_OP_CODES::UWOP_SET_FPREG:
            {
                code.type = Unwind_Code_Type::SET_FRAME_POINTER_REGISTER;
                if (unwind_info.FrameRegister == 0) {
                    printf("   Invalid unwind code, frame_register set to 0\n");
                    success = false;
                    break;
                }
                code.options.set_frame_pointer.fp_reg = to_register_id(unwind_info.FrameRegister);
                code.options.set_frame_pointer.offset_from_rsp = unwind_info.FrameOffset * 16;
                break;
            }
            case UNWIND_OP_CODES::UWOP_SAVE_NONVOL_FAR:
            case UNWIND_OP_CODES::UWOP_SAVE_NONVOL: {
                code.type = Unwind_Code_Type::SAVE_REG;
                code.options.save_reg.reg = to_register_id(slot.OpInfo);
                code.options.save_reg.offset_from_rsp =
                    parse_next_slot_as_value((UNWIND_OP_CODES)slot.UnwindOp == UNWIND_OP_CODES::UWOP_SAVE_NONVOL_FAR);
                break;
            }
            case UNWIND_OP_CODES::UWOP_SAVE_XMM128:
            case UNWIND_OP_CODES::UWOP_SAVE_XMM128_FAR: {
                code.type = Unwind_Code_Type::SAVE_XMM_128;
                code.options.save_xmm_128.xmm_number = slot.OpInfo;
                code.options.save_xmm_128.offset_from_rsp =
                    parse_next_slot_as_value((UNWIND_OP_CODES)slot.UnwindOp == UNWIND_OP_CODES::UWOP_SAVE_XMM128_FAR);
                break;
            }
            case UNWIND_OP_CODES::UWOP_PUSH_MACHFRAME: {
                //printf("    Push_machine_frame should only be used before some interrupt functions, so we don't handle dis\n");
                success = false;
                break;
            }
            default: {
                // printf("    Invalid unwind code encountered, unknown op_code\n");
                success = false;
                break;
            }
            }

            if (success) {
                slot_index += slot_count;
                dynamic_array_push_back(&unwind_block.unwind_codes, code);
            }
        }

        // Store unwind code block
        unwind_block.parsed_successfully = success;
        int unwind_block_index = pe_info->unwind_blocks.size;
        dynamic_array_push_back(&pe_info->unwind_blocks, unwind_block);
        hashtable_insert_element(already_generated_blocks, unwind_data_rva, unwind_block_index);

        // Store chained unwind info
        if (success && unwind_info.Flags & UNW_FLAG_CHAININFO)
        {
            RUNTIME_FUNCTION chain_info;
            success = Process_Memory::read_single_value(
                process_handle,
                (void*)(base_virtual_address + unwind_data_rva + sizeof(UNWIND_INFO) + ((unwind_info.CountOfCodes + 1) & ~1) * sizeof(u16)),
                &chain_info
            );

            if (success)
            {
                int chain_block_index = add_unwind_block(
                    pe_info, process_handle, chain_info.UnwindData,
                    base_virtual_address, already_generated_blocks, already_generated_fn_infos, code_slot_buffer
                );
                pe_info->unwind_blocks[unwind_block_index].next_chained_unwind_block_index = chain_block_index;

                // I'm not sure if I want to add those, because this may cause with resolution during stack walking
                // Function_Unwind_Info fn_unwind_info;
                // fn_unwind_info.start_rva = chain_info.BeginAddress;
                // fn_unwind_info.end_rva = chain_info.EndAddress;
                // fn_unwind_info.unwind_block_index = chain_block_index;
                // add_function_unwind_info(pe_info, fn_unwind_info, already_generated_fn_infos);
            }
        }

        return unwind_block_index;
    }

    bool pe_info_fill_from_executable_image(PE_Info* pe_info, u64 base_virtual_address, HANDLE process_handle, void* image_name_addr_opt, bool name_is_unicode)
    {
        if (base_virtual_address == 0 || process_handle == nullptr) return false;
        pe_info->base_address = base_virtual_address;

        Dynamic_Array<u8> byte_buffer = dynamic_array_create<u8>(512);
        SCOPE_EXIT(dynamic_array_destroy(&byte_buffer));

        // Load name if specified from debugger infos (parameters)
        if (image_name_addr_opt != nullptr) {
            void* address = nullptr;
            bool success = Process_Memory::read_single_value(process_handle, image_name_addr_opt, &address);
            if (success && address != nullptr) {
                Process_Memory::read_string(process_handle, address, &pe_info->name, MAX_STRING_LENGTH, name_is_unicode, &byte_buffer);
            }
        }

        // Load header
        IMAGE_DOS_HEADER header_dos;
        IMAGE_NT_HEADERS64 header_nt;

        IMAGE_DATA_DIRECTORY location_export_table;
        IMAGE_DATA_DIRECTORY location_debug_table;
        IMAGE_DATA_DIRECTORY location_exception_data;

        location_export_table.Size = 0;
        location_debug_table.Size = 0;
        location_exception_data.Size = 0;

        // Read DOS and PE Header
        if (!Process_Memory::read_single_value(process_handle, (void*)base_virtual_address, &header_dos)) {
            return false;
        }
        if (!Process_Memory::read_single_value(process_handle, (void*)(base_virtual_address + header_dos.e_lfanew), &header_nt)) {
            return false;
        }

        if (header_nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
            printf("PE file is not for AMD64!\n");
            return false;
        }

        // Read section information
        if (header_nt.FileHeader.NumberOfSections > 0)
        {
            u64 section_start_offset = header_dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + header_nt.FileHeader.SizeOfOptionalHeader;
            void* section_table_virtual_address = (void*)(base_virtual_address + section_start_offset);
            int section_count = header_nt.FileHeader.NumberOfSections;

            // Note: dynamic_array size is zero if this fails --> Maybe we want to not store success for failure...
            Dynamic_Array<IMAGE_SECTION_HEADER> section_infos = dynamic_array_create<IMAGE_SECTION_HEADER>(section_count);
            SCOPE_EXIT(dynamic_array_destroy(&section_infos));
            Process_Memory::read_array(process_handle, section_table_virtual_address, &section_infos, section_count);

            // Not sure if we want to fully abort if we cannot query section infos, but it's probably the right thing to do...
            if (section_infos.size == 0) {
                return false;
            }

            for (int i = 0; i < section_infos.size; i++)
            {
                IMAGE_SECTION_HEADER& section = section_infos[i];
                char section_name_buffer[9];
                memory_set_bytes(section_name_buffer, 9, 0);
                memory_copy(section_name_buffer, section.Name, 8); // Section name is not null-terminated if size = 8

                Section_Info info;
                info.name = string_create(section_name_buffer);
                info.section_index = i;
                info.size = section.Misc.VirtualSize;
                info.rva = section.VirtualAddress;
                info.flag_read = (section.Characteristics & IMAGE_SCN_MEM_READ) != 0;
                info.flag_write = (section.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
                info.flag_execute = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
                dynamic_array_push_back(&pe_info->sections, info);
            }
        }

        // Find table headers and read data
        location_export_table = header_nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        location_debug_table = header_nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        location_exception_data = header_nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];

        // Read infos from export_table
        IMAGE_EXPORT_DIRECTORY header_export_table;
        bool export_header_loaded = false;
        if (location_export_table.Size != 0) {
            export_header_loaded = Process_Memory::read_single_value(
                process_handle, (void*)(base_virtual_address + location_export_table.VirtualAddress), &header_export_table
            );
        }
        if (export_header_loaded)
        {
            // Load name from export table if not already set
            if (pe_info->name.size == 0 && header_export_table.Name != 0) {
                Process_Memory::read_string(
                    process_handle, (void*)(base_virtual_address + header_export_table.Name), &pe_info->name, MAX_STRING_LENGTH, false, &byte_buffer
                );
            }

            Dynamic_Array<u32> function_locations = dynamic_array_create<u32>(header_export_table.NumberOfFunctions);
            SCOPE_EXIT(dynamic_array_destroy(&function_locations));
            bool function_locations_read = Process_Memory::read_array(
                process_handle,
                (void*)(base_virtual_address + header_export_table.AddressOfFunctions),
                &function_locations,
                header_export_table.NumberOfFunctions
            );
            if (function_locations_read)
            {
                for (int i = 0; i < function_locations.size; i++)
                {
                    u32 rva = function_locations[i];
                    Export_Symbol_Info symbol_info;
                    symbol_info.rva = rva;
                    symbol_info.name = optional_make_failure<String>();
                    symbol_info.forwarder_name = optional_make_failure<String>();

                    bool is_forwarder =
                        rva >= location_export_table.VirtualAddress &&
                        rva < location_export_table.VirtualAddress + location_export_table.Size;
                    if (is_forwarder)
                    {
                        String forwarder_name = string_create();
                        Process_Memory::read_string(process_handle, (void*)(base_virtual_address + rva), &forwarder_name, 260, false, &byte_buffer);
                        // Note: normally forwarder name should have a specific format, but we don't care for now...
                        symbol_info.forwarder_name = optional_make_success(forwarder_name);
                    }
                    dynamic_array_push_back(&pe_info->exported_symbols, symbol_info);
                }
            }

            // Try to read names and symbol_indices
            Dynamic_Array<u16> symbol_indices_unbiased = dynamic_array_create<u16>(header_export_table.NumberOfNames);
            Dynamic_Array<u32> symbol_name_rvas = dynamic_array_create<u32>(header_export_table.NumberOfNames);
            SCOPE_EXIT(dynamic_array_destroy(&symbol_indices_unbiased));
            SCOPE_EXIT(dynamic_array_destroy(&symbol_name_rvas));

            bool indices_available = Process_Memory::read_array(
                process_handle,
                (void*)(base_virtual_address + header_export_table.AddressOfNameOrdinals),
                &symbol_indices_unbiased,
                header_export_table.NumberOfNames
            );
            bool names_available = Process_Memory::read_array(
                process_handle,
                (void*)(base_virtual_address + header_export_table.AddressOfNames),
                &symbol_name_rvas,
                header_export_table.NumberOfNames
            );

            if (indices_available && names_available)
            {
                assert(
                    symbol_indices_unbiased.size == symbol_name_rvas.size && symbol_name_rvas.size == header_export_table.NumberOfNames,
                    "Load should have failed otherwise"
                );

                for (int i = 0; i < symbol_indices_unbiased.size; i++)
                {
                    int export_symbol_index = symbol_indices_unbiased[i];
                    u32 name_rva = symbol_name_rvas[i];
                    if (name_rva == 0) {
                        // printf("Encountered export name entry with name_rva == 0\n");
                        continue;
                    }
                    if (export_symbol_index < 0 || export_symbol_index >= (int)header_export_table.NumberOfFunctions) {
                        // printf("Encountered invalid value for unbiased ordinal value %d", export_symbol_index);
                        continue;
                    }

                    String name = string_create();
                    bool name_read_success = Process_Memory::read_string(
                        process_handle, (void*)(base_virtual_address + name_rva), &name, MAX_STRING_LENGTH, false, &byte_buffer
                    );
                    if (function_locations_read && name_read_success)
                    {
                        auto& symbol = pe_info->exported_symbols[export_symbol_index];
                        if (symbol.name.available) {
                            // printf("Error: Name of extern symbol #%d found twice in name table!\n", export_symbol_index);
                            string_destroy(&name);
                        }
                        else {
                            symbol.name = optional_make_success(name);
                            // printf("    Found symbol: \"%s\"\n", symbol.name.value.characters);
                        }
                    }
                    else {
                        string_destroy(&name);
                    }
                }
            }
        }

        // Read infos from debug table
        if (true)
        {
            int debug_info_count = location_debug_table.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
            for (int i = 0; i < debug_info_count; i++)
            {
                IMAGE_DEBUG_DIRECTORY debug_table_entry;
                bool load_worked = Process_Memory::read_single_value(
                    process_handle,
                    (void*)(base_virtual_address + location_debug_table.VirtualAddress + i * sizeof(IMAGE_DEBUG_DIRECTORY)),
                    &debug_table_entry
                );
                if (!load_worked) {
                    continue;
                }

                if (debug_table_entry.Type == IMAGE_DEBUG_TYPE_FPO) {
                    // printf("Found FPO (Frame pointer ommision) info in debug-table\n");
                }
                if (debug_table_entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW) {
                    continue;
                }

                if (pe_info->pdb_name.size != 0) {
                    // printf("Found pdb info multiple times in debug-table\n");
                    continue;
                }

                // We don't really care about the pdb-info now
                // PDB_INFO_DUMMY dummy_info;
                // read_process_memory_datatype((u8*)base_of_module + debug_table_entry.AddressOfRawData, &dummy_info);

                Process_Memory::read_string(
                    process_handle,
                    (void*)(base_virtual_address + debug_table_entry.AddressOfRawData + sizeof(PDB_INFO_DUMMY)),
                    &pe_info->pdb_name,
                    MAX_STRING_LENGTH,
                    false,
                    &byte_buffer
                );
            }
        }

        // Read Exception_data
        if (location_exception_data.Size != 0)
        {
            int function_count = location_exception_data.Size / sizeof(RUNTIME_FUNCTION);
            Dynamic_Array<RUNTIME_FUNCTION> runtime_functions = dynamic_array_create<RUNTIME_FUNCTION>();
            SCOPE_EXIT(dynamic_array_destroy(&runtime_functions));
            Process_Memory::read_array(
                process_handle,
                (void*)(base_virtual_address + location_exception_data.VirtualAddress),
                &runtime_functions,
                function_count
            );

            // Note: Unwind-infos shouldn't be duplicate in table, but because of chained-unwind-infos, we don't want to reparse already parsed codes
            Hashtable<u64, int> already_analysed_unwind_blocks = hashtable_create_empty<u64, int>(function_count + 1, hash_u64, equals_u64);
            SCOPE_EXIT(hashtable_destroy(&already_analysed_unwind_blocks));

            Hashset<Function_Unwind_Info> already_analysed_function_infos =
                hashset_create_empty<Function_Unwind_Info>(function_count + 1, hash_function_unwind_info, equals_function_unwind_info);
            SCOPE_EXIT(hashset_destroy(&already_analysed_function_infos));

            Dynamic_Array<Unwind_Code_Slot> code_slot_buffer = dynamic_array_create<Unwind_Code_Slot>();
            SCOPE_EXIT(dynamic_array_destroy(&code_slot_buffer));
            for (int i = 0; i < runtime_functions.size; i++)
            {
                auto& runtime_function = runtime_functions[i];

                Function_Unwind_Info fn_unwind_info;
                fn_unwind_info.start_rva = runtime_function.BeginAddress;
                fn_unwind_info.end_rva = runtime_function.EndAddress;
                fn_unwind_info.unwind_block_index = add_unwind_block(
                    pe_info, process_handle, runtime_function.UnwindData, base_virtual_address,
                    &already_analysed_unwind_blocks, &already_analysed_function_infos, code_slot_buffer
                );

                add_function_unwind_info(pe_info, fn_unwind_info, &already_analysed_function_infos);
            }
        }

        return true;
    }

    void print_unwind_code(Unwind_Code code)
    {
        printf("    %d ", code.instruction_offset);
        switch (code.type)
        {
        case Unwind_Code_Type::ALLOC:
            printf("ALLOC: %d\n", code.options.alloc_size);
            break;
        case Unwind_Code_Type::PUSH_REG:
            printf("PUSH_REG: %s\n", x64_integer_register_to_name(code.options.push_reg));
            break;
        case Unwind_Code_Type::SAVE_REG:
            printf(
                "SAVE_REG: %s, offset: %d\n",
                x64_integer_register_to_name(code.options.save_reg.reg),
                code.options.save_reg.offset_from_rsp
            );
            break;
        case Unwind_Code_Type::SAVE_XMM_128:
            printf("SAVE_XMM_128: reg: %d, offset: %d\n", code.options.save_xmm_128.xmm_number, code.options.save_xmm_128.offset_from_rsp);
            break;
        case Unwind_Code_Type::SET_FRAME_POINTER_REGISTER:
            printf(
                "SET_FRAME_POINTER_REGISTER: %s, offset: %d\n",
                x64_integer_register_to_name(code.options.set_frame_pointer.fp_reg),
                code.options.set_frame_pointer.offset_from_rsp
            );
            break;
        }
    }
}




// Debugger
struct Statement_Mapping;
struct IR_Instruction_Mapping;
struct Upp_Line_Mapping;

// Source Mapping Info
struct Machine_Code_Range
{
    u64 start_virtual_address;
    u64 end_virtual_address;
};

struct C_Line_Mapping
{
    int c_line_index; // Global line-index
    Machine_Code_Range range;
    IR_Instruction_Mapping* instruction_mapping;
};

struct IR_Instruction_Mapping
{
    Statement_Mapping* statement;
    IR_Code_Block* code_block;
    int instruction_index;
    Dynamic_Array<C_Line_Mapping*> c_lines;
};

struct Statement_Mapping
{
    AST::Statement* statement;
    Upp_Line_Mapping* line_mapping;
    Dynamic_Array<IR_Instruction_Mapping*> ir_instructions;
};

struct Upp_Line_Mapping
{
    int compilation_unit_mapping_index;
    int line_number;
    Dynamic_Array<Statement_Mapping*> statements;
};

struct Compilation_Unit_Mapping
{
    Compilation_Unit* compilation_unit;
    Dynamic_Array<Upp_Line_Mapping> lines;
};

struct Function_Slot_Mapping
{
    Dynamic_Array<C_Line_Mapping*> c_lines;
    u64 virtual_address_start;
    u64 virtual_address_end;
    String name;
};

struct Machine_Code_Segment
{
    u64 virtual_address_start;
    u64 virtual_address_end;
    int function_slot_index;
    int c_line_index_with_offset;
};

struct Address_Breakpoint
{
    u64 address;
    bool is_software_breakpoint;
    union {
        int hardware_breakpoint_index;
        struct {
            u8 original_byte;
            bool is_installed; // Address breakpoints get installed (e.g. 0xCC is inserted) on demand, when we continue from a thread
            bool remove_after_next_event; // Software breakpoints cannot de deleted
        } software_bp;
    } options;
};

enum class Hardware_Breakpoint_Type
{
    BREAK_ON_EXECUTE,
    BREAK_ON_READ,
    BREAK_ON_READ_OR_WRITE,
};

struct Hardware_Breakpoint
{
    u64 address;
    bool enabled;
    int length_bits; // For data_breakpoints (read/write), 2 bits are used, and this indicated a length of 1, 2, 4, or 8 bytes
    Hardware_Breakpoint_Type type;
};

struct Thread_Info
{
    HANDLE handle;
    DWORD id;
};

const int HARDWARE_BREAKPOINT_COUNT = 4;

struct Debugger
{
    // Process Infos
    HANDLE process_handle; // From CreateProcessA
    HANDLE main_thread_handle; // From CreateProcessA
    HANDLE start_event_process_handle; // From Create_Process_Debug_Event
    HANDLE start_event_thread_handle; // From Create_Process_Debug_Event (This is also included in thread_info)

    DWORD  main_thread_id; // From CreateProcessA
    DWORD  process_id; // From CreateProcessA

    Dynamic_Array<Thread_Info> threads;
    Dynamic_Array<PE_Analysis::PE_Info> pe_infos;
    PDB_Analysis::PDB_Information* pdb_info;
    int main_thread_info_index; // The main thread is determined by the thread that starts executing main
    int exe_pe_info_index; // PE-Info for executable (Determined by first Create_Process debug event)

    // Debugger Data
    Debugger_State state;
    Dynamic_Array<Stack_Frame> stack_frames;
    Dynamic_Array<Address_Breakpoint> address_breakpoints;
    Dynamic_Array<Source_Breakpoint*> source_breakpoints;

    // Source to assembly mapping
    Dynamic_Array<Compilation_Unit_Mapping> compilation_unit_mapping;
    Dynamic_Array<Statement_Mapping> statement_mapping;
    Dynamic_Array<IR_Instruction_Mapping> ir_instruction_mapping;
    Dynamic_Array<C_Line_Mapping> c_line_mapping;
    Dynamic_Array<Function_Slot_Mapping> function_slot_mapping;

    // Helpers
    String string_buffer;
    Dynamic_Array<u8> byte_buffer;
    Dynamic_Array<INSTRUX> disassembly_buffer;
    Hardware_Breakpoint hardware_breakpoints[HARDWARE_BREAKPOINT_COUNT];

    // Event handling
    DEBUG_EVENT last_debug_event;
    DWORD continue_status;
    bool last_debug_event_requires_handling;
    int event_count;

    int last_stack_walk_event_count;
};

Debugger* debugger_create()
{
    Debugger* result = new Debugger;

    result->threads = dynamic_array_create<Thread_Info>();
    result->main_thread_info_index = -1;
    result->pe_infos = dynamic_array_create<PE_Analysis::PE_Info>();
    result->exe_pe_info_index = -1;
    result->pdb_info = nullptr;

    result->state.process_state = Debug_Process_State::NO_ACTIVE_PROCESS;
    result->stack_frames = dynamic_array_create<Stack_Frame>();
    result->address_breakpoints = dynamic_array_create<Address_Breakpoint>();
    result->source_breakpoints = dynamic_array_create<Source_Breakpoint*>();

    result->compilation_unit_mapping = dynamic_array_create<Compilation_Unit_Mapping>();
    result->statement_mapping = dynamic_array_create<Statement_Mapping>();
    result->ir_instruction_mapping = dynamic_array_create<IR_Instruction_Mapping>();
    result->c_line_mapping = dynamic_array_create<C_Line_Mapping>();
    result->function_slot_mapping = dynamic_array_create<Function_Slot_Mapping>();

    result->string_buffer = string_create();
    result->byte_buffer = dynamic_array_create<u8>();
    result->disassembly_buffer = dynamic_array_create<INSTRUX>();

    debugger_reset(result);

    return result;
}

void debugger_reset(Debugger* debugger, Debugger_Reset_Type reset_type)
{
    // Terminate running process if any
    if (debugger->state.process_state != Debug_Process_State::NO_ACTIVE_PROCESS)
    {
        if (debugger->state.process_state == Debug_Process_State::HALTED) {
            ContinueDebugEvent(debugger->last_debug_event.dwProcessId, debugger->last_debug_event.dwThreadId, DBG_EXCEPTION_NOT_HANDLED);
            debugger->state.process_state = Debug_Process_State::RUNNING;
        }

        // Close all open handles
        for (int i = 0; i < debugger->threads.size; i++) {
            auto& info = debugger->threads[i];
            if (info.handle != nullptr) {
                CloseHandle(info.handle);
            }
            info.handle = nullptr;
        }
        if (debugger->start_event_process_handle != nullptr) {
            CloseHandle(debugger->start_event_process_handle);
            debugger->start_event_process_handle = nullptr;
        }

        switch (reset_type)
        {
        case Debugger_Reset_Type::DETACH_FROM_PROCESS: {
            DebugActiveProcessStop(debugger->process_id);
            break;
        }
        case Debugger_Reset_Type::TERMINATE_PROCESS: {
            TerminateProcess(debugger->process_handle, 69);
            DebugActiveProcessStop(debugger->process_id);
            break;
        }
        case Debugger_Reset_Type::PROCESS_EXIT_RECEIVED: {
            DebugActiveProcessStop(debugger->process_id);
            break;
        }
        default: panic("");
        }

        // Close process handle
        if (debugger->main_thread_handle != nullptr) {
            CloseHandle(debugger->main_thread_handle);
            debugger->main_thread_handle = nullptr;
        }
        if (debugger->process_handle != nullptr) {
            CloseHandle(debugger->process_handle);
            debugger->process_handle = nullptr;
        }

        debugger->state.process_state = Debug_Process_State::NO_ACTIVE_PROCESS;
    }

    debugger->process_handle = nullptr;
    debugger->main_thread_handle = nullptr;
    debugger->start_event_process_handle = nullptr;
    debugger->start_event_thread_handle = nullptr;

    debugger->process_id = -1;
    debugger->main_thread_id = -1;

    debugger->exe_pe_info_index = -1;
    debugger->main_thread_info_index = -1;
    debugger->last_debug_event_requires_handling = false;
    debugger->event_count = 0;
    debugger->last_stack_walk_event_count = -1;

    for (int i = 0; i < HARDWARE_BREAKPOINT_COUNT; i++) {
        auto& bp = debugger->hardware_breakpoints[i];
        bp.address = 0;
        bp.enabled = false;
        bp.length_bits = 0;
        bp.type = Hardware_Breakpoint_Type::BREAK_ON_EXECUTE;
    }

    for (int i = 0; i < debugger->pe_infos.size; i++) {
        PE_Analysis::pe_info_destroy(&debugger->pe_infos[i]);
    }
    dynamic_array_reset(&debugger->pe_infos);

    if (debugger->pdb_info != nullptr) {
        PDB_Analysis::pdb_information_destroy(debugger->pdb_info);
        debugger->pdb_info = nullptr;
    }

    dynamic_array_reset(&debugger->threads);
    dynamic_array_reset(&debugger->stack_frames);
    dynamic_array_reset(&debugger->address_breakpoints);
    dynamic_array_reset(&debugger->byte_buffer);
    dynamic_array_reset(&debugger->disassembly_buffer);
    string_reset(&debugger->string_buffer);

    for (int i = 0; i < debugger->source_breakpoints.size; i++) {
        Source_Breakpoint* bp = debugger->source_breakpoints[i];
        dynamic_array_destroy(&bp->addresses);
        delete bp;
    }
    dynamic_array_reset(&debugger->source_breakpoints);

    // Reset mappings
    for (int i = 0; i < debugger->compilation_unit_mapping.size; i++) {
        auto unit = debugger->compilation_unit_mapping[i];
        for (int k = 0; k < unit.lines.size; k++) {
            auto line_mapping = unit.lines[k];
            dynamic_array_destroy(&line_mapping.statements);
        }
        dynamic_array_destroy(&unit.lines);
    }
    dynamic_array_reset(&debugger->compilation_unit_mapping);

    for (int i = 0; i < debugger->statement_mapping.size; i++) {
        dynamic_array_destroy(&debugger->statement_mapping[i].ir_instructions);
    }
    dynamic_array_reset(&debugger->statement_mapping);

    for (int i = 0; i < debugger->ir_instruction_mapping.size; i++) {
        dynamic_array_destroy(&debugger->ir_instruction_mapping[i].c_lines);
    }
    dynamic_array_reset(&debugger->ir_instruction_mapping);

    for (int i = 0; i < debugger->function_slot_mapping.size; i++) {
        dynamic_array_destroy(&debugger->function_slot_mapping[i].c_lines);
    }
    dynamic_array_reset(&debugger->function_slot_mapping);
    dynamic_array_reset(&debugger->c_line_mapping);
}

void debugger_destroy(Debugger* debugger)
{
    debugger_reset(debugger);

    debugger->process_handle = nullptr;
    debugger->process_id = -1;
    debugger->exe_pe_info_index = -1;
    debugger->main_thread_info_index = -1;
    debugger->last_debug_event_requires_handling = false;

    dynamic_array_destroy(&debugger->pe_infos);
    dynamic_array_destroy(&debugger->threads);
    dynamic_array_destroy(&debugger->stack_frames);
    dynamic_array_destroy(&debugger->address_breakpoints);
    dynamic_array_destroy(&debugger->byte_buffer);
    dynamic_array_destroy(&debugger->disassembly_buffer);
    string_destroy(&debugger->string_buffer);

    dynamic_array_destroy(&debugger->source_breakpoints);

    dynamic_array_destroy(&debugger->compilation_unit_mapping);
    dynamic_array_destroy(&debugger->statement_mapping);
    dynamic_array_destroy(&debugger->ir_instruction_mapping);
    dynamic_array_destroy(&debugger->function_slot_mapping);
    dynamic_array_destroy(&debugger->c_line_mapping);

    delete debugger;
}



// DEBUGGER ADDRESS TRANSLATIONS
u64 static_location_to_virtual_address(Debugger* debugger, PDB_Analysis::PDB_Location_Static location)
{
    if (debugger->state.process_state == Debug_Process_State::NO_ACTIVE_PROCESS) return 0;
    if (debugger->exe_pe_info_index == -1) return 0;

    auto& pe_info = debugger->pe_infos[debugger->exe_pe_info_index];
    int section_index = location.section_index - 1;
    if (section_index < 0 || section_index >= pe_info.sections.size) return 0;
    return pe_info.base_address + pe_info.sections[section_index].rva + location.offset;
}

PE_Analysis::PE_Info* debugger_find_module_of_address(Debugger* debugger, u64 address, bool must_be_executable_section)
{
    for (int i = 0; i < debugger->pe_infos.size; i++)
    {
        auto& info = debugger->pe_infos[i];
        auto& sections = info.sections;
        for (int j = 0; j < sections.size; j++)
        {
            auto& section = sections[j];
            if (!section.flag_execute && must_be_executable_section) continue;

            u64 section_start_addr = info.base_address + section.rva;
            if (address >= section_start_addr && address < section_start_addr + section.size) {
                return &debugger->pe_infos[i];
            }
        }
    }

    return nullptr;
}

u64 debugger_find_address_of_function(Debugger* debugger, String name)
{
    if (debugger->pdb_info == nullptr || debugger->exe_pe_info_index == -1) return 0;
    // Search in pdb for function with this name...
    auto pdb_info = debugger->pdb_info;
    for (int i = 0; i < pdb_info->functions.size; i++)
    {
        auto& function = pdb_info->functions[i];
        if (!string_equals(&name, &function.name)) continue;

        int section_index = function.location.section_index - 1;
        u64 offset = function.location.offset;

        auto& main_pe = debugger->pe_infos[debugger->exe_pe_info_index];
        auto& sections = main_pe.sections;
        if (section_index >= 0 && section_index < sections.size) {
            auto& section = sections[section_index];
            return main_pe.base_address + section.rva + offset;
        }
    }
    return 0;
}

String* debugger_find_closest_symbol_name(Debugger* debugger, u64 address, u64* out_dist = nullptr, String* out_dll_name = nullptr)
{
    if (debugger->pe_infos.size == 0) return nullptr;

    // Check if address is inside function found in pdb
    if (debugger->pdb_info != nullptr)
    {
        auto& main_pe = debugger->pe_infos[debugger->exe_pe_info_index];
        auto& section_infos = main_pe.sections;
        for (int i = 0; i < debugger->pdb_info->functions.size; i++)
        {
            auto& function = debugger->pdb_info->functions[i];
            auto loc = function.location;
            if (loc.section_index == 0) continue; // Section 0 should not be valid?

            int section_index = loc.section_index - 1; // Sections indices are 1 based...
            if (section_index < 0 || section_index >= section_infos.size) continue;

            auto& section = section_infos[section_index];
            u64 fn_address = main_pe.base_address + section.rva + loc.offset;

            if (address >= fn_address && address < fn_address + function.length)
            {
                if (out_dist != nullptr) {
                    *out_dist = address - fn_address;
                }
                return &function.name;
            }
        }
    }

    // Otherwise find closest address from export-table of loaded Dll/PEs
    String* closest_name = nullptr;
    u64 closest_distance = (u64)(i64)-1;

    auto& pe_infos = debugger->pe_infos;
    for (int i = 0; i < pe_infos.size; i++)
    {
        auto& pe_info = pe_infos[i];
        for (int i = 0; i < pe_info.exported_symbols.size; i++) {
            auto& symbol = pe_info.exported_symbols[i];
            if (symbol.forwarder_name.available || !symbol.name.available) continue;

            u64 symbol_address = symbol.rva + pe_info.base_address;
            if (address < symbol_address) continue;

            u64 distance = address - symbol_address;
            if (distance < closest_distance) {
                closest_name = &symbol.name.value;
                closest_distance = distance;
                if (out_dll_name != nullptr) {
                    *out_dll_name = pe_info.name;
                }
            }
        }
    }


    if (closest_name != nullptr && out_dist != nullptr) {
        *out_dist = closest_distance;
    }
    return closest_name;
}

u64 debugger_find_address_of_c_line_from_pdb_info(Debugger* debugger, int line_index)
{
    if (debugger->state.process_state == Debug_Process_State::NO_ACTIVE_PROCESS || debugger->pdb_info == nullptr) return 0;

    for (int i = 0; i < debugger->pdb_info->source_infos.size; i++) {
        auto& fn_info = debugger->pdb_info->source_infos[i];
        for (int j = 0; j < fn_info.line_infos.size; j++) {
            auto& line_info = fn_info.line_infos[j];
            if (line_info.line_num == line_index + 1) {
                u64 address = static_location_to_virtual_address(debugger, line_info.location);
                if (address != 0) return address;
            }
        }
    }

    return 0;
}

// Returns true if the whole read_size was disassembled. If only partial read was successfull, false is returned, but out_instructions is filled
bool debugger_disassemble_bytes(Debugger* debugger, u64 virtual_address, u32 read_size)
{
    auto& instructions = debugger->disassembly_buffer;
    auto& byte_buffer = debugger->byte_buffer;
    dynamic_array_reset(&instructions);

    // Read bytes
    Process_Memory::read_as_much_as_possible(debugger->process_handle, (void*)virtual_address, &byte_buffer, read_size);

    // Handle software breakpoints
    for (int i = 0; i < debugger->address_breakpoints.size; i++) {
        const auto& bp = debugger->address_breakpoints[i];
        if (!bp.is_software_breakpoint) continue;
        if (bp.address < virtual_address || bp.address >= virtual_address + byte_buffer.size) continue;
        int offset = bp.address - virtual_address;
        assert(offset >= 0 && offset < byte_buffer.size, "");
        byte_buffer[offset] = bp.options.software_bp.original_byte;
    }

    // Disassemble bytes
    u32 byte_index = 0;
    dynamic_array_reserve(&instructions, byte_buffer.size / 4); // Reserve buffer size assuming a normal instruction stakes ~4 bytes
    while (byte_index < (u32) byte_buffer.size)
    {
        INSTRUX instruction;
        NDSTATUS status = NdDecodeEx(&instruction, byte_buffer.data + byte_index, byte_buffer.size - byte_index, ND_CODE_64, ND_DATA_64);
        if (!ND_SUCCESS(status)) {
            break;
        }
        assert(instruction.Length > 0, "");
        dynamic_array_push_back(&instructions, instruction);
        byte_index += instruction.Length;
    }

    return byte_index >= read_size;
}

void debugger_print_last_disassembly(Debugger* debugger, u64 address, int indentation_spaces, bool print_addresses = true, bool print_raw_bytes = true)
{
    String str = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&str));

    auto& instructions = debugger->disassembly_buffer;
    auto& byte_buffer = debugger->byte_buffer;

    u32 byte_index = 0;
    for (int i = 0; i < instructions.size; i++)
    {
        INSTRUX instr = instructions.data[i];

        for (int i = 0; i < indentation_spaces; i++) {
            printf(" ");
        }
        // Print bytes of instruction
        if (print_addresses) {
            printf("[0x%08llX] ", address + byte_index);
        }
        if (print_raw_bytes)
        {
            for (int i = 0; i < 6; i++)
            {
                if (i < instr.Length) {
                    if (i == 5 && instr.Length > 6) {
                        printf(".. ");
                    }
                    else {
                        printf("%02X ", (i32)(byte_buffer.data[byte_index + i]));
                    }
                }
                else {
                    printf("   ");
                }
            }
        }

        // Print instruction
        NdToText(&instr, address + byte_index, str.capacity - 1, str.characters);
        str.size = (int)strlen(str.characters);
        printf("%s\n", str.characters);
        byte_index += instr.Length;
    }
}

void do_stack_walk(Debugger* debugger)
{
    if (debugger->last_stack_walk_event_count == debugger->event_count) {
        return;
    }

    dynamic_array_reset(&debugger->stack_frames);
    if (debugger->state.process_state != Debug_Process_State::HALTED) return;
    debugger->last_stack_walk_event_count = debugger->event_count;

    CONTEXT context;
    context.ContextFlags = CONTEXT_ALL;
    auto& main_thread_info = debugger->threads[debugger->main_thread_info_index];
    if (!GetThreadContext(main_thread_info.handle, &context)) {
        printf("Couldn't retrieve thread context!");
        helper_print_last_error();
        return;
    }

    u64 stack_min_address;
    u64 stack_max_address;
    {
        MEMORY_BASIC_INFORMATION memory_info;
        int written_bytes = VirtualQueryEx(debugger->process_handle, (void*)context.Rsp, &memory_info, sizeof(memory_info));
        if (written_bytes == 0) {
            printf("Could not determine stack start address\n");
            return;
        }

        if (memory_info.State != MEM_COMMIT) {
            return;
        }
        stack_min_address = context.Rsp;
        stack_max_address = ((u64)memory_info.BaseAddress) + memory_info.RegionSize;
        assert(stack_min_address <= stack_max_address, "");
    }

    auto& disassembly_buffer = debugger->disassembly_buffer;

    const int max_depth = 8;
    int frame_depth = 0;
    while (true)
    {
        // Check for exit criteria (Left stack-region or return address invalid)
        {
            if (context.Rsp > stack_max_address) {
                printf("Rsp went out of stack-space (Above stack end)\n");
                return;
            }
            else if (context.Rsp < stack_min_address) {
                printf("Rsp went out of stack-space (Below stack-start)\n");
                return;
            }
            else if (context.Rip == 0) {
                printf("Rip is null (Return address on stack was null...)\n");
                return;
            }
            else if (frame_depth >= max_depth) {
                printf("Reached max frame_depth\n");
                return;
            }
        }

        // Find module of current Rip
        PE_Analysis::PE_Info* found_module = debugger_find_module_of_address(debugger, context.Rip, true);
        if (found_module == nullptr) {
            printf("Module for Rip address: %p not found\n", (void*)context.Rip);
            return;
        }

        // Store frame info
        {
            Stack_Frame frame_info;
            frame_info.instruction_pointer = context.Rip;

            Machine_Code_Address_To_Line_Result line_result = source_mapping_machine_instruction_address_to_upp_line(debugger, context.Rip);
            if (line_result.unit == nullptr || line_result.line_index == -1 || line_result.function_slot == -1 || line_result.statement == nullptr)
            {
                frame_info.inside_upp_function = false;
                u64 out_distance = 0;
                String out_dll_name = string_create_static("");
                String* symbol_name = debugger_find_closest_symbol_name(debugger, context.Rip, &out_distance, &out_dll_name);
                if (symbol_name == nullptr) {
                    frame_info.options.other.symbol_name = string_create_static("");
                }
                frame_info.options.other.dll_name = out_dll_name;
                frame_info.options.other.offset_from_symbol_start = out_distance;
            }
            else
            {
                frame_info.inside_upp_function = true;
                frame_info.options.upp_function.line_index = line_result.line_index;
                frame_info.options.upp_function.unit = line_result.unit;
                frame_info.options.upp_function.slot = line_result.function_slot;
            }
            dynamic_array_push_back(&debugger->stack_frames, frame_info);
        }

        // Print stack frame Info
        {
            printf("Stack-Frame #%d: [0x%08llX]", frame_depth, context.Rip);
            frame_depth += 1;
            u64 offset = 0;
            String* name = debugger_find_closest_symbol_name(debugger, context.Rip, &offset);
            if (name != nullptr) {
                printf(" [+0x%04llX] %s \n", offset, name->characters);
            }
            else {
                printf(" could not find closest symbol...\n");
            }
        }

        // Find unwind infos
        int found_index = -1;
        for (int i = 0; i < found_module->function_unwind_infos.size; i++) {
            u64 addr = context.Rip;
            auto& unwind_info = found_module->function_unwind_infos[i];
            if (addr >= unwind_info.start_rva + found_module->base_address && addr <= unwind_info.end_rva + found_module->base_address) {
                found_index = i;
                break;
            }
        }

        // Execute unwind operations if found, otherwise assume this is a leaf-function (No need to execute unwind-codes)
        if (found_index != -1)
        {
            auto& unwind_info = found_module->function_unwind_infos[found_index];
            auto& first_unwind_block = found_module->unwind_blocks[unwind_info.unwind_block_index];
            if (!first_unwind_block.parsed_successfully) {
                printf("Unwind info for this function was NOT parsed successfully (Maybe newer version)!\n");
                return;
            }

            // Helper functions
            auto get_reg_ptr = [](X64_Integer_Register reg, CONTEXT* context) -> u64* {
                // Registers are stored in context the same as Unwind-Register order...
                return (&context->Rax) + (int)reg;
            };
            auto get_xmm_ptr = [](u32 xmm_index, CONTEXT* context) -> M128A* {
                // Registers are stored in context the same as Unwind-Register order...
                return ((&context->Xmm0) + xmm_index);
            };

            u64 func_offset = context.Rip - (unwind_info.start_rva + found_module->base_address);

            // Check if we are in epilog
            bool inside_epilog = false;
            bool success = true;
            {
                u32 remaining_bytes = (unwind_info.end_rva + found_module->base_address) - context.Rip;
                remaining_bytes = math_minimum(remaining_bytes, (u32)(16 * 32)); // Limit count of instructions read to max 32 instructions with 16 bytes each
                debugger_disassemble_bytes(debugger, context.Rip, remaining_bytes);

                // Analyse instructions to check if we are in epilog
                // Epilog must only consist of 8byte register pops and a return or a non-relative jmp
                // We ignore the non-relative jump now, because I can't figure out the encoding for this (ModRM bits need to be checked...)
                for (int i = 0; i < disassembly_buffer.size; i++)
                {
                    auto& instr = disassembly_buffer[i];
                    if (instr.Category == ND_CAT_POP && instr.OpMode == ND_ADDR_64 && instr.OperandsCount == 1)
                    {
                        auto& operand = instr.Operands[0];
                        if (operand.Type != ND_OPERAND_TYPE::ND_OP_REG) {
                            inside_epilog = false;
                            break;
                        }
                        if (operand.Info.Register.Size != 8 || operand.Info.Register.Type != ND_REG_TYPE::ND_REG_GPR || operand.Info.Register.IsBlock) {
                            inside_epilog = false;
                            break;
                        }
                        if (operand.Info.Register.Reg >= 16) {
                            inside_epilog = false;
                            break;
                        }
                    }
                    else if (instr.Instruction == ND_INS_RETF || instr.Instruction == ND_INS_RETN) {
                        inside_epilog = true;
                        break;
                    }
                    else {
                        // Unknown instruction
                        inside_epilog = false;
                        break;
                    }
                }

                // Execute epilog if we are inside, to get the previous context back
                if (inside_epilog)
                {
                    for (int i = 0; i < disassembly_buffer.size && success; i++)
                    {
                        auto& instr = disassembly_buffer[i];
                        if (instr.Category == ND_CAT_POP && instr.OpMode == ND_ADDR_64 && instr.OperandsCount == 1) {
                            auto& operand = instr.Operands[0];
                            X64_Integer_Register reg_id = (X64_Integer_Register)operand.Info.Register.Reg;
                            u64* reg = get_reg_ptr(reg_id, &context);
                            success = Process_Memory::read_single_value(debugger->process_handle, (void*)context.Rsp, reg);
                        }
                        else {
                            break;
                        }
                    }
                }

                if (!success) {
                    printf("Unwinding failed, couldn't read stack values\n");
                    return;
                }
            }

            // Reverse register state by unwinding
            int unwind_block_index = unwind_info.unwind_block_index;
            while (unwind_block_index != -1 && success && !inside_epilog)
            {
                auto& unwind_block = found_module->unwind_blocks[unwind_block_index];
                SCOPE_EXIT(unwind_block_index = unwind_block.next_chained_unwind_block_index);

                for (int i = 0; i < unwind_block.unwind_codes.size && success; i++)
                {
                    using PE_Analysis::Unwind_Code_Type;

                    auto& code = unwind_block.unwind_codes[i];

                    // Check if instruction was executed yet
                    if (func_offset < code.instruction_offset) {
                        continue;
                    }

                    switch (code.type)
                    {
                    case Unwind_Code_Type::ALLOC:
                        context.Rsp = context.Rsp + code.options.alloc_size;
                        break;
                    case Unwind_Code_Type::PUSH_REG: {
                        u64* reg = get_reg_ptr(code.options.push_reg, &context);
                        success = Process_Memory::read_single_value(debugger->process_handle, (void*)context.Rsp, reg);
                        context.Rsp = context.Rsp + 8;
                        break;
                    }
                    case Unwind_Code_Type::SAVE_REG: {
                        u64* reg = get_reg_ptr(code.options.save_reg.reg, &context);
                        success = Process_Memory::read_single_value(debugger->process_handle, (void*)(context.Rsp + code.options.save_reg.offset_from_rsp), reg);
                        break;
                    }
                    case Unwind_Code_Type::SAVE_XMM_128: {
                        M128A* xmm_reg = get_xmm_ptr(code.options.save_xmm_128.xmm_number, &context);
                        success = Process_Memory::read_single_value(
                            debugger->process_handle, (void*)(context.Rsp + code.options.save_xmm_128.offset_from_rsp), xmm_reg);
                        break;
                    }
                    case Unwind_Code_Type::SET_FRAME_POINTER_REGISTER: {
                        u64 frame_pointer_value = *get_reg_ptr(code.options.set_frame_pointer.fp_reg, &context);
                        context.Rsp = frame_pointer_value - code.options.set_frame_pointer.offset_from_rsp;
                        break;
                    }
                    default: {
                        success = false;
                        panic("Shouldn't happen");
                        break;
                    }
                    }
                }
            }

            if (!success) {
                printf("Couldn't undo some unwind operations!\n");
                return;
            }
        }

        // Undo Call instruction (Pop return address from stack)
        {
            u64 return_addr = 0;
            bool success = Process_Memory::read_single_value(debugger->process_handle, (void*)context.Rsp, &return_addr);
            if (!success) {
                printf("Couldn't load return-address from stack!\n");
                return;
            }

            context.Rsp += 8;
            context.Rip = return_addr;
        }
    }
}




// SOURCE_MAPPING
// Note: This only adds the statement mappings, but not line-to statement mapping (Must be done in seperate case for pointers to work)
void source_mapping_generate_statement_to_line_mapping_recursive(
    AST::Node* node, Debugger* debugger, Hashtable<AST::Statement*, int>* statement_to_mapping_table, Compilation_Unit_Mapping* unit_mapping)
{
    if (node->type == AST::Node_Type::STATEMENT) {
        AST::Statement* statement = downcast<AST::Statement>(node);
        if (!hashtable_insert_element(statement_to_mapping_table, statement, debugger->statement_mapping.size)) {
            return;
        }
        Statement_Mapping stat_mapping;
        stat_mapping.ir_instructions = dynamic_array_create<IR_Instruction_Mapping*>();
        stat_mapping.statement = statement;
        stat_mapping.line_mapping = &unit_mapping->lines[node->bounding_range.start.line];
        dynamic_array_push_back(&debugger->statement_mapping, stat_mapping);
    }

    int child_index = 0;
    AST::Node* child = AST::base_get_child(node, child_index);
    while (child != nullptr)
    {
        source_mapping_generate_statement_to_line_mapping_recursive(child, debugger, statement_to_mapping_table, unit_mapping);
        child_index += 1;
        child = AST::base_get_child(node, child_index);
    }
}

void source_mapping_generate_ir_instruction_mapping_recursive(
    IR_Code_Block* block, Debugger* debugger, Hashtable<IR_Code_Block*, int>* code_block_start_offsets)
{
    int offset_start = debugger->ir_instruction_mapping.size;
    if (!hashtable_insert_element(code_block_start_offsets, block, offset_start)) {
        return;
    }

    // Push dummy mappings for now
    dynamic_array_reserve(&debugger->ir_instruction_mapping, debugger->ir_instruction_mapping.size + block->instructions.size);
    for (int i = 0; i < block->instructions.size; i++)
    {
        IR_Instruction_Mapping instr_mapping;
        instr_mapping.code_block = block;
        instr_mapping.instruction_index = i;
        instr_mapping.c_lines = dynamic_array_create<C_Line_Mapping*>();
        instr_mapping.statement = nullptr;
        dynamic_array_push_back(&debugger->ir_instruction_mapping, instr_mapping);
    }

    // Afterwards recurse to lower blocks
    for (int i = 0; i < block->instructions.size; i++)
    {
        IR_Instruction& instr = block->instructions[i];
        switch (instr.type)
        {
        case IR_Instruction_Type::IF: {
            source_mapping_generate_ir_instruction_mapping_recursive(instr.options.if_instr.true_branch, debugger, code_block_start_offsets);
            source_mapping_generate_ir_instruction_mapping_recursive(instr.options.if_instr.false_branch, debugger, code_block_start_offsets);
            break;
        }
        case IR_Instruction_Type::WHILE: {
            source_mapping_generate_ir_instruction_mapping_recursive(instr.options.while_instr.code, debugger, code_block_start_offsets);
            break;
        }
        case IR_Instruction_Type::BLOCK: {
            source_mapping_generate_ir_instruction_mapping_recursive(instr.options.block, debugger, code_block_start_offsets);
            break;
        }
        }
    }
}

struct Segment_Comparator
{
    bool operator()(const Machine_Code_Segment& a, const Machine_Code_Segment& b)
    {
        if (a.function_slot_index != b.function_slot_index) {
            return a.function_slot_index < b.function_slot_index;
        }
        return a.virtual_address_start < b.virtual_address_start;
    }
};

void source_mapping_upp_line_to_machine_code_segments(
    Debugger* debugger, Compilation_Unit* compilation_unit, int line_index, Dynamic_Array<Machine_Code_Segment>* out_machine_code_segments)
{
    dynamic_array_reset(out_machine_code_segments);
    Compilation_Unit_Mapping* unit_map = nullptr;
    for (int i = 0; i < debugger->compilation_unit_mapping.size; i++) {
        auto& unit = debugger->compilation_unit_mapping[i];
        if (unit.compilation_unit == compilation_unit) {
            unit_map = &unit;
            break;
        }
    }
    if (unit_map == nullptr) return;
    if (line_index < 0 || line_index >= unit_map->lines.size)  return;

    auto& line_map = unit_map->lines[line_index];
    for (int i = 0; i < line_map.statements.size; i++)
    {
        auto stat_map = line_map.statements[i];
        for (int j = 0; j < stat_map->ir_instructions.size; j++)
        {
            auto ir_instr_map = stat_map->ir_instructions[j];
            for (int k = 0; k < ir_instr_map->c_lines.size; k++)
            {
                auto c_line_info = ir_instr_map->c_lines[k];
                Machine_Code_Segment segment;
                segment.function_slot_index = ir_instr_map->code_block->function->function_slot_index;
                segment.virtual_address_start = c_line_info->range.start_virtual_address;
                segment.virtual_address_end = c_line_info->range.end_virtual_address;
                segment.c_line_index_with_offset = c_line_info->c_line_index;
                if (segment.virtual_address_start == 0 || segment.virtual_address_end == 0) continue;
                dynamic_array_push_back(out_machine_code_segments, segment);
            }
        }
    }

    // Sort segments and fuse together...
    Segment_Comparator comp;
    dynamic_array_sort(out_machine_code_segments, comp);
    for (int i = 0; i < out_machine_code_segments->size - 1; i++)
    {
        auto& current = out_machine_code_segments->data[i];
        auto& next = out_machine_code_segments->data[i + 1];

        if (current.function_slot_index != next.function_slot_index) continue;
        if (current.virtual_address_end == next.virtual_address_start) {
            current.virtual_address_end = next.virtual_address_end;
            dynamic_array_remove_ordered(out_machine_code_segments, i + 1);
            i = i - 1;
        }
    }
}

Machine_Code_Address_To_Line_Result source_mapping_machine_instruction_address_to_upp_line(Debugger* debugger, u64 virtual_address)
{
    Machine_Code_Address_To_Line_Result result;
    result.unit = nullptr;
    result.line_index = -1;
    result.function_slot = -1;
    result.statement = nullptr;

    int slot_index = -1;
    for (int i = 0; i < debugger->function_slot_mapping.size; i++) {
        auto& slot = debugger->function_slot_mapping[i];
        if (virtual_address >= slot.virtual_address_start && virtual_address < slot.virtual_address_end) {
            slot_index = i;
            break;
        }
    }
    if (slot_index == -1) return result;

    result.function_slot = slot_index;
    auto& slot_mapping = debugger->function_slot_mapping[slot_index];
    int segment_index = -1;

    for (int i = 0; i < slot_mapping.c_lines.size; i++)
    {
        auto& c_line = slot_mapping.c_lines[i];
        if (virtual_address >= c_line->range.start_virtual_address && virtual_address < c_line->range.end_virtual_address)
        {
            auto& upp_line = c_line->instruction_mapping->statement->line_mapping;
            result.statement = c_line->instruction_mapping->statement->statement;
            result.line_index = upp_line->line_number;
            result.unit = debugger->compilation_unit_mapping[upp_line->compilation_unit_mapping_index].compilation_unit;
            return result;
        }
    }

    return result;
}



// DEBUGGER CONTROLS
bool debugger_add_address_breakpoint(Debugger* debugger, u64 address)
{
    // Check if breakpoint at this address already exists
    for (int i = 0; i < debugger->address_breakpoints.size; i++) {
        auto& bp = debugger->address_breakpoints[i];
        if (bp.address == address ) {
            if (bp.is_software_breakpoint) {
                bp.options.software_bp.remove_after_next_event = false;
            }
            return true;
        }
    }

    Address_Breakpoint breakpoint;
    breakpoint.address = address;
    breakpoint.is_software_breakpoint = true;

    // Check if hardware breakpoints are available
    for (int i = 0; i < HARDWARE_BREAKPOINT_COUNT; i++) {
        auto& hw_bp = debugger->hardware_breakpoints[i];
        if (hw_bp.enabled) continue;

        breakpoint.is_software_breakpoint = false;
        breakpoint.options.hardware_breakpoint_index = i;
        hw_bp.address = address;
        hw_bp.enabled = true;
        hw_bp.length_bits = 0;
        hw_bp.type = Hardware_Breakpoint_Type::BREAK_ON_EXECUTE;
        break;
    }

    if (breakpoint.is_software_breakpoint)
    {
        breakpoint.options.software_bp.is_installed = false;
        breakpoint.options.software_bp.remove_after_next_event = false;
        bool success = Process_Memory::read_single_value<byte>(debugger->process_handle, (void*)address, &breakpoint.options.software_bp.original_byte);
        if (!success) {
            return false;
        }
    }

    dynamic_array_push_back(&debugger->address_breakpoints, breakpoint);
    return true;
}

bool debugger_remove_address_breakpoint(Debugger* debugger, u64 address)
{
    // Find breakpoint to remove
    int index = -1;
    for (int i = 0; i < debugger->address_breakpoints.size; i++) {
        auto& bp = debugger->address_breakpoints[i];
        if (bp.address == address) {
            index = i;
            break;
        }
    }
    if (index == -1) return false;

    // Different handling based on software/hardware breakpoint
    auto& bp = debugger->address_breakpoints[index];
    if (bp.is_software_breakpoint) {
        bp.options.software_bp.remove_after_next_event = true;
        return true;
    }

    debugger->hardware_breakpoints[bp.options.hardware_breakpoint_index].enabled = false;
    dynamic_array_swap_remove(&debugger->address_breakpoints, index);
    return true;
}



void debugger_receive_next_debug_event(Debugger* debugger, bool wait_until_event_occurs)
{
    if (debugger->state.process_state != Debug_Process_State::RUNNING) return;

    auto& debug_event = debugger->last_debug_event;

    ZeroMemory(&debug_event, sizeof(debug_event));
    bool success = WaitForDebugEventEx(&debug_event, wait_until_event_occurs ? INFINITE : 0);
    if (!success) {
        if (!wait_until_event_occurs) {
            return;
        }
        helper_print_last_error();
        debugger_reset(debugger);
        return;
    }

    debugger->state.process_state = Debug_Process_State::HALTED;
    debugger->state.halt_type = Halt_Type::DEBUG_EVENT_RECEIVED;
    debugger->continue_status = DBG_CONTINUE;
    debugger->last_debug_event_requires_handling = true;
    debugger->event_count += 1;
}

void debugger_handle_last_debug_event(Debugger* debugger)
{
    // Continue from last event
    if (debugger->state.process_state != Debug_Process_State::HALTED || !debugger->last_debug_event_requires_handling) return;
    debugger->last_debug_event_requires_handling = false;

    auto& debug_event = debugger->last_debug_event;
    if (debug_event.dwProcessId != debugger->process_id) {
        panic("Debug event from other process received");
    }
    if (DEBUG_OUTPUT_ENABLED) {
        printf("Process ID: %5d, thread_id: %5d, Event: ", debug_event.dwProcessId, debug_event.dwThreadId);
    }

    // Handle events
    switch (debug_event.dwDebugEventCode)
    {
    case CREATE_PROCESS_DEBUG_EVENT:
    {
        auto& create_info = debug_event.u.CreateProcessInfo;
        CloseHandle(create_info.hFile); // Close handle to image file, as win32 doc describes, and we don't need the file info
        if (DEBUG_OUTPUT_ENABLED) { printf("Create_Process\n"); }

        // Store handles
        debugger->start_event_process_handle = create_info.hProcess;
        debugger->start_event_thread_handle = create_info.hThread;
        if (debug_event.dwProcessId != debugger->process_id) {
            printf("WARNING: Create_Process_Debug_Event process id does not match CreateProcessA process id\n");
        }
        if (create_info.hThread != nullptr) {
            Thread_Info info;
            info.id = GetThreadId(create_info.hThread);
            info.handle = create_info.hThread;
            dynamic_array_push_back(&debugger->threads, info);
            debugger->main_thread_info_index = debugger->threads.size - 1;
        }

        // Load portable-executable information
        PE_Analysis::PE_Info pe_info = PE_Analysis::pe_info_create();
        bool success = PE_Analysis::pe_info_fill_from_executable_image(
            &pe_info, (u64)create_info.lpBaseOfImage, debugger->process_handle, create_info.lpImageName, create_info.fUnicode
        );
        if (success) {
            debugger->exe_pe_info_index = debugger->pe_infos.size;
            dynamic_array_push_back(&debugger->pe_infos, pe_info);
        }
        else {
            printf("Could not parse main executable pe info!\n");
            debugger_reset(debugger);
            PE_Analysis::pe_info_destroy(&pe_info);
        }

        break;
    }
    case RIP_EVENT: // Note: As I understood it, RIP events only happen when system debuggers fail?
    case EXIT_PROCESS_DEBUG_EVENT: 
    {
        if (debug_event.dwDebugEventCode == RIP_EVENT) {
            panic("RIP event occured!\n");
        }

        // Note: ContinueDebugEvent closes the process handle and the thread handle received through the create_process_debug_event
        ContinueDebugEvent(debug_event.dwProcessId, debug_event.dwThreadId, DBG_CONTINUE);
        debugger->state.process_state = Debug_Process_State::RUNNING;
        debugger->last_debug_event_requires_handling = false;

        for (int i = 0; i < debugger->threads.size; i++) {
            if (debugger->threads[i].handle == debugger->start_event_thread_handle) {
                dynamic_array_swap_remove(&debugger->threads, i);
                break;
            }
        }
        debugger->start_event_process_handle = nullptr;
        debugger->start_event_thread_handle = nullptr;

        debugger_reset(debugger, Debugger_Reset_Type::PROCESS_EXIT_RECEIVED);
        if (DEBUG_OUTPUT_ENABLED) { printf("Exit_Process\n"); }
        break;
    }
    case LOAD_DLL_DEBUG_EVENT:
    {
        if (DEBUG_OUTPUT_ENABLED) { printf("Load DLL event: "); }
        auto& dll_load = debug_event.u.LoadDll;
        CloseHandle(dll_load.hFile);

        // Load portable-executable information
        PE_Analysis::PE_Info pe_info = PE_Analysis::pe_info_create();
        bool success = PE_Analysis::pe_info_fill_from_executable_image(
            &pe_info, (u64)dll_load.lpBaseOfDll, debugger->process_handle, dll_load.lpImageName, dll_load.fUnicode
        );
        if (success)
        {
            dynamic_array_push_back(&debugger->pe_infos, pe_info);

            if (DEBUG_OUTPUT_ENABLED)
            {
                if (pe_info.name.size > 0) {
                    printf("\"%s\" ", pe_info.name.characters);
                }
                else {
                    printf("Analysis success, but name not retrievable ");
                }
                if (pe_info.pdb_name.size > 0) {
                    printf("pdb: \"%s\" ", pe_info.pdb_name.characters);
                }
                printf("\n");
            }
        }
        else {
            PE_Analysis::pe_info_destroy(&pe_info);
            if (DEBUG_OUTPUT_ENABLED) { printf("Analysis failed!\n"); }
        }

        break;
    }
    case UNLOAD_DLL_DEBUG_EVENT:
    {
        if (DEBUG_OUTPUT_ENABLED) { printf("Unload_Dll: "); }
        u64 dll_base = (u64)debug_event.u.UnloadDll.lpBaseOfDll;
        for (int i = 0; i < debugger->pe_infos.size; i++) {
            auto& pe_info = debugger->pe_infos[i];
            if (pe_info.base_address == dll_base && i != debugger->exe_pe_info_index) {
                printf(pe_info.name.characters);
                PE_Analysis::pe_info_destroy(&pe_info);
                dynamic_array_swap_remove(&debugger->pe_infos, i);
                break;
            }
        }
        if (DEBUG_OUTPUT_ENABLED) { printf("\n"); }
        break;
    }
    case CREATE_THREAD_DEBUG_EVENT:
    {
        Thread_Info info;
        info.handle = debug_event.u.CreateThread.hThread;
        info.id = debug_event.dwThreadId;
        dynamic_array_push_back(&debugger->threads, info);

        if (DEBUG_OUTPUT_ENABLED) { printf("Create_thread\n"); }
        break;
    }
    case EXIT_THREAD_DEBUG_EVENT: 
    {
        // Remove thread from thread list
        for (int i = 0; i < debugger->threads.size; i++) {
            auto& thread = debugger->threads[i];
            if (thread.id == debug_event.dwThreadId) {
                if (i == debugger->main_thread_info_index) {
                    debugger->main_thread_info_index = -1;
                }
                else if (i < debugger->main_thread_info_index) {
                    debugger->main_thread_info_index -= 1;
                }
                dynamic_array_remove_ordered(&debugger->threads, i);
                break;
            }
        }
        if (DEBUG_OUTPUT_ENABLED) { printf("Exit_Thread\n"); }
        break;
    }
    case EXCEPTION_DEBUG_EVENT:
    {
        int code = debug_event.u.Exception.ExceptionRecord.ExceptionCode;
        debugger->continue_status = DBG_EXCEPTION_NOT_HANDLED;
        debugger->state.halt_type = Halt_Type::EXCEPTION_OCCURED;
        auto& exception_name = debugger->state.exception_name;
        exception_name = "";

        switch (code)
        {
        case EXCEPTION_BREAKPOINT:  
        case EXCEPTION_SINGLE_STEP: 
        {
            exception_name = code == EXCEPTION_SINGLE_STEP ? "SINGLE_STEP" : "BREAKPOINT";
            debugger->continue_status = DBG_EXCEPTION_HANDLED; // Always treat breakpoints and steps as handled exceptions

            // Get Thread-Context
            u64 instruction_pointer = 0;
            for (int i = 0; i < debugger->threads.size; i++) {
                auto& thread_info = debugger->threads[i];
                if (thread_info.id == debug_event.dwThreadId) {
                    CONTEXT thread_context;
                    thread_context.ContextFlags = CONTEXT_ALL;
                    if (GetThreadContext(thread_info.handle, &thread_context)) {
                        instruction_pointer = thread_context.Rip;
                    }
                    break;
                }
            }

            // Check if we hit any of our breakpoints, otherwise it's a debug_break
            debugger->state.halt_type = Halt_Type::DEBUG_BREAK_HIT;
            for (int i = 0; i < debugger->address_breakpoints.size; i++) {
                auto& bp = debugger->address_breakpoints[i];
                if (instruction_pointer == bp.address) {
                    debugger->state.halt_type = Halt_Type::BREAKPOINT_HIT;
                    // bool hardware_breakpoint_flag_set = (thread_context.Dr6 & 0b1111) != 0;
                    break;
                }
            }

            break;
        }
        case EXCEPTION_ACCESS_VIOLATION:         exception_name = "ACCESS_VIOLATION"; break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:    exception_name = "DATATYPE_MISALIGNMENT"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    exception_name = "ARRAY_BOUNDS_EXCEEDED"; break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:     exception_name = "FLT_DENORMAL_OPERAND"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       exception_name = "FLT_DIVIDE_BY_ZERO"; break;
        case EXCEPTION_FLT_INEXACT_RESULT:       exception_name = "FLT_INEXACT_RESULT"; break;
        case EXCEPTION_FLT_INVALID_OPERATION:    exception_name = "FLT_INVALID_OPERATION"; break;
        case EXCEPTION_FLT_OVERFLOW:             exception_name = "FLT_OVERFLOW"; break;
        case EXCEPTION_FLT_STACK_CHECK:          exception_name = "FLT_STACK_CHECK"; break;
        case EXCEPTION_FLT_UNDERFLOW:            exception_name = "FLT_UNDERFLOW"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       exception_name = "INT_DIVIDE_BY_ZERO"; break;
        case EXCEPTION_INT_OVERFLOW:             exception_name = "INT_OVERFLOW"; break;
        case EXCEPTION_PRIV_INSTRUCTION:         exception_name = "PRIV_INSTRUCTION"; break;
        case EXCEPTION_IN_PAGE_ERROR:            exception_name = "IN_PAGE_ERROR"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:      exception_name = "ILLEGAL_INSTRUCTION"; break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: exception_name = "NONCONTINUABLE_EXCEPTION"; break;
        case EXCEPTION_STACK_OVERFLOW:           exception_name = "STACK_OVERFLOW"; break;
        case EXCEPTION_INVALID_DISPOSITION:      exception_name = "INVALID_DISPOSITION"; break;
        case EXCEPTION_GUARD_PAGE:               exception_name = "GUARD_PAGE"; break;
        case EXCEPTION_INVALID_HANDLE:           exception_name = "INVALID_HANDLE"; break;
        default:                                 exception_name = "UNKNOWN_EXCEPTION_CODE"; __debugbreak(); break;
        }

        if (DEBUG_OUTPUT_ENABLED) { printf("Exception %s\n", exception_name); }
        break;
    }
    case OUTPUT_DEBUG_STRING_EVENT:
    {
        auto& debug_str = debug_event.u.DebugString;
        auto& str = debugger->string_buffer;
        string_reset(&str);
        bool success = Process_Memory::read_string(
            debugger->process_handle, (void*)(debug_str.lpDebugStringData),
            &str, debug_str.nDebugStringLength + 1, debug_str.fUnicode, &debugger->byte_buffer
        );
        if (success) {
            printf("Output_Debug_String: \"%s\"\n", str.characters);
        }
        else {
            printf("Debug string could not be read\n");
        }
        break;
    }
    default: {
        if (DEBUG_OUTPUT_ENABLED) { printf("Debugger received unknown debug event code: #%d\n", debug_event.dwDebugEventCode); }
        debugger_reset(debugger);
        break;
    }
    }
}

// Does a single step of the thread, and handles all debug-events which may happen inbetween (Note: may uninstall software breakpoints)
void debugger_single_step_thread(Debugger* debugger, HANDLE thread_handle)
{
    if (debugger->state.process_state != Debug_Process_State::HALTED) return;
    if (debugger->last_debug_event_requires_handling) {
        debugger_handle_last_debug_event(debugger);
        if (debugger->state.process_state != Debug_Process_State::HALTED) return;
    }

    // Check that thread-handle is an active thread
    bool found = false;
    DWORD thread_id = 0;
    for (int i = 0; i < debugger->threads.size; i++) {
        if (debugger->threads[i].handle == thread_handle) {
            found = true;
            thread_id =debugger->threads[i].id;
            break;
        }
    }
    if (!found) return;
    
    CONTEXT thread_context;
    thread_context.ContextFlags = CONTEXT_ALL;
    if (!GetThreadContext(thread_handle, &thread_context)) {
        return;
    }
    thread_context.EFlags = thread_context.EFlags | (u32)X64_Flags::TRAP;

    // Handle software/hardware breakpoints
    for (int i = 0; i < debugger->address_breakpoints.size; i++) 
    {
        auto& bp = debugger->address_breakpoints[i];
        if (bp.address != thread_context.Rip) continue;

        if (bp.is_software_breakpoint) {
            if (bp.options.software_bp.is_installed) {
                Process_Memory::write_byte(debugger->process_handle, (void*)bp.address, bp.options.software_bp.original_byte);
                FlushInstructionCache(debugger->process_handle, (void*)bp.address, 1);
                bp.options.software_bp.is_installed = false;
            }
        }
        else {
            // Resume flag causes the thread to ignore hardware breakpoints for a single instruction
            thread_context.EFlags = thread_context.EFlags | (u32)X64_Flags::RESUME;
        }
    }

    // Set Context again
    if (!SetThreadContext(thread_handle, &thread_context)) {
        return;
    }

    // Suspend all other threads
    for (int i = 0; i < debugger->threads.size; i++) {
        if (debugger->threads[i].handle == thread_handle) {
            continue;
        }
        SuspendThread(debugger->threads[i].handle);
    }
    SCOPE_EXIT(
        if (debugger->state.process_state == Debug_Process_State::HALTED)
        {
            for (int i = 0; i < debugger->threads.size; i++) {
                if (debugger->threads[i].handle == thread_handle) {
                    continue;
                }
                ResumeThread(debugger->threads[i].handle);
            }
        }
    );

    // Handle events until we hit our stepping event
    while (true)
    {
        BOOL continue_success = ContinueDebugEvent(debugger->last_debug_event.dwProcessId, debugger->last_debug_event.dwThreadId, debugger->continue_status);
        if (!continue_success) {
            debugger_reset(debugger);
            return;
        }
        debugger->state.process_state = Debug_Process_State::RUNNING;
        debugger_receive_next_debug_event(debugger, true);
        if (debugger->state.process_state != Debug_Process_State::HALTED) return;
        debugger_handle_last_debug_event(debugger);
        if (debugger->state.process_state != Debug_Process_State::HALTED) return;

        // Check if it was a stepping event
        auto& last_event = debugger->last_debug_event;
        if (last_event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
            last_event.dwProcessId == debugger->process_id &&
            last_event.dwThreadId == thread_id &&
            (last_event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP ||
                last_event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT))
        {
            debugger->state.halt_type = Halt_Type::STEPPING;
            break;
        }
    }
}

// Steps threads which are currently on breakpoints, sets up breakpoints (Hardware + software), and continues execution
void debugger_continue_from_last_debug_event(Debugger* debugger)
{
    if (debugger->state.process_state != Debug_Process_State::HALTED) return;
    if (debugger->last_debug_event_requires_handling) {
        debugger_handle_last_debug_event(debugger);
        if (debugger->state.process_state != Debug_Process_State::HALTED) return;
    }

    // Remove all software breakpoints to remove
    for (int i = 0; i < debugger->address_breakpoints.size; i++)
    {
        auto& bp = debugger->address_breakpoints[i];
        if (!bp.is_software_breakpoint) continue;
        if (!bp.options.software_bp.remove_after_next_event) continue;

        if (bp.options.software_bp.is_installed) {
            bool success = Process_Memory::write_byte(debugger->process_handle, (void*)bp.address, bp.options.software_bp.original_byte);
            FlushInstructionCache(debugger->process_handle, (void*)bp.address, 1);
            bp.options.software_bp.is_installed = false;
        }
        dynamic_array_swap_remove(&debugger->address_breakpoints, i);
        i = i - 1;
    }

    // Single-Step all threads which are currently on breakpoints
    for (int i = 0; i < debugger->threads.size; i++)
    {
        auto& thread_info = debugger->threads[i];

        CONTEXT thread_context;
        thread_context.ContextFlags = CONTEXT_ALL;
        if (!GetThreadContext(thread_info.handle, &thread_context)) {
            continue;
        }

        // Step thread if on breakpoint
        bool on_breakpoint = false;
        for (int i = 0; i < debugger->address_breakpoints.size; i++) {
            auto& bp = debugger->address_breakpoints[i];
            if (bp.address == thread_context.Rip) {
                // Note: In theory this the single step could create/delete new threads, which would throw off the current loop
                debugger_single_step_thread(debugger, thread_info.handle);
                if (debugger->state.process_state != Debug_Process_State::HALTED) return;
                break;
            }
        }
    }

    // Install all software breakpoints (Which aren't installed yet)
    for (int i = 0; i < debugger->address_breakpoints.size; i++)
    {
        auto& bp = debugger->address_breakpoints[i];
        if (!bp.is_software_breakpoint) continue;
        if (bp.options.software_bp.is_installed) continue;
        Process_Memory::write_byte(debugger->process_handle, (void*)bp.address, 0xCC);
        FlushInstructionCache(debugger->process_handle, (void*)bp.address, 1);
        bp.options.software_bp.is_installed = true;
    }

    // Set hardware breakpoints for all threads (Set debug registers in thread-context)
    for (int i = 0; i < debugger->threads.size; i++)
    {
        auto& thread_info = debugger->threads[i];

        CONTEXT thread_context;
        thread_context.ContextFlags = CONTEXT_ALL;
        if (!GetThreadContext(thread_info.handle, &thread_context)) {
            printf("Get thread context failed?\n");
            continue;
        }

        auto set_u64_bits = [](u64 initial_value, int bit_index, int bit_length, u64 bits_to_set) -> u64 {
            if (bit_length == 0) return initial_value;

            u64 mask = (1 << bit_length) - 1; // Generate 1s
            bits_to_set = bits_to_set & mask; // Truncate bits_to_set by size
            mask = mask << bit_index;
            bits_to_set = bits_to_set << bit_index;
            return ((~mask) & initial_value) | (mask & bits_to_set);
        };

        // Transfer hardware-breakpoints into ThreadContext
        for (int i = 0; i < HARDWARE_BREAKPOINT_COUNT; i++)
        {
            auto& bp = debugger->hardware_breakpoints[i];
            switch (i)
            {
            case 0: thread_context.Dr0 = bp.address; break;
            case 1: thread_context.Dr1 = bp.address; break;
            case 2: thread_context.Dr2 = bp.address; break;
            case 3: thread_context.Dr3 = bp.address; break;
            default: panic("");
            }

            int length_bits = bp.length_bits;
            int read_write_value = 0;
            switch (bp.type)
            {
            case Hardware_Breakpoint_Type::BREAK_ON_EXECUTE:       read_write_value = 0; length_bits = 0; break;
            case Hardware_Breakpoint_Type::BREAK_ON_READ:          read_write_value = 1; break;
            case Hardware_Breakpoint_Type::BREAK_ON_READ_OR_WRITE: read_write_value = 3; break;
            default: panic("");
            }

            int local_enabled_bit_offset = i * 2;
            int read_write_bits_offset = 16 + i * 4;
            int len_bit_offset = 18 + i * 4;

            u64 dr7 = thread_context.Dr7;
            dr7 = set_u64_bits(dr7, local_enabled_bit_offset, 1, bp.enabled ? 1 : 0);
            dr7 = set_u64_bits(dr7, read_write_bits_offset, 2, read_write_value);
            dr7 = set_u64_bits(dr7, len_bit_offset, 2, length_bits);
            thread_context.Dr7 = dr7;
        }

        // Update thread context
        thread_context.ContextFlags = CONTEXT_ALL;
        // thread_context.EFlags = thread_context.EFlags & (~(u32)X64_Flags::RESUME);
        if (!SetThreadContext(thread_info.handle, &thread_context)) {
            // Maybe some error logging at some point?
            printf("Set thread context failed?\n");
        }
    }

    // Continue from debug event
    {
        auto& debug_event = debugger->last_debug_event;
        BOOL continue_success = ContinueDebugEvent(debug_event.dwProcessId, debug_event.dwThreadId, debugger->continue_status);
        if (!continue_success) {
            printf("ContinueDebugEvent failed!\n");
            helper_print_last_error();
            debugger_reset(debugger);
            return;
        }
        debugger->state.process_state = Debug_Process_State::RUNNING;
    }
}

void debugger_resume_until_next_halt_or_exit(Debugger* debugger)
{
    // Get debugger into running state
    if (debugger->state.process_state == Debug_Process_State::HALTED) {
        debugger_handle_last_debug_event(debugger);
        debugger_continue_from_last_debug_event(debugger);
    }
    if (debugger->state.process_state == Debug_Process_State::NO_ACTIVE_PROCESS) return;

    // Handle events until we hit a breakpoint
    while (true)
    {
        debugger_receive_next_debug_event(debugger, true);
        debugger_handle_last_debug_event(debugger);
        if (debugger->state.process_state == Debug_Process_State::NO_ACTIVE_PROCESS) return;
        if (debugger->state.process_state == Debug_Process_State::HALTED && 
            (debugger->state.halt_type == Halt_Type::BREAKPOINT_HIT || 
                debugger->state.halt_type == Halt_Type::DEBUG_BREAK_HIT ||
                debugger->state.halt_type == Halt_Type::EXCEPTION_OCCURED)) return;
        debugger_continue_from_last_debug_event(debugger);
    }
}

bool debugger_start_process(
    Debugger* debugger, const char* exe_filepath, const char* pdb_filepath, const char* main_obj_filepath, Compiler_Analysis_Data* analysis_data)
{
    debugger_reset(debugger);

    // Load pdb file
    debugger->pdb_info = PDB_Analysis::pdb_information_create();
    if (!PDB_Analysis::pdb_information_fill_from_file(debugger->pdb_info, pdb_filepath, main_obj_filepath)) {
        PDB_Analysis::pdb_information_destroy(debugger->pdb_info);
        debugger->pdb_info = nullptr;
        printf("Couldn't parse pdb file!\n");
        return false;
    }

    // Create process
    {
        STARTUPINFO startup_info;
        ZeroMemory(&startup_info, sizeof(startup_info));
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info;
        bool success = CreateProcessA(
            exe_filepath, // application path
            nullptr, // Command line args
            nullptr, // Security-Attributes (if child processes can access this handle)
            nullptr, // Security-attributes for thread
            false, // Inherit handles
            CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_CONSOLE | CREATE_SUSPENDED | DEBUG_ONLY_THIS_PROCESS,
            nullptr, // Environment
            nullptr, // Current directory
            &startup_info,
            &process_info
        );

        if (!success) {
            logg("CreateProcessA failed\n");
            helper_print_last_error();
            return false;
        }
        debugger->process_handle = process_info.hProcess;
        debugger->process_id = process_info.dwProcessId;
        debugger->main_thread_handle = process_info.hThread;
        debugger->main_thread_id = process_info.dwThreadId;

        ResumeThread(process_info.hThread);
        debugger->state.process_state = Debug_Process_State::RUNNING;
    }

    // Handle initial debug events (Create_Process, Create_Threads, Load_Dlls, until first breakpoint hit)
    u64 main_address = 0;
    while (true)
    {
        debugger_receive_next_debug_event(debugger, true);
        if (debugger->state.process_state != Debug_Process_State::HALTED) {
            debugger_reset(debugger);
            return false;
        }
        debugger_handle_last_debug_event(debugger);

        // Add breakpoint for main-function
        // Note: Using hardware breakpoints between the Create_Process_Event and entering main does not work reliably
        //       I guess this has something to do with the thread setting itself up, and maybe manipulating it's own context
        if (debugger->last_debug_event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            main_address = debugger_find_address_of_function(debugger, string_create_static("main"));
            if (main_address == 0) {
                printf("No main function found!\n");
                debugger_reset(debugger);
                return false;
            }
            bool success = debugger_add_address_breakpoint(debugger, main_address);
            if (!success) {
                debugger_reset(debugger);
                return false;
            }
        }

        if (debugger->state.process_state != Debug_Process_State::HALTED) {
            debugger_reset(debugger);
            return false;
        }

        if (debugger->last_debug_event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
            debugger->last_debug_event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) 
        {
            break;
        }
        else {
            debugger_continue_from_last_debug_event(debugger);
        }
    }

    // Sanity check that main-thread was reported by initial debug events...
    {
        bool found = false;
        for (int i = 0; i < debugger->threads.size; i++) {
            auto& thread = debugger->threads[i];
            if (thread.id == debugger->main_thread_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            panic("Main thread not reported by initial events!");
        }
    }

    // Set breakpoint on main and execute until main start
    {
        // Wait until we hit the main function 
        debugger_resume_until_next_halt_or_exit(debugger);
        if (debugger->state.process_state != Debug_Process_State::HALTED) {
            debugger_reset(debugger);
            return false;
        }
        // Set the thread which hit the breakpoint as main thread
        for (int i = 0; i < debugger->threads.size; i++) {
            if (debugger->threads[i].id == debugger->last_debug_event.dwThreadId) {
                debugger->main_thread_info_index = i;
                break;
            }
        }
        debugger_remove_address_breakpoint(debugger, main_address);
    }

    // Generate all mappings (Upp-Code <-> Statements <-> IR_Instructions <-> C-Lines <-> Assembly)
    if (analysis_data != nullptr)
    {
        auto pdb_info = debugger->pdb_info;
        C_Program_Translation* c_translation = c_generator_get_translation();

        Hashtable<AST::Statement*, int> statement_to_mapping_table = hashtable_create_pointer_empty<AST::Statement*, int>(1024);
        SCOPE_EXIT(hashtable_destroy(&statement_to_mapping_table));

        // Compilation unit mappings (Unit <-> Upp_Line <-> Statements)
        for (int i = 0; i < compiler.compilation_units.size; i++)
        {
            auto& unit = compiler.compilation_units[i];

            if (!unit->used_in_last_compile) continue;

            Compilation_Unit_Mapping unit_mapping;
            unit_mapping.lines = dynamic_array_create<Upp_Line_Mapping>(unit->code->line_count);
            unit_mapping.compilation_unit = unit;

            for (int i = 0; i < unit->code->line_count; i++) {
                Upp_Line_Mapping line_mapping;
                line_mapping.compilation_unit_mapping_index = debugger->compilation_unit_mapping.size;
                line_mapping.line_number = i;
                line_mapping.statements = dynamic_array_create<Statement_Mapping*>();
                dynamic_array_push_back(&unit_mapping.lines, line_mapping);
            }
            dynamic_array_push_back(&debugger->compilation_unit_mapping, unit_mapping);

            source_mapping_generate_statement_to_line_mapping_recursive(
                upcast(unit->root), debugger, &statement_to_mapping_table, &debugger->compilation_unit_mapping[debugger->compilation_unit_mapping.size - 1]
            );
        }

        // Add connections from Upp_Line to Statements
        for (int i = 0; i < debugger->statement_mapping.size; i++) {
            auto& stat_mapping = debugger->statement_mapping[i];
            dynamic_array_push_back(&stat_mapping.line_mapping->statements, &stat_mapping);
        }

        // Statement <-> IR_Instruction 
        Hashtable<IR_Code_Block*, int> ir_block_mapping_offset = hashtable_create_pointer_empty<IR_Code_Block*, int>(64);
        SCOPE_EXIT(hashtable_destroy(&ir_block_mapping_offset));
        for (int i = 0; i < compiler.ir_generator->program->functions.size; i++) {
            auto ir_fn = compiler.ir_generator->program->functions[i];
            source_mapping_generate_ir_instruction_mapping_recursive(ir_fn->code, debugger, &ir_block_mapping_offset);
        }
        // Add connections Statement -> IR_Instruction
        for (int i = 0; i < debugger->ir_instruction_mapping.size; i++)
        {
            auto& ir_mapping = debugger->ir_instruction_mapping[i];
            auto& ir_instr = ir_mapping.code_block->instructions[ir_mapping.instruction_index];
            if (ir_instr.associated_statement != 0) {
                int* map_index = hashtable_find_element(&statement_to_mapping_table, ir_instr.associated_statement);
                if (map_index != 0) {
                    ir_mapping.statement = &debugger->statement_mapping[*map_index];
                }
            }

            if (ir_mapping.statement != 0) {
                dynamic_array_push_back(&ir_mapping.statement->ir_instructions, &ir_mapping);
            }
        }

        // IR_Instruction <-> C-Line
        dynamic_array_reset(&debugger->c_line_mapping);
        dynamic_array_reserve(&debugger->c_line_mapping, c_translation->line_infos.size);
        for (int i = 0; i < c_translation->line_infos.size; i++)
        {
            auto line_info = c_translation->line_infos[i];

            C_Line_Mapping line_map;
            line_map.c_line_index = i + c_translation->line_offset;
            line_map.range.start_virtual_address = 0;
            line_map.range.end_virtual_address = 0;
            line_map.instruction_mapping = nullptr;

            int* block_start_offset = hashtable_find_element(&ir_block_mapping_offset, line_info.ir_block);
            if (block_start_offset != 0) {
                line_map.instruction_mapping = &debugger->ir_instruction_mapping[*block_start_offset + line_info.instruction_index];
            }

            dynamic_array_push_back(&debugger->c_line_mapping, line_map);
        }
        // Connection IR_Instruction -> C-Line
        for (int i = 0; i < debugger->c_line_mapping.size; i++)
        {
            auto& line_mapping = debugger->c_line_mapping[i];
            if (line_mapping.instruction_mapping != 0) {
                dynamic_array_push_back(&line_mapping.instruction_mapping->c_lines, &line_mapping);
            }
        }

        // C-Line/Function-Slot <-> Machine-Code Range
        dynamic_array_reset(&debugger->function_slot_mapping);
        dynamic_array_reserve(&debugger->function_slot_mapping, analysis_data->function_slots.size);
        for (int i = 0; i < analysis_data->function_slots.size; i++) {
            Function_Slot_Mapping slot_mapping;
            slot_mapping.c_lines = dynamic_array_create<C_Line_Mapping*>();
            slot_mapping.name = string_create_static("");
            slot_mapping.virtual_address_start = 0;
            slot_mapping.virtual_address_end = 0;
            dynamic_array_push_back(&debugger->function_slot_mapping, slot_mapping);
        }
        Hashtable<String, int> c_function_name_to_function_slot_map = hashtable_create_empty<String, int>(64, hash_string, string_equals);
        SCOPE_EXIT(hashtable_destroy(&c_function_name_to_function_slot_map));
        for (auto iter = hashtable_iterator_create(&c_translation->name_mapping); hashtable_iterator_has_next(&iter); hashtable_iterator_next(&iter))
        {
            String name = *iter.value;
            C_Translation& translation = *iter.key;
            if (translation.type == C_Translation_Type::FUNCTION) {
                hashtable_insert_element(&c_function_name_to_function_slot_map, name, translation.options.function_slot_index);
            }
        }
        for (int i = 0; i < pdb_info->source_infos.size; i++)
        {
            auto& src_info = pdb_info->source_infos[i];
            auto& fn_info = pdb_info->functions[src_info.function_index];

            int* function_slot_index = hashtable_find_element(&c_function_name_to_function_slot_map, fn_info.name);
            if (function_slot_index == nullptr) continue;
            auto& slot_mapping = debugger->function_slot_mapping[*function_slot_index];
            slot_mapping.name = fn_info.name;
            slot_mapping.virtual_address_start = static_location_to_virtual_address(debugger, fn_info.location);
            slot_mapping.virtual_address_end = slot_mapping.virtual_address_start + fn_info.length;

            for (int j = 0; j < src_info.line_infos.size; j++)
            {
                auto& pdb_line_info = src_info.line_infos[j];
                int line_map_index = pdb_line_info.line_num - 1 - c_translation->line_offset;
                assert(line_map_index >= 0, "");

                auto& c_line_mapping = debugger->c_line_mapping[line_map_index];
                c_line_mapping.range.start_virtual_address = static_location_to_virtual_address(debugger, pdb_line_info.location);
                c_line_mapping.range.end_virtual_address = c_line_mapping.range.start_virtual_address + pdb_line_info.length;

                dynamic_array_push_back(&slot_mapping.c_lines, &c_line_mapping);
            }
        }
    }

    return debugger->state.process_state != Debug_Process_State::NO_ACTIVE_PROCESS;
}

Debugger_State debugger_get_state(Debugger* debugger) {
    return debugger->state;
}



void debugger_wait_for_console_command(Debugger* debugger)
{
    bool wait_for_next_command = true;
    while (wait_for_next_command)
    {
        if (debugger->state.process_state != Debug_Process_State::HALTED) break;
        CONTEXT thread_context;
        thread_context.ContextFlags = CONTEXT_ALL;
        auto& main_thread_info = debugger->threads[debugger->main_thread_info_index];
        if (!GetThreadContext(main_thread_info.handle, &thread_context)) {
            printf("GetThreadContext failed!\n");
            return;
        }

        // Print current state
        String input_line = string_create();
        SCOPE_EXIT(string_destroy(&input_line));
        {
            printf("rip=[0x%08llX] ", thread_context.Rip);

            u64 dist = 0;
            String* closest_symbol = debugger_find_closest_symbol_name(debugger, thread_context.Rip, &dist);
            if (closest_symbol != nullptr) {
                printf(", %s + [%04llX]", closest_symbol->characters, dist);
            }

            printf("\n> ");
            if (string_fill_from_line(&input_line)) {
                return;
            }
        }

        // Handle commands
        Array<String> parts = string_split(input_line, ' ');
        SCOPE_EXIT(string_split_destroy(parts));
        if (parts.size == 0) continue;
        String command = parts[0];

        if (string_equals_cstring(&command, "?")) {
            printf("Commands:\n");
            printf("    c  - continue until next debug-event\n");
            printf("    s  - single step\n");
            printf("    q  - quit\n");
            printf("    r  - show registers\n");
            printf("    d  - display disassembly at current instrution/at specified symbol\n");
            printf("    bp - add breakpoint at symbol\n");
            printf("    bl - list active breakpoints\n");
            printf("    bd - delete breakpoint\n");
            printf("    k  - show stack/do stack-walk\n");
            printf("    i  - show PDB-Infos (All functions in src)\n");
        }
        else if (string_equals_cstring(&command, "c") || string_equals_cstring(&command, "continue")) {
            return;
        }
        else if (string_equals_cstring(&command, "s") || string_equals_cstring(&command, "step")) {
            debugger_single_step_thread(debugger, debugger->threads[debugger->main_thread_info_index].handle);
            continue;
        }
        else if (string_equals_cstring(&command, "q") || string_equals_cstring(&command, "quit") || string_equals_cstring(&command, "exit")) {
            debugger_reset(debugger);
            return;
        }
        else if (string_equals_cstring(&command, "registers") || string_equals_cstring(&command, "r"))
        {
            auto& c = thread_context;
            auto flag = [&](int bit_index)->int { return (thread_context.EFlags & (1 << bit_index)) == 0 ? 0 : 1; };
            printf("    rax=0x%016llx rbx=0x%016llx rcx=0x%016llx\n", c.Rax, c.Rbx, c.Rcx);
            printf("    rdx=0x%016llx rsi=0x%016llx rdi=0x%016llx\n", c.Rdx, c.Rsi, c.Rdi);
            printf("    rip=0x%016llx rsp=0x%016llx rbp=0x%016llx\n", c.Rip, c.Rsp, c.Rbp);
            printf("     r8=0x%016llx  r9=0x%016llx r10=0x%016llx\n", c.R8, c.R9, c.R10);
            printf("    r11=0x%016llx r12=0x%016llx r13=0x%016llx\n", c.R11, c.R12, c.R13);
            printf("    r14=0x%016llx r15=0x%016llx eflags=0x%08lx\n", c.R14, c.R15, c.EFlags);
            printf("    CF: %d, PF: %d, AF: %d, ZF: %d, SF: %d\n", flag(0), flag(2), flag(4), flag(6), flag(7));
            printf("    TF: %d, IF: %d, DF: %d, OF: %d, RF: %d\n", flag(8), flag(9), flag(10), flag(11), flag(16));
            printf("    Carry, Parity, Auxillary-Carry, Zero, Sign, Trap, Interrupt-enabled, Direction, Overflow, Resume\n");
        }
        else if (string_equals_cstring(&command, "d") || string_equals_cstring(&command, "display"))
        {
            auto& bytes = debugger->byte_buffer;

            void* virtual_address = (void*)thread_context.Rip;
            u64 byte_length = 32;
            if (parts.size == 2 && debugger->pdb_info != nullptr)
            {
                String symbol_name = parts[1];
                auto pdb_info = debugger->pdb_info;

                // Search in pdb for function with this name...
                PDB_Analysis::PDB_Function* function = nullptr;
                for (int i = 0; i < pdb_info->functions.size; i++) {
                    auto& fn = pdb_info->functions[i];
                    if (string_equals(&symbol_name, &fn.name)) {
                        function = &fn;
                        break;
                    }
                }

                if (function != nullptr)
                {
                    printf(
                        "Found function: %s, section: %d, offset: %lld\n",
                        function->name.characters, function->location.section_index, function->location.offset
                    );
                    u64 fn_address = static_location_to_virtual_address(debugger, function->location);
                    if (fn_address != 0) {
                        virtual_address = (void*)fn_address;
                        byte_length = function->length;
                    }
                }
                else {
                    printf("Could not find function, continuing with normal disassembly output\n");
                }
            }

            debugger_disassemble_bytes(debugger, (u64)virtual_address, (u32)byte_length);
            debugger_print_last_disassembly(debugger, (u64)virtual_address, 2);
        }
        else if (string_equals_cstring(&command, "bp") || string_equals_cstring(&command, "ba"))  // Add breakpoint
        {
            if (parts.size != 2) {
                printf("Add breakpoint command requires an argument\n");
                continue;
            }

            auto param = parts[1];
            bool is_address = false;
            u64 function_address = 0;
            if (param.size > 2 && (string_starts_with(param, "0x") || string_starts_with(param, "0X"))) {
                Optional<i64> value = string_parse_i64_hex(string_create_substring_static(&param, 2, param.size));
                if (!value.available) {
                    printf("Add breakpoint failed, couldn't parse hexadecimal value\n");
                    continue;
                }
                if (value.value == 0) {
                    printf("Add breakpoint failed, value is not a valid address\n");
                    continue;
                }
                function_address = (u64)value.value;
            }
            else {
                function_address = debugger_find_address_of_function(debugger, parts[1]);
            }

            if (function_address == 0) {
                printf("Add breakpoint failed, could not find address of symbol\n");
                continue;
            }

            bool other_exists = false;
            auto& breakpoints = debugger->address_breakpoints;
            for (int i = 0; i < breakpoints.size; i++) {
                auto& other = breakpoints[i];
                if (other.address == function_address) {
                    other_exists = true;
                    break;
                }
            }
            if (other_exists) {
                printf("Add breakpoint failed, breakpoint with this address already set\n");
                continue;
            }
            if (breakpoints.size > 3) {
                printf("Add breakpoint failed, reached maximum breakpoint count (4)\n");
                continue;
            }

            bool success = debugger_add_address_breakpoint(debugger, function_address);
            if (success) {
                printf("Added new breakpoint at [0x%08llX]\n", function_address);
            }
            else {
                printf("Could not add breakpoint at [0x%08llX]\n", function_address);
            }
        }
        else if (string_equals_cstring(&command, "bd") || string_equals_cstring(&command, "bc"))  // Delete breakpoint
        {
            if (parts.size != 2) {
                printf("Delete breakpoint command requires an argument (id)\n");
                continue;
            }

            auto id_opt = string_parse_int(&parts[1]);
            if (!id_opt.available) {
                printf("Delete breakpoint failed, could not parse argument\n");
                continue;
            }
            int index = id_opt.value;

            auto& breakpoints = debugger->address_breakpoints;
            if (index < 0 || index >= breakpoints.size) {
                printf("Delete breakpoint failed, breakpoint with given id does not exist");
                continue;
            }

            dynamic_array_remove_ordered(&breakpoints, index);
            printf("Removed breakpoint %d\n", index);
        }
        else if (string_equals_cstring(&command, "bl") || string_equals_cstring(&command, "breakpoint_list")) // List breakpoints
        {
            auto& breakpoints = debugger->address_breakpoints;
            for (int i = 0; i < breakpoints.size; i++) {
                auto& bp = breakpoints[i];
                printf("    #%2d, Address: [0x%08llX]\n", i, bp.address);
            }
        }
        else if (string_equals_cstring(&command, "i"))
        {
            for (int i = 0; i < debugger->pdb_info->source_infos.size; i++) {
                auto& src_info = debugger->pdb_info->source_infos[i];
                auto& fn_info = debugger->pdb_info->functions[src_info.function_index];
                printf(
                    "  %s, address: 0x%08llX, length: %lld\n",
                    fn_info.name.characters,
                    static_location_to_virtual_address(debugger, fn_info.location),
                    fn_info.length
                );
            }
        }
        else if (string_equals_cstring(&command, "k") || string_equals_cstring(&command, "stack")) {
            do_stack_walk(debugger);
        }
        else {
            printf("Invalid command: \"%s\"\nRetry: ", command.characters);
        }
    }
}

Source_Breakpoint* debugger_add_source_breakpoint(Debugger* debugger, int line_index, Compilation_Unit* unit)
{
    for (int i = 0; i < debugger->source_breakpoints.size; i++) {
        auto& bp = debugger->source_breakpoints[i];
        if (bp->compilation_unit == unit && bp->line_index == line_index) {
            bp->active_reference_count += 1;
            // Re-add address-breakpoints if breakpoint was previously inactive
            if (bp->active_reference_count == 1) {
                for (int i = 0; i < bp->addresses.size; i++) {
                    debugger_add_address_breakpoint(debugger, bp->addresses[i]);
                }
            }
            return bp;
        }
    }

    Dynamic_Array<Machine_Code_Segment> segments = dynamic_array_create<Machine_Code_Segment>();
    SCOPE_EXIT(dynamic_array_destroy(&segments));
    source_mapping_upp_line_to_machine_code_segments(debugger, unit, line_index, &segments);

    Source_Breakpoint* breakpoint = new Source_Breakpoint;
    breakpoint->addresses = dynamic_array_create<u64>();
    breakpoint->compilation_unit = unit;
    breakpoint->line_index = line_index;
    breakpoint->active_reference_count = 1;

    for (int i = 0; i < segments.size; i++) {
        auto& segment = segments[i];
        bool success = debugger_add_address_breakpoint(debugger, segment.virtual_address_start);
        dynamic_array_push_back(&breakpoint->addresses, segment.virtual_address_start);
    }

    dynamic_array_push_back(&debugger->source_breakpoints, breakpoint);
    return breakpoint;
}

void debugger_remove_source_breakpoint(Debugger* debugger, Source_Breakpoint* breakpoint)
{
    if (breakpoint == nullptr) return;
    bool was_active = breakpoint->active_reference_count > 0;
    breakpoint->active_reference_count = math_maximum(0, breakpoint->active_reference_count - 1);
    if (breakpoint->active_reference_count == 0 && was_active) {
        for (int i = 0; i < breakpoint->addresses.size; i++) {
            debugger_remove_address_breakpoint(debugger, breakpoint->addresses[i]);
        }
    }
}

Array<Stack_Frame> debugger_get_stack_frames(Debugger* debugger)
{
    if (debugger->state.process_state != Debug_Process_State::HALTED) {
        Array<Stack_Frame> empty;
        empty.data = 0;
        empty.size = 0;
        return empty;
    }

    do_stack_walk(debugger);
    return dynamic_array_as_array(&debugger->stack_frames);
}

void debugger_print_line_translation(Debugger* debugger, Compilation_Unit* compilation_unit, int line_index, Compiler_Analysis_Data* analysis_data)
{
    printf("Mapping info for line #%d of %s\n", line_index, compilation_unit->filepath.characters);

    Compilation_Unit_Mapping* unit_map = nullptr;
    for (int i = 0; i < debugger->compilation_unit_mapping.size; i++) {
        auto& unit = debugger->compilation_unit_mapping[i];
        if (unit.compilation_unit == compilation_unit) {
            unit_map = &unit;
            break;
        }
    }
    if (unit_map == nullptr) return;
    if (line_index < 0 || line_index >= unit_map->lines.size)  return;

    // Prepare temporary values
    auto c_source = c_generator_get_translation()->source_code;
    auto c_line_array = string_split(c_source, '\n');
    SCOPE_EXIT(string_split_destroy(c_line_array));

    String tmp = string_create(128);
    SCOPE_EXIT(string_destroy(&tmp));

    Dynamic_Array<byte> byte_buffer = dynamic_array_create<byte>(256);
    Dynamic_Array<INSTRUX> disassembly = dynamic_array_create<INSTRUX>(16);
    SCOPE_EXIT(dynamic_array_destroy(&byte_buffer));
    SCOPE_EXIT(dynamic_array_destroy(&disassembly));

    auto& line_map = unit_map->lines[line_index];
    for (int i = 0; i < line_map.statements.size; i++)
    {
        auto stat_map = line_map.statements[i];
        AST::base_append_to_string(upcast(stat_map->statement), &tmp);
        printf("Statement #%d: %s\n", i, tmp.characters);
        string_reset(&tmp);

        for (int j = 0; j < stat_map->ir_instructions.size; j++)
        {
            auto ir_instr_map = stat_map->ir_instructions[j];
            ir_instruction_append_to_string(
                &ir_instr_map->code_block->instructions[ir_instr_map->instruction_index], &tmp, 0, ir_instr_map->code_block, analysis_data);
            printf("  IR-Instr: #%d: %s\n", j, tmp.characters);
            string_reset(&tmp);

            for (int k = 0; k < ir_instr_map->c_lines.size; k++)
            {
                auto c_line = ir_instr_map->c_lines[k];
                int c_index = c_line->c_line_index;
                if (c_index < 0 || c_index >= c_line_array.size) {
                    printf("    INVALID line index: %d\n", c_index);
                    continue;
                }
                string_append_string(&tmp, &c_line_array[c_index]);
                printf("    C-Line: #%d: %s\n", c_index, tmp.characters);
                string_reset(&tmp);

                // Print disassembly
                if (c_line->range.start_virtual_address != 0 && c_line->range.start_virtual_address < c_line->range.end_virtual_address) {
                    u64 size = c_line->range.end_virtual_address - c_line->range.start_virtual_address;
                    debugger_disassemble_bytes(debugger, c_line->range.start_virtual_address, size);
                    debugger_print_last_disassembly(debugger, c_line->range.start_virtual_address, 6);
                }
            }
        }
    }
}
