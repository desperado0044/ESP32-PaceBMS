#include "BmsActivity.h"
#include <Arduino.h>

namespace BmsActivity {

namespace {
volatile unsigned long lastRequest = 0;
volatile unsigned long lastResponse = 0;
}  // namespace

void markRequestSent() { lastRequest = millis(); }
void markResponseReceived() { lastResponse = millis(); }

unsigned long lastRequestMs() { return lastRequest; }
unsigned long lastResponseMs() { return lastResponse; }

}  // namespace BmsActivity
