#include "Debug.h"
#include "Telnet.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace Debug {

/** Writes one line as-is to Serial and mirrors it to the Telnet client
    (no-op there while disabled/unconnected - see Telnet.cpp). Does not
    append a newline - callers pass one if they want one, same as the
    original PRNT::_print(). */
void print(const char *line) {
    Serial.print(line);
    TELNET::Mirror(line);
}

/** printf-style helper, newline-terminated. Truncates silently past 160
    chars - every real call site in this codebase is well under that. */
void log(const char *format, ...) {
    char buf[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    size_t len = strlen(buf);
    if (len < sizeof(buf) - 1) {
        buf[len] = '\n';
        buf[len + 1] = '\0';
    }
    print(buf);
}

} // namespace Debug
