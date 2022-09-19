#include <emscripten.h>
#include "SDL/gui.h"
#include "menu_items.h"
#include "wasm_serial.h"

EM_JS(void, synchronize_save_files, (unsigned index), {
    Module._save_configuration();
    Module._save_battery();
    Module.gb_syncfs();
});

EM_JS(void, open_save_manager, (unsigned index), {
    Module.gb_open_save_manager();
});

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
});

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

EM_JS(void, open_printout, (unsigned index), {
    Module.gb_open_printer_dialog();
});

static struct menu_item printer_menu[] = {
    {"Connect", connect_printer},
    {"Open Printout", open_printout},
    {"Back", return_to_root_menu},
    {NULL,}
};

static struct menu_item workboy_menu[] = {
    {"Connect", connect_workboy},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void open_printer_menu(unsigned index)
{
    reset_menus();

    current_menu = printer_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static void open_workboy_menu(unsigned index)
{
    reset_menus();

    current_menu = workboy_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static const struct menu_item serial_device_menu[] = {
    {"Printer", open_printer_menu},
    {"WorkBoy", open_workboy_menu},
    {"Back", return_to_root_menu},
    {NULL,}
};

void enter_serial_device_menu(unsigned index)
{
    current_menu = serial_device_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

void reset_menus(void)
{
    switch (connected_device) {
        case SERIAL_DEVICE_NONE:
            printer_menu[0].string = "Connect";
            printer_menu[0].handler = connect_printer;

            workboy_menu[0].string = "Connect";
            workboy_menu[0].handler = connect_workboy;
            break;

        case SERIAL_DEVICE_PRINTER:
            printer_menu[0].string = "Disconnect";
            printer_menu[0].handler = disconnect_serial;

            workboy_menu[0].string = "Connect";
            workboy_menu[0].handler = connect_workboy;
            break;

        case SERIAL_DEVICE_WORKBOY:
            printer_menu[0].string = "Connect";
            printer_menu[0].handler = connect_printer;

            workboy_menu[0].string = "Disconnect";
            workboy_menu[0].handler = disconnect_serial;
           break;
    }
}
