#pragma once
#include "track_insight/domain.hpp"

namespace track_insight {
// Validate in recording order, before timestamp sorting or frame conversion.
void isolateVelocityClocks(std::vector<Velocity>& values,const Config& config,
                           std::vector<Diagnostic>& diagnostics);
}
