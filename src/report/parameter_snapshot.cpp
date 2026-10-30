#include "track_insight/parameter_snapshot.hpp"
#include <yaml-cpp/yaml.h>
#include <chrono>
#include <cmath>

namespace track_insight {
namespace {
Json value(const YAML::Node& node,int depth) {
  if(depth>16)throw std::runtime_error("PID parameter nesting exceeds 16 levels");
  if(node.IsNull())return nullptr;
  if(node.IsSequence()) {
    Json result=Json::array();for(const auto& child:node)result.push_back(value(child,depth+1));return result;
  }
  if(node.IsMap()) {
    Json result=Json::object();
    for(const auto& child:node) {
      const auto key=child.first.as<std::string>();
      if(result.contains(key))throw std::runtime_error("duplicate PID parameter: "+key);
      result[key]=value(child.second,depth+1);
    }
    return result;
  }
  const auto text=node.as<std::string>();
  if(node.Tag()=="!"||node.Tag()=="tag:yaml.org,2002:str")return text;
  if(text=="true"||text=="True"||text=="TRUE")return true;
  if(text=="false"||text=="False"||text=="FALSE")return false;
  try{return node.as<std::int64_t>();}catch(const YAML::BadConversion&){}
  try {
    const double number=node.as<double>();
    if(!std::isfinite(number))throw std::runtime_error("non-finite PID parameter value");
    return number;
  }catch(const YAML::BadConversion&){}
  return text;
}
void flatten(const Json& node,const std::string& prefix,Json& values) {
  if(node.is_object()) {
    for(auto it=node.begin();it!=node.end();++it)flatten(it.value(),prefix.empty()?it.key():prefix+"."+it.key(),values);
  } else {
    if(values.contains(prefix))throw std::runtime_error("duplicate PID parameter path: "+prefix);
    values[prefix]=node;
  }
}
bool snapshotAvailable(const Json& snapshot) {
  return snapshot.is_object()&&snapshot.value("schema_version",0)==1&&snapshot.contains("values")&&snapshot["values"].is_object();
}
}
Json readPidParameters(const std::filesystem::path& file) {
  if(std::filesystem::file_size(file)>1024*1024)throw std::runtime_error("PID configuration exceeds 1 MiB");
  const auto yaml=YAML::LoadFile(file.string());
  const auto params=yaml["controller_server"]["ros__parameters"];
  if(!params.IsMap()||!params["PidTrackingController"].IsMap())
    throw std::runtime_error("PID configuration requires controller_server.ros__parameters.PidTrackingController");
  Json values=Json::object();flatten(value(params,0),"",values);
  const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  return {{"schema_version",1},{"source_path",std::filesystem::canonical(file).string()},
    {"captured_at_ns",std::to_string(now)},{"captured_at_label",formatTimestamp(now,captureDisplayClock(now,now,true))},
    {"values",values}};
}
void attachPidParameterBaseline(Json& analysis,const Json& runs) {
  if(!analysis.contains("pid_parameters")||!snapshotAvailable(analysis["pid_parameters"]))return;
  auto& snapshot=analysis["pid_parameters"];
  if(snapshot.contains("comparison"))return; // Re-rendering must retain the originally recorded baseline.
  const auto start=std::stoll(analysis.at("meta").at("start_ns").get<std::string>());
  const auto hash=analysis.at("meta").at("input_sha256").get<std::string>();
  const Json* previous=nullptr;
  for(const auto& run:runs) {
    if(run.at("source_sha256")==hash)continue;
    const auto time=std::stoll(run.at("start_ns").get<std::string>());
    if(time>=start)continue;
    if(!previous||time>std::stoll(previous->at("start_ns").get<std::string>())||
       (time==std::stoll(previous->at("start_ns").get<std::string>())&&run.at("id")<previous->at("id")))previous=&run;
  }
  Json comparison={{"status","no_previous_round"}};
  if(previous) {
    comparison={{"status","previous_snapshot_missing"},{"run_id",previous->at("id")},{"run_name",previous->at("name")},
      {"start_ns",previous->at("start_ns")}};
    if(previous->contains("pid_parameters")&&snapshotAvailable(previous->at("pid_parameters"))) {
      comparison["status"]="available";
      comparison["values"]=previous->at("pid_parameters").at("values");
    }
  }
  snapshot["comparison"]=std::move(comparison);
}
}
