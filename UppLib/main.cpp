#include "programs/upp_lang/upp_lang.hpp"
#include "programs/c_importer/import_gui.hpp"
#include "programs/imgui_test/imgui_test.hpp"
#include "programs/console_debugger/console_debugger.hpp"
#include "programs/test/test.hpp"

#include <cstdio>
#include "datastructures/string.hpp"
#include "utility/directory_crawler.hpp"
#include "utility/file_io.hpp"

void syntax_renaming()
{
    String input_line = string_create_empty(1024);
    SCOPE_EXIT(string_destroy(&input_line));

    Directory_Crawler* crawler = directory_crawler_create();
    SCOPE_EXIT(directory_crawler_destroy(crawler));

    printf("Directory to scan: ");
    string_reset(&input_line);
    fgets(input_line.characters, input_line.capacity - 1, stdin);
    input_line.size = strlen(input_line.characters);
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

int main(int argc, char** argv)
{
    // syntax_renaming();
    // return 0;

    //test_entry();
    upp_lang_main();
    //imgui_test_entry();
    //proc_city_main();
    return 0;
}