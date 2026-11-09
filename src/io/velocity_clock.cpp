#include "velocity_clock.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace track_insight {
namespace {
struct ClockState {
  std::optional<Stamp> previous,header;
  std::vector<std::size_t> rejected;
  TimestampNs boundary{};
};
}
void isolateVelocityClocks(std::vector<Velocity>& values,const Config& config,
                           std::vector<Diagnostic>& diagnostics) {
  std::map<std::pair<std::string,int>,ClockState> clocks;
  auto finish=[&](ClockState& state,const std::optional<TimestampNs>& end) {
    if(state.rejected.empty())return;
    const auto& first=values[state.rejected.front()];
    const auto& last=values[state.rejected.back()];
    diagnostics.push_back({"clock_domain_unverified",
      "Velocity clock interval excluded: reason="+first.reason+
      " begin_ns="+std::to_string(state.boundary)+
      " end_ns="+(end?std::to_string(*end):"unrecovered")+
      " rejected="+std::to_string(state.rejected.size())+
      " first_header_ns="+std::to_string(first.stamp.header_ns)+
      " first_record_ns="+std::to_string(first.stamp.record_ns)+
      " last_header_ns="+std::to_string(last.stamp.header_ns)+
      " last_record_ns="+std::to_string(last.stamp.record_ns),
      first.stamp.source,first.stamp.record_ns});
    state.rejected.clear();
  };
  for(std::size_t i=0;i<values.size();++i) {
    auto& v=values[i];auto& s=v.stamp;
    // Downstream outputs intentionally use receipt time, regardless of retained headers.
    if(s.clock_domain!="message_header"&&s.clock_domain!="record_fallback")continue;
    auto& state=clocks[{s.source,s.epoch}];std::string reason;
    if(s.header_ns) {
      const long double offset=static_cast<long double>(s.header_ns)-s.record_ns;
      if(std::abs(offset)>config.clock_domain_tolerance)reason="header_record_clock_mismatch";
      else if(state.header) {
        const auto& previous=*state.header;
        if(static_cast<long double>(previous.header_ns)-s.header_ns>config.clock_reset_tolerance)
          reason="header_clock_reset";
        else if(std::abs(offset-(static_cast<long double>(previous.header_ns)-previous.record_ns))>config.clock_reset_tolerance)
          reason="header_record_offset_jump";
      }
    }
    if(reason.empty()&&!state.rejected.empty()&&state.previous&&s.ns<=state.boundary)
      reason="clock_recovery_not_after_boundary";
    if(!reason.empty()) {
      if(state.rejected.empty())state.boundary=state.previous?state.previous->ns:s.record_ns;
      state.rejected.push_back(i);s.valid=false;s.clock_domain="unverified";v.reason=reason;
      // An invalid fence blocks interpolation/hold without invalidating the good anchor.
      // Original header/record/send times remain untouched for provenance.
      s.ns=state.boundary;continue;
    }
    if(!state.rejected.empty()) {
      if(!state.previous) {
        // A bad prefix has no trusted anchor. Keep its fence before the first good sample,
        // including when good headers have a stable delay relative to receipt time.
        state.boundary=std::min(state.boundary,s.ns);
        for(const auto index:state.rejected)values[index].stamp.ns=state.boundary;
      }
      finish(state,s.ns);
    }
    state.previous=s;
    // Zero headers explicitly use receipt time; they do not verify a changed header clock.
    if(s.header_ns)state.header=s;
  }
  for(auto& item:clocks)finish(item.second,std::nullopt);
}
}
