#include "track_insight/domain.hpp"
#include <algorithm>
#include <cmath>

namespace track_insight {
namespace {
struct MotionEvidence {
  std::optional<TimestampNs> first, stopped, last;
  bool seen{false};
};
MotionEvidence evidence(const std::vector<Velocity>& values,const Interval& window,
                        const Config& config,bool feedback) {
  MotionEvidence out;
  for(const auto& v:values) {
    if(v.stamp.epoch!=window.epoch||v.stamp.ns<window.start||v.stamp.ns>=window.end)continue;
    out.seen=true;
    const bool valid=v.valid&&v.stamp.valid&&std::isfinite(v.vx)&&std::isfinite(v.vy)&&
      std::isfinite(v.omega)&&v.frame==(feedback?config.base:config.command_frame);
    // A feedback gap cannot establish continuous standstill. Begin confirmation again.
    if(!valid||(feedback&&out.last&&(v.stamp.ns<=*out.last||v.stamp.ns-*out.last>config.velocity_gap)))out.stopped.reset();
    out.last=v.stamp.ns;
    if(!valid)continue;
    const bool moving=std::hypot(v.vx,v.vy)>(feedback?config.stopped_linear:1e-6)||
      std::abs(v.omega)>(feedback?config.stopped_angular:1e-6);
    if(moving) {if(!out.first)out.first=v.stamp.ns;out.stopped.reset();}
    else if(!out.stopped)out.stopped=v.stamp.ns;
  }
  if(feedback&&(!out.last||window.end-*out.last>config.velocity_gap))out.stopped.reset();
  return out;
}
}
Interval motionWindow(const RunData& data,Interval window,const Config& config) {
  // Recorded task/adoption evidence remains authoritative. Refine publication-only windows.
  if(!window.navigation_id.empty()||window.identity=="confirmed"||window.end_quality=="confirmed")return window;
  const auto command=evidence(causalCommands(data.commands),window,config,false);
  const auto downstream=evidence(causalCommands(data.downstream),window,config,false);
  const auto feedback=evidence(data.feedback,window,config,true);
  if(!feedback.seen||(!command.seen&&!downstream.seen))return window;
  std::optional<TimestampNs> first;
  for(const auto* stream:{&command,&downstream,&feedback})if(stream->first)
    first=first?std::min(*first,*stream->first):stream->first;
  if(!first)return window;
  const auto original_start=window.start,original_end=window.end;
  // Keep the entire span between first and last activity, including intermediate stops.
  window.start=*first;
  if(feedback.stopped&&(!command.seen||command.stopped)&&(!downstream.seen||downstream.stopped)) {
    auto stop=*feedback.stopped;
    if(command.stopped)stop=std::max(stop,*command.stopped);
    if(downstream.stopped)stop=std::max(stop,*downstream.stopped);
    const auto settle=static_cast<TimestampNs>(config.settle_duration*kSecond);
    if(settle>=0&&stop>window.start&&settle<original_end-stop) {
      window.end=stop+settle;window.termination="inferred_velocity_standstill";
    }
  }
  if(window.start!=original_start||window.end!=original_end)
    window.reason+="; velocity_activity_window; publication_window_ns="+std::to_string(original_start)+".."+std::to_string(original_end);
  return window;
}
}
