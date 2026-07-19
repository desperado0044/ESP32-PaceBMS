#pragma once
#include "BmsData.h"

// Thread-safe holder for the latest BMS snapshot: the network task (Core 0) writes it after every
// successful poll, the display loop (Core 1, default Arduino loop()) and the async web server's
// request handlers (their own AsyncTCP task context) read it. All access goes through get()/set()
// - a mutex-protected full copy - so a String field is never read while another task is mutating
// it (a real crash risk, not just a theoretical one).
namespace SnapshotStore {

void begin();

PaceBmsSnapshot get();
void set(const PaceBmsSnapshot& snapshot);

}  // namespace SnapshotStore
