// Small logging helper shared by the runtime pieces.
//
// Everything goes to stderr so it interleaves correctly with the runtime's own
// diagnostics and survives a crash without flushing stdout first.

#ifndef SCARS_LOG_H
#define SCARS_LOG_H

namespace scars {

// printf-style, prefixed with "[scars] " and flushed.
void logf(const char* fmt, ...);

// Logs at most `limit` times under `label`, then goes quiet. For hot paths.
void log_throttled(const char* label, unsigned long long limit, const char* fmt, ...);

// Mirrors everything from logf/log_throttled into `path` as well as stderr.
// Used so a double-clicked exe leaves a diagnosable trail.
void log_open_file(const char* path);

// Waits for Enter before returning when stdin is a console, so a fatal startup
// error is readable in a window opened by Explorer. No-op under a pipe/CI.
void pause_before_exit();

} // namespace scars

#endif // SCARS_LOG_H
