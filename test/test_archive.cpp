#include "track_insight/archive.hpp"
#include <gtest/gtest.h>
#include <unistd.h>
#include <algorithm>
#include <csignal>
#include <sys/wait.h>

using namespace track_insight;
namespace fs=std::filesystem;
namespace {
struct ArchiveTest:testing::Test {
  fs::path root,archive;
  void SetUp() override {
    auto pattern=(fs::temp_directory_path()/"track_insight_archive_XXXXXX").string();
    std::vector<char> chars(pattern.begin(),pattern.end());chars.push_back(0);
    root=mkdtemp(chars.data());archive=root/"history";
  }
  void TearDown() override {fs::remove_all(root);}
  Json analysis(char hash='a') {
    RunData data;data.input_hash=std::string(64,hash);data.input_path="/recordings/测试 bag/";
    data.start=1793006081000000000LL;data.end=data.start+10*kSecond;
    Evaluation evaluation;
    for(int i=1;i<=3;++i) {
      Task task;task.id="N"+std::to_string(i);task.outcome=i==3?"failed":"succeeded";
      evaluation.tasks.push_back(task);
      PathResult path;path.interval.id="E"+std::to_string(i);path.interval.plan_id="P"+std::to_string(i);
      path.interval.navigation_id=task.id;path.interval.start=data.start;path.interval.end=data.end;
      Metric metric;metric.rmse=i==3?9:.1*i;metric.valid_s=5;metric.candidate_s=10;metric.unit="m";
      path.metrics["lateral"]=metric;path.metrics["path_distance"]=metric;path.metrics["vx_residual"]=metric;
      evaluation.paths.push_back(path);
    }
    return makeAnalysis(data,evaluation,Config{},DisplayClock{data.start,true,{{data.start,28800}}});
  }
  ArchiveResult render(Json a,bool replace=false,std::optional<std::string> name={},std::optional<std::string> note={}) {
    return renderArchive(std::move(a),archive,TRACK_INSIGHT_RESOURCES,replace,name,note);
  }
  Json catalog(){return Json::parse(readText(archive/"runs.json"));}
};
TEST_F(ArchiveTest, AccumulatesSameTimeBagsAndReplacePreservesOtherRoundsAndLabels) {
  const auto first=render(analysis(),false,"第一轮","原备注");ASSERT_EQ(first.code,0);
  const auto original=readText(first.report),home=readText(first.home);
  EXPECT_THROW(render(analysis()),std::runtime_error);EXPECT_EQ(readText(first.report),original);
  EXPECT_EQ(readText(first.home),home);
  const auto second=render(analysis('b'));EXPECT_NE(second.report,first.report);
  const auto other=readText(second.report);auto changed=analysis();changed["paths"][0]["metrics"]["lateral"]["rmse"]=.05;
  const auto replaced=render(changed,true);EXPECT_EQ(replaced.report,first.report);
  const auto c=catalog();ASSERT_EQ(c["runs"].size(),2U);EXPECT_EQ(readText(second.report),other);
  const auto row=*std::find_if(c["runs"].begin(),c["runs"].end(),[](const auto& r){return r["name"]=="第一轮";});
  EXPECT_NE(home.find("id=\"roundComparison\""),std::string::npos);
  EXPECT_NE(home.find("archiveCompareBaseline"),std::string::npos);
  EXPECT_EQ(home.find("__TRACK_ARCHIVE_COMPARISON_"),std::string::npos);
  EXPECT_EQ(row["note"],"原备注");EXPECT_NEAR(row["overview"]["averages"]["lateral"]["case_mean_rmse"].template get<double>(),.125,1e-12);
  EXPECT_EQ(row["overview"]["excluded_cases"],1);EXPECT_EQ(row["worst"][0]["plan_id"],"P2");
  EXPECT_EQ(row["worst"][0]["rmse"],.2);
  EXPECT_EQ(Json::parse(readText(first.report.parent_path()/"analysis.json"))["paths"],changed["paths"]);
  EXPECT_NE(readText(first.report).find("../../index.html"),std::string::npos);
  render(changed,true,"改名","");const auto updated=catalog();
  EXPECT_TRUE(std::any_of(updated["runs"].begin(),updated["runs"].end(),[](const auto& r){return r["name"]=="改名"&&r["note"]=="";}));
}
TEST_F(ArchiveTest, InvalidGenerationAndConcurrentWriterLeavePublishedFilesUntouched) {
  const auto first=render(analysis());const auto home=readText(first.home),report=readText(first.report),index=readText(archive/"runs.json");
  auto broken=analysis();broken["paths"][0]["tracking"].push_back(Json::array({1}));
  EXPECT_THROW(render(broken,true),std::runtime_error);
  EXPECT_EQ(readText(first.home),home);EXPECT_EQ(readText(first.report),report);EXPECT_EQ(readText(archive/"runs.json"),index);
  fs::remove(archive.string()+".track_insight.archive.lock");
  fs::create_directory(archive.string()+".track_insight.archive.lock");
  EXPECT_THROW(render(analysis('b')),std::runtime_error);EXPECT_EQ(readText(first.home),home);
  fs::remove(archive.string()+".track_insight.archive.lock");
  writeText(first.report.parent_path()/"user.txt","keep");
  EXPECT_THROW(render(analysis(),true),std::runtime_error);EXPECT_EQ(readText(first.report),report);
}
TEST_F(ArchiveTest, RefusesForeignDirectoriesSymlinksAndCorruptIndexes) {
  fs::create_directories(archive);writeText(archive/"notes.txt","keep");
  EXPECT_THROW(render(analysis()),std::runtime_error);EXPECT_EQ(readText(archive/"notes.txt"),"keep");
  archive=root/"valid";const auto first=render(analysis());const auto home=readText(first.home);
  fs::rename(archive/"runs.json",root/"catalog");fs::create_symlink(root/"catalog",archive/"runs.json");
  EXPECT_THROW(render(analysis('b')),std::runtime_error);EXPECT_EQ(readText(first.home),home);
  fs::remove(archive/"runs.json");writeText(archive/"runs.json","{}");
  EXPECT_THROW(render(analysis('b')),std::exception);EXPECT_EQ(readText(first.home),home);
  archive=root/"link";fs::create_directory_symlink(root/"valid",archive);
  EXPECT_THROW(render(analysis()),std::runtime_error);
}
TEST_F(ArchiveTest, MissingMetricsStayNullAndUntrustedTextIsEmbeddedAsData) {
  auto a=analysis();for(auto& p:a["paths"])p["metrics"]["lateral"]["rmse"]=nullptr;
  const std::string name="测试 </script><script>alert(1)</script> & \"round\"";
  const auto result=render(a,false,name,"<备注>");const auto row=catalog()["runs"][0];
  EXPECT_EQ(row["name"],name);EXPECT_EQ(row["note"],"<备注>");
  EXPECT_TRUE(row["overview"]["averages"]["lateral"]["case_mean_rmse"].is_null());EXPECT_TRUE(row["worst"].empty());
  EXPECT_EQ(readText(result.home).find("<script>alert(1)</script>"),std::string::npos);
  EXPECT_EQ(readText(result.report).find("<script>alert(1)</script>"),std::string::npos);
}
TEST_F(ArchiveTest, PublicationFailureRestoresCatalogAndPreviousRound) {
  const auto first=render(analysis());const auto before=readText(first.report),home=readText(first.home),index=readText(archive/"runs.json");
  EXPECT_THROW(renderArchive(analysis(),archive,TRACK_INSIGHT_RESOURCES,true,"replacement",{},
    [](double value){if(value==1)throw std::runtime_error("publication interrupted");}),std::runtime_error);
  EXPECT_EQ(readText(first.report),before);EXPECT_EQ(readText(first.home),home);EXPECT_EQ(readText(archive/"runs.json"),index);
  EXPECT_THROW(renderArchive(analysis('b'),archive,TRACK_INSIGHT_RESOURCES,false,{},{},
    [](double value){if(value==1)throw std::runtime_error("publication interrupted");}),std::runtime_error);
  EXPECT_EQ(std::distance(fs::directory_iterator(archive/"runs"),fs::directory_iterator{}),1);
  EXPECT_EQ(readText(archive/"runs.json"),index);
}

TEST_F(ArchiveTest, TrailingSlashAndDotReferToTheSameArchive) {
  archive=root/"history/";const auto first=render(analysis());
  archive=root/"history/.";const auto second=render(analysis('b'));
  EXPECT_EQ(first.home,second.home);EXPECT_EQ(catalog()["runs"].size(),2U);
  EXPECT_EQ(render(analysis(),true).report,first.report);
}
TEST_F(ArchiveTest, SwappedIdentityAndMalformedSummaryNeverReplaceAnotherRound) {
  const auto first=render(analysis()),second=render(analysis('b'));
  const auto first_text=readText(first.report),second_text=readText(second.report),home=readText(first.home);
  auto corrupt=catalog();const auto original=corrupt;
  std::swap(corrupt["runs"][0]["source_sha256"],corrupt["runs"][1]["source_sha256"]);
  writeText(archive/"runs.json",corrupt.dump());
  EXPECT_THROW(render(analysis(),true),std::runtime_error);
  EXPECT_EQ(readText(first.report),first_text);EXPECT_EQ(readText(second.report),second_text);EXPECT_EQ(readText(first.home),home);
  corrupt=original;corrupt["runs"][0]["overview"]["case_ids"]=42;writeText(archive/"runs.json",corrupt.dump());
  EXPECT_THROW(render(analysis('c')),std::runtime_error);EXPECT_EQ(readText(first.home),home);
}

}

TEST_F(ArchiveTest, StraightPathsDoNotEnterArchiveCountsMeansOrBestWorst) {
  auto a=analysis();
  a["plans"]=Json::array({{{"id","P1"},{"valid",true},{"poses",{{0,0,0},{1,0,0}}}},
    {{"id","P2"},{"valid",true},{"poses",{{0,0,0},{.5,.1,0},{1,0,0}}}},
    {{"id","P3"},{"valid",true},{"poses",{{0,0,0},{1,0,0}}}}});
  const auto result=render(a);ASSERT_EQ(result.code,0);const auto row=catalog()["runs"][0];
  EXPECT_EQ(row["paths"],1);EXPECT_EQ(row["overview"]["case_ids"],Json::array({"E2"}));
  EXPECT_EQ(row["overview"]["best_ids"],Json::array({"E2"}));
  EXPECT_EQ(row["overview"]["worst_ids"],Json::array({"E2"}));
  EXPECT_DOUBLE_EQ(row["overview"]["averages"]["lateral"]["case_mean_rmse"].get<double>(),.2);
  const auto saved=Json::parse(readText(result.report.parent_path()/"analysis.json"));
  EXPECT_EQ(saved["paths"],a["paths"]);EXPECT_EQ(saved["plans"],a["plans"]);
}

TEST_F(ArchiveTest, ParameterSnapshotsAndComparisonSurviveRerenderWithoutNestedHistory) {
  auto first=analysis();first["pid_parameters"]={{"schema_version",2},{"source_kind","navigation_log"},{"status","available"},{"values",{{"kp_x",1}}}};
  const auto one=render(first);ASSERT_EQ(one.code,0);
  auto second=analysis('b');second["meta"]["start_ns"]="1793006082000000000";
  second["pid_parameters"]={{"schema_version",2},{"source_kind","navigation_log"},{"status","available"},{"values",{{"kp_x",2},{"kp_y",1}}}};
  const auto two=render(second);ASSERT_EQ(two.code,0);
  const auto saved=Json::parse(readText(two.report.parent_path()/"analysis.json"));
  EXPECT_EQ(saved["pid_parameters"]["comparison"]["values"]["kp_x"],1);
  EXPECT_EQ(saved["pid_parameters"]["comparison"]["run_id"],one.report.parent_path().filename().string());
  const auto snapshots=catalog();
  for(const auto& row:snapshots["runs"])EXPECT_FALSE(row["pid_parameters"].contains("comparison"));
  first["pid_parameters"]["values"]["kp_x"]=99;render(first,true);render(saved,true);
  EXPECT_EQ(Json::parse(readText(two.report.parent_path()/"analysis.json"))["pid_parameters"],saved["pid_parameters"]);
}

TEST_F(ArchiveTest, KilledGenerationAndPublicationRecoverBeforeCatalogValidation) {
  const auto first=render(analysis());
  const auto report=readText(first.report),home=readText(first.home),index=readText(archive/"runs.json");
  for(bool replacement:{false,true})for(double cutoff:{.9,1.0}) {
    const auto pid=fork();ASSERT_GE(pid,0);
    if(pid==0) {
      try {renderArchive(analysis(replacement?'a':'b'),archive,TRACK_INSIGHT_RESOURCES,replacement,"new",{},
        [cutoff](double p){if(p>=cutoff)raise(SIGKILL);});}catch(...){_exit(91);}_exit(92);
    }
    int status=0;ASSERT_EQ(waitpid(pid,&status,0),pid);ASSERT_TRUE(WIFSIGNALED(status));ASSERT_EQ(WTERMSIG(status),SIGKILL);
    // Same-round refusal still performs recovery, without generating another report.
    EXPECT_THROW(render(analysis()),std::runtime_error);
    EXPECT_EQ(readText(first.report),report);EXPECT_EQ(readText(first.home),home);EXPECT_EQ(readText(archive/"runs.json"),index);
    EXPECT_EQ(std::distance(fs::directory_iterator(archive/"runs"),fs::directory_iterator{}),1);
  }
  EXPECT_EQ(render(analysis('b')).code,0);EXPECT_EQ(catalog()["runs"].size(),2);
}
TEST_F(ArchiveTest, FirstPublicationInterruptedLeavesNoPartialArchive) {
  for(double cutoff:{.9,1.0}) {
    archive=root/(cutoff==1?"initial_publish":"initial_generate");
    const auto pid=fork();ASSERT_GE(pid,0);
    if(pid==0){try{renderArchive(analysis(),archive,TRACK_INSIGHT_RESOURCES,false,{},{},
      [cutoff](double p){if(p>=cutoff)raise(SIGKILL);});}catch(...){_exit(91);}_exit(92);}
    int status=0;ASSERT_EQ(waitpid(pid,&status,0),pid);ASSERT_TRUE(WIFSIGNALED(status));
    const auto result=render(analysis());EXPECT_EQ(result.code,0);EXPECT_EQ(catalog()["runs"].size(),1);
  }
}

TEST_F(ArchiveTest, StandaloneRenderInsideArchiveDoesNotLeaveUnmanagedLockFiles) {
  const auto first=render(analysis());const auto report=readText(first.report),home=readText(first.home);
  for(const auto& output:{archive,archive/"runs",first.report.parent_path(),first.report.parent_path()/"nested"}) {
    for(bool replace:{false,true})EXPECT_THROW(renderReport(analysis(),output,TRACK_INSIGHT_RESOURCES,replace),std::runtime_error);
    EXPECT_FALSE(fs::exists(output.string()+".track_insight.lock"));
  }
  const auto alias=root/"alias";fs::create_directory_symlink(archive/"runs",alias);
  EXPECT_THROW(renderReport(analysis(),alias/first.report.parent_path().filename(),TRACK_INSIGHT_RESOURCES,true),std::runtime_error);
  EXPECT_FALSE(fs::exists(first.report.parent_path().string()+".track_insight.lock"));
  EXPECT_EQ(readText(first.report),report);EXPECT_EQ(readText(first.home),home);
  EXPECT_EQ(render(analysis(),true).code,0);EXPECT_EQ(render(analysis('b')).code,0);
}

TEST_F(ArchiveTest, ParentAliasesUseTheSameLockAndRecoveryIdentity) {
  const auto alias=root/"parent_alias";fs::create_directory_symlink(root,alias);
  const auto first=render(analysis());const auto report=readText(first.report),home=readText(first.home);
  const auto pid=fork();ASSERT_GE(pid,0);
  if(pid==0){try{renderArchive(analysis('b'),alias/"history",TRACK_INSIGHT_RESOURCES,false,{},{},
    [](double p){if(p==1)raise(SIGKILL);});}catch(...){_exit(91);}_exit(92);}
  int status=0;ASSERT_EQ(waitpid(pid,&status,0),pid);ASSERT_TRUE(WIFSIGNALED(status));ASSERT_EQ(WTERMSIG(status),SIGKILL);
  EXPECT_THROW(render(analysis()),std::runtime_error);
  EXPECT_EQ(readText(first.report),report);EXPECT_EQ(readText(first.home),home);
  EXPECT_EQ(renderArchive(analysis('b'),alias/"history",TRACK_INSIGHT_RESOURCES,false).code,0);
}
