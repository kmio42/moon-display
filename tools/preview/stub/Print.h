#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Print {
public:
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
    size_t write(const char* s) { return write((const uint8_t*) s, strlen(s)); }
    size_t print(const char* s) { return write(s); }
    virtual ~Print() {}
};
