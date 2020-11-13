#pragma once

#include "../datastructures/array.hpp"
#include "../utility/datatypes.hpp"
#include "../datastructures/string.hpp"

struct DirectoryCrawler;

struct FileInfo
{
    String name;
    i64 size;
    bool is_directory;
};

DirectoryCrawler* directory_crawler_create();
void directory_crawler_destroy(DirectoryCrawler* directory_crawler);

const char* directory_crawler_get_path(DirectoryCrawler* crawler);
bool directory_crawler_go_up_one_directory(DirectoryCrawler* crawler);
void directory_crawler_print_all_files(DirectoryCrawler* crawler);

Array<FileInfo> directory_crawler_create_file_infos(DirectoryCrawler* crawler);
void directory_crawler_destroy_file_infos(Array<FileInfo>* file_infos);

