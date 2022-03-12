#include <Core/gb.h>
#include <emscripten.h>
#include <time.h>
#include "SDL/gui.h"
#include "menu_items.h"
#include "SDL/utils.h"
#include "wasm_serial.h"

extern GB_gameboy_t gb;
extern char *battery_save_path_ptr;

static time_t workboy_time = 0;

static void printer_callback(GB_gameboy_t *gb, uint32_t *image, uint8_t height, uint8_t top_margin, uint8_t bottom_margin, uint8_t exposure)
{
    char filename[strlen(battery_save_path_ptr) - 9]; // remove /persist/
    replace_extension(battery_save_path_ptr + 9, strlen(battery_save_path_ptr + 9), filename, "");

    EM_ASM(({
        const image_ptr = $0;
        const height = $1;
        const top_margin = $2;
        const bottom_margin = $3;
        const exposure = $4;
        const filename_ptr = $5;
        const filename_length = $6;

        const width = 160;
        const size = width * height * 4;
        const buf = new Uint8Array(Module.HEAPU8.buffer, image_ptr, size);
        const game_filename = UTF8ToString(filename_ptr, filename_length);

        document.querySelector('#printerDialog .prints').dataset.game = game_filename;

        // If there’s a margin, we are most likely dealing with a new image
        const new_image = top_margin > 0 || bottom_margin > 0;
        let canvas = document.querySelector('#printerDialog .prints .wrapper:last-child > canvas');

        if (!canvas || new_image) {
            const date = new Date();
            const date_str = date.toISOString().replace(':', '-');

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
            wrapper.dataset.date_str = date_str;

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
                anchor.download = `${game_filename}-${wrapper.dataset.date_str}.png`;
                anchor.style.display = 'none';
                document.body.appendChild(anchor);
                anchor.click();
                anchor.remove();
            });

            wrapper.appendChild(buttons);

            document.querySelector('#printerDialog .prints')
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

static void workboy_set_time_callback(GB_gameboy_t *gb, time_t new_time)
{
    workboy_time = time(NULL) - new_time;
}

static time_t workboy_get_time_callback(GB_gameboy_t *gb)
{
    return time(NULL) - workboy_time;
}

void connect_printer(unsigned index)
{
    GB_connect_printer(&gb, printer_callback);

    connected_device = SERIAL_DEVICE_PRINTER;
    reset_menus();
}

void connect_workboy(unsigned index)
{
    SDL_StartTextInput();
    GB_connect_workboy(&gb, workboy_set_time_callback, workboy_get_time_callback);

    EM_ASM({ Module.enable_workboy(); });

    connected_device = SERIAL_DEVICE_WORKBOY;
    reset_menus();
}

void disconnect_serial(unsigned index)
{
    GB_disconnect_serial(&gb);
    SDL_StopTextInput();

    EM_ASM({ Module.disable_workboy(); });

    connected_device = SERIAL_DEVICE_NONE;
    reset_menus();
}

void connect_serial(void)
{
    switch (connected_device) {
        case SERIAL_DEVICE_PRINTER:
            connect_printer(0);
            break;

        case SERIAL_DEVICE_WORKBOY:
            connect_workboy(0);
            break;

        default:
            disconnect_serial(0);
            break;
    }
}