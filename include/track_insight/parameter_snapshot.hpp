#pragma once
#include "track_insight/report.hpp"

namespace track_insight {
Json readPidParameters(const std::filesystem::path& file);
void attachPidParameterBaseline(Json& analysis,const Json& runs);
}
