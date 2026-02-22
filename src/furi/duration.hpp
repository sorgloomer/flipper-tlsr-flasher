
#include <stdint.h>
#include <chrono>

namespace furi {

typedef std::chrono::duration<uint32_t, std::milli> u32ms;
typedef std::chrono::duration<uint32_t, std::micro> u32us;
typedef std::chrono::duration<uint32_t, std::nano> u32ns;

}
