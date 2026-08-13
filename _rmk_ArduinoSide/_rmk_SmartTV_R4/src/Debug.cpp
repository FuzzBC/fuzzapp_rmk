#include "Debug.h"
#include "Telnet.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace Debug {

/** Writes one line to Serial only if a host is actually connected and
    reading (checked via `if (Serial)`) - this board's Serial is native
    USB (TinyUSB), and writing with nothing enumerated/reading can block.
    Same guard the original firmware used for its Serial output on this
    same hardware, which is why it's safe for termMsgLog() (AppLink.cpp)
    to mirror every INF-and-above call here unconditionally instead of
    only the 2 boot-banner lines this module started with. */
void printSerial(const char *line) {
    if (Serial) Serial.print(line);
}

/** printSerial() + mirrors to the Telnet client (no-op there while
    disabled/unconnected - see Telnet.cpp). Does not append a newline -
    callers pass one if they want one, same as the original PRNT::_print(). */
void print(const char *line) {
    printSerial(line);
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
