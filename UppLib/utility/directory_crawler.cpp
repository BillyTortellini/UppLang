#include "directory_crawler.hpp"

#include <Windows.h>

#include "../datastructures/string.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../utility/utils.hpp"
#include "../win32/windows_helper_functions.hpp"
#include "../utility/file_io.hpp"

struct Directory_Crawler {
    String path;
    Dynamic_Array<File_Info> file_infos;
    bool path_changed;
};

Directory_Crawler* directory_crawler_create()
{
    Directory_Crawler* result = new Directory_Crawler();
    result->path = string_create(".");
    result->path_changed = true;
    result->file_infos = dynamic_array_create<File_Info>();
    directory_crawler_set_to_working_directory(result);
    return result;
}

void directory_crawler_destroy(Directory_Crawler* crawler) {
    string_destroy(&crawler->path);
    for (int i = 0; i < crawler->file_infos.size; i++) {
        string_destroy(&crawler->file_infos[i].name);
    }
    dynamic_array_destroy(&crawler->file_infos);
    delete crawler;
}

void directory_crawler_set_path(Directory_Crawler* crawler, String path)
{
    string_reset(&crawler->path);
    string_append_string(&crawler->path, &path);
    file_io_relative_to_full_path(&crawler->path);
    crawler->path_changed = true;
}

void directory_crawler_set_path_to_file_dir(Directory_Crawler* crawler, String file_path) {
    auto& path = crawler->path;
    string_reset(&path);
    string_append_string(&path, &file_path);
    int trim_pos = 0;
    auto last_slash = string_find_character_index_reverse(&path, '/', path.size - 1);
    if (!last_slash.available) {
        last_slash = string_find_character_index_reverse(&path, '\\', path.size - 1);
    }
    if (last_slash.available) {
        trim_pos = last_slash.value;
    }
    string_truncate(&path, trim_pos);
    file_io_relative_to_full_path(&path);
    crawler->path_changed = true;
}

void directory_crawler_set_to_working_directory(Directory_Crawler* crawler) {
    int path_length = GetCurrentDirectory(0, 0);
    String path = string_create_empty(path_length);
    SCOPE_EXIT(string_destroy(&path));
    int written_string_length = GetCurrentDirectory(path.capacity, path.characters);
    string_replace_character(&path, '\\', '/');
    directory_crawler_set_path(crawler, path);
}

String directory_crawler_get_path(Directory_Crawler* crawler) {
    return crawler->path;
}

bool directory_crawler_go_up_one_directory(Directory_Crawler* crawler) {
    String* path = &crawler->path;
    Optional<int> last_pos = string_find_character_index_reverse(path, '/', path->size-1);
    if (!last_pos.available) {
        return false;
    }
    string_truncate(path, last_pos.value);
    return true;
}

bool directory_crawler_go_down_one_directory(Directory_Crawler* crawler, int dir_index) {
    auto files = directory_crawler_get_content(crawler);
    if (dir_index < 0 || dir_index >= files.size) return false;
    auto file = files[dir_index];
    if (!file.is_directory) return false;
    string_append_formated(&crawler->path, "/%s", file.name.characters);
    crawler->path_changed = true;
    return true;
}

Array<File_Info> directory_crawler_get_content(Directory_Crawler* crawler)
{
    if (!crawler->path_changed) {
        return dynamic_array_as_array(&crawler->file_infos);
    }
    crawler->path_changed = false;

    for (int i = 0; i < crawler->file_infos.size; i++) {
        string_destroy(&crawler->file_infos[i].name);
    }
    dynamic_array_reset(&crawler->file_infos);

    HANDLE search_handle;
    WIN32_FIND_DATA found_file_description;
    {
        String search_path = string_create_from_string_with_extra_capacity(&crawler->path, 3);
        SCOPE_EXIT(string_destroy(&search_path));
        string_append(&search_path, "/*");
        search_handle = FindFirstFile(search_path.characters, &found_file_description);
    }
    if (search_handle == INVALID_HANDLE_VALUE) {
        return dynamic_array_as_array(&crawler->file_infos);
    }

    // Loop over all found files
    do {
        File_Info info;
        info.is_directory = found_file_description.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        info.size = found_file_description.nFileSizeHigh * (((i64)MAXDWORD)+1) + found_file_description.nFileSizeLow;
        info.name = string_create(found_file_description.cFileName);
        dynamic_array_push_back(&crawler->file_infos, info);
    } while (FindNextFile(search_handle, &found_file_description) != 0);

    // Check if errors appeared and close the search handle
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        logg("Errors appeared during directory crawling");
        helper_print_last_error();
    }
    FindClose(search_handle);

    return dynamic_array_as_array(&crawler->file_infos);
}

void directory_crawler_print_all_files(Directory_Crawler* crawler) 
{
    auto files = directory_crawler_get_content(crawler);
    logg("Directory: %s\n", crawler->path.characters);
    for (int i = 0; i < files.size; i++) {
        auto file = files[i];
        logg("    %s %s\n", files[i].name.characters, file.is_directory ? "d" : "");
    }
}
    