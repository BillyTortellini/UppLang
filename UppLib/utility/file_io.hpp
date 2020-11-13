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

bool file_io_write_file(const char* filepath, Array<byte> data);
