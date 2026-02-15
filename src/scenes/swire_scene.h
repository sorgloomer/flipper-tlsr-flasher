#pragma once

#include <gui/scene_manager.h>

typedef enum SwireScene {
    SwireSceneStart = 0,
    SwireSceneCount
} SwireScene;

extern const SceneManagerHandlers swire_app_scene_handlers;
