#pragma once

typedef void(*file_listener_callback_func)(void* userdata, const char* filename);

struct FileListener;
struct WatchedFile;

FileListener* file_listener_create();
WatchedFile* file_listener_add_file(FileListener* listener, const char* filepath, file_listener_callback_func callback, void* userdata);
bool file_listener_remove_file(FileListener* listener, WatchedFile* file);
void file_listener_check_if_files_changed(FileListener* listener);
void file_listener_destroy(FileListener* listener);

