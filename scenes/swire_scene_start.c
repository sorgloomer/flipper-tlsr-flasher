#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_usb.h>
#include <dolphin/dolphin.h>

#include "scenes/swire_scene.h"
#include "scenes/swire_scene_start.h"
#include "scenes/swire_gui_event.h"
#include "src/app/app.h"
#include "src/utils/light_rgb.h"
#include "src/commands/commands_bitbang.h"

enum SwireStartItem {
    SwireStartItemTest,
};

enum GpioOtg {
    GpioOtgOff,
    GpioOtgOn,
    GpioOtgSettingsNum,
};

const char* const gpio_otg_text[GpioOtgSettingsNum] = {
    "OFF",
    "ON",
};

static void gpio_scene_start_var_list_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    SwireApp* app = context;
    switch(index) {
    case SwireStartItemTest: {
        view_dispatcher_send_custom_event(app->view_dispatcher, SwireGuiEventTest);
    } break;
    }
}

static void gpio_scene_start_var_list_change_callback(VariableItem* item) {
    SwireApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, gpio_otg_text[index]);
    if(index == GpioOtgOff) {
        view_dispatcher_send_custom_event(app->view_dispatcher, GpioStartEventOtgOff);
    } else if(index == GpioOtgOn) {
        view_dispatcher_send_custom_event(app->view_dispatcher, GpioStartEventOtgOn);
    }
}

void gpio_scene_start_on_enter(void* context) {
    SwireApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;

    VariableItem* item;
    variable_item_list_set_enter_callback(
        var_item_list, gpio_scene_start_var_list_enter_callback, app);

    variable_item_list_add(var_item_list, "USB-UART Bridge", 0, NULL, NULL);

    variable_item_list_add(var_item_list, "GPIO Manual Control", 0, NULL, NULL);

    item = variable_item_list_add(
        var_item_list,
        "5V on GPIO",
        GpioOtgSettingsNum,
        gpio_scene_start_var_list_change_callback,
        app);

    if(true) {
        variable_item_set_current_value_index(item, GpioOtgOn);
        variable_item_set_current_value_text(item, gpio_otg_text[GpioOtgOn]);
    } else {
        variable_item_set_current_value_index(item, GpioOtgOff);
        variable_item_set_current_value_text(item, gpio_otg_text[GpioOtgOff]);
    }

    variable_item_list_set_selected_item(
        var_item_list, scene_manager_get_scene_state(app->scene_manager, SwireSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, SwireAppViewVarItemList);
}

bool swire_scene_start_on_event(void* context, SceneManagerEvent event) {
    SwireApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SwireGuiEventTest:
            light_rgb_set(0xff0000);
            furi_delay_ms(200);
            cmd_bitbang_test_simple();
            break;
        case GpioStartEventOtgOn:
            app_set_blinker(app, 0x00ffff, 1000);
            break;
        case GpioStartEventOtgOff:
            app_set_blinker(app, 0x000080, 2000);
            break;
        }
        return true;
    }
    return false;
}

void gpio_scene_start_on_exit(void* context) {
    SwireApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
