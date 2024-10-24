#pragma once

#include "../utility/datatypes.hpp"
#include "../utility/utils.hpp"
#include "../datastructures/array.hpp"
#include "../datastructures/string.hpp"

Optional<Array<byte>> file_io_load_binary_file(const char* filepath);
void file_io_unload_binary_file(Optional<Array<byte>>* file_content);

Optional<String> file_io_load_text_file(const char* filepath);
void file_io_unload_text_file(Optional<String>* file_content);

Optional<u64> file_io_get_file_size(const char* filepath);
bool file_io_check_if_file_exists(const char* filepath);

bool file_io_is_directory(const char* filepath);
Optional<u64> file_io_get_last_write_access_time(const char* filepath);
u64 file_io_get_current_file_time();

bool file_io_write_file(const char* filepath, Array<byte> data);
void file_io_relative_to_full_path(String* relative_path);

// Returns true if file was selected, and sets string (reset+append) to filepath
bool file_io_open_file_selection_dialog(String* write_to);
