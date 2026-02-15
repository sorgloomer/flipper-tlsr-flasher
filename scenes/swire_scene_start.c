#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_usb.h>
#include <dolphin/dolphin.h>
#include <string.h>

#include "scenes/swire_scene.h"
#include "scenes/swire_scene_start.h"
#include "scenes/swire_gui_event.h"
#include "src/app/app.h"
#include "src/utils/light_rgb.h"
#include "src/commands/commands_bitbang.h"

#define _BITRATE_STR_BUFFER_SIZE 16

const uint32_t bitrates[] = {
    4,
    60,
    1200,
    24000,
    30000,
    48000,
    60000,
    96000,
    120000,
    180000,
    240000,
    360000,
    480000,
    720000,
    960000,
};
const uint32_t bitrates__count = sizeof(bitrates) / sizeof(bitrates[0]);

enum SwireStartItem {
    SwireStartItemBitbangTest,
    SwireStartItemBitrate,
    SwireStartItemGpioManual,
    SwireStartItemUsbOnOff,
    SwireStartItem__count,
};

enum SwireUsbEnabled {
    SwireUsbEnabledOff,
    SwireUsbEnabledOn,
    SwireUsbEnabled__count,
};

static void bitrate_update_text(VariableItem* item);
static void bitrate_change_callback(VariableItem* item);
static int32_t bitrate_find_index(uint32_t value, int32_t default_index);

const char* const swire_usb_enabled_text[SwireUsbEnabled__count] = {
    "OFF",
    "ON",
};

static void scene_start_var_list_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    SwireApp* app = context;
    switch(index) {
    case SwireStartItemBitbangTest:
        view_dispatcher_send_custom_event(app->view_dispatcher, SwireGuiEvenBitbangTest);
        break;
    }
}

static void usb_enabled_change_callback(VariableItem* item) {
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

static void bitrate_change_callback(VariableItem* item) {
    SwireApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    bitrate_update_text(item);
    app->config->bitrate = bitrates[index];
}

static void bitrate_update_text(VariableItem* item) {
    int index = variable_item_get_current_value_index(item);
    furi_assert(index < (int)bitrates__count);
    char bitratebuffer[_BITRATE_STR_BUFFER_SIZE];
    snprintf(bitratebuffer, _BITRATE_STR_BUFFER_SIZE, "%ld", bitrates[index]);
    variable_item_set_current_value_text(item, bitratebuffer);
}

void swire_scene_start_on_enter(void* context) {
    SwireApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;

    variable_item_list_set_enter_callback(var_item_list, scene_start_var_list_enter_callback, app);

    for(int i = 0; i < SwireStartItem__count; i++) {
        switch(i) {
        case SwireStartItemBitbangTest:
            variable_item_list_add(var_item_list, "BitBang Test", 0, NULL, NULL);
            break;
        case SwireStartItemGpioManual:
            variable_item_list_add(var_item_list, "GPIO Manual Control", 0, NULL, NULL);
            break;
        case SwireStartItemBitrate: {
            VariableItem* item = variable_item_list_add(
                var_item_list, "bitrate", bitrates__count, bitrate_change_callback, app);
            int32_t index = bitrate_find_index(app->config->bitrate, 9);
            variable_item_set_current_value_index(item, index);
            bitrate_update_text(item);
        } break;

        case SwireStartItemUsbOnOff: {
            VariableItem* item = variable_item_list_add(
                var_item_list, "USB", SwireUsbEnabled__count, usb_enabled_change_callback, app);

            if(app->usb != NULL) {
                variable_item_set_current_value_index(item, SwireUsbEnabledOn);
                variable_item_set_current_value_text(
                    item, swire_usb_enabled_text[SwireUsbEnabledOn]);
            } else {
                variable_item_set_current_value_index(item, SwireUsbEnabledOff);
                variable_item_set_current_value_text(
                    item, swire_usb_enabled_text[SwireUsbEnabledOff]);
            }
        } break;
        }
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
        cmd_bitbang_test_simple(app);
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

static int32_t bitrate_find_index(uint32_t value, int32_t default_index) {
    furi_assert(default_index < (int32_t)bitrates__count, "bitrate_find_index");
    for(int32_t i = 0; i < (int32_t)bitrates__count; i++) {
        if(bitrates[i] == value) {
            return i;
        }
    }
    return default_index;
}
