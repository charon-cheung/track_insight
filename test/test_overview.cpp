#include "track_insight/report.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <limits>

using namespace track_insight;
namespace {
Json path(const std::string& id,const std::string& task,Json rmse,double valid,double candidate) {
  return {{"interval",{{"id",id},{"navigation_id",task}}},
    {"metrics",{{"lateral",{{"rmse",rmse},{"valid_s",valid},{"candidate_s",candidate},{"unit","m"}}}}}};
}
}
TEST(Overview, ExcludesFailedAndReplacedWithoutCountingNavigationAgain) {
  Json a={{"tasks",Json::array({{{"id","N1"},{"outcome","succeeded"}},{{"id","N2"},{"outcome","failed"}},
    {{"id","N3"},{"outcome","replaced"}}})},{"paths",Json::array({path("E1","N1",.1,1,2),
    path("E2","N1",.3,3,3),path("E3","N2",0.0,1,1),path("E4","N3",9.0,1,1),
    path("E5","",nullptr,0,5)})}};
  const auto result=makeOverview(a);
  EXPECT_EQ(result["case_ids"],Json::array({"E1","E2","E5"}));EXPECT_EQ(result["excluded_cases"],2);
  EXPECT_EQ(result["best_ids"],Json::array({"E1"}));EXPECT_EQ(result["worst_ids"],Json::array({"E2"}));
  EXPECT_EQ(result["ranked_cases"],2);
  const auto& m=result["averages"]["lateral"];
  EXPECT_EQ(m["case_count"],2);EXPECT_NEAR(m["case_mean_rmse"].get<double>(),.2,1e-12);
  EXPECT_NEAR(m["pooled_rmse"].get<double>(),std::sqrt(.07),1e-12);
  EXPECT_NEAR(m["coverage"].get<double>(),.4,1e-12);EXPECT_EQ(m["valid_s"],4);
  EXPECT_EQ(m["candidate_s"],10);
}
TEST(Overview, MissingValuesDoNotBecomeZerosAndTiesRemainVisible) {
  Json a={{"tasks",Json::array()},{"paths",Json::array({path("E1","",0.0,2,2),path("E2","",0.0,1,1),
    path("E3","",nullptr,0,1),path("E4","",0.0,0,1)})}};
  auto result=makeOverview(a);
  EXPECT_EQ(result["best_ids"],Json::array({"E1","E2"}));EXPECT_EQ(result["worst_ids"],result["best_ids"]);
  EXPECT_EQ(result["averages"]["lateral"]["case_count"],2);
  EXPECT_EQ(result["averages"]["lateral"]["case_mean_rmse"],0);
  a["paths"]=Json::array({path("E1","",nullptr,0,2)});result=makeOverview(a);
  EXPECT_TRUE(result["best_ids"].empty());EXPECT_TRUE(result["worst_ids"].empty());
  EXPECT_TRUE(result["averages"]["lateral"]["case_mean_rmse"].is_null());
  EXPECT_TRUE(result["averages"]["lateral"]["pooled_rmse"].is_null());
  a["paths"]=Json::array();result=makeOverview(a);
  EXPECT_TRUE(result["case_ids"].empty());EXPECT_TRUE(result["averages"].empty());
}
TEST(Overview, EachSignalUsesItsOwnValidDurationAndDoesNotRoundRanking) {
  Json a={{"tasks",Json::array()},{"paths",Json::array({path("E1","",.01049,1,1),path("E2","",.01041,1,1)})}};
  a["paths"][0]["metrics"]["vx_residual"]={{"rmse",.2},{"valid_s",.25},{"candidate_s",1},{"unit","m/s"}};
  a["paths"][1]["metrics"]["vx_residual"]={{"rmse",nullptr},{"valid_s",0},{"candidate_s",1},{"unit","m/s"}};
  const auto result=makeOverview(a);EXPECT_EQ(result["best_ids"],Json::array({"E2"}));
  EXPECT_EQ(result["worst_ids"],Json::array({"E1"}));
  const auto& m=result["averages"]["vx_residual"];
  EXPECT_EQ(m["case_count"],1);EXPECT_NEAR(m["pooled_rmse"].get<double>(),.2,1e-12);
  EXPECT_NEAR(m["coverage"].get<double>(),.125,1e-12);
}

TEST(Overview, StraightPathsAreExcludedFromEveryAverageAndRankingAtTwoCentimeters) {
  Json a={{"tasks",Json::array()},{"plans",Json::array()},{"paths",Json::array()}};
  const std::vector<double> offsets{0,.02,.020001,.1};
  for(std::size_t i=0;i<offsets.size();++i) {
    const auto id="P"+std::to_string(i+1);auto p=path("E"+std::to_string(i+1),"",i<2?9.0:.1*(i-1),1,1);
    p["interval"]["plan_id"]=id;a["paths"].push_back(p);
    a["plans"].push_back({{"id",id},{"valid",true},{"poses",{{0,0,0},{.5,offsets[i],2},{1,0,3}}}});
  }
  const auto result=makeOverview(a);
  EXPECT_EQ(result["case_ids"],Json::array({"E3","E4"}));
  EXPECT_EQ(result["best_ids"],Json::array({"E3"}));EXPECT_EQ(result["worst_ids"],Json::array({"E4"}));
  EXPECT_NEAR(result["averages"]["lateral"]["case_mean_rmse"].get<double>(),.15,1e-12);
  EXPECT_NEAR(result["averages"]["lateral"]["pooled_rmse"].get<double>(),std::sqrt(.025),1e-12);
  a["paths"].erase(a["paths"].begin()+2,a["paths"].end());
  const auto empty=makeOverview(a);EXPECT_TRUE(empty["case_ids"].empty());
  EXPECT_TRUE(empty["best_ids"].empty());EXPECT_TRUE(empty["averages"].empty());
}

TEST(Overview, GeometryUsesFiniteSegmentAndIgnoresHeadingWithoutGuessingInvalidPaths) {
  Json a={{"plans",Json::array({
    {{"id","vertical"},{"valid",true},{"poses",{{2,1,0},{2.02,2,1},{2,3,2}}}},
    {{"id","diagonal"},{"valid",true},{"poses",{{0,0,0},{1,1,1},{2,2,2}}}},
    {{"id","overshoot"},{"valid",true},{"poses",{{0,0},{2,0},{1,0}}}},
    {{"id","closed"},{"valid",true},{"poses",{{0,0},{1,0},{0,0}}}},
    {{"id","short"},{"valid",true},{"poses",{{0,0}}}},
    {{"id","invalid"},{"valid",false},{"poses",{{0,0},{1,0}}}},
    {{"id","missing"},{"valid",true},{"poses",{{0,0},{nullptr,0},{1,0}}}}
  })}};
  EXPECT_EQ(straightPlanIds(a),(std::set<std::string>{"vertical","diagonal"}));
}

TEST(Overview, ReportProjectionRemovesStraightPathsAndContaminatedNavigationAggregates) {
  Json a={{"tasks",Json::array({{{"id","N1"},{"outcome","succeeded"}},{{"id","N2"},{"outcome","succeeded"}}})},
    {"plans",Json::array({{{"id","P1"},{"valid",true},{"poses",{{0,0},{1,0}}}},
                         {{"id","P2"},{"valid",true},{"poses",{{0,0},{.5,.1},{1,0}}}}})},
    {"paths",Json::array()},{"navigation_metrics",{{"N1/confirmed",{{"rmse",9}}},{"N2/inferred",{{"rmse",9}}}}}};
  for(int i=1;i<=4;++i) {
    auto p=path("E"+std::to_string(i),i==4?"N2":"N1",.1*i,1,1);
    p["interval"]["plan_id"]=(i==1||i==4)?"P1":"P2";a["paths"].push_back(p);
  }
  a["overview"]=makeOverview(a);const auto original=a;excludeStraightPaths(a);
  ASSERT_EQ(a["paths"].size(),2U);EXPECT_EQ(a["paths"][0],original["paths"][1]);
  EXPECT_EQ(a["paths"][1],original["paths"][2]);ASSERT_EQ(a["plans"].size(),1U);
  EXPECT_EQ(a["plans"][0]["id"],"P2");ASSERT_EQ(a["tasks"].size(),1U);
  EXPECT_EQ(a["tasks"][0]["id"],"N1");EXPECT_EQ(a["tasks"][0]["path_filter_applied"],true);
  EXPECT_TRUE(a["navigation_metrics"].empty());EXPECT_EQ(a["overview"],original["overview"]);
  const auto once=a;excludeStraightPaths(a);EXPECT_EQ(a,once);
}

TEST(Overview, WorstCandidateWithinBothLimitsIsNotMarked) {
  Json a={{"tasks",Json::array()},{"paths",Json::array({path("E1","",.02,1,1),path("E2","",.03,1,1)})}};
  auto& m=a["paths"][1]["metrics"]["lateral"];m["p95"]=.05;m["max"]=.055;
  const auto baseline=makeOverview(a);
  EXPECT_TRUE(baseline["worst_ids"].empty());
  EXPECT_EQ(baseline["suppressed_worst_ids"],Json::array({"E2"}));
  EXPECT_EQ(baseline["best_ids"],Json::array({"E1"}));
  for(const auto* key:{"p95","max"}) {
    const double limit=m[key];m[key]=std::nextafter(limit,1.0);
    EXPECT_EQ(makeOverview(a)["worst_ids"],Json::array({"E2"}));
    m[key]=nullptr;EXPECT_EQ(makeOverview(a)["worst_ids"],Json::array({"E2"}));
    m[key]=limit;
  }
  a["paths"][0]["metrics"]["lateral"]["rmse"]=.03;
  EXPECT_EQ(makeOverview(a)["worst_ids"],Json::array({"E1"}));
  EXPECT_NEAR(baseline["averages"]["lateral"]["case_mean_rmse"].get<double>(),.025,1e-12);
}

TEST(Overview, SavedWorstLimitsOverrideDefaultsWithoutChangingStatistics) {
  Json a={{"tasks",Json::array()},{"paths",Json::array({path("E1","",.02,1,1),path("E2","",.03,1,1)})}};
  auto& metric=a["paths"][1]["metrics"]["lateral"];metric["p95"]=.08;metric["max"]=.09;
  const auto legacy=makeOverview(a);EXPECT_EQ(legacy["worst_ids"],Json::array({"E2"}));
  a["config"]["report"]={{"worst_case_p95_limit_m",.08},{"worst_case_max_limit_m",.09}};
  const auto custom=makeOverview(a);EXPECT_TRUE(custom["worst_ids"].empty());
  EXPECT_EQ(custom["suppressed_worst_ids"],Json::array({"E2"}));
  EXPECT_EQ(custom["averages"],legacy["averages"]);EXPECT_EQ(custom["best_ids"],legacy["best_ids"]);
  ASSERT_TRUE(custom.contains("worst_case_limits"));
  EXPECT_EQ(custom["worst_case_limits"],(Json{{"p95_m",.08},{"max_m",.09}}));
  metric["max"]=std::nextafter(.09,1.0);EXPECT_EQ(makeOverview(a)["worst_ids"],Json::array({"E2"}));
}

TEST(Overview, SavesCaseContributionsAndRankingDecisions) {
  Json a={{"tasks",Json::array()},{"paths",Json::array({path("E1","",.02,1,2),
    path("E2","",.03,3,3),path("E3","",nullptr,0,5),path("E4","",.03,1,1)})}};
  a["paths"][1]["metrics"]["lateral"]["p95"]=.05;
  a["paths"][1]["metrics"]["lateral"]["max"]=.055;
  const auto overview=makeOverview(a);
  ASSERT_TRUE(overview.contains("selection"));const auto& selection=overview.at("selection");
  EXPECT_EQ(selection.at("version"),1);
  EXPECT_EQ(selection.at("ranking_groups"),(Json::array({Json::array({"E1"}),Json::array({"E2","E4"})})));
  EXPECT_EQ(selection.at("worst_exempt_ids"),Json::array({"E2"}));
  const auto& c=selection.at("contributions").at("E2").at("lateral");
  EXPECT_EQ(c.at("count"),1);EXPECT_DOUBLE_EQ(c.at("rmse_sum"),.03);
  EXPECT_NEAR(c.at("squared_error_s").get<double>(),.0027,1e-15);
  EXPECT_DOUBLE_EQ(c.at("valid_s"),3);EXPECT_DOUBLE_EQ(c.at("candidate_s"),3);
  const auto& missing=selection.at("contributions").at("E3").at("lateral");
  EXPECT_EQ(missing.at("count"),0);EXPECT_EQ(missing.at("candidate_s"),5);
  EXPECT_EQ(missing.at("rmse_sum"),0);EXPECT_EQ(missing.at("valid_s"),0);
}

TEST(Overview, SelectionContractForDeletedSubsets) {
  Json fixtures=Json::array();
  for(int variant:{0,1,2}) {
    const bool custom=variant==1;
    Json a={{"meta",{{"input_path","fixture"},{"log_path","fixture.log"}}},
      {"tasks",Json::array({{{"id","failed"},{"outcome","failed"}},{{"id","replaced"},{"outcome","replaced"}}})},
      {"plans",Json::array({{{"id","straight"},{"valid",true},{"poses",{{0,0},{1,0}}}}})},
      {"paths",Json::array({path("zero","",0,2,3),path("tie1","",.02,.25,1),path("tie2","",.02,3,4),
        path("missing","",nullptr,0,5),path("negative","",-1,1,2),path("no_duration","",.9,0,2),
        path("large","",variant==2?1e160:1e12,1,1),path("tiny","",1e-12,1,1),path("failed","failed",9,1,1),
        path("replaced","replaced",0,1,1),path("straight","",12,1,1)})}};
    a["paths"].back()["interval"]["plan_id"]="straight";
    if(custom)a["config"]["report"]={{"worst_case_p95_limit_m",.08},{"worst_case_max_limit_m",.09}};
    for(auto& p:a["paths"]) {
      p["interval"]["plan_id"]=p["interval"].value("plan_id",p["interval"]["id"].get<std::string>());
      p["metrics"]["vx_residual"]={{"rmse",.1},{"valid_s",.5},{"candidate_s",2},{"unit","m/s"}};
    }
    a["paths"][1]["metrics"]["lateral"]["p95"]=custom?.08:.05;
    a["paths"][1]["metrics"]["lateral"]["max"]=custom?.09:.055;
    a["paths"][2]["metrics"]["lateral"]["p95"]=custom?.08:.05;
    a["paths"][2]["metrics"]["lateral"]["max"]=std::nextafter(custom?.09:.055,1.0);
    a["paths"][3]["metrics"]["vx_residual"]["valid_s"]=std::numeric_limits<double>::infinity();
    a["paths"][4]["metrics"]["vx_residual"]["candidate_s"]=-1;
    a["paths"][5]["metrics"].erase("vx_residual");
    a["overview"]=makeOverview(a);const auto& overview=a["overview"];
    ASSERT_TRUE(overview.contains("selection"));const auto& selection=overview.at("selection");
    const auto ids=overview.at("case_ids").get<std::vector<std::string>>();ASSERT_EQ(ids.size(),8U);
    EXPECT_FALSE(selection.at("contributions").contains("failed"));
    EXPECT_FALSE(selection.at("contributions").contains("replaced"));
    EXPECT_FALSE(selection.at("contributions").contains("straight"));
    Json checks=Json::array();
    for(unsigned mask=0;mask<(1U<<ids.size());++mask) {
      auto filtered=a;Json removed=Json::array();
      for(std::size_t i=0;i<ids.size();++i)if(mask&(1U<<i))removed.push_back(ids[i]);
      auto& paths=filtered["paths"];
      paths.erase(std::remove_if(paths.begin(),paths.end(),[&](const Json& p){
        return std::find(removed.begin(),removed.end(),p["interval"]["id"])!=removed.end();}),paths.end());
      auto expected=makeOverview(filtered);expected.erase("selection");
      checks.push_back({{"deleted",removed},{"expected",expected}});
    }
    fixtures.push_back({{"analysis",a},{"checks",checks}});
  }
  // Export the actual C++ decisions for the browser contract check; no production test hooks.
  if(const char* output=std::getenv("TRACK_INSIGHT_OVERVIEW_FIXTURE"))writeText(output,fixtures.dump());
}
