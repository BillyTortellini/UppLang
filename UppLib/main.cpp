#include "programs/upp_lang/upp_lang.hpp"
#include "programs/proc_city/proc_city.hpp"
#include "programs/upp_lang/compiler_misc.hpp"
#include "win32/process.hpp"
#include "programs/render_rework/render_rework.hpp"
#include "programs/bachelor_thesis/bachelor.hpp"
#include "programs/c_importer/import_gui.hpp"
#include "programs/game/game.hpp"

int main(int argc, char** argv)
{
    //render_rework();
    //bachelor_thesis();
    // return run_import_gui();
    upp_lang_main();
    //game_entry();
    //proc_city_main();
    return 0;
}