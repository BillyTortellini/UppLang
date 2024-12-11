#include "game.hpp"

#include <Windows.h>
#include "../../win32/windows_helper_functions.hpp"
#include <cstdio>
#include <iostream>
#include "../../utility/file_io.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include <bddisasm.h>

#include <dia2.h>
#include <atlbase.h>

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

    enum class Register_x64
    {
        RAX,
        RBX,
        RCX,
        RDX,
        RSI,
        RDI,
        RIP,
        RSP,
        RBP,

        R8,
        R9,
        R10,
        R11,
        R12,
        R13,
        R14,
        R15,

        MM_N,  // Floating point register (0-7)
        XMM_N, // 128-Bit Media registers (0-15)

        OTHER, // Other register on x64 which I'll ignore for now
    };

    struct PDB_Location_Register
    {
        Register_x64 reg;
        u8 size; // Byte-count, either 1, 2, 4, 8, 16...
        u8 offset; // e.g. 0 for first 
        int numbered_register_index; // For xmm and mmx registers
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
            PDB_Location_Register register_loc;
            struct {
                PDB_Location_Register reg;
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
        String normal_name;
        String mangled_name;
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

    PDB_Information pdb_information_create()
    {
        PDB_Information information;
        information.source_infos      = dynamic_array_create<PDB_Function_Source_Info>();
        information.block_infos       = dynamic_array_create<PDB_Code_Block_Info>();
        information.global_infos      = dynamic_array_create<PDB_Variable_Info>();
        information.functions         = dynamic_array_create<PDB_Function>();
        information.source_file_paths = hashtable_create_empty<i32, String>(3, hash_i32, equals_i32);
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
            string_destroy(&fn.mangled_name);
            string_destroy(&fn.normal_name);
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

    PDB_Location_Register register_id_to_location(u32 reg_id)
    {
        PDB_Location_Register loc;
        loc.reg = Register_x64::OTHER;
        loc.numbered_register_index = 0;
        loc.size = 8;
        loc.offset = 0;
        switch (reg_id)
        {
        case CV_REG_NONE: break;

        case CV_AMD64_AL: loc.reg = Register_x64::RAX; loc.size = 1; break;
        case CV_AMD64_CL: loc.reg = Register_x64::RCX; loc.size = 1; break;
        case CV_AMD64_DL: loc.reg = Register_x64::RDX; loc.size = 1; break;
        case CV_AMD64_BL: loc.reg = Register_x64::RBX; loc.size = 1; break;

        case CV_AMD64_AH: loc.reg = Register_x64::RAX; loc.size = 1; loc.offset = 1; break;
        case CV_AMD64_CH: loc.reg = Register_x64::RCX; loc.size = 1; loc.offset = 1; break;
        case CV_AMD64_DH: loc.reg = Register_x64::RDX; loc.size = 1; loc.offset = 1; break;
        case CV_AMD64_BH: loc.reg = Register_x64::RBX; loc.size = 1; loc.offset = 1; break;

        case CV_AMD64_SIL: loc.reg = Register_x64::RSI; loc.size = 1; break;
        case CV_AMD64_DIL: loc.reg = Register_x64::RDI; loc.size = 1; break;
        case CV_AMD64_BPL: loc.reg = Register_x64::RBP; loc.size = 1; break;
        case CV_AMD64_SPL: loc.reg = Register_x64::RSP; loc.size = 1; break;

        case CV_AMD64_AX: loc.reg = Register_x64::RAX; loc.size = 2; break;
        case CV_AMD64_CX: loc.reg = Register_x64::RCX; loc.size = 2; break;
        case CV_AMD64_DX: loc.reg = Register_x64::RDX; loc.size = 2; break;
        case CV_AMD64_BX: loc.reg = Register_x64::RBX; loc.size = 2; break;
        case CV_AMD64_SP: loc.reg = Register_x64::RSP; loc.size = 2; break;
        case CV_AMD64_BP: loc.reg = Register_x64::RBP; loc.size = 2; break;
        case CV_AMD64_SI: loc.reg = Register_x64::RSI; loc.size = 2; break;
        case CV_AMD64_DI: loc.reg = Register_x64::RDI; loc.size = 2; break;

        case CV_AMD64_EAX: loc.reg = Register_x64::RAX; loc.size = 4; break;
        case CV_AMD64_ECX: loc.reg = Register_x64::RCX; loc.size = 4; break;
        case CV_AMD64_EDX: loc.reg = Register_x64::RDX; loc.size = 4; break;
        case CV_AMD64_EBX: loc.reg = Register_x64::RBX; loc.size = 4; break;
        case CV_AMD64_ESP: loc.reg = Register_x64::RSP; loc.size = 4; break;
        case CV_AMD64_EBP: loc.reg = Register_x64::RBP; loc.size = 4; break;
        case CV_AMD64_ESI: loc.reg = Register_x64::RSI; loc.size = 4; break;
        case CV_AMD64_EDI: loc.reg = Register_x64::RDI; loc.size = 4; break;

        case CV_AMD64_RIP:    loc.reg = Register_x64::RIP; loc.size = 8; break;

        case CV_AMD64_RAX: loc.reg = Register_x64::RAX; loc.size = 8; break;
        case CV_AMD64_RBX: loc.reg = Register_x64::RBX; loc.size = 8; break;
        case CV_AMD64_RCX: loc.reg = Register_x64::RCX; loc.size = 8; break;
        case CV_AMD64_RDX: loc.reg = Register_x64::RDX; loc.size = 8; break;
        case CV_AMD64_RSI: loc.reg = Register_x64::RSI; loc.size = 8; break;
        case CV_AMD64_RDI: loc.reg = Register_x64::RDI; loc.size = 8; break;
        case CV_AMD64_RBP: loc.reg = Register_x64::RBP; loc.size = 8; break;
        case CV_AMD64_RSP: loc.reg = Register_x64::RSP; loc.size = 8; break;

        case CV_AMD64_R8:  loc.reg = Register_x64::R8;  loc.size = 8; break;
        case CV_AMD64_R9:  loc.reg = Register_x64::R9;  loc.size = 8; break;
        case CV_AMD64_R10: loc.reg = Register_x64::R10; loc.size = 8; break;
        case CV_AMD64_R11: loc.reg = Register_x64::R11; loc.size = 8; break;
        case CV_AMD64_R12: loc.reg = Register_x64::R12; loc.size = 8; break;
        case CV_AMD64_R13: loc.reg = Register_x64::R13; loc.size = 8; break;
        case CV_AMD64_R14: loc.reg = Register_x64::R14; loc.size = 8; break;
        case CV_AMD64_R15: loc.reg = Register_x64::R15; loc.size = 8; break;

        case CV_AMD64_R8B:  loc.reg = Register_x64::R8;  loc.size = 1; break;
        case CV_AMD64_R9B:  loc.reg = Register_x64::R9;  loc.size = 1; break;
        case CV_AMD64_R10B: loc.reg = Register_x64::R10; loc.size = 1; break;
        case CV_AMD64_R11B: loc.reg = Register_x64::R11; loc.size = 1; break;
        case CV_AMD64_R12B: loc.reg = Register_x64::R12; loc.size = 1; break;
        case CV_AMD64_R13B: loc.reg = Register_x64::R13; loc.size = 1; break;
        case CV_AMD64_R14B: loc.reg = Register_x64::R14; loc.size = 1; break;
        case CV_AMD64_R15B: loc.reg = Register_x64::R15; loc.size = 1; break;

        case CV_AMD64_R8W:  loc.reg = Register_x64::R8;  loc.size = 2; break;
        case CV_AMD64_R9W:  loc.reg = Register_x64::R9;  loc.size = 2; break;
        case CV_AMD64_R10W: loc.reg = Register_x64::R10; loc.size = 2; break;
        case CV_AMD64_R11W: loc.reg = Register_x64::R11; loc.size = 2; break;
        case CV_AMD64_R12W: loc.reg = Register_x64::R12; loc.size = 2; break;
        case CV_AMD64_R13W: loc.reg = Register_x64::R13; loc.size = 2; break;
        case CV_AMD64_R14W: loc.reg = Register_x64::R14; loc.size = 2; break;
        case CV_AMD64_R15W: loc.reg = Register_x64::R15; loc.size = 2; break;

        case CV_AMD64_R8D:  loc.reg = Register_x64::R8;  loc.size = 4; break;
        case CV_AMD64_R9D:  loc.reg = Register_x64::R9;  loc.size = 4; break;
        case CV_AMD64_R10D: loc.reg = Register_x64::R10; loc.size = 4; break;
        case CV_AMD64_R11D: loc.reg = Register_x64::R11; loc.size = 4; break;
        case CV_AMD64_R12D: loc.reg = Register_x64::R12; loc.size = 4; break;
        case CV_AMD64_R13D: loc.reg = Register_x64::R13; loc.size = 4; break;
        case CV_AMD64_R14D: loc.reg = Register_x64::R14; loc.size = 4; break;
        case CV_AMD64_R15D: loc.reg = Register_x64::R15; loc.size = 4; break;

        case CV_AMD64_MM0: loc.reg = Register_x64::MM_N; loc.numbered_register_index = 0; loc.size = 8; break;
        case CV_AMD64_MM1: loc.reg = Register_x64::MM_N; loc.numbered_register_index = 1; loc.size = 8; break;
        case CV_AMD64_MM2: loc.reg = Register_x64::MM_N; loc.numbered_register_index = 2; loc.size = 8; break;
        case CV_AMD64_MM3: loc.reg = Register_x64::MM_N; loc.numbered_register_index = 3; loc.size = 8; break;
        case CV_AMD64_MM4: loc.reg = Register_x64::MM_N; loc.numbered_register_index = 4; loc.size = 8; break;
        case CV_AMD64_MM5: loc.reg = Register_x64::MM_N; loc.numbered_register_index = 5; loc.size = 8; break;
        case CV_AMD64_MM6: loc.reg = Register_x64::MM_N; loc.numbered_register_index = 6; loc.size = 8; break;
        case CV_AMD64_MM7: loc.reg = Register_x64::MM_N; loc.numbered_register_index = 7; loc.size = 8; break;

        // Handle ranges seperately
        default: 
        {
            // MM Registers (Float registers, actually 80 bit large)
            if (reg_id >= CV_AMD64_MM0 && reg_id <= CV_AMD64_MM7) {
                loc.reg = Register_x64::MM_N;
                loc.size = 8;
                loc.numbered_register_index = reg_id - CV_AMD64_MM0;
                loc.offset = 0;
            }
            // MM Register sub-range
            else if (reg_id >= CV_AMD64_MM00 && reg_id <= CV_AMD64_MM71) {
                loc.reg = Register_x64::MM_N;
                loc.size = 4;
                loc.numbered_register_index = (reg_id - CV_AMD64_MM00) / 2;
                loc.offset = (reg_id - CV_AMD64_MM00) % 2 == 0 ? 0 : 4;
            }
            // Full XMM Registers
            else if (reg_id >= CV_AMD64_XMM0 && reg_id <= CV_AMD64_XMM15) {
                loc.reg = Register_x64::XMM_N;
                loc.numbered_register_index = reg_id - CV_AMD64_XMM0;
                loc.size = 16;
            }
            // XMM float sub-ranges
            else if (reg_id >= CV_AMD64_XMM0_0 && reg_id <= CV_AMD64_XMM7_3) {
                loc.reg = Register_x64::XMM_N;
                loc.size = 4;
                loc.numbered_register_index = (reg_id - CV_AMD64_XMM0) / 4;
                loc.offset = ((reg_id - CV_AMD64_XMM0) % 4) * 4;
            }
            else if (reg_id >= CV_AMD64_XMM8_0 && reg_id <= CV_AMD64_XMM15_3) {
                loc.reg = Register_x64::XMM_N;
                loc.size = 4;
                loc.numbered_register_index = 7 + (reg_id - CV_AMD64_XMM7) / 4;
                loc.offset = ((reg_id - CV_AMD64_XMM7) % 4) * 4;
            }
            // XMM double sub-range
            else if (reg_id >= CV_AMD64_XMM0L && reg_id <= CV_AMD64_XMM7L) {
                loc.reg = Register_x64::XMM_N;
                loc.size = 8;
                loc.numbered_register_index = reg_id - CV_AMD64_XMM0L;
                loc.offset = 0;
            }
            else if (reg_id >= CV_AMD64_XMM0H && reg_id <= CV_AMD64_XMM7H) {
                loc.reg = Register_x64::XMM_N;
                loc.size = 8;
                loc.numbered_register_index = reg_id - CV_AMD64_XMM0H;
                loc.offset = 8;
            }
            else if (reg_id >= CV_AMD64_XMM8L && reg_id <= CV_AMD64_XMM15L) {
                loc.reg = Register_x64::XMM_N;
                loc.size = 8;
                loc.numbered_register_index = reg_id - CV_AMD64_XMM8L;
                loc.offset = 0;
            }
            else if (reg_id >= CV_AMD64_XMM8H && reg_id <= CV_AMD64_XMM15H) {
                loc.reg = Register_x64::XMM_N;
                loc.size = 8;
                loc.numbered_register_index = reg_id - CV_AMD64_XMM8H;
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
            location.options.register_loc.reg = Register_x64::OTHER;
            location.options.register_loc.numbered_register_index = 0;
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
            location.options.register_relative.reg.reg = Register_x64::OTHER;
            location.options.register_relative.reg.numbered_register_index = 0;
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
                fn.normal_name = string_create();
                fn.mangled_name = string_create();
                fn.source_info_index = -1;
                pdb_symbol_get_undecorated_name(symbol, &fn.normal_name);
                pdb_symbol_get_name(symbol, &fn.mangled_name);
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

    bool read_as_much_as_possible(HANDLE process_handle, void* virtual_address, Dynamic_Array<u8>* out_bytes, u64 read_size)
    {
        dynamic_array_reset(out_bytes);
        dynamic_array_reserve(out_bytes, read_size);

        if (virtual_address == nullptr || read_size == 0 || process_handle == nullptr) {
            return false;
        }

        size_t bytes_written = 0;
        if (ReadProcessMemory(process_handle, virtual_address, out_bytes->data, read_size, &bytes_written) != 0) {
            out_bytes->size = read_size;
            return true;
        }

        // Check largest possible read size
        {
            MEMORY_BASIC_INFORMATION memory_info;
            int written_bytes = VirtualQueryEx(process_handle, virtual_address, &memory_info, sizeof(memory_info));
            if (written_bytes == 0) {
                return false;
            }

            if (memory_info.State != MEM_COMMIT) {
                return false;
            }

            i64 max_read_length = (i64)memory_info.RegionSize - ((i64)virtual_address - (i64)memory_info.BaseAddress);
            if (max_read_length < 0) {
                return false;
            }
            read_size = math_minimum((u64)max_read_length, read_size);
        }

        if (ReadProcessMemory(process_handle, virtual_address, out_bytes->data, read_size, &bytes_written) != 0) {
            return false;
        }
        out_bytes->size = (int)read_size;
        return true;
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

        dynamic_array_reset(byte_buffer);
        dynamic_array_reserve(byte_buffer, max_size + 2);
        if (!read_as_much_as_possible(process_handle, virtual_address, byte_buffer, max_size)) {
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
    struct Export_Symbol_Info
    {
        u32 rva; // Relative Virtual Address...
        Optional<String> name; // Symbols may not have a name if they are referenced by ordinal
        Optional<String> forwarder_name; // If the symbol is a forward to another dll
    };

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

    struct PE_Info
    {
        u64 base_address; // VirtualAddress where PE image is loaded
        String name; // Name of executable or dll
        String pdb_name;
        Dynamic_Array<Export_Symbol_Info> exported_symbols;
        Dynamic_Array<Section_Info> sections;
        // TODO: At some point the stack-walking information...
    };

    PE_Info pe_info_create()
    {
        PE_Info info;
        info.base_address = 0;
        info.name = string_create();
        info.pdb_name = string_create();
        info.exported_symbols = dynamic_array_create<Export_Symbol_Info>();
        info.sections = dynamic_array_create<Section_Info>();
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

        for (int i = 0; i < info->sections.size; i++) {
            string_destroy(&info->sections[i].name);
        }
        dynamic_array_destroy(&info->sections);
    }

    struct PDB_INFO_DUMMY
    {
        u32 signature;
        GUID guid;
        u32 age;
    };

    const int MAX_STRING_LENGTH = 260;

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
        IMAGE_DOS_HEADER dos_header;
        IMAGE_NT_HEADERS64 nt_header;
        IMAGE_DATA_DIRECTORY export_table_address_and_size;
        IMAGE_DATA_DIRECTORY debug_table_address_size;
        IMAGE_EXPORT_DIRECTORY export_table_header;
        bool export_table_exists = false;
        bool debug_table_exists = false;

        // Read DOS and PE Header
        bool success = Process_Memory::read_single_value(process_handle, (void*)base_virtual_address, &dos_header);
        if (success) {
            success = Process_Memory::read_single_value(process_handle, (void*)(base_virtual_address + dos_header.e_lfanew), &nt_header);
        }

        // Early exit if we can't even read the header infos
        if (!success) {
            return false;
        }

        // Read export table header
        if (success)
        {
            export_table_address_and_size = nt_header.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            export_table_exists = export_table_address_and_size.VirtualAddress != 0;
            if (export_table_exists) {
                export_table_exists = Process_Memory::read_single_value(
                    process_handle, (void*)(base_virtual_address + export_table_address_and_size.VirtualAddress), &export_table_header
                );
            }
            debug_table_address_size = nt_header.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
            debug_table_exists = debug_table_address_size.VirtualAddress != 0;
        }

        // Read section information
        if (success && nt_header.FileHeader.NumberOfSections > 0)
        {
            u64 section_start_offset = dos_header.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt_header.FileHeader.SizeOfOptionalHeader;
            void* section_table_virtual_address = (void*)(base_virtual_address + section_start_offset);
            int section_count = nt_header.FileHeader.NumberOfSections;

            // Note: dynamic_array size is zero if this fails --> Maybe we want to not store success for failure...
            Dynamic_Array<IMAGE_SECTION_HEADER> section_infos = dynamic_array_create<IMAGE_SECTION_HEADER>(section_count);
            SCOPE_EXIT(dynamic_array_destroy(&section_infos));
            Process_Memory::read_array(process_handle, section_table_virtual_address, &section_infos, section_count);
            if (section_infos.size == 0) {
                success = false;
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
                info.flag_read    = (section.Characteristics & IMAGE_SCN_MEM_READ) != 0;
                info.flag_write   = (section.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
                info.flag_execute = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
                dynamic_array_push_back(&pe_info->sections, info);
            }
        }

        // Read infos from export_table
        if (export_table_exists)
        {
            // Load name from export table if not already set
            if (pe_info->name.size == 0 && export_table_header.Name != 0) {
                Process_Memory::read_string(
                    process_handle, (void*)(base_virtual_address + export_table_header.Name), &pe_info->name, MAX_STRING_LENGTH, false, &byte_buffer
                );
            }

            Dynamic_Array<u32> function_locations = dynamic_array_create<u32>(export_table_header.NumberOfFunctions);
            SCOPE_EXIT(dynamic_array_destroy(&function_locations));
            bool function_locations_read = Process_Memory::read_array(
                process_handle,
                (void*)(base_virtual_address + export_table_header.AddressOfFunctions),
                &function_locations,
                export_table_header.NumberOfFunctions
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
                        rva >= export_table_address_and_size.VirtualAddress &&
                        rva < export_table_address_and_size.VirtualAddress + export_table_address_and_size.Size;
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
            Dynamic_Array<u16> symbol_indices_unbiased = dynamic_array_create<u16>(export_table_header.NumberOfNames);
            Dynamic_Array<u32> symbol_name_rvas = dynamic_array_create<u32>(export_table_header.NumberOfNames);
            SCOPE_EXIT(dynamic_array_destroy(&symbol_indices_unbiased));
            SCOPE_EXIT(dynamic_array_destroy(&symbol_name_rvas));

            bool indices_available = Process_Memory::read_array(
                process_handle,
                (void*)(base_virtual_address + export_table_header.AddressOfNameOrdinals),
                &symbol_indices_unbiased,
                export_table_header.NumberOfNames
            );
            bool names_available = Process_Memory::read_array(
                process_handle,
                (void*)(base_virtual_address + export_table_header.AddressOfNames),
                &symbol_name_rvas,
                export_table_header.NumberOfNames
            );

            if (indices_available && names_available)
            {
                assert(
                    symbol_indices_unbiased.size == symbol_name_rvas.size && symbol_name_rvas.size == export_table_header.NumberOfNames,
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
                    if (export_symbol_index < 0 || export_symbol_index >= (int)export_table_header.NumberOfFunctions) {
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
        if (debug_table_exists)
        {
            int debug_info_count = debug_table_address_size.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
            for (int i = 0; i < debug_info_count; i++)
            {
                IMAGE_DEBUG_DIRECTORY debug_table_entry;
                bool load_worked = Process_Memory::read_single_value(
                    process_handle,
                    (void*)(base_virtual_address + debug_table_address_size.VirtualAddress + i * sizeof(IMAGE_DEBUG_DIRECTORY)),
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

        return success;
    }
}





struct Debugger
{
    HANDLE process_handle;
    HANDLE main_thread_handle;
    DWORD process_id;
    DWORD main_thread_id;
};

static Debugger debugger;

void debugger_initialize()
{
    debugger.process_handle = nullptr;
    debugger.main_thread_handle = nullptr;
}

void debugger_shutdown()
{
    if (debugger.main_thread_handle != nullptr) {
        CloseHandle(debugger.main_thread_handle);
        debugger.main_thread_handle = nullptr;
    }
    if (debugger.process_handle != nullptr) {
        CloseHandle(debugger.process_handle);
        debugger.process_handle = nullptr;
    }
}

bool debugger_start_process(const char* filepath)
{
    if (debugger.process_handle != nullptr) {
        panic("Debugger already has open process!");
    }

    STARTUPINFO startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info;
    bool success = CreateProcessA(
        filepath, // application path
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

    debugger.process_handle = process_info.hProcess;
    debugger.process_id = process_info.dwProcessId;
    debugger.main_thread_handle = process_info.hThread;
    debugger.main_thread_id = process_info.dwThreadId;

    ResumeThread(process_info.hThread);

    return true;
}

// Returns true if stdio was closed
bool string_fill_from_line(String* to_fill)
{
    string_reset(to_fill);
    while (true)
    {
        int c = getc(stdin);
        if (c == 0 || c == EOF) {
            return true;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\r' || c < ' ') {
            continue;
        }
        string_append_character(to_fill, c);
    }

    return false;
}

u64 find_address_of_symbol(String name, PDB_Analysis::PDB_Information pdb_info, PE_Analysis::PE_Info main_pe_info)
{
    // Search in pdb for function with this name...
    for (int i = 0; i < pdb_info.functions.size; i++) {
        auto& function = pdb_info.functions[i];
        if (!string_equals(&name, &function.normal_name)) continue;

        int section_index = function.location.section_index - 1;
        u64 offset = function.location.offset;

        auto& sections = main_pe_info.sections;
        if (section_index >= 0 && section_index < sections.size) {
            auto& section = sections[section_index];
            return main_pe_info.base_address + section.rva + offset;
        }
    }
    return 0;
}

String* find_closest_symbol_name(u64 address, Dynamic_Array<PE_Analysis::PE_Info> pe_infos, PDB_Analysis::PDB_Information pdb_info, u64* out_dist = nullptr)
{
    String* closest_name = nullptr;
    u64 closest_distance = (u64)(i64)-1;

    if (pe_infos.size == 0) {
        return nullptr;
    }

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
            }
        }
    }

    auto& section_infos = pe_infos[0].sections;
    for (int i = 0; i < pdb_info.functions.size; i++)
    {
        auto& function = pdb_info.functions[i];
        auto loc = function.location;
        if (function.normal_name.size == 0 && function.mangled_name.size == 0) continue;
        if (loc.section_index == 0) continue; // Section 0 should not be valid?

        int section_index = loc.section_index - 1; // Sections indices are 1 based...
        if (section_index < 0 || section_index >= section_infos.size) continue;

        auto& section = section_infos[section_index];

        u64 fn_address = pe_infos[0].base_address + section.rva + loc.offset;

        if (address >= fn_address && address < fn_address + function.length)
        {
            if (out_dist != nullptr) {
                *out_dist = address - fn_address;
            }
            if (function.normal_name.size > 0) {
                return &function.normal_name;
            }
            else {
                return &function.mangled_name;
            }
        }
    }

    if (closest_name != nullptr && out_dist != nullptr) {
        *out_dist = closest_distance;
    }
    return closest_name;
}

struct Breakpoint
{
    u64 address;
    int id; // To remove the breakpoint
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

void game_entry()
{
    PDB_Analysis::PDB_Information pdb_info = PDB_Analysis::pdb_information_create();
    SCOPE_EXIT(PDB_Analysis::pdb_information_destroy(&pdb_info));
    bool success = PDB_Analysis::pdb_information_fill_from_file(
        &pdb_info,
        "P:/Martin/Projects/UppLib/backend/test/main.pdb",
        "P:/Martin/Projects/UppLib/backend/test/main.obj"
    );

    if (!success) {
        printf("Couldn't load pdb infos");
        std::cin.ignore();
        return;
    }

    Dynamic_Array<Breakpoint> breakpoints = dynamic_array_create<Breakpoint>();
    SCOPE_EXIT(dynamic_array_destroy(&breakpoints));
    int next_breakpoint_id = 0;

    Dynamic_Array<PE_Analysis::PE_Info> pe_infos = dynamic_array_create<PE_Analysis::PE_Info>();
    SCOPE_EXIT(
        for (int i = 0; i < pe_infos.size; i++) {
            PE_Analysis::pe_info_destroy(&pe_infos[i]);
        }
    dynamic_array_destroy(&pe_infos);
    );

    debugger_initialize();
    SCOPE_EXIT(debugger_shutdown());
    success = debugger_start_process("P:/Martin/Projects/UppLib/backend/test/main.exe");
    if (!success) {
        exit(-1);
    }

    u64 base_executable_image_va = 0;
    String string_buffer = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&string_buffer));
    Dynamic_Array<u8> byte_buffer = dynamic_array_create<u8>(256);
    SCOPE_EXIT(dynamic_array_destroy(&byte_buffer));

    String input_line = string_create();
    SCOPE_EXIT(string_destroy(&input_line));
    bool loop_var = true;
    bool waiting_for_step_trap = false;
    while (loop_var)
    {
        DEBUG_EVENT debug_event;
        ZeroMemory(&debug_event, sizeof(debug_event));
        bool success = WaitForDebugEventEx(&debug_event, INFINITE);
        if (!success) {
            helper_print_last_error();
            break;
        }
        DWORD continue_status = DBG_CONTINUE;

        // Ignore events from other processes
        if (debug_event.dwProcessId != debugger.process_id) {
            printf("Debug event from other process with id: %d\n", debug_event.dwProcessId);
            ContinueDebugEvent(debug_event.dwProcessId, debug_event.dwThreadId, DBG_EXCEPTION_NOT_HANDLED);
            continue;
        }

        if (debug_event.dwThreadId != debugger.main_thread_id) {
            printf("Debug event from thread with id: %d\n", debug_event.dwThreadId);
            ContinueDebugEvent(debug_event.dwProcessId, debug_event.dwThreadId, DBG_EXCEPTION_NOT_HANDLED);
            continue;
        }

        // Get thread context
        CONTEXT thread_context;
        thread_context.ContextFlags = CONTEXT_ALL;
        if (!GetThreadContext(debugger.main_thread_handle, &thread_context)) {
            helper_print_last_error();
            panic("Should work!");
        }

        // Handle step instruction
        bool is_step_event = false;
        if (waiting_for_step_trap && debug_event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            if (debug_event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP) {
                is_step_event = true;
                waiting_for_step_trap = false;
            }
        }

        // Handle events
        if (!is_step_event)
        {
            printf("Process ID: %5d, thread_id: %5d, Event: ", debug_event.dwProcessId, debug_event.dwThreadId);
            switch (debug_event.dwDebugEventCode)
            {
            case CREATE_PROCESS_DEBUG_EVENT:
            {
                auto& create_info = debug_event.u.CreateProcessInfo;
                base_executable_image_va = (u64)create_info.lpBaseOfImage;
                printf("Create_Process\n");

                // Load portable-executable information
                PE_Analysis::PE_Info pe_info = PE_Analysis::pe_info_create();
                bool success = PE_Analysis::pe_info_fill_from_executable_image(
                    &pe_info, (u64)create_info.lpBaseOfImage, debugger.process_handle, create_info.lpImageName, create_info.fUnicode
                );
                if (success) {
                    dynamic_array_push_back(&pe_infos, pe_info);
                }
                else {
                    PE_Analysis::pe_info_destroy(&pe_info);
                }

                CloseHandle(debug_event.u.CreateProcessInfo.hFile); // Close handle to image file, as win32 doc describes
                break;
            }
            case LOAD_DLL_DEBUG_EVENT:
            {
                auto& dll_load = debug_event.u.LoadDll;
                printf("Load DLL event: ");

                // Load portable-executable information
                PE_Analysis::PE_Info pe_info = PE_Analysis::pe_info_create();
                bool success = PE_Analysis::pe_info_fill_from_executable_image(
                    &pe_info, (u64)dll_load.lpBaseOfDll, debugger.process_handle, dll_load.lpImageName, dll_load.fUnicode
                );
                if (success)
                {
                    if (pe_info.name.size > 0) {
                        printf("\"%s\" \n", pe_info.name.characters);
                    }
                    else {
                        printf("Analysis success, but name not retrievable \n");
                    }
                    dynamic_array_push_back(&pe_infos, pe_info);
                }
                else {
                    printf("Analysis failed!\n");
                    PE_Analysis::pe_info_destroy(&pe_info);
                }

                CloseHandle(debug_event.u.LoadDll.hFile);
                break;
            }
            case EXCEPTION_DEBUG_EVENT: 
            {
                int code = debug_event.u.Exception.ExceptionRecord.ExceptionCode;

                // Check if any of our breakpoints were hit by breakpoint
                if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP)
                {
                    bool our_breakpoint = false;
                    for (int i = 0; i < breakpoints.size; i++) {
                        auto& bp = breakpoints[i];
                        if (thread_context.Rip == bp.address) {
                            our_breakpoint = true;
                            break;
                        }
                    }

                    if (our_breakpoint) {
                        printf("Our Breakpoint was hit!\n");
                        // Set resume flag, so that execution can continue (Do i need to clear this again?)
                        continue_status = DBG_EXCEPTION_HANDLED;
                        thread_context.EFlags = thread_context.EFlags | 0x10000;
                        break;
                    }
                }

                const char* exception_name = "";
                continue_status = DBG_EXCEPTION_NOT_HANDLED;
                switch (code)
                {
                case EXCEPTION_ACCESS_VIOLATION: exception_name = "EXCEPTION_ACCESS_VIOLATION"; break;
                case EXCEPTION_DATATYPE_MISALIGNMENT: exception_name = "EXCEPTION_DATATYPE_MISALIGNMENT"; break;
                case EXCEPTION_BREAKPOINT: exception_name = "EXCEPTION_BREAKPOINT"; break;
                case EXCEPTION_SINGLE_STEP: exception_name = "EXCEPTION_SINGLE_STEP"; break;
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: exception_name = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED"; break;
                case EXCEPTION_FLT_DENORMAL_OPERAND: exception_name = "EXCEPTION_FLT_DENORMAL_OPERAND"; break;
                case EXCEPTION_FLT_DIVIDE_BY_ZERO: exception_name = "EXCEPTION_FLT_DIVIDE_BY_ZERO"; break;
                case EXCEPTION_FLT_INEXACT_RESULT: exception_name = "EXCEPTION_FLT_INEXACT_RESULT"; break;
                case EXCEPTION_FLT_INVALID_OPERATION: exception_name = "EXCEPTION_FLT_INVALID_OPERATION"; break;
                case EXCEPTION_FLT_OVERFLOW: exception_name = "EXCEPTION_FLT_OVERFLOW"; break;
                case EXCEPTION_FLT_STACK_CHECK: exception_name = "EXCEPTION_FLT_STACK_CHECK"; break;
                case EXCEPTION_FLT_UNDERFLOW: exception_name = "EXCEPTION_FLT_UNDERFLOW"; break;
                case EXCEPTION_INT_DIVIDE_BY_ZERO: exception_name = "EXCEPTION_INT_DIVIDE_BY_ZERO"; break;
                case EXCEPTION_INT_OVERFLOW: exception_name = "EXCEPTION_INT_OVERFLOW"; break;
                case EXCEPTION_PRIV_INSTRUCTION: exception_name = "EXCEPTION_PRIV_INSTRUCTION"; break;
                case EXCEPTION_IN_PAGE_ERROR: exception_name = "EXCEPTION_IN_PAGE_ERROR"; break;
                case EXCEPTION_ILLEGAL_INSTRUCTION: exception_name = "EXCEPTION_ILLEGAL_INSTRUCTION"; break;
                case EXCEPTION_NONCONTINUABLE_EXCEPTION: exception_name = "EXCEPTION_NONCONTINUABLE_EXCEPTION"; break;
                case EXCEPTION_STACK_OVERFLOW: exception_name = "EXCEPTION_STACK_OVERFLOW"; break;
                case EXCEPTION_INVALID_DISPOSITION: exception_name = "EXCEPTION_INVALID_DISPOSITION"; break;
                case EXCEPTION_GUARD_PAGE: exception_name = "EXCEPTION_GUARD_PAGE"; break;
                case EXCEPTION_INVALID_HANDLE: exception_name = "EXCEPTION_INVALID_HANDLE"; break;
                }
                printf("Exception %s\n", exception_name);
                break;
            }
            case OUTPUT_DEBUG_STRING_EVENT:
            {
                auto& debug_str = debug_event.u.DebugString;
                auto& str = string_buffer;
                string_reset(&str);
                bool success = Process_Memory::read_string(
                    debugger.process_handle, (void*)(debug_str.lpDebugStringData),
                    &str, debug_str.nDebugStringLength + 1, debug_str.fUnicode, &byte_buffer
                );
                if (success) {
                    printf("Output_Debug_String: \"%s\"\n", str.characters);
                }
                else {
                    printf("Debug string could not be read\n");
                }
                break;
            }
            case UNLOAD_DLL_DEBUG_EVENT:     printf("Unload_Dll\n"); break;
            case CREATE_THREAD_DEBUG_EVENT:  printf("Create_thread\n"); break;
            case EXIT_THREAD_DEBUG_EVENT:    printf("Exit_Thread\n"); break;
            case EXIT_PROCESS_DEBUG_EVENT:   printf("Exit_Process\n"); break;
            case RIP_EVENT:                  printf("RIP event \n"); break;
            default: loop_var = false; break;
            }
        }

        // Parse/execute commands
        while (true && !waiting_for_step_trap)
        {
            // Print current state
            {
                printf("rip=[0x%08llX] ", thread_context.Rip);

                u64 dist = 0;
                String* closest_symbol = find_closest_symbol_name(thread_context.Rip, pe_infos, pdb_info, &dist);
                if (closest_symbol != nullptr) {
                    printf(", %s + [%04llX]", closest_symbol->characters, dist);
                }

                printf("\n> ");
                if (string_fill_from_line(&input_line)) {
                    loop_var = false;
                    break;
                }
            }

            // Handle commands
            Array<String> parts = string_split(input_line, ' ');
            SCOPE_EXIT(string_split_destroy(parts));
            if (parts.size == 0) continue;
            String command = parts[0];

            if (string_equals_cstring(&command, "c") || string_equals_cstring(&command, "continue")) {
                break;
            }
            else if (string_equals_cstring(&command, "s") || string_equals_cstring(&command, "step")) {
                waiting_for_step_trap = true;
                break;
            }
            else if (string_equals_cstring(&command, "q") || string_equals_cstring(&command, "quit") || string_equals_cstring(&command, "exit")) {
                loop_var = false;
                break;
            }
            else if (string_equals_cstring(&command, "registers") || string_equals_cstring(&command, "r"))
            {
                auto& c = thread_context;
                printf("    rax=0x%016llx rbx=0x%016llx rcx=0x%016llx\n", c.Rax, c.Rbx, c.Rcx);
                printf("    rdx=0x%016llx rsi=0x%016llx rdi=0x%016llx\n", c.Rdx, c.Rsi, c.Rdi);
                printf("    rip=0x%016llx rsp=0x%016llx rbp=0x%016llx\n", c.Rip, c.Rsp, c.Rbp);
                printf("     r8=0x%016llx  r9=0x%016llx r10=0x%016llx\n", c.R8, c.R9, c.R10);
                printf("    r11=0x%016llx r12=0x%016llx r13=0x%016llx\n", c.R11, c.R12, c.R13);
                printf("    r14=0x%016llx r15=0x%016llx eflags=0x%08lx\n", c.R14, c.R15, c.EFlags);
            }
            else if (string_equals_cstring(&command, "d") || string_equals_cstring(&command, "display"))
            {
                auto& bytes = byte_buffer;

                void* virtual_address = (void*)thread_context.Rip;
                u64 byte_length = 32;
                if (parts.size == 2)
                {
                    String symbol_name = parts[1];

                    // Search in pdb for function with this name...
                    PDB_Analysis::PDB_Function* function = nullptr;
                    for (int i = 0; i < pdb_info.functions.size; i++) {
                        auto& fn = pdb_info.functions[i];
                        if (string_equals(&symbol_name, &fn.normal_name)) {
                            function = &fn;
                            break;
                        }
                    }

                    if (function != nullptr)
                    {
                        printf(
                            "Found function: %s, section: %d, offset: %lld\n",
                            function->normal_name.characters, function->location.section_index, function->location.offset
                        );
                        int section_index = function->location.section_index - 1;
                        u64 offset = function->location.offset;

                        auto& sections = pe_infos[0].sections;
                        if (section_index >= 0 && section_index < sections.size)
                        {
                            auto& section = sections[section_index];
                            virtual_address = (void*)(base_executable_image_va + section.rva + offset);
                            byte_length = function->length;
                        }
                    }
                    else {
                        printf("Could not find function, continuing with normal disassembly output\n");
                    }
                }

                bool success = Process_Memory::read_as_much_as_possible(debugger.process_handle, virtual_address, &bytes, byte_length);
                if (!success) {
                    printf("Could not read memory at specified address...\n");
                    continue;
                }

                // Print bytes as x64 assembly
                int byte_index = 0;
                while (byte_index < byte_length)
                {
                    INSTRUX instruction;
                    NDSTATUS status = NdDecodeEx(&instruction, bytes.data + byte_index, bytes.size - byte_index, ND_CODE_64, ND_DATA_64);
                    if (!ND_SUCCESS(status)) {
                        break;
                    }
                    assert(instruction.Length > 0, "");

                    // Print bytes of instruction
                    printf("[0x%08llX] ", (u64)(virtual_address)+byte_index);
                    for (int i = 0; i < 6; i++)
                    {
                        if (i < instruction.Length) {
                            if (i == 5 && instruction.Length > 6) {
                                printf(".. ");
                            }
                            else {
                                printf("%02X ", bytes[byte_index + i]);
                            }
                        }
                        else {
                            printf("   ");
                        }
                    }

                    // Print instruction
                    String str = string_create_empty(256);
                    SCOPE_EXIT(string_destroy(&str));
                    NdToText(&instruction, thread_context.Rip, str.capacity - 1, str.characters);
                    str.size = (int)strlen(str.characters);
                    printf("%s\n", str.characters);

                    byte_index += instruction.Length;
                }
            }
            else if (string_equals_cstring(&command, "bp") || string_equals_cstring(&command, "ba"))  // Add breakpoint
            {
                if (parts.size != 2) {
                    printf("Add breakpoint command requires an argument\n");
                    continue;
                }

                u64 function_address = find_address_of_symbol(parts[1], pdb_info, pe_infos[0]);
                if (function_address == 0) {
                    printf("Add breakpoint failed, could not find address of symbol\n");
                    continue;
                }

                bool other_exists = false;
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

                Breakpoint breakpoint;
                breakpoint.address = function_address;
                breakpoint.id = next_breakpoint_id;
                next_breakpoint_id++;
                dynamic_array_push_back(&breakpoints, breakpoint);
                printf("Added new breakpoint %d at [0x%08llX]\n", breakpoint.id, breakpoint.address);
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
                int id = id_opt.value;

                int index = -1;
                for (int i = 0; i < breakpoints.size; i++) {
                    auto& other = breakpoints[i];
                    if (other.id == id) {
                        index = i;
                        break;
                    }
                }
                if (index == -1) {
                    printf("Delete breakpoint failed, breakpoint with given id does not exist");
                    continue;
                }

                dynamic_array_swap_remove(&breakpoints, index);
                printf("Removed breakpoint %d\n", id);
            }
            else if (string_equals_cstring(&command, "bl") || string_equals_cstring(&command, "breakpoint_list")) // List breakpoints
            {
                for (int i = 0; i < breakpoints.size; i++) {
                    auto& bp = breakpoints[i];
                    printf("    ID: %2d, Address: [0x%08llX]\n", bp.id, bp.address);
                }
            }
            else {
                printf("Invalid command: \"%s\"\nRetry: ", command.characters);
            }
        }

        // Set hardware breakpoints
        if (true)
        {
            // Initialize hardware breakpoints as not-set
            Hardware_Breakpoint hw_points[4];
            for (int i = 0; i < 4; i++) {
                Hardware_Breakpoint& bp = hw_points[i];
                bp.address = 0;
                bp.enabled = false;
                bp.length_bits = 0;
                bp.type = Hardware_Breakpoint_Type::BREAK_ON_EXECUTE;
            }

            // Update hardware_breakpoints based on breakpoint list
            for (int i = 0; i < breakpoints.size && i < 4; i++)
            {
                auto& breakpoint = breakpoints[i];
                hw_points[i].enabled = true;
                hw_points[i].address = breakpoint.address;
                hw_points[i].length_bits = 0;
                hw_points[i].type = Hardware_Breakpoint_Type::BREAK_ON_EXECUTE;
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
            for (int i = 0; i < 4; i++)
            {
                auto& bp = hw_points[i];
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
                int read_write_bits_offset   = 16 + i * 4;
                int len_bit_offset           = 18 + i * 4;

                u64 dr7 = thread_context.Dr7;
                dr7 = set_u64_bits(dr7, local_enabled_bit_offset, 1, bp.enabled ? 1 : 0);
                dr7 = set_u64_bits(dr7, read_write_bits_offset,   2, read_write_value);
                dr7 = set_u64_bits(dr7, len_bit_offset,           2, length_bits);
                thread_context.Dr7 = dr7;
            }
        }

        // Set trap flag if we want to do a step
        if (waiting_for_step_trap) {
            thread_context.EFlags = thread_context.EFlags | 0x100;
        }

        // Update thread context
        thread_context.ContextFlags = CONTEXT_ALL;
        if (!SetThreadContext(debugger.main_thread_handle, &thread_context)) {
            helper_print_last_error();
            panic("Should work!");
        }

        // Continue until next event
        ContinueDebugEvent(debug_event.dwProcessId, debug_event.dwThreadId, continue_status);

        if (debug_event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT || debug_event.dwDebugEventCode == RIP_EVENT) {
            break;
        }
    }

    if (loop_var) {
        printf("\n-----------\nProcess finished\n");
        std::cin.ignore();
    }
}
