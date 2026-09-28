#pragma once

#include "../utility/datatypes.hpp"
#include "../utility/utils.hpp"
#include "../datastructures/allocators.hpp"
#include "../datastructures/string.hpp"

enum class File_Info_Status
{
	SUCCESS,
	FILE_DOES_NOT_EXIST,
	COULD_NOT_QUERY // In case of insufficient rights, or other processes currently have openend the file
};

struct File_Info
{
	File_Info_Status status;
	u64 last_write_access_time;
	u64 file_size;
	bool is_directory;
};

struct Directory_Item
{
	DynString filename;
	u64 size;
	bool is_directory; // Otherwise it's a file
};

// If arena is nullptr, then the file will be allocated with system allocator
Optional<Array<byte>> file_io_load_binary_file(DynString path, Arena* arena);
Optional<DynString> file_io_load_text_file(DynString path, Arena* arena);
bool file_io_write_binary_file(DynString path, Array<byte> data);
bool file_io_write_text_file(DynString path, DynString text);

File_Info file_io_get_file_info(DynString path);
bool file_io_open_file_selection_dialog(DynString* write_to);
List<Directory_Item> file_io_get_directory_content(DynString directory_path, Arena* arena);


// Path manipulation
// Note: Paths in this project use / as the delimiter, altough windows infamously uses \\ for this
//		Paths to directories don't include the directory, so 
//		"C:/Pictures" is the directory path, not "C:/Pictures/"
struct Filepath_Parts
{
	DynString directory; // Without / at the end, may be empty if it's a relative path
	DynString filename; // With extension
};

struct Filename_Parts
{
	DynString name;
	DynString extension; // Without the .
};

Filepath_Parts filepath_get_parts(DynString path);
Filename_Parts filename_get_parts(DynString filename);
void filepath_relative_to_absolute_path(DynString* filepath);
