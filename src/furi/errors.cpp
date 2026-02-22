#include <furi.h>

bool furi_status_is_error(FuriStatus status) {
    return ((FuriFlag)status & FuriFlagError) != 0;
}
