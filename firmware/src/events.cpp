#include "events.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace events {
namespace {
QueueHandle_t queue = nullptr;
}

// Safe from any task (Wi-Fi events arrive on the system event task); printed from loop().
void emit(JsonDocument& ev) {
  if (!queue) queue = xQueueCreate(16, sizeof(char*));
  size_t len = measureJson(ev) + 1;
  char* buf = (char*)malloc(len);
  if (!buf) return;
  serializeJson(ev, buf, len);
  if (xQueueSend(queue, &buf, 0) != pdTRUE) free(buf);
}

void flush(Print& out) {
  if (!queue) return;
  char* buf;
  while (xQueueReceive(queue, &buf, 0) == pdTRUE) {
    out.println(buf);
    free(buf);
  }
}

}  // namespace events
