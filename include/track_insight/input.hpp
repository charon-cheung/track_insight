#pragma once
#include "track_insight/domain.hpp"
#include <memory>
#include <functional>

namespace track_insight {
class InputPackage {
public:
  InputPackage(const std::filesystem::path& input, const Config& config);
  ~InputPackage();
  InputPackage(const InputPackage&) = delete;
  InputPackage& operator=(const InputPackage&) = delete;
  const std::filesystem::path& bagPath() const { return bag_; }
private:
  std::filesystem::path bag_, temporary_;
};
std::string hashInput(const std::filesystem::path& path);
std::string hashText(const std::string& text);
RunData readBag(const std::filesystem::path& input, const std::filesystem::path& log, const Config& config,
                const std::function<void(double)>& progress={});
}
