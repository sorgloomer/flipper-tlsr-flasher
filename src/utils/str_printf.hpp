#pragma once

#include <string>
std::string str_vprintf(const char* format, va_list args);
void str_vprintf(std::string& str, const char* format, va_list args);

std::string str_printf(const char* format, ...);
void str_printf(std::string& str, const char* format, ...);
