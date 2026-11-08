#pragma once
#include <stddef.h>
#include <stdbool.h>
// Bound nesting before cJSON recursion, including hostile small-but-deep bodies.
static inline bool box_json_safe(const char *data, size_t length)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];
        if (quoted) {
            if (c < 32) return false;
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; depth--; }
        else if (c < 32 && c != '\t' && c != '\r' && c != '\n') return false;
        else if (c == '-' || (c >= '0' && c <= '9')) {
            // cJSON accepts some non-JSON number spellings; enforce JSON grammar.
            size_t n = i;
            if (data[n] == '-') n++;
            if (n >= length || data[n] < '0' || data[n] > '9') return false;
            if (data[n] == '0') {
                n++;
                if (n < length && data[n] >= '0' && data[n] <= '9') return false;
            } else while (n < length && data[n] >= '0' && data[n] <= '9') n++;
            if (n < length && data[n] == '.') {
                n++;
                if (n >= length || data[n] < '0' || data[n] > '9') return false;
                while (n < length && data[n] >= '0' && data[n] <= '9') n++;
            }
            if (n < length && (data[n] == 'e' || data[n] == 'E')) {
                n++;
                if (n < length && (data[n] == '+' || data[n] == '-')) n++;
                if (n >= length || data[n] < '0' || data[n] > '9') return false;
                while (n < length && data[n] >= '0' && data[n] <= '9') n++;
            }
            i = n - 1;
        }
    }
    return !quoted && !depth;
}
