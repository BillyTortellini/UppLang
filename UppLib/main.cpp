#include "programs/upp_lang/upp_lang.hpp"
#include "programs/c_importer/import_gui.hpp"
#include "programs/imgui_test/imgui_test.hpp"
#include "programs/console_debugger/console_debugger.hpp"
#include "programs/test/test.hpp"

int main(int argc, char** argv)
{
    //test_entry();
    upp_lang_main();
    int x = 10;
    int y = 12;
    int result = x ^ y;
    //imgui_test_entry();
    //proc_city_main();
    return 0;
}