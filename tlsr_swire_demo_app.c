#include <furi.h>
#include <furi_hal_resources.h>
#include <gui/gui.h>

#include "swire.h"

// For list of pins see https://github.com/flipperdevices/flipperzero-firmware/blob/dev/firmware/targets/f7/furi_hal/furi_hal_resources.c
const GpioPin* const pin_sws = &gpio_ext_pa7;
const GpioPin* const pin_back = &gpio_button_back;

static void my_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 8, "TLSR SWIRE");
}

void do_read() {
    Swire* swire = malloc(sizeof(Swire));

    SwireInit(swire, pin_sws);

    SwireTransactionStart(swire, 0x0d, RwWrite, 0);
    SwireByteWrite(swire, 0);
    SwireTransactionEnd(swire);

    SwireTransactionStart(swire, 0x0c, RwWrite, 0);
    SwireByteWrite(swire, 0x03);
    SwireTransactionEnd(swire);

    SwireTransactionStart(swire, 0x0c, RwWrite, 0);
    SwireByteWrite(swire, 0x00);
    SwireTransactionEnd(swire);

    SwireTransactionStart(swire, 0x0c, RwWrite, 0);
    SwireByteWrite(swire, 0x00);
    SwireTransactionEnd(swire);

    SwireTransactionStart(swire, 0x0c, RwWrite, 0);
    SwireByteWrite(swire, 0x00);
    SwireTransactionEnd(swire);

    SwireTransactionStart(swire, 0x0c, RwWrite, 0);
    SwireByteWrite(swire, 0x00);
    SwireByteWrite(swire, 0x0a);
    SwireTransactionEnd(swire);

    SwireTransactionStart(swire, 0xb3, RwWrite, 0);
    SwireByteWrite(swire, 0x80);
    SwireTransactionEnd(swire);
    if(SwireHasError(swire)) goto exit;

    SwireTransactionStart(swire, 0x0c, RwRead, 0);
    for(int i = 0; i < 256; i++) {
        SwireByteRead(swire);
    }
    SwireTransactionEnd(swire);

exit:
    free(swire);
}

int tlsr_swire_demo_app(void* p) {
    UNUSED(p);

    swire_global_init();

    // Show directions to user.
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, my_draw_callback, NULL);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    furi_hal_gpio_init(pin_sws, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(pin_sws, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);

    for(;;) {
        if(furi_hal_gpio_read(&gpio_button_back)) goto exit;
        if(furi_hal_gpio_read(&gpio_button_right)) {
            do_read();
        }
        furi_delay_ms(10);
    }

exit:

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_sws, GpioModeAnalog);

    // Remove the directions from the screen.
    gui_remove_view_port(gui, view_port);
    return 0;
}
