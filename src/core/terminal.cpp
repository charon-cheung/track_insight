#include "track_insight/domain.hpp"
#include <algorithm>
#include <cmath>

namespace track_insight {
namespace {
Metric unavailable(const std::string& unit,const std::string& reason) {
  Metric m;m.unit=unit;m.reason=reason;return m;
}
Metric value(double v,const std::string& unit,TimestampNs ns,const std::string& quality) {
  Metric m;m.value=v;m.unit=unit;m.count=1;m.maximum_ns=ns;m.quality=quality;return m;
}
const PoseSample* nearbyPose(const RunData& data,TimestampNs ns,const Config& c) {
  const PoseSample* found=nullptr;
  for(const auto& p:data.poses)if(p.valid&&p.stamp.valid&&std::abs(p.stamp.ns-ns)<=c.tf_gap&&
    (!found||std::abs(p.stamp.ns-ns)<std::abs(found->stamp.ns-ns)))found=&p;
  return found;
}
}
std::map<std::string,Metric> terminalMetrics(const RunData& data,const Task* task,
                                            const Interval& interval,const Config& c) {
  std::map<std::string,Metric> out;
  const std::string no_result=!task?"navigation_goal_unavailable":
    task->reason!="recorded_navigation_result"?"navigation_result_unavailable":
    interval.end!=task->end?"not_final_execution_interval":"";
  out["goal_position_error"]=unavailable("m",no_result.empty()?"goal_or_terminal_pose_unavailable":no_result);
  out["goal_yaw_error"]=unavailable("rad","navigation_goal_yaw_not_recorded");
  out["stopped_residual_error"]=unavailable("m",no_result.empty()?"insufficient_stopped_data":no_result);
  out["stopped_drift"]=unavailable("m",no_result.empty()?"insufficient_stopped_data":no_result);
  out["settling_time"]=unavailable("s","position_settling_tolerance_not_configured");
  if(!no_result.empty()||!task->goal)return out;
  const auto* terminal=nearbyPose(data,task->end,c);
  if(terminal)out["goal_position_error"]=value(std::hypot(terminal->pose.x-task->goal->x,
    terminal->pose.y-task->goal->y),"m",terminal->stamp.ns,"navigation_result_pose");
  if(terminal&&task->goal_yaw)out["goal_yaw_error"]=value(wrapAngle(terminal->pose.yaw-*task->goal_yaw),"rad",terminal->stamp.ns,"navigation_result_pose");
  const TimestampNs dwell=static_cast<TimestampNs>(c.settle_duration*kSecond);
  TimestampNs begin=0,previous=0,stop=0;
  for(const auto& v:data.feedback) {
    if(v.stamp.ns<task->end||v.stamp.ns>task->end+10*kSecond)continue;
    if(!v.valid||!v.stamp.valid||v.stamp.epoch!=interval.epoch||
       std::hypot(v.vx,v.vy)>c.stopped_linear||std::abs(v.omega)>c.stopped_angular) {begin=0;previous=0;continue;}
    if(!begin||v.stamp.ns-previous>c.velocity_gap)begin=v.stamp.ns;
    previous=v.stamp.ns;
    if(previous-begin>=dwell){stop=begin;break;}
  }
  if(!stop)return out;
  const auto* first=nearbyPose(data,stop,c);if(!first)return out;
  double drift=0;TimestampNs last=stop;bool complete=true;std::size_t count=0;
  for(const auto& p:data.poses)if(p.stamp.ns>=stop&&p.stamp.ns<=stop+dwell) {
    if(!p.valid||!p.stamp.valid||p.stamp.epoch!=interval.epoch||p.stamp.ns-last>c.tf_gap){complete=false;break;}
    drift=std::max(drift,std::hypot(p.pose.x-first->pose.x,p.pose.y-first->pose.y));last=p.stamp.ns;++count;
  }
  if(!complete||stop+dwell-last>c.tf_gap||count<2)return out;
  out["stopped_residual_error"]=value(std::hypot(first->pose.x-task->goal->x,first->pose.y-task->goal->y),
    "m",first->stamp.ns,"observed_stopped_window");
  out["stopped_drift"]=value(drift,"m",stop,"observed_stopped_window");
  for(const auto& key:{"stopped_residual_error","stopped_drift"}) {
    auto& m=out[key];m.valid_s=dwell/1e9;m.candidate_s=m.valid_s;m.coverage=1;m.count=count;
  }
  if(c.terminal_tolerance) {
    out["settling_time"]=unavailable("s","no_supported_position_settling_window");
    bool within=true;
    for(const auto& p:data.poses)if(p.stamp.ns>=stop&&p.stamp.ns<=stop+dwell)
      if(std::hypot(p.pose.x-task->goal->x,p.pose.y-task->goal->y)>*c.terminal_tolerance)within=false;
    if(within)out["settling_time"]=value((stop-task->end)/1e9,"s",stop,"stationary_and_goal_tolerance_dwell_after_result");
  }
  return out;
}
}
