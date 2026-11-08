#include "directory_crawler.hpp"

#include <Windows.h>

#include "../datastructures/string.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../utility/utils.hpp"
#include "../win32/windows_helper_functions.hpp"

struct Directory_Crawler {
    DynString path;
    Arena arena;
    Arena directory_name_arena;
    DynArray<Directory_Item> file_infos;
    bool path_changed;
};

Directory_Crawler* directory_crawler_create()
{
    Directory_Crawler* result = new Directory_Crawler();
    result->arena = Arena::create();
    result->directory_name_arena = Arena::create();
    result->path = dynstring_create(&result->arena);
    result->path.append(".");
    result->path_changed = true;
    result->file_infos = DynArray<Directory_Item>::create(&result->arena);
    directory_crawler_set_to_working_directory(result);
    return result;
}

void directory_crawler_destroy(Directory_Crawler* crawler) {
    crawler->arena.destroy();
    crawler->directory_name_arena.destroy();
    delete crawler;
}

void directory_crawler_set_path(Directory_Crawler* crawler, String path)
{
    crawler->path.reset()->append(path);
    filepath_relative_to_absolute_path(&crawler->path);
    crawler->path_changed = true;
}

void directory_crawler_set_path_to_file_dir(Directory_Crawler* crawler, String file_path) 
{
    auto& path = crawler->path;
    path.reset()->append(file_path);
    int trim_pos = 0;
    auto last_slash = string_find_character_index_reverse(path.to_string(), '/', path.size - 1);
    if (!last_slash.available) {
        last_slash = string_find_character_index_reverse(path.to_string(), '\\', path.size - 1);
    }
    if (last_slash.available) {
        trim_pos = last_slash.value;
    }
    dynstring_truncate(&path, trim_pos);
    filepath_relative_to_absolute_path(&path);
    crawler->path_changed = true;
}

void directory_crawler_set_to_working_directory(Directory_Crawler* crawler) {
    int path_length = GetCurrentDirectory(0, 0);
    crawler->path.reset()->reserve(path_length + 1);
    int written_string_length = GetCurrentDirectory(crawler->path.capacity, crawler->path.characters);
    crawler->path.size = strlen(crawler->path.characters);
    crawler->path_changed = true;
    filepath_relative_to_absolute_path(&crawler->path);
}

String directory_crawler_get_path(Directory_Crawler* crawler) {
    return crawler->path.to_string();
}

bool directory_crawler_go_up_one_directory(Directory_Crawler* crawler) {
    DynString* path = &crawler->path;
    Optional<int> last_pos = string_find_character_index_reverse(path->to_string(), '/', path->size - 1);
    if (!last_pos.available) {
        return false;
    }
    dynstring_truncate(path, last_pos.value);
    return true;
}

bool directory_crawler_go_down_one_directory(Directory_Crawler* crawler, int dir_index) {
    auto files = directory_crawler_get_content(crawler);
    if (dir_index < 0 || dir_index >= files.size) return false;
    auto file = files[dir_index];
    if (!file.is_directory) return false;
    dynstring_append_formated(&crawler->path, "/%s", file.filename.characters);
    crawler->path_changed = true;
    return true;
}

Array<Directory_Item> directory_crawler_get_content(Directory_Crawler* crawler)
{
    SCRATCH_ARENA_MAKE_SCOPED(&crawler->arena);
    DynString tmp_string = DynString::create(scratch_arena);

    if (!crawler->path_changed) {
        return crawler->file_infos.as_array();
    }

    crawler->path_changed = false;
    crawler->directory_name_arena.reset(true);
    crawler->file_infos.reset();

    WIN32_FIND_DATA found_file_description;
    HANDLE search_handle = FindFirstFile(tmp_string.reset()->append(crawler->path)->append("/*")->to_cstring(), &found_file_description);
    if (search_handle == INVALID_HANDLE_VALUE) {
        return crawler->file_infos.as_array();
    }

    // Loop over all found files
    do {
        Directory_Item info;
        info.is_directory = found_file_description.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        info.size         = found_file_description.nFileSizeHigh * (((i64)MAXDWORD)+1) + found_file_description.nFileSizeLow;
        info.filename     = string_allocate_copy(string_make(found_file_description.cFileName), crawler->directory_name_arena.upcast());
        crawler->file_infos.push_back(info);
    } while (FindNextFile(search_handle, &found_file_description) != 0);

    // Check if errors appeared and close the search handle
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        logg("Errors appeared during directory crawling");
        helper_print_last_error();
    }
    FindClose(search_handle);

    return crawler->file_infos.as_array();
}

void directory_crawler_print_all_files(Directory_Crawler* crawler) 
{
    auto files = directory_crawler_get_content(crawler);
    logg("Directory: %s\n", crawler->path.characters);
    for (int i = 0; i < files.size; i++) {
        Directory_Item& file = files[i];
        logg("    %.%s %s\n", files[i].filename.size, files[i].filename.characters, file.is_directory ? "d" : "");
    }
}
    