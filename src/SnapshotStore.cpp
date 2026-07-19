#include "SnapshotStore.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace SnapshotStore {

namespace {
PaceBmsSnapshot storedSnapshot;
SemaphoreHandle_t mutex = nullptr;
}  // namespace

void begin() { mutex = xSemaphoreCreateMutex(); }

PaceBmsSnapshot get() {
    PaceBmsSnapshot copy;
    xSemaphoreTake(mutex, portMAX_DELAY);
    copy = storedSnapshot;
    xSemaphoreGive(mutex);
    return copy;
}

void set(const PaceBmsSnapshot& snapshot) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    storedSnapshot = snapshot;
    xSemaphoreGive(mutex);
}

}  // namespace SnapshotStore
