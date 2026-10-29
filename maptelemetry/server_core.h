#pragma once
#include "../src/game/map_telemetry.h"
#include <string>

namespace mapserver {
std::string FullJson(const maptelemetry::Snapshot& snapshot,bool connected);
std::string DeltaJson(const maptelemetry::Snapshot& before,
                      const maptelemetry::Snapshot& after);
}
