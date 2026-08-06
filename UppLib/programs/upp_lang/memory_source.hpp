#pragma once

#include "../../utility/datatypes.hpp"
#include "../../datastructures/dynamic_array.hpp"

struct String;

struct Page_Info
{
	bool readable;
	bool writable;
	bool executable;
};

// Note:
struct Memory_Source
{
	void* process_handle;

	Memory_Source(void* process_handle) : process_handle(process_handle) {};
	Memory_Source() : process_handle(nullptr) {};

	Page_Info get_page_info(void* address, uint size);

	// Returns true if successfull
	bool read(void* destination, void* source, uint size);
	bool write(void* destination, void* source, uint size);

	void read_as_much_as_possible(void* virtual_address, Dynamic_Array<u8>* out_bytes, uint read_size);

	// Reads null-terminated string from other process, also converts wide_strings to normal strings...
	bool read_null_terminated_string(void* virtual_address, String* out_string, uint max_char_count, bool is_wide_char, Dynamic_Array<u8>* byte_buffer);

    // Templated functions
    template<typename T>
    bool read_single_value(void* virtual_address, T* out_data) {
        return read((void*)out_data, virtual_address, sizeof(T));
    }

    template<typename T>
    bool read_array(void* virtual_address, Dynamic_Array<T>* buffer, uint count) 
    {
        dynamic_array_reset(buffer);
        dynamic_array_reserve(buffer, (int)count);
        bool success = read((void*)buffer->data, virtual_address, count * sizeof(T));
        if (!success) return false;
        buffer->size = (int)count;
        return true;
    }
};

// Wide-String conversions
void wide_string_from_utf8(Dynamic_Array<wchar_t>* character_buffer, const char* string);
void wide_string_to_utf8(const wchar_t* wide_string, String* string);
