#pragma once

#include <stdbool.h>
#include <gui/scene_manager.h>

enum SwireStartItem {
    SwireStartItemBitbangTest,
    SwireStartItemFreqTest,
    SwireStartItemBitrate,
    SwireStartItemGpioManual,
    SwireStartItemUsbOnOff,
    SwireStartItem__count,
};

bool swire_scene_start_on_event(void* context, SceneManagerEvent event);
void swire_scene_start_on_enter(void* context);
void swire_scene_start_on_exit(void* context);
