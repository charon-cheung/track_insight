#pragma once
#include "track_insight/report.hpp"

namespace track_insight {
struct ArchiveResult {
  int code;
  std::filesystem::path report,home;
};
ArchiveResult renderArchive(Json analysis,const std::filesystem::path& root,
  const std::filesystem::path& resources,bool replace=false,
  const std::optional<std::string>& name={},const std::optional<std::string>& note={},
  const std::function<void(double)>& progress={});
}
