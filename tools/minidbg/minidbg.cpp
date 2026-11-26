// minidbg: a minimal Windows debugger that prints symbolized stacks.
//
// The recompiled runtime can die in ways an in-process handler never sees:
// fail-fast exceptions (0xC0000409), stack-cookie failures and CRT invalid
// parameters all bypass VectoredExceptionHandler and the unhandled-exception
// filter. This tool launches the target under DEBUG_ONLY_THIS_PROCESS, and
// walks the faulting thread's stack with DbgHelp at the debug event, before
// the process is allowed to die.
//
// Usage:
//   minidbg <path-to-exe> [args...]
//
// It forwards the child's stdout/stderr untouched (the child inherits our
// console), so the runtime's own log appears interleaved with stack dumps.
//
// Windows only. Dev tool; not part of the shipped runtime.

#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <Psapi.h>

namespace {

bool symbols_ready = false;
HANDLE child_process = nullptr;

std::string directory_of(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return ".";
    }
    return path.substr(0, slash);
}

struct Breakpoint {
    std::string name;
    unsigned char original_byte;
};
std::map<DWORD64, Breakpoint> breakpoints;
std::unordered_map<DWORD, HANDLE> threads;

const char* kInterestingFunctions[] = {
    "exit", "_exit", "_Exit", "quick_exit", "abort", "terminate", "_invoke_watson", "_report_gsfailure",
    "do_break", "switch_error", "unload_overlays",
};

void ensure_symbols() {
    if (symbols_ready) {
        return;
    }
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_INCLUDE_32BIT_MODULES);
    char exe_path[MAX_PATH]{};
    std::string search = ".";
    if (GetModuleFileNameExA(child_process, nullptr, exe_path, MAX_PATH) > 0) {
        search = directory_of(std::string(exe_path));
    }
    symbols_ready = SymInitialize(child_process, search.c_str(), TRUE) != FALSE;
}

void install_breakpoints() {
    ensure_symbols();
    if (!symbols_ready) {
        return;
    }
    for (const char* name : kInterestingFunctions) {
        char buffer[sizeof(SYMBOL_INFO) + 256]{};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        if (!SymFromName(child_process, name, symbol)) {
            continue;
        }
        const DWORD64 address = symbol->Address;
        unsigned char original = 0;
        SIZE_T read = 0;
        if (!ReadProcessMemory(child_process, reinterpret_cast<LPCVOID>(address), &original, 1, &read) || read != 1) {
            continue;
        }
        unsigned char int3 = 0xCC;
        SIZE_T written = 0;
        if (!WriteProcessMemory(child_process, reinterpret_cast<LPVOID>(address), &int3, 1, &written) || written != 1) {
            continue;
        }
        breakpoints[address] = Breakpoint{name, original};
        std::fprintf(stderr, "[minidbg] breakpoint set on %s (0x%llX)\n", name, static_cast<unsigned long long>(address));
    }
    FlushInstructionCache(child_process, nullptr, 0);
}


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
    case EXCEPTION_PRIV_INSTRUCTION:
        return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "IN_PAGE_ERROR";
    case EXCEPTION_BREAKPOINT:
        return "BREAKPOINT";
    case 0xE06D7363:
        return "CPP_EXCEPTION";
    case 0xC0000409:
        return "STACK_BUFFER_OVERRUN/FASTFAIL";
    case 0xC0000374:
        return "HEAP_CORRUPTION";
    default:
        return "EXCEPTION";
    }
}

void print_stack(HANDLE thread) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_ALL;
    if (!GetThreadContext(thread, &context)) {
        std::fprintf(stderr, "[minidbg] GetThreadContext failed (%lu)\n", GetLastError());
        return;
    }

    ensure_symbols();

    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    std::fprintf(stderr, "[minidbg] stack:\n");
    for (unsigned int index = 0; index < 48; index++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, child_process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            break;
        }

        const DWORD64 address = frame.AddrPC.Offset;
        const DWORD64 module_base = SymGetModuleBase64(child_process, address);

        char module_name[MAX_PATH] = "?";
        if (module_base != 0) {
            char module_path[MAX_PATH]{};
            if (GetModuleFileNameExA(child_process, reinterpret_cast<HMODULE>(module_base), module_path, MAX_PATH) > 0) {
                const char* slash = std::strrchr(module_path, '\\');
                std::snprintf(module_name, sizeof(module_name), "%s", slash != nullptr ? slash + 1 : module_path);
            }
        }

        char symbol_buffer[sizeof(SYMBOL_INFO) + 256]{};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;

        char line_info[320] = "";
        DWORD line_displacement = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        if (SymGetLineFromAddr64(child_process, address, &line_displacement, &line)) {
            std::snprintf(line_info, sizeof(line_info), " (%s:%lu)", line.FileName,
                          static_cast<unsigned long>(line.LineNumber));
        }

        if (SymFromAddr(child_process, address, &displacement, symbol)) {
            std::fprintf(stderr, "[minidbg]   #%02u %s!%s+0x%llX%s\n", index, module_name, symbol->Name,
                         static_cast<unsigned long long>(displacement), line_info);
        } else {
            const unsigned long long offset = module_base != 0 ? address - module_base : address;
            std::fprintf(stderr, "[minidbg]   #%02u %s+0x%llX%s\n", index, module_name, offset, line_info);
        }
    }
    std::fflush(stderr);
}

void dump_all_threads(const char* reason) {
    std::fprintf(stderr, "[minidbg] ---- all-thread dump (%s, %zu threads) ----\n", reason, threads.size());
    for (const auto& [thread_id, thread_handle] : threads) {
        wchar_t description[128] = L"?";
        PWSTR description_ptr = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread_handle, &description_ptr)) && description_ptr != nullptr) {
            std::swprintf(description, 128, L"%ls", description_ptr);
            LocalFree(description_ptr);
        }
        std::fprintf(stderr, "[minidbg] thread %lu (%ls):\n", static_cast<unsigned long>(thread_id), description);
        // A running thread cannot be walked reliably; park it for the walk.
        SuspendThread(thread_handle);
        print_stack(thread_handle);
        ResumeThread(thread_handle);
    }
    std::fflush(stderr);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: minidbg <exe> [args...]\n");
        return 2;
    }

    std::string command_line;
    unsigned dump_every_ms = 0;
    bool have_child = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--dump-every") == 0 && i + 1 < argc) {
            dump_every_ms = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10)) * 1000u;
            continue;
        }
        if (have_child) {
            command_line += ' ';
        }
        have_child = true;
        if (std::strchr(argv[i], ' ') != nullptr) {
            command_line += '"';
            command_line += argv[i];
            command_line += '"';
        } else {
            command_line += argv[i];
        }
    }
    if (!have_child) {
        std::fprintf(stderr, "usage: minidbg [--dump-every <seconds>] <exe> [args...]\n");
        return 2;
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    std::vector<char> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back('\0');

    if (!CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                        DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "[minidbg] CreateProcess failed (%lu)\n", GetLastError());
        return 1;
    }

    child_process = process.hProcess;

    bool initial_breakpoint_seen = false;
    bool reported = false;

    const ULONGLONG started_at = GetTickCount64();
    ULONGLONG last_dump = started_at;
    DEBUG_EVENT event{};
    while (true) {
        if (WaitForDebugEvent(&event, dump_every_ms > 0 ? 250 : INFINITE) == FALSE) {
            if (dump_every_ms > 0 && (GetTickCount64() - last_dump) >= dump_every_ms) {
                last_dump = GetTickCount64();
                char reason[64];
                std::snprintf(reason, sizeof(reason), "%llus", static_cast<unsigned long long>((last_dump - started_at) / 1000));
                dump_all_threads(reason);
            }
            continue;
        }

        DWORD continue_status = DBG_CONTINUE;

        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (event.u.CreateProcessInfo.hFile != nullptr) {
                CloseHandle(event.u.CreateProcessInfo.hFile);
            }
            break;

        case CREATE_THREAD_DEBUG_EVENT:
            if (event.u.CreateThread.hThread != nullptr) {
                threads[event.dwThreadId] = event.u.CreateThread.hThread;
            }
            break;

        case EXIT_THREAD_DEBUG_EVENT: {
            auto found = threads.find(event.dwThreadId);
            if (found != threads.end()) {
                CloseHandle(found->second);
                threads.erase(found);
            }
            break;
        }

        case EXCEPTION_DEBUG_EVENT: {
            const EXCEPTION_RECORD& record = event.u.Exception.ExceptionRecord;

            if (record.ExceptionCode == EXCEPTION_BREAKPOINT && !initial_breakpoint_seen) {
                initial_breakpoint_seen = true;
                install_breakpoints();
                break;
            }

            // A breakpoint we planted on an interesting function.
            if (record.ExceptionCode == EXCEPTION_BREAKPOINT) {
                const DWORD64 hit_address = reinterpret_cast<DWORD64>(record.ExceptionAddress);
                auto hit = breakpoints.find(hit_address);
                if (hit != breakpoints.end()) {
                    std::fprintf(stderr, "[minidbg] hit: %s (thread %lu)\n", hit->second.name.c_str(),
                                 static_cast<unsigned long>(event.dwThreadId));
                    auto found = threads.find(event.dwThreadId);
                    if (found != threads.end()) {
                        print_stack(found->second);
                        // Restore the byte and rewind RIP past the int3 so the
                        // function still runs and we see the real behaviour.
                        CONTEXT context{};
                        context.ContextFlags = CONTEXT_ALL;
                        if (GetThreadContext(found->second, &context)) {
                            SIZE_T written = 0;
                            WriteProcessMemory(child_process, reinterpret_cast<LPVOID>(hit_address),
                                               &hit->second.original_byte, 1, &written);
                            FlushInstructionCache(child_process, reinterpret_cast<LPCVOID>(hit_address), 1);
                            context.Rip = hit_address;
                            SetThreadContext(found->second, &context);
                        }
                    }
                    breakpoints.erase(hit);
                    continue_status = DBG_CONTINUE;
                    break;
                }
            }

            // C++ exceptions are thrown and caught all over the runtime.
            if (record.ExceptionCode == 0xE06D7363) {
                continue_status = DBG_EXCEPTION_NOT_HANDLED;
                break;
            }

            const bool second_chance = event.u.Exception.dwFirstChance == FALSE;
            const bool fatal_code = record.ExceptionCode == 0xC0000409 || record.ExceptionCode == 0xC0000374;

            if (second_chance || fatal_code) {
                const char* access = "";
                if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
                    access = record.ExceptionInformation[0] == 0   ? " (read)"
                             : record.ExceptionInformation[0] == 1 ? " (write)"
                                                                   : " (execute)";
                    std::fprintf(stderr, "[minidbg] %s 0x%08lX at 0x%p%s of 0x%p (thread %lu, %s)\n",
                                 exception_name(record.ExceptionCode), static_cast<unsigned long>(record.ExceptionCode),
                                 record.ExceptionAddress, access,
                                 reinterpret_cast<void*>(record.ExceptionInformation[1]),
                                 static_cast<unsigned long>(event.dwThreadId),
                                 second_chance ? "second chance" : "first chance");
                } else {
                    std::fprintf(stderr, "[minidbg] %s 0x%08lX at 0x%p (thread %lu, %s)\n",
                                 exception_name(record.ExceptionCode), static_cast<unsigned long>(record.ExceptionCode),
                                 record.ExceptionAddress, static_cast<unsigned long>(event.dwThreadId),
                                 second_chance ? "second chance" : "first chance");
                }

                auto found = threads.find(event.dwThreadId);
                if (found != threads.end()) {
                    print_stack(found->second);
                }
                reported = true;

                if (fatal_code) {
                    // A guest deadlock or fail-fast is rarely local: dump every
                    // other thread too so the cause is visible in one run.
                    for (const auto& [thread_id, thread_handle] : threads) {
                        if (thread_id == event.dwThreadId) {
                            continue;
                        }
                        wchar_t description[128] = L"?";
                        PWSTR description_ptr = nullptr;
                        if (SUCCEEDED(GetThreadDescription(thread_handle, &description_ptr)) && description_ptr != nullptr) {
                            std::swprintf(description, 128, L"%ls", description_ptr);
                            LocalFree(description_ptr);
                        }
                        std::fprintf(stderr, "[minidbg] other thread %lu (%ls):\n", static_cast<unsigned long>(thread_id), description);
                        print_stack(thread_handle);
                    }
                }
            }

            continue_status = DBG_EXCEPTION_NOT_HANDLED;
            break;
        }

        case EXIT_PROCESS_DEBUG_EVENT:
            std::fprintf(stderr, "[minidbg] process exited with code 0x%08lX%s\n",
                         static_cast<unsigned long>(event.u.ExitProcess.dwExitCode),
                         reported ? " (after a reported fault)" : "");
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
            if (symbols_ready) {
                SymCleanup(child_process);
            }
            return 0;

        default:
            break;
        }

        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continue_status);
    }

    return 1;
}
