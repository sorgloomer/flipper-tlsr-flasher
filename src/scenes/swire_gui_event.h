#pragma once

typedef enum {
    SwireGuiEventBitbangRead = 1,
    SwireGuiEventBitbangTest,
    SwireGuiEventFreqTest,
    SwireGuiEventErrorBack,
    SwireGuiEventUsbEnabledOn,
    SwireGuiEventUsbEnabledbOff
} SwireGuiEvent;
#define SwireGuiEvent__count 5
