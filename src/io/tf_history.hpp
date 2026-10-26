#pragma once
#include "track_insight/domain.hpp"
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <tf2/buffer_core.hpp>
#include <memory>
#include <set>

namespace track_insight {
class TfHistory {
public:
  TfHistory(const Config& config, TimestampNs duration);
  void add(const geometry_msgs::msg::TransformStamped& transform, bool is_static, int epoch, const Stamp& source = {});
  void finish();
  std::vector<PoseSample> poses();
  void convert(Velocity& velocity);
  bool hasClockReset() const { return !clock_resets_.empty(); }
private:
  struct Record { geometry_msgs::msg::TransformStamped transform; bool fixed; int epoch; Stamp source; };
  struct Edge { std::string parent; bool fixed{false}; std::vector<TimestampNs> times; bool conflict{false}; };
  bool validChain(const std::string& target,const std::string& source,TimestampNs ns,int epoch) const;
  Config config_;
  TimestampNs duration_;
  std::vector<Record> records_;
  std::set<std::string> clock_resets_;
  std::map<int,std::unique_ptr<tf2::BufferCore>> buffers_;
  std::map<int,std::map<std::string,Edge>> edges_;
};
}
