#include "file_io.hpp"

#include <cstdio>
#include <Windows.h>

#include "../utility/utils.hpp"
#include "../datastructures/allocators.hpp"

Optional<Array<byte>> file_io_load_binary_file(String path, Arena* arena)
{
    SCRATCH_ARENA_MAKE_SCOPED(arena);
    String null_terminated_string = string_copy(path, scratch_arena);
    string_add_null_terminator(&null_terminated_string);

    FILE* file;
    if (fopen_s(&file, null_terminated_string.characters, "rb") != 0) {
        return optional_make_failure<Array<byte>>();
    }
    SCOPE_EXIT(fclose(file));

    // Get File size
    fseek(file, 0, SEEK_END); 
    u64 file_size =  ftell(file);
    fseek(file, 0, SEEK_SET); // Put cursor back to start of file
    if (file_size == 0) {
        return optional_make_success(array_create_static<byte>(nullptr, 0));
    }
    
    // Allocate memory for result 
    Arena_Checkpoint checkpoint = arena->make_checkpoint();
    Array<byte> result = arena->allocate_array<byte>(file_size);

    // Read from file handle
    u64 read_size = (u64) fread(result.data, 1, file_size, file); 
    if (read_size != file_size) // If not all could be read, return error
    {
        checkpoint.rewind();
        return optional_make_failure<Array<byte>>();
    }

    return optional_make_success(result);
}

Optional<String> file_io_load_text_file(String path, Arena* arena)
{
    Optional<Array<byte>> file_content_opt = file_io_load_binary_file(path, arena);
    if (!file_content_opt.available) {
        return optional_make_failure<String>();
    }

    String string;
    string.characters = (char*) file_content_opt.value.data;
    string.capacity   = file_content_opt.value.size;
    string.size       = string.capacity;
    string.arena      = arena;
    return optional_make_success(string);
}

bool file_io_write_binary_file(String path, Array<byte> data)
{
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);
    String null_terminated_path = string_copy(path, scratch_arena);
    string_add_null_terminator(&null_terminated_path);

    FILE* file;
    if (fopen_s(&file, null_terminated_path.characters, "wb") != 0) {
        return false;
    }
    SCOPE_EXIT(fclose(file));

    fwrite(data.data, 1, data.size, file);
    return true;
}

bool file_io_write_text_file(String path, String text) 
{
    return file_io_write_binary_file(path, array_create_static<byte>((byte*)text.characters, text.size));
}

u64 helper_dwords_to_u64(DWORD high, DWORD low) 
{
    return (((u64)high) << 32) | ((u64)low);
}

File_Info file_io_get_file_info(String path)
{
    File_Info file_info;
    file_info.status = File_Info_Status::COULD_NOT_QUERY;
    file_info.file_size = 0;
    file_info.is_directory = false;
    file_info.last_write_access_time = 0;

    SCRATCH_ARENA_MAKE_SCOPED(nullptr);
    String null_terminated_path = string_copy(path, scratch_arena);
    string_add_null_terminator(&null_terminated_path);

    // Try opening file
    HANDLE file_handle = CreateFileA(
        null_terminated_path.characters, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (file_handle == INVALID_HANDLE_VALUE) 
    {
        DWORD last_error = GetLastError();
        if (last_error == ERROR_FILE_NOT_FOUND) {
            file_info.status = File_Info_Status::FILE_DOES_NOT_EXIST;
        }
        return file_info;
    }
    SCOPE_EXIT(CloseHandle(file_handle));

    BY_HANDLE_FILE_INFORMATION handle_info;
    bool success = GetFileInformationByHandle(file_handle, &handle_info) != 0;
    if (!success) {
        return file_info;
    }

    file_info.status = File_Info_Status::SUCCESS;
    file_info.file_size = helper_dwords_to_u64(handle_info.nFileSizeHigh, handle_info.nFileSizeLow);
    file_info.is_directory = (handle_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    file_info.last_write_access_time = helper_dwords_to_u64(handle_info.ftLastWriteTime.dwHighDateTime, handle_info.ftLastWriteTime.dwLowDateTime);
    return file_info;
}

static char buffer[256];
bool file_io_open_file_selection_dialog(String* write_to)
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
    if (ret_val == 0) return false;
    string_reset(write_to);
    string_append(write_to, buffer);
    return true;
}

List<Directory_Item> file_io_get_directory_content(String directory_path, Arena* arena)
{
    assert(!string_ends_with(directory_path, "/"), "");

    SCRATCH_ARENA_MAKE_SCOPED(arena);

    String search_string = string_copy(directory_path, scratch_arena);
    search_string.append("/*");
    string_add_null_terminator(&search_string);

    WIN32_FIND_DATA found_file_description;
    HANDLE search_handle = FindFirstFileA(search_string.characters, &found_file_description);
    if (search_handle == INVALID_HANDLE_VALUE) {
        return List<Directory_Item>::create(arena->upcast());
    }

    // Loop over all found files
    List<Directory_Item> items = List<Directory_Item>::create(arena->upcast());
    do {
        Directory_Item item;
        item.is_directory = found_file_description.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        item.size = helper_dwords_to_u64(found_file_description.nFileSizeHigh, found_file_description.nFileSizeLow);
        item.filename = string_create(found_file_description.cFileName, arena);
        items.append(item);
    } while (FindNextFile(search_handle, &found_file_description) != 0);

    // Check if errors appeared and close the search handle
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        // logg("Errors appeared during directory crawling");
        // helper_print_last_error();
    }
    FindClose(search_handle);

    return items;
}



Filepath_Parts filepath_get_parts(String path)
{
    Filepath_Parts parts;
    parts.directory = string_create_static("");
    parts.filename = string_create_static("");

    if (path.size == 0) return parts;

    Optional<int> backslash_pos_opt = string_find_character_index_reverse(&path, '\\', path.size - 1);
    Optional<int> slash_pos_opt = string_find_character_index_reverse(&path, '/', path.size - 1);

    if (!slash_pos_opt.available && !backslash_pos_opt.available) {
        parts.filename = string_create_substring_static(&path, 0, path.size);
        return parts;
    }

    int backslash_pos = backslash_pos_opt.available ? backslash_pos_opt.value : 0;
    int slash_pos = slash_pos_opt.available ? slash_pos_opt.value : 0;
    int last_seperator = math_maximum(backslash_pos, slash_pos);

    parts.filename  = string_create_substring_static(&path, last_seperator + 1, path.size);
    parts.directory = string_create_substring_static(&path, 0, last_seperator);
    return parts;
}

Filename_Parts filename_get_parts(String filename)
{
    Filename_Parts parts;
    Optional<int> dot_pos_opt = string_find_character_index_reverse(&filename, '.', filename.size - 1);
    int dot_pos = dot_pos_opt.available ? dot_pos_opt.value : filename.size;
    parts.name      = string_create_substring_static(&filename, 0, dot_pos);
    parts.extension = string_create_substring_static(&filename, dot_pos + 1, filename.size);
    return parts;
}

void filepath_relative_to_absolute_path(String* filepath)
{
    string_add_null_terminator(filepath);
    char buffer[1024];
    int length = GetFullPathNameA(filepath->characters, 1024, buffer, 0);
    if (length == 0 || length >= 1024) {
        return;
    }
    string_reset(filepath);
    string_append(filepath, buffer);
    string_replace_character(filepath, '\\', '/');
}
