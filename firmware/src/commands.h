#pragma once
#include <ArduinoJson.h>

// Single entry point for every interface. Request/response follow the v1 schema in PLAN.md.
namespace commands {

void handle(JsonObjectConst req, JsonDocument& resp);

// Translates a human shell line ("rgb 255 0 0", "effect rainbow") into a v1 request.
// Returns false and fills `err` if the line is not understood.
bool parseText(const char* line, JsonDocument& req, String& err);

}  // namespace commands
