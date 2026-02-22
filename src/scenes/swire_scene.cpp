#include <gui/scene_manager.h>

#include "swire_scene.hpp"
#include "swire_scene_start.hpp"

const AppSceneOnEnterCallback swire_app_scene_on_enter_handlers[] = {swire_scene_start_on_enter};
const AppSceneOnExitCallback swire_app_scene_on_exit_handlers[] = {swire_scene_start_on_exit};
const AppSceneOnEventCallback swire_app_scene_on_event_handlers[] = {swire_scene_start_on_event};

const SceneManagerHandlers swire_app_scene_handlers = {
    .on_enter_handlers = swire_app_scene_on_enter_handlers,
    .on_event_handlers = swire_app_scene_on_event_handlers,
    .on_exit_handlers = swire_app_scene_on_exit_handlers,
    .scene_num = 1,
};
