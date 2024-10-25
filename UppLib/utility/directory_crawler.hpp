#pragma once

#include "../datastructures/array.hpp"
#include "../utility/datatypes.hpp"
#include "../datastructures/string.hpp"

struct Directory_Crawler;

struct File_Info
{
    String name;
    i64 size;
    bool is_directory;
};

Directory_Crawler* directory_crawler_create();
void directory_crawler_destroy(Directory_Crawler* directory_crawler);

String directory_crawler_get_path(Directory_Crawler* crawler);
void directory_crawler_set_path(Directory_Crawler* crawler, String path);
void directory_crawler_set_path_to_file_dir(Directory_Crawler* crawler, String file_path);
void directory_crawler_set_to_working_directory(Directory_Crawler* crawler);

Array<File_Info> directory_crawler_get_content(Directory_Crawler* crawler);
bool directory_crawler_go_up_one_directory(Directory_Crawler* crawler);
bool directory_crawler_go_down_one_directory(Directory_Crawler* crawler, int dir_index); // Directory index from crawler content

void directory_crawler_print_all_files(Directory_Crawler* crawler);
