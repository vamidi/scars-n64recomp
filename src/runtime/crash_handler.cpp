#include "crash_handler.h"
#include "scars_log.h"

#include <cstdio>
#include <exception>
#include <new>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <process.h>

#include <atomic>

namespace {

std::atomic<bool> handling{false};
bool symbols_initialised = false;

const char* exception_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:
        return "STACK_OVERFLOW";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "IN_PAGE_ERROR";
    default:
        return "EXCEPTION";
    }
}

void print_stack(CONTEXT* context, unsigned int skip_frames) {
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!symbols_initialised) {
        symbols_initialised = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
        if (!symbols_initialised) {
            scars::logf("crash: SymInitialize failed (%lu), addresses only", GetLastError());
        }
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    std::fprintf(stderr, "[scars] crash: call stack (innermost first):\n");

    for (unsigned int index = 0; index < 48; index++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            break;
        }

        const DWORD64 address = frame.AddrPC.Offset;
        const DWORD64 module_base = SymGetModuleBase64(process, address);

        char module_name[MAX_PATH] = "?";
        if (module_base != 0) {
            GetModuleFileNameA(reinterpret_cast<HMODULE>(module_base), module_name, MAX_PATH);
            const char* slash = std::strrchr(module_name, '\\');
            if (slash != nullptr) {
                std::memmove(module_name, slash + 1, std::strlen(slash + 1) + 1);
            }
        }

        char symbol_buffer[sizeof(SYMBOL_INFO) + 256]{};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;

        char line_info[256] = "";
        DWORD line_displacement = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);

        if (SymFromAddr(process, address, &displacement, symbol)) {
            if (SymGetLineFromAddr64(process, address, &line_displacement, &line)) {
                std::snprintf(line_info, sizeof(line_info), " (%s:%lu)", line.FileName,
                              static_cast<unsigned long>(line.LineNumber));
            }
            std::fprintf(stderr, "[scars]   #%02u %s!%s+0x%llX%s\n", index, module_name, symbol->Name,
                         static_cast<unsigned long long>(displacement), line_info);
        } else {
            const unsigned long long offset = module_base != 0 ? address - module_base : address;
            std::fprintf(stderr, "[scars]   #%02u %s+0x%llX\n", index, module_name, offset);
        }
    }
    std::fflush(stderr);
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* info) {
    // MSVC's C++ exception code. It reaches a vectored handler on every throw,
    // including the ones the runtime catches on purpose (thread termination).
    // Leave those alone; a genuinely uncaught one ends in std::terminate,
    // which the terminate hook reports below.
    if (info->ExceptionRecord->ExceptionCode == 0xE06D7363) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // 0x406D1388 is MS_VC_EXCEPTION, raised by driver/threading code purely to
    // tell an attached debugger the thread's name. It is handled and never a
    // fault; treat it as noise.
    if (info->ExceptionRecord->ExceptionCode == 0x406D1388) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (handling.exchange(true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    std::fprintf(stderr, "[scars] crash: %s 0x%08lX at 0x%p (thread %ld)\n", exception_name(record->ExceptionCode),
                 static_cast<unsigned long>(record->ExceptionCode), record->ExceptionAddress,
                 static_cast<long>(GetCurrentThreadId()));

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const char* access = record->ExceptionInformation[0] == 0 ? "read"
                            : record->ExceptionInformation[0] == 1 ? "write"
                                                                   : "execute";
        std::fprintf(stderr, "[scars] crash: %s of address 0x%p\n", access,
                     reinterpret_cast<void*>(record->ExceptionInformation[1]));
    }

    print_stack(info->ContextRecord, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

void on_terminate() {
    if (handling.exchange(true)) {
        std::abort();
    }
    scars::logf("crash: std::terminate");    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) {
            scars::logf("crash: std::terminate: %s", error.what());
        } catch (...) {
            scars::logf("crash: std::terminate: non-standard exception");
        }
    } else {
        scars::logf("crash: std::terminate without an active exception");
    }
    std::abort();
}

// Prints the stack for failures the vectored handler never sees: __fastfail
// (abort, __report_gsfailure, STATUS_STACK_BUFFER_OVERRUN) goes straight to the
// unhandled-exception filter.
LONG WINAPI on_unhandled(EXCEPTION_POINTERS* info) {
    if (handling.exchange(true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    std::fprintf(stderr, "[scars] fatal: %s 0x%08lX at 0x%p (thread %ld)\n", exception_name(record->ExceptionCode),
                 static_cast<unsigned long>(record->ExceptionCode), record->ExceptionAddress,
                 static_cast<long>(GetCurrentThreadId()));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const char* access = record->ExceptionInformation[0] == 0 ? "read"
                            : record->ExceptionInformation[0] == 1 ? "write"
                                                                   : "execute";
        std::fprintf(stderr, "[scars] fatal: %s of address 0x%p\n", access,
                     reinterpret_cast<void*>(record->ExceptionInformation[1]));
    }
    print_stack(info->ContextRecord, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

void on_invalid_parameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
                          unsigned int line, uintptr_t reserved) {
    (void) reserved;
    std::fprintf(stderr, "[scars] fatal: invalid CRT parameter: func=%ls file=%ls line=%u expr=%ls\n",
                 function != nullptr ? function : L"?", file != nullptr ? file : L"?", line,
                 expression != nullptr ? expression : L"?");
    std::fflush(stderr);
}

void on_purecall() {
    std::fprintf(stderr, "[scars] fatal: pure virtual function call\n");
    std::fflush(stderr);
}

void on_new_handler() {
    std::fprintf(stderr, "[scars] fatal: operator new failed (out of memory)\n");
    std::fflush(stderr);
    throw std::bad_alloc{};
}

} // namespace

namespace scars {

void install_crash_handler() {
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    AddVectoredExceptionHandler(1, &on_exception);
    SetUnhandledExceptionFilter(&on_unhandled);
    _set_invalid_parameter_handler(&on_invalid_parameter);
    _set_purecall_handler(&on_purecall);
    std::set_new_handler(&on_new_handler);
    std::set_terminate(&on_terminate);

    // By default abort() raises a fail-fast exception, which bypasses every
    // handler in this file. With the report-fault path disabled it prints a
    // message and exits with code 3 instead, so an abort is identifiable.
    _set_abort_behavior(0, 0);
}

} // namespace scars

#else

namespace scars {
void install_crash_handler() {}
} // namespace scars

#endif
