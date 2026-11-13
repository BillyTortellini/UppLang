#include "memory_source.hpp"

#include <Windows.h>
#include <strsafe.h>
#include "../../datastructures/string.hpp"

Page_Info Memory_Source::get_page_info(void* address, uint size) 
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
	if ((uint)address + size > (uint)memory_info.BaseAddress + memory_info.RegionSize) {
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

bool Memory_Source::read(void* destination, void* source, uint size) 
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

bool Memory_Source::write(void* destination, void* source, uint size) 
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

uint Memory_Source::query_max_readable_size(void* address) 
{
    if (address == nullptr) {
		return 0;
    }

	// Query memory to check how much we are allowed to read
	MEMORY_BASIC_INFORMATION memory_info = { 0 };
	if (process_handle == nullptr) {
		DWORD success = VirtualQuery(address, &memory_info, sizeof(memory_info));
		if (success == 0) return 0;
	}
	else {
		HANDLE handle = process_handle;
		DWORD success = VirtualQueryEx(handle, address, &memory_info, sizeof(memory_info));
		if (success == 0) return 0;
	}
	assert(address >= memory_info.BaseAddress && (uint)address < (uint)memory_info.BaseAddress + memory_info.RegionSize, "Should be true with VirtualQuery");

	// Check that we are allowed to read
	{
		if (memory_info.State != MEM_COMMIT) {
		    return 0;
		}
		if (memory_info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) {
			return 0;
		}
		bool readable = (memory_info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)) != 0;
		if (!readable) {
			return 0;
		}
	}

	// Figure out max-read size
	return (uint)memory_info.BaseAddress + memory_info.RegionSize - (uint)address;
}

bool Memory_Source::read_string_utf8_null_terminated(void* virtual_address, DynString* append_to, uint max_byte_size)
{
	if (virtual_address == nullptr || max_byte_size == 0) {
		return false;
	}

	// Check how much memory is readable at that location
	uint read_size = math_minimum(query_max_readable_size(virtual_address), max_byte_size);
	if (read_size == 0) {
		return false;
	}

	// Try to read memory
	SCRATCH_ARENA_MAKE_SCOPED(append_to->arena);
	Array<byte> char_buffer = scratch_arena->allocate_array<byte>(read_size);
	bool success = read(&char_buffer.data, virtual_address, read_size);
	if (!success) return false;

	// Try to find 0 terminator
	int str_length = -1;
	for (int i = 0; i < char_buffer.size; i++) {
		if (char_buffer[i] == '\0') {
			str_length = i;
			break;
		}
	}
	if (str_length == -1) {
		return false;
	}
	append_to->append(string_make((char*) char_buffer.data, str_length));
	return true;
}

bool Memory_Source::read_string_utf16_null_terminated(void* virtual_address, DynString* append_to, uint max_byte_size)
{
	if (virtual_address == nullptr || max_byte_size == 0) {
		return false;
	}

	// Check how much memory is readable at that location
	uint read_size = math_minimum(query_max_readable_size(virtual_address), max_byte_size);
	if (read_size == 0) {
		return false;
	}

	// Try to read memory
	SCRATCH_ARENA_MAKE_SCOPED(append_to->arena);
	Array<byte> char_buffer = scratch_arena->allocate_array<byte>(read_size);
	bool read_success = read(&char_buffer.data, virtual_address, read_size);
	if (!read_success) return false;


	// Check if string has null-terminator
	size_t out_size = 0;
	HRESULT success = StringCchLengthW((STRSAFE_PCNZWCH)char_buffer.data, char_buffer.size, &out_size);
	if (FAILED(success)) {
		return false;
	}

	// Otherwise convert to utf8 (I assume this conversion works)
	wide_string_to_utf8((wchar_t*) char_buffer.data, append_to);
	return true;
}

bool Memory_Source::read_string_null_terminated_generic(void* virtual_address, DynString* append_to, uint max_byte_size, bool is_utf8) {
	if (is_utf8) {
		return this->read_string_utf8_null_terminated(virtual_address, append_to, max_byte_size);
	}
	else {
		return this->read_string_utf16_null_terminated(virtual_address, append_to, max_byte_size);
	}
}

wchar_t* wide_string_from_utf8(String string, Arena* arena)
{
	// Note: I think (string.size + 1) * sizeof(wchar_t) should be enough (+1 for null-terminator),
	//   but the MultiByteToWideChar size parameter is in data, and I don't know if microsoft
	//   means 2 bytes for a wchar or possible 4 bytes (UTF16 extended chars)
	Array<byte> buffer = arena->allocate_array<byte>((string.size + 1) * sizeof(wchar_t) * 2);

    // Early exit on length 0 as MultiByteToWodeChar uses 0 as error-code
    if (string.size == 0) {
		*((wchar_t*) buffer.data[0]) = (wchar_t) 0;
        return (wchar_t*) buffer.data;
    }

    int written_chars = MultiByteToWideChar(CP_UTF8, NULL, string.data, string.size, (LPWSTR) buffer.data, buffer.size / 2);
    if (written_chars == 0) {
		*((wchar_t*) &buffer.data[0]) = (wchar_t)0;
    }
    return (wchar_t*) buffer.data;
}

void wide_string_to_utf8(const wchar_t* wide_string, DynString* string)
{
    int character_count = lstrlenW(wide_string) + 1;
    dynstring_reserve_capacity(string, string->size + character_count * 4); // Max 4 bytes per char in UTF-8
    int written_bytes = WideCharToMultiByte(CP_UTF8, 0, wide_string, character_count, &string->data[string->size], string->capacity - string->size, 0, 0);
	string->size += written_bytes;
	
	// I think WideCharToMultiByte writes null-terminator, so we remove that if bytes were written
	if (written_bytes != 0 && string->data[string->size - 1] == '\0') {
		string->size -= 1;
	}
}

