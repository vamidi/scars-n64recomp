// Crash reporting for the runtime harness.
//
// A static recompilation crashes in ways a debugger makes obvious and a log
// does not: a wild guest pointer, a null section table, a stack that walked off
// the guest's 4 MiB. Printing a symbolized stack at the fault site turns "exit
// code 0xC0000005 after 80 ms" into a file and line, which is the difference
// between a five-minute fix and an afternoon.
//
// Windows-only for now (DbgHelp). The handler never changes control flow: it
// prints and lets the process die exactly as it would have.

#ifndef SCARS_CRASH_HANDLER_H
#define SCARS_CRASH_HANDLER_H

namespace scars {

// Installs the vectored exception handler and std::terminate hook. Idempotent.
void install_crash_handler();

} // namespace scars

#endif // SCARS_CRASH_HANDLER_H
