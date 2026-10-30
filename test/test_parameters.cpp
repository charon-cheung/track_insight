#include "track_insight/parameter_snapshot.hpp"
#include <gtest/gtest.h>
#include <unistd.h>

using namespace track_insight;
namespace {
struct Parameters:testing::Test {
  std::filesystem::path root;
  void SetUp() override {
    std::string pattern="/tmp/track_pid_parameters_XXXXXX";root=mkdtemp(pattern.data());
  }
  void TearDown() override {std::filesystem::remove_all(root);}
  Json read(const std::string& body) {
    const auto file=root/"pid.yaml";writeText(file,"controller_server:\n  ros__parameters:\n    controller_frequency: 15.0\n    PidTrackingController:\n"+body);
    return readPidParameters(file);
  }
};
Json run(const std::string& id,const std::string& start,Json snapshot=nullptr) {
  Json result={{"id",id},{"name",id},{"start_ns",start},{"source_sha256",id}};
  if(!snapshot.is_null())result["pid_parameters"]=snapshot;
  return result;
}
}
TEST_F(Parameters, CapturesAllTypedValuesAndIgnoresFormattingAndComments) {
  const auto a=read("      kp_x: 0.80 # baseline\n      hold: true\n      frame: base_link\n      code: '001'\n      axes: [0.4, 0.8]\n");
  const auto b=read("      kp_x: .8\n      hold: True\n      frame: base_link\n      code: '001'\n      axes: [0.40, 0.80]\n");
  EXPECT_EQ(a["values"],b["values"]);EXPECT_EQ(a["values"].size(),6U);
  EXPECT_EQ(a["values"]["PidTrackingController.code"],"001");EXPECT_EQ(a["values"]["PidTrackingController.hold"],true);
  EXPECT_EQ(a["values"]["controller_frequency"],15);EXPECT_EQ(a["source_path"],(root/"pid.yaml").string());
  EXPECT_FALSE(a["captured_at_label"].get<std::string>().empty());
  EXPECT_THROW(read("      kp_x: .nan\n"),std::exception);
  EXPECT_THROW(read("      kp_x: 1\n      kp_x: 2\n"),std::exception);
  EXPECT_THROW(readPidParameters(root/"missing.yaml"),std::exception);
  writeText(root/"other.yaml","unexpected: true");EXPECT_THROW(readPidParameters(root/"other.yaml"),std::exception);
}
TEST_F(Parameters, ImmediateEarlierRoundIsBaselineAndRerenderKeepsItFrozen) {
  const auto first=read("      kp_x: 1\n"),second=read("      kp_x: 2\n");
  Json analysis={{"meta",{{"start_ns","30"},{"input_sha256","self"}}},{"pid_parameters",second}};
  auto runs=Json::array({run("future","40",second),run("older","10",first),run("previous","20",first),run("self","30",second)});
  attachPidParameterBaseline(analysis,runs);
  EXPECT_EQ(analysis["pid_parameters"]["comparison"]["run_id"],"previous");
  EXPECT_EQ(analysis["pid_parameters"]["comparison"]["values"],first["values"]);
  const auto saved=analysis;runs[2]["pid_parameters"]=second;attachPidParameterBaseline(analysis,runs);EXPECT_EQ(analysis,saved);
  analysis["pid_parameters"]=second;runs[2].erase("pid_parameters");attachPidParameterBaseline(analysis,runs);
  EXPECT_EQ(analysis["pid_parameters"]["comparison"]["status"],"previous_snapshot_missing");
  EXPECT_FALSE(analysis["pid_parameters"]["comparison"].contains("values"));
  analysis["pid_parameters"]=second;attachPidParameterBaseline(analysis,Json::array());
  EXPECT_EQ(analysis["pid_parameters"]["comparison"]["status"],"no_previous_round");
  analysis.erase("pid_parameters");const auto legacy=analysis;attachPidParameterBaseline(analysis,runs);EXPECT_EQ(analysis,legacy);
}
