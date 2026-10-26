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
  stringValue("topics","feedback",c.feedback_topic);stringValue("topics","downstream",c.downstream_topic);
  stringValue("frames","world",c.frame);stringValue("frames","base",c.base);stringValue("frames","command",c.command_frame);
  auto duration=[&](const char* key,TimestampNs& value){
    if(root["timing"]&&root["timing"][key]){double ms=root["timing"][key].as<double>();
      if(!std::isfinite(ms)||ms<=0||ms>60000)throw std::invalid_argument(std::string("invalid duration: ")+key);
      value=static_cast<TimestampNs>(ms*1000000);}
  };
  duration("max_tf_gap_ms",c.tf_gap);duration("max_velocity_gap_ms",c.velocity_gap);
  duration("max_command_age_ms",c.command_age);duration("max_log_age_ms",c.log_age);
  auto threshold=[&](const char* key,std::optional<double>& value){
    if(root["thresholds"]&&root["thresholds"][key]&&!root["thresholds"][key].IsNull()){
      double number=root["thresholds"][key].as<double>();
      if(!std::isfinite(number)||number<=0)throw std::invalid_argument(std::string("invalid threshold: ")+key);
      value=number;}
  };
  threshold("lateral_m",c.lateral_threshold);threshold("velocity_mps",c.velocity_threshold);
  threshold("angular_velocity_radps",c.angular_velocity_threshold);
  threshold("terminal_position_m",c.terminal_tolerance);
  auto positive=[&](const char* group,const char* key,double& value){
    if(root[group]&&root[group][key]){const double number=root[group][key].as<double>();
      if(!std::isfinite(number)||number<=0||number>1000000)throw std::invalid_argument(std::string("invalid value: ")+key);
      value=number;}
  };
  positive("geometry","max_jump_speed_mps",c.max_position_jump_speed);
  positive("geometry","jump_distance_m",c.jump_distance);positive("geometry","progress_window_m",c.progress_window);
  positive("motion","stopped_linear_mps",c.stopped_linear);positive("motion","stopped_angular_radps",c.stopped_angular);
  positive("motion","stationary_dwell_s",c.settle_duration);
  if(c.frame.empty()||c.base.empty()||c.command_frame.empty())throw std::invalid_argument("empty reference frame");
  return c;
}
}
