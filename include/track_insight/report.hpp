#pragma once
#include "track_insight/domain.hpp"
#include <nlohmann/json.hpp>
namespace track_insight {
using Json=nlohmann::json;
Json makeAnalysis(const RunData& data,const Evaluation& evaluation,const Config& config,
                  const DisplayClock& clock);
int renderReport(Json analysis,const std::filesystem::path& output,
                 const std::filesystem::path& resources,bool replace=false);
std::vector<std::string> writeFigures(const Json& analysis,const std::filesystem::path& output,
                                     std::vector<std::string>& failures);
std::string readText(const std::filesystem::path& path);
void writeText(const std::filesystem::path& path,const std::string& text);
DisplayClock readClock(const Json& analysis);
}
