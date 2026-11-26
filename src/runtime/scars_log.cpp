#include "scars_log.h"

#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#include <io.h>
#endif

namespace scars {
namespace {

std::FILE* log_file = nullptr;

void write_mirror(const char* text, int length) {
    if (log_file != nullptr && length > 0) {
        std::fwrite(text, 1, static_cast<size_t>(length), log_file);
        std::fflush(log_file);
    }
}

} // namespace

void log_open_file(const char* path) {
    if (log_file != nullptr) {
        return;
    }
    log_file = std::fopen(path, "w");
}

void pause_before_exit() {
#ifdef _WIN32
    if (_isatty(_fileno(stdin))) {
        std::fputs("press Enter to close...", stderr);
        std::fflush(stderr);
        int ch;
        while ((ch = std::getchar()) != '\n' && ch != EOF) {
        }
    }
#endif
}

void logf(const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    const int length = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::fputs("[scars] ", stderr);
    std::fputs(buffer, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);

    write_mirror("[scars] ", 8);
    write_mirror(buffer, length);
    write_mirror("\n", 1);
}

void log_throttled(const char* label, unsigned long long limit, const char* fmt, ...) {
    static unsigned long long counters[64];
    static const char* labels[64];
    static int used = 0;

    unsigned long long* counter = nullptr;
    for (int i = 0; i < used; i++) {
        if (labels[i] == label) {
            counter = &counters[i];
            break;
        }
    }
    if (counter == nullptr) {
        if (used < 64) {
            labels[used] = label;
            counter = &counters[used];
            used++;
        } else {
            return;
        }
    }

    if (*counter >= limit) {
        return;
    }
    (*counter)++;

    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    const int length = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::fputs("[scars] ", stderr);
    std::fputs(buffer, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);

    write_mirror("[scars] ", 8);
    write_mirror(buffer, length);
    write_mirror("\n", 1);
}

} // namespace scars
