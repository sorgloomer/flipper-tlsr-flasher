
#include <stdint.h>
#include <chrono>

namespace furi {

typedef std::chrono::duration<uint32_t, std::milli> milli;
typedef std::chrono::duration<uint32_t, std::micro> micro;
typedef std::chrono::duration<uint32_t, std::nano> nano;

}
