#include "file_io.hpp"

#include <cstdio>
#include <Windows.h>

#include "../utility/utils.hpp"

Optional<u64> file_io_get_file_size(const char* filepath)
{
    Optional<u64> result;

    FILE* file;
    if (fopen_s(&file, filepath, "rb") != 0) {
        result.available = false;
        return result;
    }
    SCOPE_EXIT(fclose(file));

    // Get File size
    fseek(file, 0, SEEK_END); 
    result.value   =  ftell(file);
    result.available = true;
    
    return result;
}

Optional<Array<byte>> file_io_load_binary_file(const char* filepath);
Optional<String> file_io_load_text_file(const char* filepath);

Optional<Array<byte>> file_io_load_binary_file(const char* filepath)
{
    Optional<Array<byte>> result;
    result.available = false;

    FILE* file;
    if (fopen_s(&file, filepath, "rb") != 0) {
        return result;
    }
    SCOPE_EXIT(fclose(file));

    // Get File size
    fseek(file, 0, SEEK_END); 
    u64 fileSize =  ftell(file);
    fseek(file, 0, SEEK_SET); // Put cursor back to start of file
    
    // Read 
    result.value = array_create_empty<byte>((int)fileSize);

    u64 readSize = (u64) fread(result.value.data, 1, fileSize, file); 
    if (readSize != fileSize) {
        array_destroy(&result.value);
        return result;
    }

    result.available = true;
    return result;
}

void file_io_unload_binary_file(Optional<Array<byte>>* memory) {
    if (memory->available) {
        array_destroy(&memory->value);
    }
}

Optional<String> file_io_load_text_file(const char* filepath)
{
    Optional<String> result;
    result.available = false;

    Optional<Array<byte>> binary_file_content = file_io_load_binary_file(filepath);
    if (binary_file_content.available == false) {
        return result;
    }
    SCOPE_EXIT(file_io_unload_binary_file(&binary_file_content));

    // Copy binary array content to string
    String* string = &result.value;
    string->characters = new char[binary_file_content.value.size+1];
    memcpy(string->characters, binary_file_content.value.data, binary_file_content.value.size);
    string->characters[binary_file_content.value.size] = 0; // Add 0 terminator
    string->size = (int)strlen(string->characters);
    assert(string->size <= binary_file_content.value.size, "Null terminator did not work!\n");
    string->capacity = binary_file_content.value.size+1;

    result.available = true;
    return result;
}

void file_io_unload_text_file(Optional<String>* file_content)
{
    if (file_content->available) {
        string_destroy(&file_content->value);
    }
}

void file_io_relative_to_full_path(String* relative_path)
{
    char buffer[1024];
    int length = GetFullPathNameA(relative_path->characters, 1024, buffer, 0);
    if (length == 0 || length > 1024) {
        return;
    }
    string_reset(relative_path);
    string_append(relative_path, buffer);
    string_replace_character(relative_path, '\\', '/');
}

bool file_io_check_if_file_exists(const char* filepath)
{
    FILE* file;
    if (fopen_s(&file, filepath, "r") != 0) {
        return false;
    }
    if (file != 0) {
        fclose(file);
    }
    return true;
}

bool file_io_is_directory(const char* filepath) 
{
    DWORD attributes = GetFileAttributesA(filepath);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

u64 file_io_get_current_file_time()
{
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    return (((u64)time.dwHighDateTime) << 32) | (time.dwLowDateTime);
}

Optional<u64> file_io_get_last_write_access_time(const char* filepath) 
{
    Optional<u64> result;
    result.available = false;

    // Get File Handle
    HANDLE file_handle = CreateFileA(filepath, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file_handle == INVALID_HANDLE_VALUE) {
        return result;
    }
    SCOPE_EXIT(CloseHandle(file_handle));


    FILETIME time;
    if (GetFileTime(file_handle, 0, 0, &time) == 0) { // Error if 0
        return result;
    }

    result.available = true;
    result.value = (((u64)time.dwHighDateTime) << 32) | (time.dwLowDateTime);

    return result;
}

bool file_io_write_file(const char* filepath, Array<byte> data)
{
    FILE* file;
    if (fopen_s(&file, filepath, "wb") != 0) {
        return false;
    }
    SCOPE_EXIT(fclose(file));

    fwrite(data.data, 1, data.size, file);
    return true;
}

static char buffer[256];
Optional<String> file_io_open_file_selection_dialog()
{
    // open a file base_name
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = buffer;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(buffer);
    ofn.lpstrFilter = "All\0*.*\0Text\0*.TXT\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    int ret_val = GetOpenFileName(&ofn);
    if (ret_val == 0) return optional_make_failure<String>();
    return optional_make_success(string_create_static(ofn.lpstrFile));
}
