#pragma once

namespace WebUiServer {

// Starts the async web server. Reads the current data via SnapshotStore::get() on every request
// (a thread-safe copy) rather than holding a pointer into shared mutable state - request handlers
// run in the AsyncTCP task's own context, not the caller's.
void begin();

}  // namespace WebUiServer
