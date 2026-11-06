#include "track_insight/report.hpp"
#include <cmath>
#include <map>
#include <set>

namespace track_insight {
Json makeOverview(const Json& analysis) {
  const auto straight=straightPlanIds(analysis);
  const auto report=analysis.value("config",Json::object()).value("report",Json::object());
  const Config defaults;
  const double p95_limit=report.value("worst_case_p95_limit_m",defaults.worst_case_p95_limit),
    max_limit=report.value("worst_case_max_limit_m",defaults.worst_case_max_limit);
  if(!std::isfinite(p95_limit)||p95_limit<0||p95_limit>1000000||
     !std::isfinite(max_limit)||max_limit<0||max_limit>1000000)throw std::invalid_argument("invalid saved worst-case limits");
  std::map<std::string,std::string> outcomes;
  for(const auto& task:analysis.at("tasks"))outcomes[task.at("id")]=task.at("outcome");
  Json result={{"case_ids",Json::array()},{"best_ids",Json::array()},{"worst_ids",Json::array()},
    {"worst_case_limits",{{"p95_m",p95_limit},{"max_m",max_limit}}},
    {"excluded_straight_plan_ids",straight},{"excluded_straight_case_ids",Json::array()},
    {"straight_path_tolerance_m",kStraightPathTolerance},{"excluded_cases",0},{"ranked_cases",0},{"ranking_metric","lateral.rmse"},{"averages",Json::object()}};
  struct Average { long double sum{0},squares{0},weight{0},candidate{0};std::size_t count{0};std::string unit; };
  std::map<std::string,Average> averages;
  const std::set<std::string> keys{"lateral","path_distance","yaw_error","vx_residual","vy_residual",
                                  "omega_residual","speed_vector_residual"};
  Json contributions=Json::object(),groups=Json::array(),exempt=Json::array();
  std::map<double,std::vector<std::string>> ranking;
  for(const auto& path:analysis.at("paths")) {
    const auto& interval=path.at("interval");
    if(straight.count(interval.value("plan_id",""))) {
      result["excluded_straight_case_ids"].push_back(interval.at("id"));continue;
    }
    const auto outcome=outcomes.find(interval.at("navigation_id").get<std::string>());
    if(outcome!=outcomes.end()&&(outcome->second=="replaced"||outcome->second=="failed")) {
      result["excluded_cases"]=result["excluded_cases"].get<int>()+1;continue;
    }
    const auto id=interval.at("id").get<std::string>();result["case_ids"].push_back(id);
    contributions[id]=Json::object();
    for(const auto& key:keys) {
      const auto found=path.at("metrics").find(key);if(found==path.at("metrics").end())continue;
      const auto& metric=*found;auto& average=averages[key];average.unit=metric.value("unit","");
      const double candidate=metric.value("candidate_s",0.0),weight=metric.value("valid_s",0.0);
      const double candidate_s=std::isfinite(candidate)&&candidate>0?candidate:0;
      average.candidate+=candidate_s;
      auto& contribution=contributions[id][key];
      contribution={{"rmse_sum",0},{"squared_error_s",0},{"valid_s",0},
        {"candidate_s",candidate_s},{"count",0},{"unit",average.unit}};
      if(!metric.contains("rmse")||!metric["rmse"].is_number()||!std::isfinite(weight)||weight<=0)continue;
      const double rmse=metric["rmse"];if(!std::isfinite(rmse)||rmse<0)continue;
      const long double squares=static_cast<long double>(rmse)*rmse*weight;
      average.sum+=rmse;average.squares+=squares;average.weight+=weight;++average.count;
      contribution["rmse_sum"]=rmse;
      contribution["squared_error_s"]=std::isfinite(double(squares))?Json(double(squares)):Json(nullptr);
      contribution["valid_s"]=weight;contribution["count"]=1;
      if(key!="lateral")continue;
      result["ranked_cases"]=result["ranked_cases"].get<int>()+1;
      ranking[rmse].push_back(id);
      const auto within=[&](const char* field,double limit) {
        return metric.contains(field)&&metric[field].is_number()&&std::isfinite(metric[field].get<double>())&&
          metric[field].get<double>()>=0&&metric[field].get<double>()<=limit;
      };
      if(within("p95",p95_limit)&&within("max",max_limit))exempt.push_back(id);
    }
  }
  for(const auto& entry:ranking)groups.push_back(entry.second);
  result["suppressed_worst_ids"]=Json::array();
  if(!groups.empty()) {
    result["best_ids"]=groups.front();
    const std::set<std::string> exempt_ids=exempt.get<std::set<std::string>>();
    for(const auto& id:groups.back())
      result[exempt_ids.count(id.get<std::string>())?"suppressed_worst_ids":"worst_ids"].push_back(id);
  }
  result["selection"]={{"version",1},{"contributions",contributions},
    {"ranking_groups",groups},{"worst_exempt_ids",exempt}};
  for(const auto& [key,a]:averages)result["averages"][key]={
    {"case_mean_rmse",a.count?Json(double(a.sum/a.count)):Json(nullptr)},
    {"pooled_rmse",a.weight>0?Json(std::sqrt(double(a.squares/a.weight))):Json(nullptr)},
    {"case_count",a.count},{"valid_s",double(a.weight)},{"candidate_s",double(a.candidate)},
    {"coverage",a.candidate>0?Json(double(a.weight/a.candidate)):Json(nullptr)},{"unit",a.unit}};
  return result;
}
}
