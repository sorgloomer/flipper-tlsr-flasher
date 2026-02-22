
#include <string>
#include <cstdarg>
#include "str_printf.hpp"

std::string str_vprintf(const char* format, va_list args) {
    int size = std::vsnprintf(nullptr, 0, format, args);
    std::string s(size, '\0');
    std::vsnprintf((char*)s.c_str(), size + 1, format, args);
    return s;
}

void str_vprintf(std::string& s, const char* format, va_list args) {
    int size = std::vsnprintf(nullptr, 0, format, args);
    if(size < 0) {
        return;
    }
    s.resize(size, '\0');
    std::vsnprintf((char*)s.c_str(), size + 1, format, args);
}
std::string str_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    auto result = str_vprintf(format, args);
    va_end(args);
    return result;
}
void str_printf(std::string& str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    str_vprintf(str, format, args);
    va_end(args);
}
