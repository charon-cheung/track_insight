#include "track_insight/report.hpp"
#include <cmath>
#include <map>
#include <set>

namespace track_insight {
Json makeOverview(const Json& analysis) {
  std::map<std::string,std::string> outcomes;
  for(const auto& task:analysis.at("tasks"))outcomes[task.at("id")]=task.at("outcome");
  Json result={{"case_ids",Json::array()},{"best_ids",Json::array()},{"worst_ids",Json::array()},
    {"excluded_cases",0},{"ranked_cases",0},{"ranking_metric","lateral.rmse"},{"averages",Json::object()}};
  struct Average { long double sum{0},squares{0},weight{0},candidate{0};std::size_t count{0};std::string unit; };
  std::map<std::string,Average> averages;
  const std::set<std::string> keys{"lateral","path_distance","yaw_error","vx_residual","vy_residual",
                                  "omega_residual","speed_vector_residual"};
  std::optional<double> best,worst;
  for(const auto& path:analysis.at("paths")) {
    const auto& interval=path.at("interval");
    const auto outcome=outcomes.find(interval.at("navigation_id").get<std::string>());
    if(outcome!=outcomes.end()&&(outcome->second=="replaced"||outcome->second=="failed")) {
      result["excluded_cases"]=result["excluded_cases"].get<int>()+1;continue;
    }
    const auto id=interval.at("id").get<std::string>();result["case_ids"].push_back(id);
    for(const auto& key:keys) {
      const auto found=path.at("metrics").find(key);if(found==path.at("metrics").end())continue;
      const auto& metric=*found;auto& average=averages[key];average.unit=metric.value("unit","");
      const double candidate=metric.value("candidate_s",0.0),weight=metric.value("valid_s",0.0);
      if(std::isfinite(candidate)&&candidate>0)average.candidate+=candidate;
      if(!metric.contains("rmse")||!metric["rmse"].is_number()||!std::isfinite(weight)||weight<=0)continue;
      const double rmse=metric["rmse"];if(!std::isfinite(rmse)||rmse<0)continue;
      average.sum+=rmse;average.squares+=static_cast<long double>(rmse)*rmse*weight;
      average.weight+=weight;++average.count;
      if(key!="lateral")continue;
      result["ranked_cases"]=result["ranked_cases"].get<int>()+1;
      if(!best||rmse<*best){best=rmse;result["best_ids"]=Json::array({id});}
      else if(rmse==*best)result["best_ids"].push_back(id);
      if(!worst||rmse>*worst){worst=rmse;result["worst_ids"]=Json::array({id});}
      else if(rmse==*worst)result["worst_ids"].push_back(id);
    }
  }
  for(const auto& [key,a]:averages)result["averages"][key]={
    {"case_mean_rmse",a.count?Json(double(a.sum/a.count)):Json(nullptr)},
    {"pooled_rmse",a.weight>0?Json(std::sqrt(double(a.squares/a.weight))):Json(nullptr)},
    {"case_count",a.count},{"valid_s",double(a.weight)},{"candidate_s",double(a.candidate)},
    {"coverage",a.candidate>0?Json(double(a.weight/a.candidate)):Json(nullptr)},{"unit",a.unit}};
  return result;
}
}
