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
  std::string line(int second,const std::string& text) {
    return "[container-1] [INFO] [17933560"+std::to_string(second)+".000000000] [controller_server]: "+text+"\n";
  }
  std::string block(int second,double gain=1) {
    std::string text=line(second,"PID_LOG parameter_snapshot_begin controller=PidTrackingController count=45");
    text+=line(second,"PID_LOG parameter name=PidTrackingController.kp_x type=double value="+std::to_string(gain));
    text+=line(second,"PID_LOG parameter name=PidTrackingController.hold type=bool value=true");
    text+=line(second,"PID_LOG parameter name=PidTrackingController.frame type=string value=\"base link\"");
    for(int i=0;i<42;++i)text+=line(second,"PID_LOG parameter name=PidTrackingController.p"+std::to_string(i)+" type=integer value="+std::to_string(i));
    return text+line(second,"PID_LOG parameter_snapshot_end controller=PidTrackingController count=45");
  }
  Json analysis(const std::string& text) {
    auto file=root/"nav.log";writeText(file,text);
    Json a={{"meta",{{"log_path",file.string()},{"log_sha256","log-hash"},{"input_sha256","bag-hash"},
      {"log_compatible",true},{"start_ns","1793356040000000000"},{"end_ns","1793356050000000000"}}},
      {"clock",{{"origin_ns","1793356040000000000"},{"wall_clock",true},{"offsets",Json::array({{{"from_ns","1793356000000000000"},{"seconds",28800}}})}}},
      {"events",Json::array()}};
    for(const auto& e:parseLog(file))a["events"].push_back({{"ns",std::to_string(e.ns)},{"line",e.line},{"source",e.source},{"raw",e.raw},{"kind",e.kind}});
    return a;
  }
  Json read(double gain) {return readPidParameters(analysis(block(30,gain)));}

};
Json run(const std::string& id,const std::string& start,Json snapshot=nullptr) {
  Json result={{"id",id},{"name",id},{"start_ns",start},{"source_sha256",id}};
  if(!snapshot.is_null())result["pid_parameters"]=snapshot;
  return result;
}
}
TEST_F(Parameters, ReadsCompleteTypedRuntimeSnapshotAtTestTime) {
  auto a=analysis(block(10,.5)+block(30,.8)+block(60,9));
  const auto snapshot=readPidParameters(a);
  ASSERT_EQ(snapshot["status"],"available");EXPECT_EQ(snapshot["values"].size(),45U);
  EXPECT_EQ(snapshot["values"]["PidTrackingController.kp_x"],.8);
  EXPECT_EQ(snapshot["values"]["PidTrackingController.hold"],true);
  EXPECT_EQ(snapshot["values"]["PidTrackingController.frame"],"base link");
  EXPECT_EQ(snapshot["values"]["PidTrackingController.p41"],41);
  EXPECT_EQ(snapshot["captured_at_ns"],"1793356030000000000");
  EXPECT_EQ(snapshot["source_kind"],"navigation_log");EXPECT_EQ(snapshot["log_sha256"],"log-hash");
  EXPECT_FALSE(snapshot["captured_at_label"].get<std::string>().empty());
  std::filesystem::remove(root/"nav.log");EXPECT_EQ(readPidParameters(a),snapshot);
}
TEST_F(Parameters, MissingOrUnrelatedLogNeverFallsBackToYaml) {
  auto a=analysis("");a["pid_parameters"]={{"schema_version",1},{"values",{{"kp_x",99}}}};
  updatePidParameters(a);EXPECT_EQ(a["pid_parameters"]["status"],"unknown");EXPECT_FALSE(a["pid_parameters"].contains("values"));
  a=analysis(block(60));EXPECT_EQ(readPidParameters(a)["status"],"unknown");
  a=analysis(block(30));a["meta"]["log_compatible"]=false;EXPECT_EQ(readPidParameters(a)["status"],"unknown");
  a=analysis(block(30));a["events"][0]["source"]="other.log";EXPECT_EQ(readPidParameters(a)["status"],"unknown");
}
TEST_F(Parameters, IncompleteOrMalformedLatestSnapshotDoesNotReusePreviousStartup) {
  const auto begin=line(30,"PID_LOG parameter_snapshot_begin controller=PidTrackingController count=45");
  EXPECT_EQ(readPidParameters(analysis(block(10)+begin))["status"],"unknown");
  auto invalid=block(30);auto pos=invalid.find("name=PidTrackingController.p41");
  invalid.replace(pos,std::string("name=PidTrackingController.p41").size(),"name=PidTrackingController.p40");
  EXPECT_EQ(readPidParameters(analysis(block(10)+invalid))["status"],"unknown");
  invalid=block(30);pos=invalid.find("type=double value=1.000000");
  invalid.replace(pos,std::string("type=double value=1.000000").size(),"type=double value=nan");
  EXPECT_EQ(readPidParameters(analysis(invalid))["status"],"unknown");
  EXPECT_EQ(readPidParameters(analysis(block(10)+line(30,"Configuring controller interface")))["status"],"unknown");
}
TEST_F(Parameters, RestartDuringTestAndClockResetAreNotOneRuntimeSnapshot) {
  EXPECT_EQ(readPidParameters(analysis(block(30)+block(45)))["status"],"unknown");
  EXPECT_EQ(readPidParameters(analysis(block(30)+line(45,"PID_LOG holonomic_parameters nominal=0.8")))["status"],"unknown");
  EXPECT_EQ(readPidParameters(analysis(block(30)+block(20)))["status"],"unknown");
  auto a=analysis(block(30));a["clock"]["wall_clock"]=false;
  EXPECT_EQ(readPidParameters(a)["status"],"unknown");
}
TEST_F(Parameters, HistoricalClockResetsDoNotInvalidateTheMatchingTestSegment) {
  const auto current=block(30,.8)+line(45,"PID_LOG tracking_parameters current=1");
  const auto historical=block(20,9)+line(10,"Creating controller server");
  auto snapshot=readPidParameters(analysis(historical+current));
  ASSERT_EQ(snapshot["status"],"available");
  EXPECT_EQ(snapshot["values"]["PidTrackingController.kp_x"],.8);
  // Later recordings outside this test must not invalidate its snapshot either.
  snapshot=readPidParameters(analysis(historical+current+block(70,9)+block(60,8)));
  EXPECT_EQ(snapshot["status"],"available");
  // Two clock epochs overlapping this test cannot be distinguished safely.
  EXPECT_EQ(readPidParameters(analysis(current+block(20,9)+line(45,"PID_LOG tracking_parameters current=2")))["status"],"unknown");
  EXPECT_EQ(readPidParameters(analysis(current+block(10,9)))["status"],"unknown");
  // Never borrow a snapshot from an older epoch.
  EXPECT_EQ(readPidParameters(analysis(block(20,9)+line(10,"Creating controller server")+
    line(45,"PID_LOG tracking_parameters current=1")))["status"],"unknown");
}
TEST_F(Parameters, ImmediateEarlierRoundIsBaselineAndRerenderKeepsItFrozen) {
  const auto first=read(1),second=read(2);
  Json analysis={{"meta",{{"start_ns","30"},{"input_sha256","self"}}},{"pid_parameters",second}};
  auto runs=Json::array({run("future","40",second),run("older","10",first),run("previous","20",first),run("self","30",second)});
  attachPidParameterBaseline(analysis,runs);
  EXPECT_EQ(analysis["pid_parameters"]["comparison"]["run_id"],"previous");
  EXPECT_EQ(analysis["pid_parameters"]["comparison"]["values"],first["values"]);
  const auto saved=analysis;runs[2]["pid_parameters"]=second;attachPidParameterBaseline(analysis,runs);EXPECT_EQ(analysis,saved);
  analysis["pid_parameters"]=second;runs[2]["pid_parameters"]={{"schema_version",1},{"values",{{"kp_x",99}}}};attachPidParameterBaseline(analysis,runs);
  EXPECT_EQ(analysis["pid_parameters"]["comparison"]["status"],"previous_snapshot_missing");
  EXPECT_FALSE(analysis["pid_parameters"]["comparison"].contains("values"));
  analysis["pid_parameters"]=second;attachPidParameterBaseline(analysis,Json::array());
  EXPECT_EQ(analysis["pid_parameters"]["comparison"]["status"],"no_previous_round");
  analysis.erase("pid_parameters");const auto legacy=analysis;attachPidParameterBaseline(analysis,runs);EXPECT_EQ(analysis,legacy);
}
