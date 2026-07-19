#pragma once

namespace WebUiServer {

// Starts the async web server. Reads the current data via SnapshotStore::get() on every request
// (a thread-safe copy) rather than holding a pointer into shared mutable state - request handlers
// run in the AsyncTCP task's own context, not the caller's. Also sets up ElegantOTA under /update.
void begin();

// Call every loop() tick (from NetworkTask, once begin() has run) - pumps ElegantOTA so a
// completed upload is detected and the device actually reboots into it.
void loop();

}  // namespace WebUiServer
