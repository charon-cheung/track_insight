#include "track_insight/report.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>

namespace track_insight {
namespace {
Json number(const std::optional<double>& v){return v&&std::isfinite(*v)?Json(*v):Json(nullptr);}
Json pose(const Pose& p){return Json::array({p.x,p.y,p.yaw});}
Json stamp(const Stamp& s) {
  return {{"ns",std::to_string(s.ns)},{"record_ns",std::to_string(s.record_ns)},
    {"header_ns",std::to_string(s.header_ns)},{"send_ns",std::to_string(s.send_ns)},
    {"sequence",s.sequence},{"epoch",s.epoch},{"source",s.source},
    {"clock_domain",s.clock_domain},{"valid",s.valid}};
}
Json metric(const Metric& m) {
  return {{"value",number(m.value)},{"bias",number(m.mean)},{"mae",number(m.mae)},{"rmse",number(m.rmse)},
    {"p95",number(m.p95)},{"max",number(m.maximum)},{"max_ns",std::to_string(m.maximum_ns)},
    {"coverage",number(m.coverage)},{"valid_s",m.valid_s},{"candidate_s",m.candidate_s},
    {"exceedance_s",number(m.exceedance_s)},{"longest_exceedance_s",number(m.longest_exceedance_s)},
    {"count",m.count},{"unit",m.unit},{"quality",m.quality},{"reason",m.reason},{"excluded",m.excluded}};
}
Json metrics(const std::map<std::string,Metric>& values) {
  Json out=Json::object();for(const auto& item:values)out[item.first]=metric(item.second);return out;
}
Json interval(const Interval& i) {
  return {{"id",i.id},{"plan_id",i.plan_id},{"navigation_id",i.navigation_id},
    {"start_ns",std::to_string(i.start)},{"end_ns",std::to_string(i.end)},{"epoch",i.epoch},
    {"controller_session",i.controller_session},{"controller_revision",i.controller_revision},{"identity",i.identity},{"start_quality",i.start_quality},{"end_quality",i.end_quality},
    {"reason",i.reason},{"termination",i.termination},{"evidence_lines",i.evidence_lines}};
}
Json velocity(const Velocity& v) {
  return {{"stamp",stamp(v.stamp)},{"value",Json::array({v.vx,v.vy,v.omega})},
    {"frame",v.frame},{"reference_point",v.reference_point},{"valid",v.valid},{"reason",v.reason}};
}
}
std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot read "+path.string());
  std::ostringstream text;text<<input.rdbuf();if(input.bad())throw std::runtime_error("read failed: "+path.string());
  return text.str();
}
void writeText(const std::filesystem::path& path,const std::string& text) {
  std::ofstream output(path,std::ios::binary);if(!output)throw std::runtime_error("cannot write "+path.string());
  output<<text;output.close();if(!output)throw std::runtime_error("write failed: "+path.string());
}
DisplayClock readClock(const Json& a) {
  DisplayClock c;c.origin_ns=std::stoll(a.at("clock").at("origin_ns").get<std::string>());
  c.wall_clock=a.at("clock").at("wall_clock").get<bool>();
  for(const auto& o:a.at("clock").at("offsets"))c.offsets.push_back({std::stoll(o.at("from_ns").get<std::string>()),o.at("seconds")});
  return c;
}
Json makeAnalysis(const RunData& d,const Evaluation& e,const Config& c,const DisplayClock& clock) {
  auto relative=[&](TimestampNs ns){return static_cast<double>(ns-clock.origin_ns)/kSecond;};
  Json a={{"schema_version",1},{"metric_version","1.0-duration-left-cells"},
    {"generator",{{"name","Track Insight"},{"version","0.1.0"}}},
    {"meta",{{"input_path",d.input_path},{"log_path",d.log_path},{"input_sha256",d.input_hash},
      {"log_sha256",d.log_hash},{"start_ns",std::to_string(d.start)},{"end_ns",std::to_string(d.end)},
      {"start_label",formatTimestamp(d.start,clock)},{"end_label",formatTimestamp(d.end,clock)},
      {"frame",d.frame},{"base",d.base},{"log_compatible",d.log_compatible},
      {"pose_semantics","recorded localization estimate; not independent ground truth"},
      {"velocity_semantics","feedback minus command; body reference point; no lag correction"},
      {"downstream_time_basis","bag_record"},
      {"cell_method","Left endpoint sample over bounded half-open cells; duration-weighted absolute P95"}}},
    {"config",{{"topics",{{"plan",c.plan_topic},{"command",c.command_topic},{"tracking_raw",c.tracking_raw_topic},{"feedback",c.feedback_topic},{"downstream",c.downstream_topic}}},
      {"frames",{{"world",c.frame},{"base",c.base},{"command",c.command_frame}}},
      {"tf_gap_ns",std::to_string(c.tf_gap)},{"velocity_gap_ns",std::to_string(c.velocity_gap)},
      {"command_age_ns",std::to_string(c.command_age)},{"log_age_ns",std::to_string(c.log_age)},
      {"jump_distance_m",c.jump_distance},{"jump_speed_mps",c.max_position_jump_speed},
      {"progress_window_m",c.progress_window},{"stopped_linear_mps",c.stopped_linear},
      {"stopped_angular_radps",c.stopped_angular},{"settle_duration_s",c.settle_duration},
      {"thresholds",{{"lateral",number(c.lateral_threshold)},{"velocity",number(c.velocity_threshold)},{"angular_velocity",number(c.angular_velocity_threshold)},{"terminal_tolerance",number(c.terminal_tolerance)}}}}},
    {"clock",{{"origin_ns",std::to_string(clock.origin_ns)},{"wall_clock",clock.wall_clock},{"offsets",Json::array()}}},
    {"topics",Json::array()},{"plans",Json::array()},{"tasks",Json::array()},
    {"requests",Json::array()},{"controller_revisions",Json::array()},{"paths",Json::array()},
    {"events",Json::array()},{"diagnostics",Json::array()},{"navigation_metrics",Json::object()},
    {"raw",{{"tracking_raw",Json::array()},{"poses",Json::array()},{"commands",Json::array()},{"feedback",Json::array()},{"downstream",Json::array()}}}};
  for(const auto& o:clock.offsets)a["clock"]["offsets"].push_back({{"from_ns",std::to_string(o.from_ns)},{"seconds",o.seconds}});
  for(const auto& t:d.topics)a["topics"].push_back({{"name",t.name},{"type",t.type},{"count",t.count}});
  for(const auto& p:d.plans) {
    Json poses=Json::array();for(const auto& point:p.poses)poses.push_back(pose(point));
    a["plans"].push_back({{"id",p.id},{"frame",p.frame},{"fingerprint",p.fingerprint},{"stamp",stamp(p.stamp)},
      {"poses",poses},{"valid",p.valid},{"reason",p.reason},{"time",relative(p.stamp.ns)},
      {"label",formatTimestamp(p.stamp.ns,clock)}});
  }
  for(const auto& t:e.tasks)a["tasks"].push_back({{"id",t.id},{"outcome",t.outcome},{"reason",t.reason},
    {"uuid",t.uuid},{"epoch",t.epoch},{"session",t.session},{"start_ns",std::to_string(t.start)},{"end_ns",std::to_string(t.end)},
    {"goal",t.goal?Json::array({t.goal->x,t.goal->y,number(t.goal_yaw)}):Json(nullptr)},{"evidence_lines",t.evidence_lines}});
  for(const auto& p:e.paths) {
    Json value={{"interval",interval(p.interval)},{"start",relative(p.interval.start)},{"end",relative(p.interval.end)},
      {"start_label",formatTimestamp(p.interval.start,clock)},{"end_label",formatTimestamp(p.interval.end,clock)},
      {"metrics",metrics(p.metrics)},{"stages",Json::object()},{"tracking",Json::array()},
      {"velocity",Json::array()},{"findings",Json::array()}};
    for(const auto& stage:p.stages)value["stages"][stage.first]=metrics(stage.second);
    for(const auto& s:p.tracking)value["tracking"].push_back(Json::array({relative(s.ns),s.actual.x,s.actual.y,s.actual.yaw,
      s.projection.x,s.projection.y,s.lateral_valid?Json(s.lateral):Json(nullptr),s.distance,s.endpoint,number(s.yaw_error),s.valid,s.stage,
      std::to_string(s.ns),relative(s.end_ns),s.segment,s.progress,s.reason}));
    for(const auto& s:p.velocity)value["velocity"].push_back(Json::array({relative(s.ns),
      s.command.vx,s.command.vy,s.command.omega,s.feedback.vx,s.feedback.vy,s.feedback.omega,
      s.valid,std::to_string(s.ns),relative(s.end_ns),s.reason}));
    for(const auto& f:p.findings)value["findings"].push_back({{"category",f.category},{"kind",f.kind},
      {"message",f.message},{"interval_id",f.interval_id},{"start",relative(f.start)},{"end",relative(f.end)},
      {"start_ns",std::to_string(f.start)},{"end_ns",std::to_string(f.end)},{"peak",number(f.peak)}});
    a["paths"].push_back(std::move(value));
  }
  for(const auto& m:e.navigation_metrics)a["navigation_metrics"][m.first]=metrics(m.second);
  for(const auto& p:d.poses)a["raw"]["poses"].push_back({{"stamp",stamp(p.stamp)},{"pose",pose(p.pose)},{"valid",p.valid},{"reason",p.reason}});
  for(const auto& v:d.commands)a["raw"]["commands"].push_back(velocity(v));
  for(const auto& v:d.feedback)a["raw"]["feedback"].push_back(velocity(v));
  for(const auto& v:d.downstream)a["raw"]["downstream"].push_back(velocity(v));
  for(const auto& v:d.tracking_raw)a["raw"]["tracking_raw"].push_back(velocity(v));
  a["signals"]={{"tracking_raw",Json::array()},{"commands",Json::array()},{"feedback",Json::array()},{"downstream",Json::array()}};
  auto signal=[&](const std::string& key,const std::vector<Velocity>& values,TimestampNs age,bool hold) {
    for(std::size_t i=0;i<values.size();++i) {
      const auto& v=values[i];TimestampNs end=v.stamp.ns;
      if(i+1<values.size()&&values[i+1].stamp.epoch==v.stamp.epoch&&values[i+1].valid&&values[i+1].stamp.valid) {
        const auto next=values[i+1].stamp.ns;
        if(next-v.stamp.ns<=age)end=next;else if(hold)end=v.stamp.ns+age;
      } else if(hold)end=std::min(d.end,v.stamp.ns+age);
      a["signals"][key].push_back(Json::array({relative(v.stamp.ns),v.vx,v.vy,v.omega,
        v.valid&&v.stamp.valid,std::to_string(v.stamp.ns),relative(end),v.reason}));
    }
  };
  signal("commands",causalCommands(d.commands),c.command_age,true);signal("feedback",d.feedback,c.velocity_gap,false);
  signal("downstream",causalCommands(d.downstream),c.command_age,true);
  signal("tracking_raw",causalCommands(d.tracking_raw),c.command_age,true);
  std::size_t request=0;
  for(const auto& event:d.events) {
    Json row={{"ns",std::to_string(event.ns)},{"time",relative(event.ns)},{"label",formatTimestamp(event.ns,clock)},
      {"line",event.line},{"session",event.session},{"kind",event.kind},{"category",event.category},
      {"raw",event.raw},{"source",event.source},{"fields",event.fields}};
    a["events"].push_back(row);
    if(event.kind=="planning_request"){row["id"]="R"+std::to_string(++request);a["requests"].push_back(row);}
    if(event.kind=="handoff"&&event.fields.count("plan_revision")) {
      row["id"]="S"+std::to_string(event.session)+"/C"+event.fields.at("plan_revision");
      row["plan_identity"]="unverified";a["controller_revisions"].push_back(row);
    }
  }
  for(const auto& f:d.diagnostics)a["diagnostics"].push_back({{"code",f.code},{"message",f.message},
    {"source",f.source},{"ns",std::to_string(f.ns)},{"time",relative(f.ns)}});
  a["columns"]={{"tracking",Json::array({"time","x","y","yaw","projection_x","projection_y","lateral",
      "path_distance","endpoint_distance","yaw_error","valid","stage","ns","end_time","segment","progress","reason"})},
    {"velocity",Json::array({"time","cmd_vx","cmd_vy","cmd_omega","feedback_vx","feedback_vy","feedback_omega","valid","ns","end_time","reason"})}};
  a["capabilities"]={{"tracking_raw",!d.tracking_raw.empty()},{"trajectory",!d.poses.empty()},{"velocity",!d.commands.empty()&&!d.feedback.empty()},
    {"navigation",!e.tasks.empty()},{"log_evidence",d.log_compatible},
    {"confirmed_intervals",false},{"png_renderer","rsvg-convert"},{"threshold_assessment",c.lateral_threshold.has_value()||c.velocity_threshold.has_value()||c.angular_velocity_threshold.has_value()}};
  for(const auto& p:e.paths)if(p.interval.identity=="confirmed"&&p.interval.start_quality=="confirmed"&&p.interval.end_quality=="confirmed")
    a["capabilities"]["confirmed_intervals"]=true;
  a["overview"]=makeOverview(a);
  return a;
}
}
