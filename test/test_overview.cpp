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
