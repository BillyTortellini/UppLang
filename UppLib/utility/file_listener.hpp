#pragma once

typedef void(*file_listener_callback_func)(void* userdata, const char* filename);

struct File_Listener;
struct WatchedFile;

File_Listener* file_listener_create();
WatchedFile* file_listener_add_file(File_Listener* listener, const char* filepath, file_listener_callback_func callback, void* userdata);
bool file_listener_remove_file(File_Listener* listener, WatchedFile* file);
void file_listener_check_if_files_changed(File_Listener* listener);
void file_listener_destroy(File_Listener* listener);

