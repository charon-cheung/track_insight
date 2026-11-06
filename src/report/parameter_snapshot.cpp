#include "track_insight/parameter_snapshot.hpp"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <iomanip>
#include <regex>
#include <sstream>
#include <cmath>

namespace track_insight {
namespace {
Json parameterValue(const std::string& type,const std::string& text) {
  if(type=="not_set") {if(text=="null")return nullptr;throw std::runtime_error("invalid unset value");}
  if(type=="string") {
    std::istringstream input(text);std::string result;
    if(text.empty()||text.front()!='"'||!(input>>std::quoted(result))||!(input>>std::ws).eof())
      throw std::runtime_error("invalid string value");
    return result;
  }
  const auto node=YAML::Load(text);
  if(type=="bool") {if(text=="true")return true;if(text=="false")return false;}
  if(type=="integer")return node.as<std::int64_t>();
  if(type=="double") {const auto v=node.as<double>();if(std::isfinite(v))return v;}
  throw std::runtime_error("unsupported or invalid parameter value");
}
bool snapshotAvailable(const Json& snapshot) {
  return snapshot.is_object()&&snapshot.value("schema_version",0)==2&&
    snapshot.value("source_kind","")=="navigation_log"&&snapshot.value("status","")=="available"&&
    snapshot.contains("values")&&snapshot["values"].is_object()&&!snapshot["values"].empty();
}
}
Json readPidParameters(const Json& analysis) {
  const auto& meta=analysis.at("meta");
  const auto config=analysis.value("config",Json::object());
  const auto saved=config.value("clock_reset_tolerance_ns",std::to_string(Config{}.clock_reset_tolerance));
  std::size_t consumed=0;const auto reset_tolerance=std::stoll(saved,&consumed);
  if(consumed!=saved.size()||reset_tolerance<0||reset_tolerance>60*kSecond)
    throw std::invalid_argument("invalid saved clock reset tolerance");
  const auto source=meta.value("log_path","");
  auto unknown=[&](const std::string& reason)->Json {
    return {{"schema_version",2},{"source_kind","navigation_log"},{"status","unknown"},
      {"source_path",source},{"reason",reason}};
  };
  if(source.empty())return unknown("未提供对应导航日志。");
  if(!meta.value("log_compatible",false))return unknown("导航日志与本轮测试的身份或时间未通过匹配。");
  if(!analysis.at("clock").value("wall_clock",false))return unknown("测试时间不是已确认的绝对时间，无法匹配日志启动记录。");
  const auto start=std::stoll(meta.at("start_ns").get<std::string>()),end=std::stoll(meta.at("end_ns").get<std::string>());
  std::vector<const Json*> events;
  for(const auto& event:analysis.at("events"))if(event.value("source","")==source)events.push_back(&event);
  std::stable_sort(events.begin(),events.end(),[](const Json* a,const Json* b){return a->at("line")<b->at("line");});
  // Appended logs may contain old clock epochs. Only a uniquely overlapping
  // epoch can supply runtime parameters; retain the conservative check when
  // there is no event in the test window to disambiguate multiple epochs.
  struct Epoch {std::size_t begin,end;TimestampNs minimum,maximum;};
  std::vector<Epoch> epochs;
  TimestampNs last=0;
  for(std::size_t i=0;i<events.size();++i) {
    const auto ns=std::stoll(events[i]->at("ns").get<std::string>());
    const bool reset=i&&ns<last-reset_tolerance;
    if(reset&&((last>=start&&last<=end)||(ns>=start&&ns<=end)))
      return unknown("测试区间内存在时钟回退，无法唯一匹配启动记录。");
    if(i==0||reset)epochs.push_back({i,i,ns,ns});
    auto& epoch=epochs.back();epoch.end=i+1;
    epoch.minimum=std::min<TimestampNs>(epoch.minimum,ns);
    epoch.maximum=std::max<TimestampNs>(epoch.maximum,ns);last=ns;
  }
  if(epochs.size()>1) {
    const Epoch* selected=nullptr;
    for(const auto& epoch:epochs)if(epoch.minimum<=end&&epoch.maximum>=start) {
      if(selected)return unknown("多个日志时钟区间与本轮测试重叠，无法唯一匹配启动记录。");
      selected=&epoch;
    }
    if(!selected)return unknown("日志存在时钟回退，且缺少本轮测试区间的日志证据，无法唯一匹配启动记录。");
    events=std::vector<const Json*>(events.begin()+selected->begin,events.begin()+selected->end);
  }
  const std::regex begin(R"(PID_LOG parameter_snapshot_begin controller=(\S+) count=([0-9]+)\s*$)");
  const std::regex finish(R"(PID_LOG parameter_snapshot_end controller=(\S+) count=([0-9]+)\s*$)");
  const std::regex parameter(R"(PID_LOG parameter name=(\S+) type=(\S+) value=(.*)$)");
  Json values=Json::object();std::string controller;
  TimestampNs previous=0,captured=0,finished=0;std::size_t first_line=0,last_line=0,count=0;
  bool active=false,valid=false,complete=false,changed_during_test=false;
  for(const auto* pointer:events) {
    const auto& event=*pointer;const auto ns=std::stoll(event.at("ns").get<std::string>());
    if(previous&&ns<previous-reset_tolerance)return unknown("日志存在时钟回退，无法唯一匹配启动记录。");
    previous=ns;
    const auto raw=event.at("raw").get<std::string>();std::smatch match;
    const bool is_begin=raw.find("PID_LOG parameter_snapshot_begin")!=std::string::npos;
    const bool restart=event.value("kind","")=="controller_start"||raw.find("PID_LOG holonomic_parameters ")!=std::string::npos;
    if(is_begin||restart) {
      if(ns>start&&ns<=end)changed_during_test=true;
      if(ns>start)continue;
      active=false;complete=false;valid=false;values=Json::object();captured=ns;finished=0;
      first_line=event.at("line").get<std::size_t>();
      if(is_begin&&std::regex_search(raw,match,begin)) {
        controller=match[1].str();
        try{count=std::stoull(match[2].str());valid=count>0&&count<=1024;}catch(...){valid=false;}
        active=true;
      }
      continue;
    }
    if(!active||ns>start)continue;
    if(raw.find("PID_LOG parameter_snapshot_end")!=std::string::npos) {
      complete=valid&&std::regex_search(raw,match,finish)&&match[1].str()==controller&&
        match[2].str()==std::to_string(count)&&values.size()==count;
      finished=ns;last_line=event.at("line").get<std::size_t>();active=false;continue;
    }
    if(raw.find("PID_LOG parameter name=")==std::string::npos)continue;
    if(!std::regex_search(raw,match,parameter)){valid=false;continue;}
    const auto name=match[1].str();
    if(values.contains(name)||(name.rfind(controller+".",0)!=0&&name!="controller_frequency"&&
       name!="failure_tolerance"&&name!="publish_zero_velocity")){valid=false;continue;}
    try{values[name]=parameterValue(match[2].str(),match[3].str());}catch(const std::exception&){valid=false;}
  }
  if(changed_during_test)return unknown("测试区间内发生控制器重新配置或多次参数快照，不能用一组参数代表整轮测试。");
  if(!complete||finished>start)return unknown("测试开始前的本次控制器启动中，没有完整且有效的运行参数快照。");
  return {{"schema_version",2},{"source_kind","navigation_log"},{"status","available"},
    {"source_path",source},{"log_sha256",meta.value("log_sha256","")},{"controller",controller},
    {"test_start_ns",meta.at("start_ns")},{"test_end_ns",meta.at("end_ns")},
    {"captured_at_ns",std::to_string(captured)},{"captured_at_label",formatTimestamp(captured,readClock(analysis))},
    {"begin_line",first_line},{"end_line",last_line},{"parameter_count",count},{"values",values}};
}
void updatePidParameters(Json& analysis) {
  auto snapshot=readPidParameters(analysis);
  // Preserve the original comparison only when the runtime evidence itself is unchanged.
  if(analysis.contains("pid_parameters")) {
    auto previous=analysis["pid_parameters"];previous.erase("comparison");
    if(previous==snapshot&&analysis["pid_parameters"].contains("comparison"))
      snapshot["comparison"]=analysis["pid_parameters"]["comparison"];
  }
  analysis["pid_parameters"]=std::move(snapshot);
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
