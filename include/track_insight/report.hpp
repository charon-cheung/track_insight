#pragma once
#include "track_insight/domain.hpp"
#include <nlohmann/json.hpp>
#include <functional>
namespace track_insight {
using Json=nlohmann::json;
Json makeOverview(const Json& analysis);
Json makeAnalysis(const RunData& data,const Evaluation& evaluation,const Config& config,
                  const DisplayClock& clock);
void validateReportOutput(const std::filesystem::path& output,bool replace);
int renderReport(Json analysis,const std::filesystem::path& output,
                 const std::filesystem::path& resources,bool replace=false,
                 const std::function<void(double)>& progress={},const std::string& archive_name={});
std::vector<std::string> writeFigures(const Json& analysis,const Json& style,const std::filesystem::path& output,
                                     std::vector<std::string>& failures,const std::function<void(double)>& progress={});
std::string readText(const std::filesystem::path& path);
void writeText(const std::filesystem::path& path,const std::string& text);
DisplayClock readClock(const Json& analysis);
}
