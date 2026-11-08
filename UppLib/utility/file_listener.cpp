#include "file_listener.hpp"

#include <Windows.h>

#include "../utility/utils.hpp"
#include "file_io.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/string.hpp"

struct Watched_File
{
    String filepath;
    file_listener_callback_func callback;
    u64 last_write_time;
    void* userdata;
};

struct File_Listener {
    Arena arena;
    DynArray<Watched_File*> files;
};

Watched_File* watched_file_create(File_Listener* listener, String filepath, file_listener_callback_func callback, void* userdata) 
{
    File_Info file_info = file_io_get_file_info(filepath);
    if (file_info.status != File_Info_Status::SUCCESS) {
        return nullptr;
    }

    // Add file to file listener list
    Watched_File* file = listener->arena.allocate<Watched_File>();
    file->callback = callback;
    file->filepath = string_allocate_copy(filepath, listener->arena.upcast());
    file->last_write_time = file_info.last_write_access_time;
    file->userdata = userdata;
    return file;
}

File_Listener* file_listener_create() {
    File_Listener* result = new File_Listener();
    result->arena = Arena::create();
    result->files = DynArray<Watched_File*>::create(&result->arena);
    return result;
}

void file_listener_destroy(File_Listener* listener) 
{
    listener->arena.destroy();
    delete listener;
}

Watched_File* file_listener_add_file(File_Listener* listener, String filepath, file_listener_callback_func callback, void* userdata) 
{
    // Check if file exists/if we can get last access time
    Watched_File* watched_file = watched_file_create(listener, filepath, callback, userdata);
    if (watched_file == 0) {
        return 0;
    }
    listener->files.push_back(watched_file);
    return watched_file;
}

bool file_listener_remove_file(File_Listener* listener, Watched_File* file)
{
    for (int i = 0; i < listener->files.size; i++) {
        if (listener->files[i] == file) {
            listener->files.swap_remove(i);
            return true;
        }
    }
    return false;
}

void file_listener_check_if_files_changed(File_Listener* listener)
{
    for (int i = 0; i < listener->files.size; i++) 
    {
        Watched_File* file = listener->files[i];
        File_Info file_info = file_io_get_file_info(file->filepath);
        if (file_info.status == File_Info_Status::SUCCESS)
        {
            u64 newest_write_time = file_info.last_write_access_time;
            if (newest_write_time > file->last_write_time) // First value is later than second
            {
                file->last_write_time = newest_write_time;
                file->callback(file->userdata, file->filepath); // Call callback
            }
        }
    }
}
