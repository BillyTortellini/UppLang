#pragma once

#include "../../utility/datatypes.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/allocators.hpp"

struct DynString;

struct Page_Info
{
	bool readable;
	bool writable;
	bool executable;
};

// Note:
struct Memory_Source
{
	void* process_handle; // If null current process

	Memory_Source(void* process_handle) : process_handle(process_handle) {};
	Memory_Source() : process_handle(nullptr) {};

	Page_Info get_page_info(void* address, uint size);

	// Returns true if successfull
	bool read(void* destination, void* source, uint size);
	bool write(void* destination, void* source, uint size);

    uint Memory_Source::query_max_readable_size(void* address); // 0 if no bytes can be read or error occured

    // These functions fail if the null-terminator cannot be found in the first max_char_count bytes of the string.
    bool Memory_Source::read_string_utf8_null_terminated(void* virtual_address, DynString* append_to, uint max_byte_size);
    bool Memory_Source::read_string_utf16_null_terminated(void* virtual_address, DynString* append_to, uint max_byte_size);
    bool Memory_Source::read_string_null_terminated_generic(void* virtual_address, DynString* append_to, uint max_byte_size, bool is_utf8);

    // Templated functions
    template<typename T>
    bool read_single_value(void* virtual_address, T* out_data) {
        return read((void*)out_data, virtual_address, sizeof(T));
    }

    template<typename T>
    bool read_array(void* virtual_address, DynArray<T>* append_to, uint count) 
    {
        append_to->reserve_space(count);
        bool success = read((void*)&append_to->data[append_to->size], virtual_address, count * sizeof(T));
        if (!success) return false;
        append_to->size += (uint)count;
        return true;
    }
};

// Wide-String conversions
wchar_t* wide_string_from_utf8(String string, Arena* arena);
void wide_string_to_utf8(const wchar_t* wide_string, DynString* string);
