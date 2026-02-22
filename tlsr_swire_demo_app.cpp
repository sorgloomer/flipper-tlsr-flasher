#include <furi.h>

#include "src/main.hpp"
#include "src/utils/externc.hpp"

EXTERN_C int tlsr_swire_demo_app(void* p) {
    UNUSED(p);
    swire_main();
    return 0;
}
