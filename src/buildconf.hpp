#pragma once

#define SW_USB_USE_QUEUE                1
#define SW_USB_USE_POLLING_WORKAROUND   1 // works
#define SW_USB_USE_DOUBLESET_WORKAROUND 0 // does not work
#define SW_USB_USE_IRQDUMMY_WORKAROUND  0
#define SW_CHECKPPOINT_TRACES           0

#define SW_DEFAULT_BITRATE 240000
#define SW_IMAGE_LEN       144000 // 16

#if SW_CHECKPPOINT_TRACES
#define SW_DEBUG_TRACE(...) FURI_LOG_T(TAG, "checkpoint " __VA_ARGS__)
#else
#define SW_DEBUG_TRACE(...)
#endif

extern const char APP_VERSION[];
extern const char TAG[];
