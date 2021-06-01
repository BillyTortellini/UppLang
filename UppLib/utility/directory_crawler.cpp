#include "directory_crawler.hpp"

#include <Windows.h>

#include "../datastructures/string.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../utility/utils.hpp"
#include "../win32/windows_helper_functions.hpp"

struct DirectoryCrawler {
    String current_path;
};

DirectoryCrawler* directory_crawler_create() {
    DirectoryCrawler* result = new DirectoryCrawler();
    result->current_path = string_create_empty(GetCurrentDirectory(0, 0));
    int written_string_length = GetCurrentDirectory(result->current_path.capacity, result->current_path.characters);
    result->current_path.size = written_string_length;
    string_replace_character(&result->current_path, '\\', '/');
    return result;
}

void directory_crawler_destroy(DirectoryCrawler* crawler) {
    string_destroy(&crawler->current_path);
    delete crawler;
}

const char* directory_crawler_get_path(DirectoryCrawler* crawler) {
    return crawler->current_path.characters;
}

bool directory_crawler_go_up_one_directory(DirectoryCrawler* crawler) {
    String* path = &crawler->current_path;
    Optional<int> last_pos = string_find_character_index_reverse(path, '/', path->size-1);
    if (!last_pos.available) {
        return false;
    }
    string_truncate(path, last_pos.value);
    return true;
}

void directory_crawler_print_all_files(DirectoryCrawler* crawler) 
{
    String search_path = string_create_from_string_with_extra_capacity(&crawler->current_path, 3);
    SCOPE_EXIT(string_destroy(&search_path));
    string_append(&search_path, "/*");
    
    HANDLE search_handle;
    WIN32_FIND_DATA found_file_description;
    search_handle = FindFirstFile(search_path.characters, &found_file_description);
    if (search_handle == INVALID_HANDLE_VALUE) {
        logg("Find first file failed for path \"%s\"\n", crawler->current_path.characters);
        return;
    }

    do {
        logg("\t%s\n", found_file_description.cFileName);
    } while (FindNextFile(search_handle, &found_file_description) != 0);

    // Check if errors appeared and close the search handle
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        logg("Errors appeared during directory crawling");
        helper_print_last_error();
    }
    FindClose(search_handle);

    logg("Printed all files\n");
}
    
Array<FileInfo> directory_crawler_create_file_infos(DirectoryCrawler* crawler) 
{
    Dynamic_Array<FileInfo> file_infos = dynamic_array_create_empty<FileInfo>(8);

    HANDLE search_handle;
    WIN32_FIND_DATA found_file_description;
    {
        String search_path = string_create_from_string_with_extra_capacity(&crawler->current_path, 3);
        SCOPE_EXIT(string_destroy(&search_path));
        string_append(&search_path, "/*");
        search_handle = FindFirstFile(search_path.characters, &found_file_description);
    }
    if (search_handle == INVALID_HANDLE_VALUE) {
        return array_create_empty<FileInfo>(0);
    }

    // Loop over all found files
    do {
        FileInfo info;
        {
            info.is_directory = found_file_description.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
            info.size = found_file_description.nFileSizeHigh * (((i64)MAXDWORD)+1) + found_file_description.nFileSizeLow;
            info.name_handle = string_create(found_file_description.cFileName);
        }
        dynamic_array_push_back(&file_infos, info);
    } while (FindNextFile(search_handle, &found_file_description) != 0);

    // Check if errors appeared and close the search handle
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        logg("Errors appeared during directory crawling");
        helper_print_last_error();
    }
    FindClose(search_handle);

    return dynamic_array_as_array<FileInfo>(&file_infos);
}

void directory_crawler_destroy_file_infos(Array<FileInfo>* file_infos) {
    for (int i = 0; i < file_infos->size; i++) {
        string_destroy(&file_infos->data[i].name_handle);
    }
    array_destroy(file_infos);
}