#pragma once

typedef void(*file_listener_callback_func)(void* userdata, const char* filename);

struct File_Listener;
struct Watched_File;

File_Listener* file_listener_create();
Watched_File* file_listener_add_file(File_Listener* listener, const char* filepath, file_listener_callback_func callback, void* userdata);
bool file_listener_remove_file(File_Listener* listener, Watched_File* file);
void file_listener_check_if_files_changed(File_Listener* listener);
void file_listener_destroy(File_Listener* listener);

