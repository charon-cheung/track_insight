#pragma once
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

namespace track_insight {
// Private report-layer transaction. A lock file is permanent; its flock is not.
class PublicationTransaction {
public:
  using Entry=std::pair<std::filesystem::path,std::filesystem::path>; // staged source, target
  PublicationTransaction(const std::filesystem::path& root,bool archive);
  ~PublicationTransaction();
  PublicationTransaction(const PublicationTransaction&)=delete;
  PublicationTransaction& operator=(const PublicationTransaction&)=delete;
  std::filesystem::path stage();
  void publish(const std::vector<Entry>& entries,const std::function<void()>& before_commit={});
private:
  void recover();
  std::filesystem::path root_,transaction_;
  bool archive_;
  int lock_fd_{-1};
};
}
