#include "track_insight/domain.hpp"
#include <algorithm>
#include <set>

namespace track_insight {
std::vector<Velocity> causalCommands(const std::vector<Velocity>& input) {
  std::map<std::pair<int,TimestampNs>,std::size_t> counts;
  for(const auto& v:input)++counts[{v.stamp.epoch,v.stamp.ns}];
  auto result=input;
  for(auto& v:result)if(counts[{v.stamp.epoch,v.stamp.ns}]>1) {
    if(!v.stamp.record_ns){v.valid=false;v.reason="duplicate_command_time_without_delivery_evidence";}
    else {v.stamp.ns=std::max(v.stamp.ns,v.stamp.record_ns);v.stamp.clock_domain="duplicate_header_record_time";}
  }
  std::stable_sort(result.begin(),result.end(),[](const Velocity& a,const Velocity& b){
    return a.stamp.ns==b.stamp.ns?a.stamp.sequence<b.stamp.sequence:a.stamp.ns<b.stamp.ns;});
  for(std::size_t i=1;i<result.size();++i)if(result[i].stamp.ns==result[i-1].stamp.ns&&result[i].stamp.epoch==result[i-1].stamp.epoch&&
    (result[i].vx!=result[i-1].vx||result[i].vy!=result[i-1].vy||result[i].omega!=result[i-1].omega)) {
      result[i].valid=result[i-1].valid=false;result[i].reason=result[i-1].reason="conflicting_command_delivery_time";
    }
  return result;
}
std::vector<VelocitySample> alignVelocity(const RunData& data,const Interval& interval,const Config& config) {
  std::vector<VelocitySample> output;
  const auto commands=causalCommands(data.commands);
  std::set<TimestampNs> times{interval.start,interval.end};
  for(const auto& v:commands) if(v.stamp.epoch==interval.epoch) {
    if(v.stamp.ns>interval.start && v.stamp.ns<interval.end) times.insert(v.stamp.ns);
    if(v.stamp.ns+config.command_age>interval.start && v.stamp.ns+config.command_age<interval.end)
      times.insert(v.stamp.ns+config.command_age);
  }
  for(const auto& v:data.feedback)
    if(v.stamp.epoch==interval.epoch && v.stamp.ns>interval.start && v.stamp.ns<interval.end) times.insert(v.stamp.ns);
  const std::vector<TimestampNs> knots(times.begin(),times.end());
  for(std::size_t i=0;i+1<knots.size();++i) {
    VelocitySample row;row.ns=knots[i];row.end_ns=knots[i+1];row.reason="missing_velocity_data";
    auto command=std::upper_bound(commands.begin(),commands.end(),row.ns,
      [](TimestampNs t,const Velocity& v){return t<v.stamp.ns;});
    if(command==commands.begin()){output.push_back(row);continue;}
    --command;row.command=*command;
    if(command->stamp.epoch!=interval.epoch || !command->valid || !command->stamp.valid ||
       row.ns-command->stamp.ns>=config.command_age) {
      row.reason="command_expired_or_invalid";output.push_back(row);continue;
    }
    auto right=std::lower_bound(data.feedback.begin(),data.feedback.end(),row.ns,
      [](const Velocity& v,TimestampNs t){return v.stamp.ns<t;});
    if(right==data.feedback.end()){output.push_back(row);continue;}
    if(right->stamp.ns==row.ns) row.feedback=*right;
    else if(right!=data.feedback.begin()) {
      const auto& left=*std::prev(right);
      if(right->stamp.ns-left.stamp.ns<=0 || right->stamp.ns-left.stamp.ns>config.velocity_gap ||
         !left.valid || !left.stamp.valid || left.stamp.epoch!=interval.epoch || left.frame!=right->frame) {
        row.reason="feedback_gap_or_frame_change";output.push_back(row);continue;
      }
      const double ratio=static_cast<double>(row.ns-left.stamp.ns)/(right->stamp.ns-left.stamp.ns);
      row.feedback=left;row.feedback.stamp.ns=row.ns;
      row.feedback.vx+=ratio*(right->vx-left.vx);row.feedback.vy+=ratio*(right->vy-left.vy);
      row.feedback.omega+=ratio*(right->omega-left.omega);
    } else {output.push_back(row);continue;}
    auto endpoint=std::lower_bound(data.feedback.begin(),data.feedback.end(),row.end_ns,
      [](const Velocity& v,TimestampNs t){return v.stamp.ns<t;});
    if(!right->valid || !right->stamp.valid || right->stamp.epoch!=interval.epoch ||
       endpoint==data.feedback.end() || !endpoint->valid || !endpoint->stamp.valid ||
       endpoint->stamp.epoch!=interval.epoch || endpoint->stamp.ns-row.ns>config.velocity_gap) {
      row.reason="feedback_gap_or_invalid";output.push_back(row);continue;
    }
    if(row.command.frame.empty() || row.command.frame!=row.feedback.frame ||
       row.command.reference_point!=row.feedback.reference_point) {
      row.reason="velocity_reference_mismatch";output.push_back(row);continue;
    }
    row.valid=true;row.reason.clear();output.push_back(row);
  }
  return output;
}
}  // namespace track_insight
