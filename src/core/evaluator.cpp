#include "track_insight/domain.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace track_insight {
namespace {
using Cells = std::map<std::string,std::vector<WeightedSample>>;
Cells cells(const PathResult& path, const std::string& stage = "") {
  Cells result;
  for(const auto& s:path.tracking) {
    if(!stage.empty() && s.stage!=stage)continue;
    for(const auto& value:std::vector<std::pair<std::string,double>>{
        {"lateral",s.lateral},{"path_distance",s.distance},{"endpoint_distance",s.endpoint}}) {
      const bool valid=s.valid&&(value.first!="lateral"||s.lateral_valid);
      result[value.first].push_back({s.ns,s.end_ns,value.second,valid,
        s.valid&&!s.lateral_valid&&value.first=="lateral"?"path_tangent_undefined":s.reason});
    }
    result["yaw_error"].push_back({s.ns,s.end_ns,s.yaw_error.value_or(0),
      s.valid&&s.yaw_error.has_value(),s.valid?"target_yaw_unavailable":s.reason});
  }
  for(const auto& s:path.velocity) {
    auto add=[&](TimestampNs begin,TimestampNs end) {
      for(const auto& value:std::vector<std::pair<std::string,double>>{
          {"vx_residual",s.feedback.vx-s.command.vx},{"vy_residual",s.feedback.vy-s.command.vy},
          {"omega_residual",s.feedback.omega-s.command.omega},
          {"speed_vector_residual",std::hypot(s.feedback.vx-s.command.vx,s.feedback.vy-s.command.vy)}})
        result[value.first].push_back({begin,end,value.second,s.valid,s.reason});
    };
    if(stage.empty())add(s.ns,s.end_ns);
    else for(const auto& t:path.tracking)if(t.stage==stage && t.end_ns>s.ns && t.ns<s.end_ns)
      add(std::max(s.ns,t.ns),std::min(s.end_ns,t.end_ns));
  }
  for(const auto& name:{"lateral","path_distance","endpoint_distance","yaw_error","vx_residual",
                       "vy_residual","omega_residual","speed_vector_residual"})result[name];
  return result;
}
std::string quality(const Interval& i) {
  return i.identity=="confirmed"&&i.start_quality=="confirmed"&&i.end_quality=="confirmed"?
    "confirmed":"inferred";
}
std::optional<double> threshold(const std::string& name,const Config& c) {
  if(name=="lateral")return c.lateral_threshold;
  if(name=="omega_residual")return c.angular_velocity_threshold;
  if(name=="vx_residual"||name=="vy_residual"||name=="speed_vector_residual")return c.velocity_threshold;
  return std::nullopt;
}
std::map<std::string,Metric> metrics(const Cells& values,const Interval& interval,const Config& config,
                                    std::optional<double> candidate=std::nullopt) {
  std::map<std::string,Metric> result;
  for(const auto& item:values) {
    const bool angle=item.first=="yaw_error";
    const std::string unit=angle?"rad":item.first=="omega_residual"?"rad/s":
      item.first.find("residual")!=std::string::npos?"m/s":"m";
    auto m=summarize(item.second,interval,unit,threshold(item.first,config));
    if(candidate) {m.candidate_s=*candidate;m.coverage=*candidate>0?
      std::optional<double>(m.valid_s / *candidate):std::nullopt;}
    if(m.valid_s>0)m.quality=quality(interval);
    result[item.first]=m;
  }
  return result;
}
void addFindings(PathResult& path,const Cells& values,const Config& config) {
  for(const auto& item:values) {
    const auto limit=threshold(item.first,config);if(!limit)continue;
    std::optional<Finding> active;
    auto finish=[&](){if(active){path.findings.push_back(*active);active.reset();}};
    for(const auto& s:item.second) {
      if(!s.valid||std::abs(s.value)<=*limit||s.end<=s.start){finish();continue;}
      if(active&&active->end!=s.start)finish();
      if(!active)active=Finding{"tracking",item.first+"_threshold",
        "Configured threshold exceeded",path.interval.id,s.start,s.end,std::abs(s.value)};
      else {active->end=s.end;active->peak=std::max(*active->peak,std::abs(s.value));}
    }
    finish();
  }
  for(const auto& s:path.tracking)if(!s.valid) {
    if(!path.findings.empty()&&path.findings.back().category=="data_quality"&&
       path.findings.back().kind==s.reason&&path.findings.back().end==s.ns)
      path.findings.back().end=s.end_ns;
    else path.findings.push_back({"data_quality",s.reason,"Geometry sample unavailable",
      path.interval.id,s.ns,s.end_ns,std::nullopt});
  }
}
}
Evaluation evaluate(const RunData& data,const Config& config) {
  Evaluation result;result.tasks=navigationTasks(data);
  std::map<std::string,Cells> navigation;
  for(const auto& interval:executionIntervals(data,result.tasks)) {
    auto plan=std::find_if(data.plans.begin(),data.plans.end(),[&](const auto& p){return p.id==interval.plan_id;});
    if(plan==data.plans.end())continue;
    PathResult path;path.interval=interval;path.tracking=matchTracking(*plan,data,interval,config);
    path.velocity=alignVelocity(data,interval,config);
    auto values=cells(path);path.metrics=metrics(values,interval,config);
    std::set<std::string> stages;for(const auto& s:path.tracking)stages.insert(s.stage);
    double covered_stage_s=0;
    for(const auto& s:path.tracking)covered_stage_s+=(s.end_ns-s.ns)/1e9;
    const double unknown_s=std::max(0.0,(interval.end-interval.start)/1e9-covered_stage_s);
    if(unknown_s>0)stages.insert("unknown");
    for(const auto& stage:stages) {
      double candidate=stage=="unknown"?unknown_s:0;for(const auto& s:path.tracking)if(s.stage==stage)candidate+=(s.end_ns-s.ns)/1e9;
      path.stages[stage]=metrics(cells(path,stage),interval,config,candidate);
    }
    const Task* task=nullptr;
    for(const auto& candidate:result.tasks)if(candidate.id==interval.navigation_id)task=&candidate;
    for(const auto& item:terminalMetrics(data,task,interval,config))path.metrics[item.first]=item.second;
    addFindings(path,values,config);
    addObservations(path,config);
    if(!interval.navigation_id.empty())for(const auto& item:values) {
      auto& target=navigation[interval.navigation_id+"/"+quality(interval)][item.first];
      target.insert(target.end(),item.second.begin(),item.second.end());
    }
    result.paths.push_back(std::move(path));
  }
  for(const auto& task:result.tasks)for(const auto& q:{"inferred","confirmed"}) {
    auto key=task.id+"/"+q;auto found=navigation.find(key);if(found==navigation.end())continue;
    Interval interval;interval.start=task.start;interval.end=task.end;
    interval.identity=q;interval.start_quality=q;interval.end_quality=q;
    result.navigation_metrics[key]=metrics(found->second,interval,config);
    for(const auto& p:result.paths)if(p.interval.navigation_id==task.id&&p.interval.end==task.end&&quality(p.interval)==q)
      for(const auto& m:terminalMetrics(data,&task,p.interval,config))result.navigation_metrics[key][m.first]=m.second;
  }
  return result;
}
}  // namespace track_insight
