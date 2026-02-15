#pragma once

#define SW_USB_USE_QUEUE                1
#define SW_USB_USE_POLLING_WORKAROUND   1 // works
#define SW_USB_USE_DOUBLESET_WORKAROUND 0 // does not work
#define SW_USB_USE_IRQDUMMY_WORKAROUND  0

#define SW_DEFAULT_BITRATE 240000
#define SW_IMAGE_LEN       144000 // 16

extern const char APP_VERSION[];
