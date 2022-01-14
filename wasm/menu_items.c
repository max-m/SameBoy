#include <emscripten.h>
#include "SDL/gui.h"

EM_JS(void, start_example, (unsigned index), {
    const names = [
        "tobudx.gb",
        "mezase.gbc",
        "pocket.gb",
        "gejmboj.gb",
        "oh.gb"
    ];

    // TODO: Visual feedback and error handling
    Module.gb_load_remote_rom('demos/' + names[index]);
})

static const struct menu_item examples_menu[] = {
    {"Tobu Tobu Girl Deluxe", start_example},
    {"Video Player 2 (GBVP2)", start_example},
    {"A demo in your Pocket?", start_example},
    {"Gejmboj", start_example},
    {"Oh!", start_example},
    {"Back", return_to_root_menu},
    {NULL,}
};

void enter_examples_menu(unsigned index)
{
    current_menu = examples_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}
