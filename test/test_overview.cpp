#include "track_insight/report.hpp"
#include <gtest/gtest.h>
#include <cmath>

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
