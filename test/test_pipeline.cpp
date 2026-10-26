#include <gtest/gtest.h>
#include "track_insight/input.hpp"
#include "track_insight/report.hpp"
#include <rosbag2_cpp/writer.hpp>
#include <nav_msgs/msg/path.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <tf2_msgs/msg/tf_message.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <fstream>
#include <cmath>
#include <unistd.h>

using namespace track_insight;
namespace {
struct Fixture {
  std::filesystem::path root,bag,log;
  TimestampNs origin{1792655909000000000LL};
  Fixture() {
    auto name=(std::filesystem::temp_directory_path()/"track_insight_fixture_XXXXXX").string();
    std::vector<char> chars(name.begin(),name.end());chars.push_back(0);root=mkdtemp(chars.data());
    bag=root/"bag";log=root/"nav2.log";
    rosbag2_cpp::Writer writer;rosbag2_storage::StorageOptions options;options.uri=bag.string();options.storage_id="mcap";
    writer.open(options);
    for(int j=0;j<=40;j++) {
      const auto ns=origin+j*100000000LL;auto time=rclcpp::Time(ns);
      if(j==2) {
        nav_msgs::msg::Path path;path.header.stamp=time;path.header.frame_id="map";
        geometry_msgs::msg::PoseStamped p;p.header=path.header;p.pose.orientation.w=1;path.poses.push_back(p);
        p.pose.position.x=1;path.poses.push_back(p);writer.write(path,"/plan",time);
      }
      tf2_msgs::msg::TFMessage tf;geometry_msgs::msg::TransformStamped transform;
      transform.header.stamp=time;transform.header.frame_id="map";transform.child_frame_id="rbs_base_link";
      transform.transform.rotation.w=1;transform.transform.translation.x=std::min(j,20)*.01;
      transform.transform.translation.y=.1;tf.transforms.push_back(transform);writer.write(tf,"/tf",time);
      geometry_msgs::msg::TwistStamped cmd;cmd.header.stamp=time;cmd.header.frame_id="rbs_base_link";
      cmd.twist.linear.x=j<20?.1:0;writer.write(cmd,"/cmd_vel_nav",time);
      nav_msgs::msg::Odometry odom;odom.header.stamp=time;odom.header.frame_id="odom";odom.child_frame_id="rbs_base_link";
      odom.twist.twist.linear.x=j<20?.08:0;writer.write(odom,"/dev/odometry/raw/dist",time);
    }
    writer.close();
    std::ofstream out(log);
    out<<"[bt_navigator] [1792655909.000000000] Begin navigating from current location (0, 0.1) to (1, 0)\n"
       <<"[controller] [1792655909.200000000] PID_LOG path_handoff plan_revision=1 frame=map poses=2 path_stamp_ns="<<origin+200000000<<"\n"
       <<"[controller] [1792655909.200000000] PID_LOG tracking_state plan_revision=1 command_stamp_ns="<<origin+200000000<<" target_yaw=0\n"
       <<"[bt_navigator] [1792655911.000000000] Goal succeeded\n";
  }
  ~Fixture(){std::filesystem::remove_all(root);}
  std::filesystem::path zip() {
    const auto output=root/"bag.zip";archive* a=archive_write_new();archive_write_set_format_zip(a);
    if(archive_write_open_filename(a,output.c_str())!=ARCHIVE_OK)throw std::runtime_error("fixture ZIP failed");
    for(const auto& item:std::filesystem::directory_iterator(bag)) {
      if(!item.is_regular_file())continue;
      const auto content=readText(item.path());
      archive_entry* entry=archive_entry_new();const auto path="bag/"+item.path().filename().string();
      archive_entry_set_pathname(entry,path.c_str());archive_entry_set_size(entry,content.size());
      archive_entry_set_filetype(entry,AE_IFREG);archive_entry_set_perm(entry,0600);
      archive_write_header(a,entry);archive_write_data(a,content.data(),content.size());archive_entry_free(entry);
    }
    archive_write_close(a);archive_write_free(a);return output;
  }
};
}
TEST(Pipeline, McapZipAndKnownMetricsMatch) {
  Fixture f;Config c;const auto directory=readBag(f.bag,f.log,c);const auto zipped=readBag(f.zip(),f.log,c);
  ASSERT_EQ(directory.plans.size(),1U);EXPECT_EQ(directory.plans[0].fingerprint,zipped.plans[0].fingerprint);
  EXPECT_EQ(directory.commands.size(),zipped.commands.size());ASSERT_TRUE(directory.log_compatible);
  const auto evaluated=evaluate(directory,c);ASSERT_EQ(evaluated.tasks.size(),1U);EXPECT_EQ(evaluated.tasks[0].outcome,"succeeded");
  ASSERT_EQ(evaluated.paths.size(),1U);const auto& p=evaluated.paths[0];
  EXPECT_EQ(p.interval.identity,"confirmed");EXPECT_EQ(p.interval.start_quality,"confirmed");EXPECT_EQ(p.interval.end_quality,"confirmed");
  ASSERT_TRUE(p.metrics.at("lateral").rmse);EXPECT_NEAR(*p.metrics.at("lateral").rmse,.1,1e-9);
  EXPECT_NEAR(*p.metrics.at("vx_residual").rmse,.02,1e-9);
  ASSERT_TRUE(p.metrics.at("goal_position_error").value);EXPECT_NEAR(*p.metrics.at("goal_position_error").value,std::hypot(.8,.1),1e-9);
}
TEST(Pipeline, OverlappingButUnprovenLogDoesNotCreateNavigation) {
  Fixture f;writeText(f.log,"[bt_navigator] [1792655909.000000000] Begin navigating from current location (9, 9) to (10, 10)\n");
  const auto data=readBag(f.bag,f.log,Config{});EXPECT_FALSE(data.log_compatible);EXPECT_TRUE(evaluate(data,Config{}).tasks.empty());
}
TEST(Report, SavedAnalysisRendersWithoutSourceAndProtectsUserFiles) {
  Fixture f;Config c;auto data=readBag(f.bag,f.log,c);
  auto a=makeAnalysis(data,evaluate(data,c),c,captureDisplayClock(data.start,data.end,true));
  ASSERT_TRUE(a["paths"][0]["tracking"][0][12].is_string());
  const auto saved=f.root/"analysis.json";writeText(saved,a.dump());std::filesystem::remove_all(f.bag);std::filesystem::remove(f.log);
  const auto output=f.root/"report";EXPECT_EQ(renderReport(Json::parse(readText(saved)),output,TRACK_INSIGHT_RESOURCES),0);
  const auto manifest=Json::parse(readText(output/"manifest.json"));EXPECT_EQ(manifest["status"],"complete");
  EXPECT_TRUE(std::filesystem::exists(output/"figures/P1_trajectory.png"));
  EXPECT_NE(readText(output/"report.html").find("Plotly"),std::string::npos);
  EXPECT_THROW(renderReport(a,output,TRACK_INSIGHT_RESOURCES),std::exception);
  writeText(output/"user-note.txt","keep");EXPECT_THROW(renderReport(a,output,TRACK_INSIGHT_RESOURCES,true),std::exception);
  EXPECT_EQ(readText(output/"user-note.txt"),"keep");
}
TEST(Report, FailedPngConversionReplacesPriorManifestWithPartial) {
  Fixture f;Config c;auto data=readBag(f.bag,{},c);auto a=makeAnalysis(data,evaluate(data,c),c,captureDisplayClock(data.start,data.end,true));
  const auto output=f.root/"report";ASSERT_EQ(renderReport(a,output,TRACK_INSIGHT_RESOURCES),0);
  const char* original=getenv("PATH");const std::string path=original?original:"";setenv("PATH","/nonexistent",1);
  int status=1;try{status=renderReport(a,output,TRACK_INSIGHT_RESOURCES,true);}catch(...){setenv("PATH",path.c_str(),1);throw;}
  setenv("PATH",path.c_str(),1);EXPECT_EQ(status,2);
  EXPECT_FALSE(std::filesystem::exists(output/"figures/P1_trajectory.png"));
  EXPECT_EQ(Json::parse(readText(output/"manifest.json"))["status"],"partial");
}

TEST(Report, MissingEvidenceDoesNotMeanGenerationFailed) {
  Fixture f;Config c;RunData data;data.start=f.origin;data.end=f.origin+kSecond;
  auto a=makeAnalysis(data,evaluate(data,c),c,captureDisplayClock(data.start,data.end,true));
  EXPECT_EQ(renderReport(a,f.root/"empty",TRACK_INSIGHT_RESOURCES),0);
  const auto manifest=Json::parse(readText(f.root/"empty/manifest.json"));
  EXPECT_EQ(manifest["status"],"complete");EXPECT_EQ(manifest["analysis_quality"],"unavailable");
}

TEST(Config, RejectsDurationOverflowAndReadsEvaluationThresholds) {
  Fixture f;const auto file=f.root/"config.yaml";
  writeText(file,"motion:\n  stationary_dwell_s: 1.0e300\n");EXPECT_THROW(loadConfig(file),std::exception);
  writeText(file,"thresholds:\n  lateral_m: 0.05\n  terminal_position_m: 0.02\nmotion:\n  stationary_dwell_s: 2\n");
  const auto config=loadConfig(file);ASSERT_TRUE(config.lateral_threshold);EXPECT_DOUBLE_EQ(*config.lateral_threshold,.05);
  ASSERT_TRUE(config.terminal_tolerance);EXPECT_DOUBLE_EQ(*config.terminal_tolerance,.02);EXPECT_DOUBLE_EQ(config.settle_duration,2);
}

TEST(Pipeline, CorruptPathKeepsPublicationIdentityAsUnavailable) {
  Fixture f;const auto path=f.root/"corrupt";
  rosbag2_cpp::Writer writer;rosbag2_storage::StorageOptions options;options.uri=path.string();options.storage_id="mcap";writer.open(options);
  auto bytes=std::make_shared<rclcpp::SerializedMessage>(4);
  auto& data=bytes->get_rcl_serialized_message();data.buffer_length=4;
  data.buffer[0]=0;data.buffer[1]=1;data.buffer[2]=0;data.buffer[3]=0;
  writer.write(bytes,"/plan","nav_msgs/msg/Path",rclcpp::Time(f.origin));writer.close();
  const auto read=readBag(path,{},Config{});ASSERT_EQ(read.plans.size(),1U);
  EXPECT_EQ(read.plans[0].id,"P1");EXPECT_FALSE(read.plans[0].valid);EXPECT_EQ(read.plans[0].reason,"path_decode_error");
  EXPECT_TRUE(evaluate(read,Config{}).paths.empty());
}
