#include "track_insight/domain.hpp"
#include <yaml-cpp/yaml.h>
#include <cmath>
#include <stdexcept>

namespace track_insight {
Config loadConfig(const std::filesystem::path& path) {
  Config c;if(path.empty())return c;
  auto root=YAML::LoadFile(path.string());
  auto stringValue=[&](const char* group,const char* key,std::string& value){if(root[group]&&root[group][key])value=root[group][key].as<std::string>();};
  stringValue("topics","plan",c.plan_topic);stringValue("topics","command",c.command_topic);
  stringValue("topics","tracking_raw",c.tracking_raw_topic);
  stringValue("topics","feedback",c.feedback_topic);stringValue("topics","downstream",c.downstream_topic);
  stringValue("frames","world",c.frame);stringValue("frames","base",c.base);stringValue("frames","command",c.command_frame);
  auto duration=[&](const char* key,TimestampNs& value,bool allow_zero=false){
    if(root["timing"]&&root["timing"][key]){double ms=root["timing"][key].as<double>();
      if(!std::isfinite(ms)||(allow_zero?ms<0:ms<=0)||ms>60000)throw std::invalid_argument(std::string("invalid duration: ")+key);
      value=static_cast<TimestampNs>(ms*1000000);}
  };
  duration("max_tf_gap_ms",c.tf_gap);duration("max_velocity_gap_ms",c.velocity_gap);
  duration("max_command_age_ms",c.command_age);duration("max_log_age_ms",c.log_age);
  duration("log_match_tolerance_ms",c.log_match_tolerance,true);
  duration("clock_reset_tolerance_ms",c.clock_reset_tolerance,true);
  duration("clock_domain_tolerance_ms",c.clock_domain_tolerance,true);
  auto threshold=[&](const char* key,std::optional<double>& value){
    if(root["thresholds"]&&root["thresholds"][key]&&!root["thresholds"][key].IsNull()){
      double number=root["thresholds"][key].as<double>();
      if(!std::isfinite(number)||number<=0)throw std::invalid_argument(std::string("invalid threshold: ")+key);
      value=number;}
  };
  threshold("lateral_m",c.lateral_threshold);threshold("velocity_mps",c.velocity_threshold);
  threshold("angular_velocity_radps",c.angular_velocity_threshold);
  threshold("terminal_position_m",c.terminal_tolerance);
  auto positive=[&](const char* group,const char* key,double& value,bool allow_zero=false){
    if(root[group]&&root[group][key]){const double number=root[group][key].as<double>();
      if(!std::isfinite(number)||(allow_zero?number<0:number<=0)||number>1000000)throw std::invalid_argument(std::string("invalid value: ")+key);
      value=number;}
  };
  positive("geometry","max_jump_speed_mps",c.max_position_jump_speed);
  positive("geometry","jump_distance_m",c.jump_distance);positive("geometry","progress_window_m",c.progress_window);
  positive("motion","stopped_linear_mps",c.stopped_linear);positive("motion","stopped_angular_radps",c.stopped_angular);
  positive("motion","stationary_dwell_s",c.settle_duration);
  positive("geometry","progress_backtrack_m",c.progress_backtrack,true);
  positive("geometry","branch_distance_tolerance_m",c.branch_distance_tolerance,true);
  positive("geometry","branch_progress_separation_m",c.branch_progress_separation,true);
  positive("log_matching","pose_tolerance_m",c.log_pose_tolerance,true);
  positive("report","worst_case_p95_limit_m",c.worst_case_p95_limit,true);
  positive("report","worst_case_max_limit_m",c.worst_case_max_limit,true);
  if(root["log_matching"]&&root["log_matching"]["min_anchor_score"]) {
    const auto score=root["log_matching"]["min_anchor_score"].as<long long>();
    if(score<1||score>1000000)throw std::invalid_argument("invalid value: min_anchor_score");
    c.min_log_anchor_score=static_cast<std::size_t>(score);
  }
  if(c.frame.empty()||c.base.empty()||c.command_frame.empty())throw std::invalid_argument("empty reference frame");
  return c;
}
}
