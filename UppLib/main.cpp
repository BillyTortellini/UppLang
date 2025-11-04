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
    String input_line = string_create_empty(1024);
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
    String filepath = string_create_empty(0);
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

        // Replace -> with =>
        String text = text_opt.value;
        for (int j = 0; j + 1 < text.size; j++) {
            char curr = text.characters[j];
            char next = text.characters[j + 1];
            if (curr == '-' && next == '>') {
                text[j] = '=';
            }
        }

        // Replace .> with ->
        for (int j = 0; j + 1 < text.size; j++) {
            char curr = text.characters[j];
            char next = text.characters[j + 1];
            if (curr == '.' && next == '>') {
                text[j] = '-';
            }
        }

        // Write back new file
        file_io_write_file(filepath.characters, array_create_static_as_bytes(text.characters, text.size));
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

int main(int argc, char** argv)
{
    // arena_test();
    // syntax_renaming();
    // return 0;

    //test_entry();
    upp_lang_main();
    //imgui_test_entry();
    //proc_city_main();
    return 0;
}