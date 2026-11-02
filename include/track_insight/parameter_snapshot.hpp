#pragma once
#include "track_insight/report.hpp"

namespace track_insight {
Json readPidParameters(const Json& analysis);
void updatePidParameters(Json& analysis);
void attachPidParameterBaseline(Json& analysis,const Json& runs);
}
