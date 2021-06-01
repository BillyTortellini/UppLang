#include "file_listener.hpp"

#include <Windows.h>

#include "../utility/utils.hpp"
#include "file_io.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/string.hpp"

struct WatchedFile
{
    String filepath;
    file_listener_callback_func callback;
    u64 last_write_time;
    void* userdata;
};

struct File_Listener {
    Dynamic_Array<WatchedFile*> files;
};

WatchedFile* watched_file_create(const char* filepath, file_listener_callback_func callback, void* userdata) 
{
    Optional<u64> last_access = file_io_get_last_write_access_time(filepath);
    if (last_access.available == false) {
        return nullptr;
    }

    // Add file to file listener list
    WatchedFile* file = new WatchedFile();
    file->callback = callback;
    file->filepath = string_create(filepath);
    file->last_write_time = last_access.value;
    file->userdata = userdata;
    return file;
}

void watched_file_destroy(WatchedFile* watched_file) {
    string_destroy(&watched_file->filepath);
    delete watched_file;
}

File_Listener* file_listener_create() {
    File_Listener* result = new File_Listener();
    result->files = dynamic_array_create_empty<WatchedFile*>(8);
    return result;
}

void file_listener_destroy(File_Listener* listener) 
{
    for (int i = 0; i < listener->files.size; i++) {
        WatchedFile* file = listener->files.data[i];
        watched_file_destroy(file);
    }
    dynamic_array_destroy<WatchedFile*>(&listener->files);
}

WatchedFile* file_listener_add_file(File_Listener* listener, const char* filepath, file_listener_callback_func callback, void* userdata) 
{
    // Check if file exists/if we can get last access time
    WatchedFile* watched_file = watched_file_create(filepath, callback, userdata);
    if (watched_file == 0) {
        return 0;
    }
    dynamic_array_push_back<WatchedFile*>(&listener->files, watched_file);

    return watched_file;
}

bool file_listener_remove_file(File_Listener* listener, WatchedFile* file)
{
    for (int i = 0; i < listener->files.size; i++) {
        if (listener->files.data[i] == file) {
            dynamic_array_swap_remove<WatchedFile*>(&listener->files, i);
            watched_file_destroy(file);
            return true;
        }
    }
    return false;
}

void file_listener_check_if_files_changed(File_Listener* listener)
{
    for (int i = 0; i < listener->files.size; i++) 
    {
        WatchedFile* file = listener->files.data[i];
        Optional<u64> newest_write_time = file_io_get_last_write_access_time(file->filepath.characters);
        if (newest_write_time.available)
        {
            if (newest_write_time.value > file->last_write_time) // First value is later than second
            {
                file->last_write_time = newest_write_time.value;
                file->callback(file->userdata, file->filepath.characters); // Call callback
            }
        }
    }
}
