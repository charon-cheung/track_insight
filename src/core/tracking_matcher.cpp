#include "track_insight/domain.hpp"
#include <algorithm>
#include <cmath>

namespace track_insight {
namespace {
struct Projection { Pose point; double s, lateral, distance; std::size_t index; };
const LogEvent* stateAt(const RunData& data, TimestampNs time, const Interval& interval, const Config& config) {
  if (!data.log_compatible || interval.identity != "confirmed") return nullptr;
  const LogEvent* best = nullptr;
  for (const auto& e : data.events) {
    if (e.kind == "tracking_state" && e.session == interval.controller_session &&
        e.fields.count("plan_revision") && e.fields.at("plan_revision") == interval.controller_revision && e.ns >= interval.start && e.ns <= time &&
        time-e.ns <= config.log_age && (!best || e.ns > best->ns)) best = &e;
  }
  return best;
}
std::string stageAt(const RunData& data, TimestampNs time, const LogEvent* state, const Config& config) {
  if (state && state->fields.count("mode")) return "controller:" + state->fields.at("mode");
  auto it=std::upper_bound(data.feedback.begin(),data.feedback.end(),time,
    [](TimestampNs t,const Velocity& v){return t<v.stamp.ns;});
  if (it==data.feedback.begin()) return "unknown";
  const auto& v=*std::prev(it);
  if (!v.valid || !v.stamp.valid || time-v.stamp.ns>config.velocity_gap) return "unknown";
  if (std::hypot(v.vx,v.vy)>config.stopped_linear) return std::abs(v.vy)>std::abs(v.vx)*2 ? "inferred:lateral_translation" : "inferred:translation";
  if (std::abs(v.omega)>config.stopped_angular) return "inferred:rotation";
  return "inferred:stationary";
}
}
std::vector<TrackingSample> matchTracking(const Plan& plan,const RunData& data,
    const Interval& interval,const Config& config) {
  std::vector<TrackingSample> output;
  if (plan.poses.empty()) return output;
  std::vector<double> arc(plan.poses.size(),0);
  for (std::size_t i=1;i<arc.size();++i) arc[i]=arc[i-1]+
    std::hypot(plan.poses[i].x-plan.poses[i-1].x,plan.poses[i].y-plan.poses[i-1].y);
  std::optional<double> previous;
  TimestampNs last_valid=interval.start;
  for (std::size_t i=0;i<data.poses.size();++i) {
    const auto& pose=data.poses[i];
    if(pose.stamp.ns<interval.start || pose.stamp.ns>=interval.end || pose.stamp.epoch!=interval.epoch) continue;
    TrackingSample sample;
    sample.ns=pose.stamp.ns; sample.end_ns=sample.ns; sample.actual=pose.pose;
    if(i+1<data.poses.size() && data.poses[i+1].stamp.epoch==pose.stamp.epoch) {
      const auto next=data.poses[i+1].stamp.ns;
      if(data.poses[i+1].valid && data.poses[i+1].stamp.valid && next>=sample.ns && next-sample.ns<=config.tf_gap) sample.end_ns=std::min(next,interval.end);
    }
    sample.lateral_valid=arc.back()>=1e-9;
    sample.endpoint=std::hypot(pose.pose.x-plan.poses.back().x,pose.pose.y-plan.poses.back().y);
    const auto* state=stateAt(data,sample.ns,interval,config);
    sample.stage=stageAt(data,sample.ns,state,config);
    if(!pose.valid || !pose.stamp.valid) {
      sample.reason=pose.reason.empty()?"invalid_pose_time":pose.reason;
      previous.reset(); output.push_back(sample); continue;
    }
    if(sample.ns-last_valid>config.tf_gap) previous.reset();
    std::vector<Projection> candidates;
    for(std::size_t j=0;j+1<plan.poses.size();++j) {
      const auto& a=plan.poses[j]; const auto& b=plan.poses[j+1];
      const double dx=b.x-a.x,dy=b.y-a.y,length=arc[j+1]-arc[j];
      if(length<1e-9) continue;
      const double fraction=std::clamp(((pose.pose.x-a.x)*dx+(pose.pose.y-a.y)*dy)/(length*length),0.0,1.0);
      const double s=arc[j]+fraction*length;
      if(previous && (s<*previous-0.15 || s>*previous+config.progress_window)) continue;
      Pose projected{a.x+fraction*dx,a.y+fraction*dy,0};
      candidates.push_back({projected,s,(dx*(pose.pose.y-a.y)-dy*(pose.pose.x-a.x))/length,
        std::hypot(projected.x-pose.pose.x,projected.y-pose.pose.y),j});
    }
    if(arc.back()<1e-9) candidates.push_back({plan.poses.front(),0,0,
      std::hypot(pose.pose.x-plan.poses.front().x,pose.pose.y-plan.poses.front().y),0});
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
    if(candidates.empty()) sample.reason="no_progress_consistent_projection";
    else {
      const auto& best=candidates.front(); bool ambiguous=false;
      for(std::size_t j=1;j<candidates.size();++j) {
        if(candidates[j].distance>best.distance+0.01) break;
        if(std::abs(candidates[j].s-best.s)>0.25) ambiguous=true;
      }
      if(ambiguous) sample.reason="ambiguous_path_branch";
      else {
        sample.valid=true;sample.projection=best.point;sample.segment=best.index;
        sample.progress=best.s;sample.lateral=best.lateral;sample.distance=best.distance;
        previous=best.s;last_valid=sample.ns;
        if(state && state->fields.count("target_yaw")) {
          try {const double target=std::stod(state->fields.at("target_yaw"));
            if(std::isfinite(target)) sample.yaw_error=wrapAngle(pose.pose.yaw-target);
          } catch(...) {}
        }
      }
    }
    output.push_back(std::move(sample));
  }
  return output;
}
}  // namespace track_insight
