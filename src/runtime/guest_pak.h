// Controller Pak storage (see guest_pak.cpp).

#ifndef SCARS_GUEST_PAK_H
#define SCARS_GUEST_PAK_H

namespace scars {

// True when the pak is reported present (SCARS_CONTROLLER_PAK=1).
bool guest_pak_enabled();

// Writes the in-memory pak image back to disk if it changed. Writes flush
// immediately as well; this exists for a future periodic/idle flush.
void guest_pak_flush();

} // namespace scars

#endif // SCARS_GUEST_PAK_H
