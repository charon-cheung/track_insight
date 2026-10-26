#include "track_insight/domain.hpp"
#include <algorithm>
#include <cmath>

namespace track_insight {
void addObservations(PathResult& path,const Config& c) {
  for(const auto& key:{"lateral","path_distance","vx_residual","vy_residual","omega_residual"}) {
    const auto found=path.metrics.find(key);
    if(found!=path.metrics.end()&&found->second.maximum)
      path.findings.push_back({"observation",std::string(key)+"_maximum","Maximum observed absolute error",
        path.interval.id,found->second.maximum_ns,found->second.maximum_ns,found->second.maximum});
  }
  TimestampNs stall_start=0,stall_end=0,gap_start=0,gap_end=0;
  auto endStall=[&](){
    if(stall_end-stall_start>=static_cast<TimestampNs>(c.settle_duration*kSecond))
      path.findings.push_back({"tracking","sustained_low_feedback",
        "Nonzero commanded motion with feedback below configured stationary thresholds",
        path.interval.id,stall_start,stall_end,std::nullopt});
    stall_start=stall_end=0;
  };
  auto endGap=[&](){
    if(gap_end>gap_start)path.findings.push_back({"data_quality","velocity_pairing_gap",
      "No valid causal command/feedback pair; interval excluded from residual metrics",
      path.interval.id,gap_start,gap_end,std::nullopt});
    gap_start=gap_end=0;
  };
  std::vector<std::pair<TimestampNs,int>> reversals;int prior_sign=0;
  for(const auto& v:path.velocity) {
    if(!v.valid) {
      endStall();prior_sign=0;reversals.clear();
      if(gap_end!=v.ns)endGap();
      if(!gap_end)gap_start=v.ns;
      gap_end=v.end_ns;continue;
    }
    endGap();
    const bool demand=std::hypot(v.command.vx,v.command.vy)>c.stopped_linear||
      std::abs(v.command.omega)>c.stopped_angular;
    const bool stopped=std::hypot(v.feedback.vx,v.feedback.vy)<=c.stopped_linear&&
      std::abs(v.feedback.omega)<=c.stopped_angular;
    if(demand&&stopped) {
      if(stall_end!=v.ns)endStall();
      if(!stall_end)stall_start=v.ns;
      stall_end=v.end_ns;
    }else endStall();
    if(c.velocity_threshold) {
      const double residual=v.feedback.vx-v.command.vx;
      const int sign=residual>*c.velocity_threshold?1:residual< -*c.velocity_threshold?-1:0;
      if(sign&&prior_sign&&sign!=prior_sign)reversals.emplace_back(v.ns,sign);
      if(sign)prior_sign=sign;
      reversals.erase(std::remove_if(reversals.begin(),reversals.end(),
        [&](const auto& item){return v.ns-item.first>2*kSecond;}),reversals.end());
      if(reversals.size()>=3) {
        path.findings.push_back({"tracking","repeated_vx_residual_sign_reversal",
          "Repeated above-threshold feedback/command residual reversals; cause unclassified",
          path.interval.id,reversals.front().first,v.end_ns,std::abs(residual)});
        reversals.clear();prior_sign=0;
      }
    }
  }
  endStall();endGap();
  TimestampNs end=path.interval.start;
  for(const auto& sample:path.tracking) {
    if(sample.ns>end)path.findings.push_back({"data_quality","pose_time_gap","No bounded pose cell in this interval",
      path.interval.id,end,sample.ns,std::nullopt});
    end=std::max(end,sample.end_ns);
  }
  if(end<path.interval.end)path.findings.push_back({"data_quality","pose_time_gap","No bounded final pose cell",
    path.interval.id,end,path.interval.end,std::nullopt});
}
}
