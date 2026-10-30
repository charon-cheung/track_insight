#include "track_insight/report.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace track_insight {
std::set<std::string> straightPlanIds(const Json& analysis) {
  std::set<std::string> result;
  if(!analysis.contains("plans"))return result;
  for(const auto& plan:analysis.at("plans")) {
    if(!plan.value("valid",false)||!plan.contains("poses"))continue;
    const auto& poses=plan.at("poses");
    if(!poses.is_array()||poses.size()<2)continue;
    bool valid=true;
    for(const auto& p:poses)if(!p.is_array()||p.size()<2||!p[0].is_number()||!p[1].is_number()||
        !std::isfinite(p[0].get<double>())||!std::isfinite(p[1].get<double>())){valid=false;break;}
    if(!valid)continue;
    const double x=poses.front()[0],y=poses.front()[1];
    const double dx=poses.back()[0].get<double>()-x,dy=poses.back()[1].get<double>()-y;
    const double length=std::hypot(dx,dy);
    // A coincident start/goal has no reference direction; keep it for review.
    if(!std::isfinite(length)||length<=1e-9)continue;
    double maximum=0;
    for(const auto& p:poses) {
      const double px=p[0].get<double>()-x,py=p[1].get<double>()-y;
      const double along=std::clamp(px*(dx/length)+py*(dy/length),0.0,length);
      const double distance=std::hypot(px-along*(dx/length),py-along*(dy/length));
      if(!std::isfinite(distance)){valid=false;break;}
      maximum=std::max(maximum,distance);
    }
    if(valid&&maximum<=kStraightPathTolerance+1e-12)result.insert(plan.at("id").get<std::string>());
  }
  return result;
}

void excludeStraightPaths(Json& analysis) {
  const auto& ids=analysis.at("overview").at("excluded_straight_plan_ids");
  const std::set<std::string> excluded=ids.get<std::set<std::string>>();
  if(excluded.empty())return;
  std::set<std::string> affected,remaining;
  auto& paths=analysis.at("paths");
  paths.erase(std::remove_if(paths.begin(),paths.end(),[&](const Json& path){
    const auto task=path.at("interval").value("navigation_id","");
    if(excluded.count(path.at("interval").value("plan_id",""))){affected.insert(task);return true;}
    remaining.insert(task);return false;
  }),paths.end());
  auto& plans=analysis.at("plans");
  plans.erase(std::remove_if(plans.begin(),plans.end(),[&](const Json& plan){
    return excluded.count(plan.at("id").get<std::string>());
  }),plans.end());
  auto& tasks=analysis.at("tasks");
  tasks.erase(std::remove_if(tasks.begin(),tasks.end(),[&](Json& task){
    const auto id=task.at("id").get<std::string>();
    if(!affected.count(id))return false;
    // Whole-navigation metrics include the excluded intervals: never reuse them as filtered metrics.
    analysis["navigation_metrics"].erase(id+"/confirmed");
    analysis["navigation_metrics"].erase(id+"/inferred");
    task["path_filter_applied"]=true;
    return !remaining.count(id);
  }),tasks.end());
}
}
