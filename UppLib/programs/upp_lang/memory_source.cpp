#include "memory_source.hpp"

#include <Windows.h>
#include <../../datastructures/string.hpp>

Page_Info Memory_Source::get_page_info(void* address, usize size) 
{
	Page_Info info;
	info.readable = false;
	info.writable = false;
	info.executable = false;

	MEMORY_BASIC_INFORMATION memory_info = { 0 };
	if (process_handle == nullptr) {
		DWORD success = VirtualQuery(address, &memory_info, sizeof(memory_info));
		if (success == 0) return info;
	}
	else {
		HANDLE handle = process_handle;
		DWORD success = VirtualQueryEx(handle, address, &memory_info, sizeof(memory_info));
		if (success == 0) return info;
	}

    if (memory_info.State != MEM_COMMIT) {
        return info;
    }
	if ((usize)address + size > (usize)memory_info.BaseAddress + memory_info.RegionSize) {
		// The whole range has to be from the same allocation for this function to work
		return info;
	}
	if (memory_info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) {
		return info;
	}

	info.executable = (memory_info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
	info.readable = (memory_info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)) != 0;
	info.writable = (memory_info.Protect & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_READWRITE | PAGE_WRITECOPY)) != 0;
	return info;
}

bool Memory_Source::read(void* destination, void* source, usize size) 
{
	if (process_handle == nullptr) {
		// Note: we don't really check if the write succeeds here, but i guess we just crash if it's an invalid address...
		memory_copy(destination, source, size);
		return true;
	}

	// Note: From documentation this function either reads the whole size, or reads nothing,
	//		 so we don't have to deal with partial reads (Which may set destination to weird values)
	HANDLE handle = process_handle;
	u64 bytes_written = 0;
    if (ReadProcessMemory(process_handle, source, destination, size, &bytes_written) == 0) {
        return false;
	}
	assert(bytes_written == size, "From documentation this should be true");
    return true;
}

bool Memory_Source::write(void* destination, void* source, usize size) 
{
	if (process_handle == nullptr) {
		memory_copy(destination, source, size);
		return true;
	}

	HANDLE handle = process_handle;
	u64 bytes_written = 0;
    bool success = WriteProcessMemory(handle, destination, source, size, &bytes_written) != 0;
	if (!success) {
		return false;
	}
	assert(bytes_written == size, "From documentation");
	return true;
}

void Memory_Source::read_as_much_as_possible(void* address, Dynamic_Array<u8>* out_bytes, usize read_size) 
{
	dynamic_array_reset(out_bytes);
    if (address == nullptr || read_size == 0) {
        return;
    }

	// Query memory to check how much we are allowed to read
	MEMORY_BASIC_INFORMATION memory_info = { 0 };
	if (process_handle == nullptr) {
		DWORD success = VirtualQuery(address, &memory_info, sizeof(memory_info));
		if (success == 0) return;
	}
	else {
		HANDLE handle = process_handle;
		DWORD success = VirtualQueryEx(handle, address, &memory_info, sizeof(memory_info));
		if (success == 0) return;
	}

	// Check that we are allowed to read
	{
		if (memory_info.State != MEM_COMMIT) {
		    return;
		}
		if (memory_info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) {
			return;
		}
		bool readable = (memory_info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)) != 0;
		if (!readable) {
			return;
		}
	}

	// Figure out max-read size
	assert(address >= memory_info.BaseAddress && (usize)address < (usize)memory_info.BaseAddress + memory_info.RegionSize, "Should be true with VirtualQuery");
	usize max_read_size = (usize)memory_info.BaseAddress + memory_info.RegionSize - (usize)address;
	read_size = math_minimum(max_read_size, read_size);

	// Fill dynamic_array
	dynamic_array_reserve(out_bytes, (int)read_size);
	bool success = read(out_bytes->data, address, read_size);
	assert(success, "Should work after checking memory");
	out_bytes->size = (int)read_size;
}

bool Memory_Source::read_null_terminated_string(
	void* virtual_address, String* out_string, usize max_char_count, bool is_wide_char, Dynamic_Array<u8>* byte_buffer)
{
	string_reset(out_string);
	if (virtual_address == nullptr || max_char_count == 0) {
		return false;
	}
	usize max_size = max_char_count + 1;
	if (is_wide_char) {
		max_size = 2 * max_char_count + 2; // + 2 just to be sure, if wide-chars use two 0 bytes
	}

	read_as_much_as_possible(virtual_address, byte_buffer, max_size);
	if (byte_buffer->size == 0) {
		return false;
	}

	if (is_wide_char)
	{
		const wchar_t* char_ptr = (wchar_t*)byte_buffer->data;
		int wchar_count = -1;
		for (int i = 0; i < max_char_count; i++) {
			if (char_ptr[i] == 0) {
				wchar_count = i;
				break;
			}
		}

		// If string wasn't null-terminated, return false...
		if (wchar_count == -1) {
			return false;
		}

		// Otherwise convert to utf8
		wide_string_to_utf8((wchar_t*)byte_buffer->data, out_string);
	}
	else 
	{
		// Find null-terminator
		const char* char_ptr = (char*)byte_buffer->data;
		int max_length = (int)max_size / 2;
		int char_count = -1;
		for (int i = 0; i < max_char_count; i++) {
			if (char_ptr[i] == 0) {
				char_count = i;
				break;
			}
		}

		// If string wasn't null-terminated, return false...
		if (char_count == -1) {
			return false;
		}

		// Otherwise copy to string
		char_count += 1; // Include null-terminator in calculation
		string_reserve(out_string, char_count);
		memory_copy(out_string->characters, byte_buffer->data, char_count);
		out_string->size = char_count - 1;
	}

	return true;
}

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

