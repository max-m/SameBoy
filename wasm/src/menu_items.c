#include <emscripten.h>
#include <time.h>
#include "SDL/gui.h"
#include "SDL/utils.h"
#include "wasm_serial.h"

extern char *battery_save_path_ptr;

EM_JS(void, synchronize_save_files, (unsigned index), {
    Module._save_configuration();
    Module._save_battery();
    Module.gb_syncfs();
});

EM_JS(void, open_save_manager, (unsigned index), {
    Module.gb_open_save_manager();
});

EM_JS(void, item_about, (unsigned index), {
    Module.gb_open_about_dialog();
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

static void connect_printer(unsigned index);
static struct menu_item printer_menu[] = {
    {"Connect", connect_printer},
    {"Open Printout", open_printout},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void connect_workboy(unsigned index);
static struct menu_item workboy_menu[] = {
    {"Connect", connect_workboy},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void disconnect_serial(unsigned index);
static void reset_menus(void)
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

static void disconnect_serial(unsigned index)
{
    GB_disconnect_serial(&gb);
    SDL_StopTextInput();

    connected_device = SERIAL_DEVICE_NONE;
    reset_menus();
}

static void printer_callback(GB_gameboy_t *gb, uint32_t *image, uint8_t height, uint8_t top_margin, uint8_t bottom_margin, uint8_t exposure)
{
    char filename[strlen(battery_save_path_ptr) - 9]; // remove /persist/
    replace_extension(battery_save_path_ptr + 9, strlen(battery_save_path_ptr + 9), filename, "");

    EM_ASM(({
        const image_ptr = $0;

        const width = 160;
        const height = $1;

        const size = width * height * 4;
        const buf = new Uint8Array(Module.HEAPU8.buffer, image_ptr, size);

        const top_margin = $2;
        const bottom_margin = $3;
        const exposure = $4;

        // If there’s a margin, we are most likely dealing with a new image
        const new_image = top_margin > 0 || bottom_margin > 0;
        let canvas = document.querySelector('#printerDialog .dialogContent .wrapper:last-child > canvas');

        if (!canvas || new_image) {
            const date = new Date();
            const filename = `${UTF8ToString($5, $6)}-${date.toISOString().replace(':', '-')}.png`;

            const wrapper = document.createElement('div');
            wrapper.classList.add('wrapper');

            canvas = document.createElement('canvas');
            canvas.width = width;
            canvas.height = height;
            const ctx = canvas.getContext('2d');

            const image_data = ctx.createImageData(width, height);
            image_data.data.set(buf);
            ctx.putImageData(image_data, 0, 0);

            wrapper.style.setProperty('--margin-top', top_margin);
            wrapper.style.setProperty('--margin-bottom', bottom_margin);

            wrapper.appendChild(canvas);

            const buttons = document.createElement('div');
            buttons.classList.add('buttonBar');
            buttons.innerHTML = `<svg class="icon delete"><use href="img/delete.svg#i"></use></svg>
            <svg class="icon download"><use href="img/download.svg#i"></use></svg>`;

            buttons.querySelector('.delete').addEventListener('click', () => {
                if (window.confirm(`Are you sure you want to delete this image?`)) {
                    wrapper.remove();
                }
            });

            buttons.querySelector('.download').addEventListener('click', () => {
                const url = canvas.toDataURL('image/png');
                const anchor = document.createElement('a');
                anchor.href = url;
                anchor.download = filename;
                anchor.style.display = 'none';
                document.body.appendChild(anchor);
                anchor.click();
                anchor.remove();
            });

            wrapper.appendChild(buttons);

            document.querySelector('#printerDialog .dialogContent')
                .appendChild(wrapper);
        }
        else {
            const ctx = canvas.getContext('2d');
            // Changing the canvas height destroys the current buffer
            const backup = canvas.getImageData(0, 0, canvas.width, canvas.height);
            canvas.height += height;
            ctx.putImageData(backup, 0, 0);

            const image_data = ctx.createImageData(width, height);
            image_data.data.set(buf);
            ctx.putImageData(image_data, 0, canvas.height);
        }

    }), image, height, top_margin, bottom_margin, exposure, filename, strlen(filename));
}

static void connect_printer(unsigned index)
{
    GB_connect_printer(&gb, printer_callback);

    connected_device = SERIAL_DEVICE_PRINTER;
    reset_menus();
}

static void open_printer_menu(unsigned index)
{
    reset_menus();

    current_menu = printer_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static time_t workboy_time = 0;
void workboy_set_time_callback(GB_gameboy_t *gb, time_t new_time)
{
    workboy_time = time(NULL) - new_time;
}

time_t workboy_get_time_callback(GB_gameboy_t *gb)
{
    return time(NULL) - workboy_time;
}

static void connect_workboy(unsigned index)
{
    SDL_StartTextInput();
    GB_connect_workboy(&gb, workboy_set_time_callback, workboy_get_time_callback);

    connected_device = SERIAL_DEVICE_WORKBOY;
    reset_menus();
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
