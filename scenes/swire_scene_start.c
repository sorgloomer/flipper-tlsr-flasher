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
    SwireStartItemBitbangTest,
};

enum SwireUsbEnabled {
    SwireUsbEnabledOff,
    SwireUsbEnabledOn,
    SwireUsbEnabledSettingCount,
};

const char* const swire_usb_enabled_text[SwireUsbEnabledSettingCount] = {
    "OFF",
    "ON",
};

static void scene_start_var_list_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    SwireApp* app = context;
    switch(index) {
    case SwireStartItemBitbangTest: {
        view_dispatcher_send_custom_event(app->view_dispatcher, SwireGuiEvenBitbangTest);
    } break;
    }
}

static void gpio_scene_start_var_list_change_callback(VariableItem* item) {
    SwireApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, swire_usb_enabled_text[index]);
    switch(index) {
    case SwireUsbEnabledOff:
        view_dispatcher_send_custom_event(app->view_dispatcher, SwireGuiEventUsbEnabledbOff);
        break;
    case SwireUsbEnabledOn:
        view_dispatcher_send_custom_event(app->view_dispatcher, SwireGuiEventUsbEnabledOn);
        break;
    }
}

void swire_scene_start_on_enter(void* context) {
    SwireApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;

    VariableItem* item;
    variable_item_list_set_enter_callback(var_item_list, scene_start_var_list_enter_callback, app);

    variable_item_list_add(var_item_list, "BitBang Test", 0, NULL, NULL);

    variable_item_list_add(var_item_list, "GPIO Manual Control", 0, NULL, NULL);

    item = variable_item_list_add(
        var_item_list,
        "USB",
        SwireUsbEnabledSettingCount,
        gpio_scene_start_var_list_change_callback,
        app);

    if(app->usb != NULL) {
        variable_item_set_current_value_index(item, SwireUsbEnabledOn);
        variable_item_set_current_value_text(item, swire_usb_enabled_text[SwireUsbEnabledOn]);
    } else {
        variable_item_set_current_value_index(item, SwireUsbEnabledOff);
        variable_item_set_current_value_text(item, swire_usb_enabled_text[SwireUsbEnabledOff]);
    }

    variable_item_list_set_selected_item(
        var_item_list, scene_manager_get_scene_state(app->scene_manager, SwireSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, SwireAppViewVarItemList);
}

bool swire_scene_start_on_event(void* context, SceneManagerEvent event) {
    SwireApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) {
        return false;
    }
    switch(event.event) {
    case SwireGuiEvenBitbangTest:
        light_rgb_set(0xff0000);
        furi_delay_ms(200);
        light_rgb_set(0xffff00);
        cmd_bitbang_test_simple();
        light_rgb_set(0x00ff00);
        break;
    case SwireGuiEventUsbEnabledOn:
        app_set_usb_enabled(app, true);
        app_set_blinker(app, 0x00ffff, 1000);
        break;
    case SwireGuiEventUsbEnabledbOff:
        app_set_usb_enabled(app, false);
        app_set_blinker(app, 0x000080, 2000);
        break;
    }
    return true;
}

void swire_scene_start_on_exit(void* context) {
    SwireApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
