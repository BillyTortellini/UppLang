#include "programs/upp_lang/upp_lang.hpp"
#include "programs/c_importer/import_gui.hpp"
#include "programs/imgui_test/imgui_test.hpp"
#include "programs/console_debugger/console_debugger.hpp"
#include "programs/test/test.hpp"

#include <cstdio>
#include "datastructures/string.hpp"
#include "utility/directory_crawler.hpp"
#include "utility/file_io.hpp"

#include "datastructures/allocators.hpp"
#include <iostream>
#include <inttypes.h>
#include "math/scalars.hpp"

void syntax_renaming()
{
    String input_line = string_create(1024);
    SCOPE_EXIT(string_destroy(&input_line));

    Directory_Crawler* crawler = directory_crawler_create();
    SCOPE_EXIT(directory_crawler_destroy(crawler));

    printf("Directory to scan: ");
    string_reset(&input_line);
    fgets(input_line.characters, input_line.capacity - 1, stdin);
    input_line.size = (int)strlen(input_line.characters);
    if (input_line.size > 0 && input_line.characters[input_line.size - 1] == '\n') {
        input_line.characters[input_line.size - 1] = '\0';
        input_line.size -= 1;
    }
    string_replace_character(&input_line, '\\', '/');

    directory_crawler_set_path(crawler, input_line);
    Array<File_Info> files = directory_crawler_get_content(crawler);
    String filepath = string_create(0);
    SCOPE_EXIT(string_destroy(&filepath));
    for (int i = 0; i < files.size; i++)
    {
        // Load file
        auto file = files[i];
        if (file.is_directory) continue;
        printf("Editing file %s\n", file.name.characters);
        
        string_reset(&filepath);
        string_append_string(&filepath, &input_line);
        if (!string_ends_with(filepath.characters, "/")) {
            string_append_character(&filepath, '/');
        }
        string_append_string(&filepath, &file.name);

        Optional<String> text_opt = file_io_load_text_file(filepath.characters);
        SCOPE_EXIT(file_io_unload_text_file(&text_opt));
        if (!text_opt.available) {
            printf("    File not available, skipping!\n");
            continue;
        }

        String text = text_opt.value;
        auto lines = string_split(text, '\n');
        SCOPE_EXIT(string_split_destroy(lines));
        String result_text = string_create(text.size);
        SCOPE_EXIT(string_destroy(&result_text));

        // Replace assert with ~assert
        String replace_text = string_create_static("Upp~");
        String replace_with = string_create_static("~");
        for (int line_index = 0; line_index < lines.size; line_index += 1)
        {
            String line = lines[line_index];
            String new_line = string_copy(line);
            SCOPE_EXIT(string_destroy(&new_line));
            int replace_index = string_contains_substring(line, 0, replace_text);
            if (replace_index != -1) 
            {
                printf("Found assert line: \n");
                printf("----%s\n", new_line.characters);
                string_remove_substring(&new_line, replace_index, replace_index + replace_text.size);
                string_insert_string(&new_line, &replace_with, replace_index);
                printf("----%s\n", new_line.characters);
            }
            string_append_string(&result_text, &new_line);
            if (line_index != lines.size - 1) {
                string_append_character(&result_text, '\n');
            }
        }
        // Write back new file
        file_io_write_file(filepath.characters, array_create_static_as_bytes(result_text.characters, result_text.size));
    }

    printf("Enter to exit");
    fgets(input_line.characters, input_line.capacity - 1, stdin);
}

struct Node
{
    int value;
    void* another;
    int data[8];
    const char* name;
};

Node node_make(const char* name, int value) {
    Node n;
    n.value = value;
    n.another = (void*)name;
    n.name = name;
    for (int i = 0; i < 8; i++) {
        n.data[i] = value;
    }
    return n;
}

u64 node_hash(Node** node) {
    return (*node)->value * 523438247;
}

bool node_equals(Node** app, Node** bpp) {
    return (*app)->value == (*bpp)->value;
}

void arena_test()
{
    for (int i = 0; i < 1024; i++)
    {
        printf("next power %d = %d\n", i, (int)integer_next_power_of_2((u32)i));
    }

    Arena arena = Arena::create();
    DynArray<Node> nodes = DynArray<Node>::create(&arena);
    DynSet<Node*> set = DynSet<Node*>::create(&arena, node_hash, node_equals);

    Node n = node_make("Hello", 5);
    const int NODE_COUNT = 5;
    Node* node_ptrs[NODE_COUNT];
    for (int i = 0; i < NODE_COUNT; i++) {
        node_ptrs[i] = arena.allocate<Node>();
        *node_ptrs[i] = node_make("Test me", i * 3);
    }
    nodes.push_back(n);
    nodes.push_back(n);
    set.insert(node_ptrs[1 % NODE_COUNT]);
    set.insert(node_ptrs[8 % NODE_COUNT]);
    Array<int> values = arena.allocate_array<int>(100);
    values[10] = 5;
    nodes.push_back(n);
    set.insert(node_ptrs[13 % NODE_COUNT]);
    nodes.push_back(n);
    set.insert(node_ptrs[14 % NODE_COUNT]);
    set.insert(node_ptrs[15 % NODE_COUNT]);
    set.insert(node_ptrs[16 % NODE_COUNT]);
    set.insert(node_ptrs[18 % NODE_COUNT]);
    set.insert(node_ptrs[19 % NODE_COUNT]);
    nodes.push_back(n);
    nodes.push_back(n);
    Node* test = arena.allocate<Node>();
    *test = n;
    nodes.push_back(n);
    set.insert(node_ptrs[14 % NODE_COUNT]);
    set.insert(node_ptrs[15 % NODE_COUNT]);
    set.insert(node_ptrs[16 % NODE_COUNT]);
    set.insert(node_ptrs[18 % NODE_COUNT]);
    set.insert(node_ptrs[19 % NODE_COUNT]);
    nodes.push_back(n);

    arena.destroy();

    std::cout << "Arena test finished!" << std::endl;
    std::cin.ignore();
}

void next_test()
{
    Arena arena = Arena::create();
    const int SIZE = 512;
    const int ARRAY_COUNT = 32;

    Array<DynArray<int>> arrays = arena.allocate_array<DynArray<int>>(ARRAY_COUNT);
    for (int i = 0; i < arrays.size; i++) {
        arrays[i] = DynArray<int>::create(&arena);
    }

    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < arrays.size; j++) {
            arrays[j].push_back(j);
        }
    }

    for (int i = 0; i < arrays.size; i++) {
        for (int j = 0; j < SIZE; j++) {
            arrays[i].push_back(i);
        }
    }

    for (int i = 0; i < arrays.size; i++) 
    {
        DynArray<int>& array = arrays[i];
        for (int j = 0; j < array.size; j++) {
            assert(array[j] == i, "");
        }
    }

    arena.destroy();
}

struct File_Line_Info
{
    String filename;
    int line_count;
};

File_Line_Info file_line_info_make(String full_path, int line_count) 
{
    File_Line_Info result;
    result.line_count = line_count;
    result.filename = full_path;

    int dir_index = -1;
    for (int i = full_path.size - 1; i >= 0; i -= 1) {
        if (full_path[i] == '/') {
            dir_index = i;
            break;
        }
    }
    if (dir_index != -1) {
        result.filename = string_create_substring_static(&result.filename, dir_index + 1, result.filename.size);
    }
    return result;
}

struct Comparator_File_Line
{
	bool operator()(const File_Line_Info& a, const File_Line_Info& b) {
        return a.line_count > b.line_count;
	}
};



int count_lines_of_code()
{
    Arena arena = arena.create();
    SCOPE_EXIT(arena.destroy());
    DynArray<String> filenames = DynArray<String>::create(&arena);

    // Find all files
    {
        DynArray<String> directory_queue = DynArray<String>::create(&arena);
        directory_queue.push_back(string_create("D:/Projects/UppLang/UppLib", &arena));
        Directory_Crawler* crawler = directory_crawler_create();
        SCOPE_EXIT(directory_crawler_destroy(crawler));

        while (directory_queue.size > 0)
        {
            String dir = directory_queue.last();
            directory_queue.size -= 1;

            directory_crawler_set_path(crawler, dir);
            Array<File_Info> infos = directory_crawler_get_content(crawler);
            for (int i = 0; i < infos.size; i++)
            {
                File_Info& file_info = infos[i];
                if (string_equals(file_info.name, string_create_static("..")) || string_equals(file_info.name, string_create_static("."))) {
                    continue;
                }

                String path = string_create(&arena);
                path.append(dir);
                if (path.size > 0 && path.characters[path.size - 1] != '/') {
                    path.append('/');
                }
                path.append(file_info.name);
                file_io_relative_to_full_path(&path);

                if (file_info.is_directory) {
                    directory_queue.push_back(path);
                }
                else 
                {
                    int dot_index = -1;
                    for (int i = path.size - 1; i >= 0; i -= 1) {
                        if (path.characters[i] == '.') {
                            dot_index = i;
                            break;
                        }
                        else if (path.characters[i] == '/') {
                            break;
                        }
                    }

                    if (dot_index == -1) continue;
                    String filetype = string_create_substring_static(&path, dot_index + 1, path.size);
                    if (!(string_equals_cstring(&filetype, "cpp") || string_equals_cstring(&filetype, "hpp"))) {
                        continue;
                    }
                    
                    filenames.push_back(path);
                }
            }
        }
    }

    DynArray<File_Line_Info> line_infos = DynArray<File_Line_Info>::create(&arena);
    int line_count_total = 0;
    for (int i = 0; i < filenames.size; i++)
    {
        String& filename = filenames[i];
        string_add_null_terminator(&filename);

        int line_count = -1;
        auto file_opt = file_io_load_text_file(filename.characters);
        SCOPE_EXIT(file_io_unload_text_file(&file_opt));
        if (file_opt.available) {
            String text = file_opt.value;
            for (int j = 0; j < text.size; j++) {
                if (text[j] == '\n') {
                    line_count += 1;
                }
            }
        }

        line_count_total += math_maximum(0, line_count);
        line_infos.push_back(file_line_info_make(filename, line_count));
    }

    Array<File_Line_Info> slice = array_create_static<File_Line_Info>(line_infos.buffer.data, line_infos.size);
    array_sort(slice, Comparator_File_Line());
    
    printf("Files: #%d\n", (int)filenames.size);
    printf("-------------\n");
    for (int i = 0; i < line_infos.size; i++)
    {
        File_Line_Info& info = line_infos[i];
        printf("%5d %s\n", info.line_count, info.filename.characters);
    }
    printf("-------------\n");
    printf("Total: #%d\n\n", line_count_total);

    return 0;
}

int main(int argc, char** argv)
{
    // count_lines_of_code();
    // std::cin.ignore();
    // return 0 ;

    // next_test();
    // arena_test();
    // syntax_renaming();
    // return 0;

    // test_entry();
    upp_lang_main();
    //imgui_test_entry();
    //proc_city_main();
    return 0;
}