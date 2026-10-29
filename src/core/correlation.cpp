#include "track_insight/domain.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace track_insight {
std::vector<Task> navigationTasks(const RunData& data) {
  std::vector<Task> tasks;std::vector<int> sessions;
  const bool action_lifecycle=std::any_of(data.events.begin(),data.events.end(),[](const LogEvent& e){
    return e.source=="bag_action_status"&&e.kind=="navigation_start";});
  for(const auto& e:data.events) {
    const bool action=e.source=="bag_action_status";
    if(action_lifecycle&&!action)continue;
    if((!data.log_compatible&&!action)||e.ns<data.start||e.ns>data.end)continue;
    const std::string uuid=e.fields.count("uuid")?e.fields.at("uuid"):"";
    if(e.kind=="navigation_start") {
      if(!uuid.empty()&&std::any_of(tasks.begin(),tasks.end(),[&](const Task& t){return t.uuid==uuid;}))continue;
      for(std::size_t i=0;i<tasks.size();++i)if(tasks[i].reason=="recording_ended_without_result") {
        tasks[i].end=e.ns;tasks[i].outcome=sessions[i]==e.session?"replaced":"unknown";
        tasks[i].reason=sessions[i]==e.session?"next_navigation_start":"session_changed_without_result";
      }
      Task t;t.session=e.session;if(action)t.epoch=e.session;t.id="N"+std::to_string(tasks.size()+1);t.start=e.ns;t.end=data.end;
      t.reason="recording_ended_without_result";t.uuid=uuid;t.evidence_lines.push_back(e.line);
      if(e.fields.count("goal_x")&&e.fields.count("goal_y"))try {
        const double x=std::stod(e.fields.at("goal_x")),y=std::stod(e.fields.at("goal_y"));
        if(std::isfinite(x)&&std::isfinite(y))t.goal=Pose{x,y,0};
      }catch(...){}
      if(e.fields.count("goal_yaw"))try {const double yaw=std::stod(e.fields.at("goal_yaw"));if(std::isfinite(yaw))t.goal_yaw=yaw;}catch(...){}
      tasks.push_back(t);sessions.push_back(e.session);
    } else if(e.kind.rfind("navigation_",0)==0) {
      const auto outcome=e.kind.substr(11);
      if(outcome!="succeeded"&&outcome!="failed"&&outcome!="canceled")continue;
      for(std::size_t i=tasks.size();i>0;--i) {
        auto& t=tasks[i-1];
        if(t.reason!="recording_ended_without_result"||sessions[i-1]!=e.session||t.start>e.ns)continue;
        if((!uuid.empty()||!t.uuid.empty())&&uuid!=t.uuid)continue;
        t.outcome=outcome;t.end=e.ns;t.reason="recorded_navigation_result";t.evidence_lines.push_back(e.line);break;
      }
    }
  }
  if(action_lifecycle&&data.log_compatible)for(auto& task:tasks) {
    std::vector<const LogEvent*> matches;
    for(const auto& e:data.events)if(e.source!="bag_action_status"&&e.kind=="navigation_start"&&
      std::abs(e.ns-task.start)<=100000000&&e.fields.count("goal_x")&&e.fields.count("goal_y"))matches.push_back(&e);
    if(matches.size()==1)try {
      const auto& e=*matches.front();const double x=std::stod(e.fields.at("goal_x")),y=std::stod(e.fields.at("goal_y"));
      if(std::isfinite(x)&&std::isfinite(y)){task.goal=Pose{x,y,0};task.evidence_lines.push_back(e.line);}
    }catch(...){}
  }
  for(auto& task:tasks)if(task.epoch<0) {
    std::set<int> epochs;
    for(const auto& p:data.plans)if(p.stamp.record_ns>=task.start&&p.stamp.record_ns<task.end)epochs.insert(p.stamp.epoch);
    if(epochs.size()==1)task.epoch=*epochs.begin();
  }
  return tasks;
}
std::vector<Interval> executionIntervals(const RunData& data,const std::vector<Task>& tasks) {
  std::vector<Interval> result;
  for(std::size_t i=0;i<data.plans.size();++i) {
    const auto& plan=data.plans[i];if(!plan.valid||plan.poses.empty())continue;
    Interval interval;interval.id="E"+std::to_string(result.size()+1);interval.plan_id=plan.id;
    interval.start=std::max(data.start,plan.stamp.record_ns);interval.end=data.end;interval.epoch=plan.stamp.epoch;
    interval.reason="publication_window_only; controller adoption unverified";
    if(i+1<data.plans.size()) {
      if(data.plans[i+1].stamp.epoch!=plan.stamp.epoch) {
        interval.end=interval.start;interval.reason="record_clock_epoch_boundary";
      } else {interval.end=std::min(interval.end,data.plans[i+1].stamp.record_ns);interval.termination="next_path_publication";}
    }
    for(const auto& task:tasks)if(task.epoch==interval.epoch&&interval.start>=task.start&&interval.start<task.end) {
      interval.navigation_id=task.id;
      if(task.end<=interval.end){interval.end=task.end;interval.termination=task.outcome;
        interval.end_quality=task.reason=="recorded_navigation_result"?"confirmed":"inferred";}
      break;
    }
    if(data.log_compatible)for(const auto& e:data.events) {
      if(e.kind!="handoff"||e.ns<interval.start||e.ns>=interval.end||!e.fields.count("path_stamp_ns")||
         !e.fields.count("poses")||!e.fields.count("frame")||!e.fields.count("plan_revision"))continue;
      try {
        const auto stamp=std::stoll(e.fields.at("path_stamp_ns"));const auto count=std::stoull(e.fields.at("poses"));
        std::size_t matches=0;
        for(const auto& p:data.plans)if(p.stamp.header_ns==stamp&&p.poses.size()==count&&p.frame==e.fields.at("frame"))++matches;
        if(matches!=1||plan.stamp.header_ns!=stamp||plan.poses.size()!=count||plan.frame!=e.fields.at("frame"))continue;
        interval.identity="confirmed";interval.start=e.ns;interval.controller_session=e.session;
        interval.controller_revision=e.fields.at("plan_revision");interval.evidence_lines.push_back(e.line);
        interval.reason="shared_path_stamp_frame_count; first control output unverified";
        for(const auto& state:data.events) {
          if(state.session!=e.session||state.ns<e.ns||state.ns>=interval.end||!state.fields.count("plan_revision")||
             state.fields.at("plan_revision")!=interval.controller_revision||!state.fields.count("command_stamp_ns"))continue;
          const auto first=std::stoll(state.fields.at("command_stamp_ns"));
          if(first<interval.start||first>=interval.end)continue;
          const auto command=std::find_if(data.commands.begin(),data.commands.end(),[&](const Velocity& v){
            return v.stamp.ns==first&&v.stamp.epoch==interval.epoch&&v.valid&&v.stamp.valid;});
          if(command!=data.commands.end()){interval.start=first;interval.start_quality="confirmed";
            interval.reason="shared_path_identity_and_recorded_first_command";interval.evidence_lines.push_back(state.line);break;}
        }
        break;
      } catch(...) {}
    }
    if(interval.end>interval.start)result.push_back(std::move(interval));
  }
  // The new path's first evidenced command terminates the previous adopted path.
  for(std::size_t i=1;i<result.size();++i) {
    auto& previous=result[i-1];const auto& next=result[i];
    if(previous.identity=="confirmed"&&next.identity=="confirmed"&&next.start_quality=="confirmed"&&
       previous.termination=="next_path_publication"&&previous.epoch==next.epoch&&
       previous.controller_session==next.controller_session&&previous.navigation_id==next.navigation_id) {
      previous.end=next.start;previous.end_quality="confirmed";previous.termination="next_confirmed_control_output";
    }
  }
  return result;
}
}
