#include "file_io.hpp"

#include <cstdio>
#include <Windows.h>

#include "../utility/utils.hpp"
#include "../datastructures/allocators.hpp"

Optional<Array<byte>> file_io_load_binary_file(String path, Arena* arena)
{
    SCRATCH_ARENA_MAKE_SCOPED(arena);
    DynString tmp_string = DynString::create(scratch_arena);

    FILE* file;
    if (fopen_s(&file, tmp_string.reset()->append(path)->to_cstring(), "rb") != 0) {
        return optional_make_failure<Array<byte>>();
    }
    SCOPE_EXIT(fclose(file));

    // Get File size
    fseek(file, 0, SEEK_END); 
    u64 file_size =  ftell(file);
    fseek(file, 0, SEEK_SET); // Put cursor back to start of file
    if (file_size == 0) {
        return optional_make_success(array_make<byte>(nullptr, 0));
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

bool file_io_load_text_file(String path, DynString* output)
{
    SCRATCH_ARENA_MAKE_SCOPED(output->arena);

    Optional<Array<byte>> file_content_opt = file_io_load_binary_file(path, scratch_arena);
    if (!file_content_opt.available) {
        return false;
    }

    output->append(string_make((char*)file_content_opt.value.data, file_content_opt.value.size));
    return true;
}

bool file_io_write_binary_file(String path, Array<byte> data)
{
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);
    DynString tmp_string = DynString::create(scratch_arena);

    FILE* file;
    if (fopen_s(&file, tmp_string.reset()->append(path)->to_cstring(), "wb") != 0) {
        return false;
    }
    SCOPE_EXIT(fclose(file));

    fwrite(data.data, 1, data.size, file);
    return true;
}

bool file_io_write_text_file(String path, String text) 
{
    return file_io_write_binary_file(path, array_make<byte>((byte*)text.data, text.size));
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
    DynString tmp_string = DynString::create(scratch_arena);

    // Try opening file
    HANDLE file_handle = CreateFileA(
        tmp_string.reset()->append(path)->to_cstring(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
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
bool file_io_open_file_selection_dialog(DynString* write_to)
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
    dynstring_reset(write_to);
    write_to->append(buffer);
    return true;
}

List<Directory_Item> file_io_get_directory_content(String directory_path, Arena* arena)
{
    assert(!string_ends_with(directory_path, string_make("/")), "");

    SCRATCH_ARENA_MAKE_SCOPED(arena);
    DynString tmp_string = dynstring_create(scratch_arena);

    WIN32_FIND_DATA found_file_description;
    HANDLE search_handle = FindFirstFileA(tmp_string.reset()->append("/*")->to_cstring(), &found_file_description);
    if (search_handle == INVALID_HANDLE_VALUE) {
        return List<Directory_Item>::create(arena->upcast());
    }

    // Loop over all found files
    List<Directory_Item> items = List<Directory_Item>::create(arena->upcast());
    do {
        Directory_Item item;
        item.is_directory = found_file_description.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        item.size = helper_dwords_to_u64(found_file_description.nFileSizeHigh, found_file_description.nFileSizeLow);
        item.filename = string_allocate_copy(string_make(found_file_description.cFileName), arena->upcast());
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
    parts.directory = string_make("");
    parts.filename = string_make("");

    if (path.size == 0) return parts;

    Optional<int> backslash_pos_opt = string_find_character_index_reverse(path, '\\', path.size - 1);
    Optional<int> slash_pos_opt = string_find_character_index_reverse(path, '/', path.size - 1);

    if (!slash_pos_opt.available && !backslash_pos_opt.available) {
        parts.filename = string_make_substring(path, 0, path.size);
        return parts;
    }

    int backslash_pos = backslash_pos_opt.available ? backslash_pos_opt.value : 0;
    int slash_pos = slash_pos_opt.available ? slash_pos_opt.value : 0;
    int last_seperator = math_maximum(backslash_pos, slash_pos);

    parts.filename  = string_make_substring(path, last_seperator + 1, path.size);
    parts.directory = string_make_substring(path, 0, last_seperator);
    return parts;
}

Filename_Parts filename_get_parts(String filename)
{
    Filename_Parts parts;
    Optional<int> dot_pos_opt = string_find_character_index_reverse(filename, '.', filename.size - 1);
    int dot_pos = dot_pos_opt.available ? dot_pos_opt.value : filename.size;
    parts.name      = string_make_substring(filename, 0, dot_pos);
    parts.extension = string_make_substring(filename, dot_pos + 1, filename.size);
    return parts;
}

bool filepath_is_absolute_path(String filepath)
{
    return string_test_char(filepath, 1, ':') && string_test_char(filepath, 2, '/');
}

void filepath_relative_to_absolute_path(DynString* filepath)
{
    if (filepath_is_absolute_path(filepath->to_string())) {
        return;
    }
    dynstring_add_null_terminator(filepath);
    char buffer[1024];
    int length = GetFullPathNameA(filepath->data, 1024, buffer, 0);
    if (length == 0 || length >= 1024) {
        return;
    }
    filepath->reset()->append(string_make(buffer, length));
    dynstring_replace_character(filepath, '\\', '/');
}

void filepath_relative_to_full_path_with_start(DynString* filepath, String start_directory)
{
    // An absolute path starts with [LETTER]:/
    if (filepath_is_absolute_path(filepath->to_string())) {
        return;
    }

    // Append start_directory at the start of path
    dynstring_insert(filepath, start_directory, 0);
    // Check if start_directory ends with /, otherwise insert slash
    if (!string_test_char(start_directory, start_directory.size - 1, '/')) {
        dynstring_insert(filepath, string_make("/"), start_directory.size);
    }

    filepath_relative_to_absolute_path(filepath);
}
