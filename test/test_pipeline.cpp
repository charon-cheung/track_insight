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
#include <limits>
#include <algorithm>
#include <unistd.h>

using namespace track_insight;
namespace {
struct Fixture {
  std::filesystem::path root,bag,log;
  TimestampNs origin{1792655909000000000LL};
  Fixture(bool with_tracking_raw=false,bool stamped=true,bool with_downstream=false) {
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
      if(with_tracking_raw&&!(j>=10&&j<=12)) {
        geometry_msgs::msg::Twist raw;raw.linear.x=j==20?std::numeric_limits<double>::quiet_NaN():.3+.01*j;
        raw.linear.y=-.2;raw.angular.z=.4;
        if(stamped) {
          geometry_msgs::msg::TwistStamped message;message.twist=raw;
          message.header.stamp=rclcpp::Time(ns-10000000);message.header.frame_id=j==30?"missing_frame":"rbs_base_link";
          writer.write(message,"/tracking_cmd_vel_raw",time);
        } else writer.write(raw,"/tracking_cmd_vel_raw",time);
      }
      if(with_downstream&&!(j>=10&&j<=12)) {
        auto output=cmd;output.twist.linear.x=j==20?std::numeric_limits<double>::quiet_NaN():.2+.01*j;
        // Reused, then backwards and stale headers must not move or invalidate record-time output.
        output.header.stamp=rclcpp::Time(origin-(j>=15?10*kSecond:50000000));
        if(stamped)writer.write(output,"/ctrl/velocity/navi/clct",time);
        else writer.write(output.twist,"/ctrl/velocity/navi/clct",time);
      }
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
  Fixture f;Config c;std::vector<double> progress;
  const auto directory=readBag(f.bag,f.log,c,[&](double value){progress.push_back(value);});
  ASSERT_GE(progress.size(),3U);EXPECT_EQ(progress.front(),0);EXPECT_EQ(progress.back(),1);
  EXPECT_TRUE(std::is_sorted(progress.begin(),progress.end()));
  const auto zipped=readBag(f.zip(),f.log,c);
  ASSERT_EQ(directory.plans.size(),1U);EXPECT_EQ(directory.plans[0].fingerprint,zipped.plans[0].fingerprint);
  EXPECT_EQ(directory.commands.size(),zipped.commands.size());ASSERT_TRUE(directory.log_compatible);
  const auto evaluated=evaluate(directory,c);ASSERT_EQ(evaluated.tasks.size(),1U);EXPECT_EQ(evaluated.tasks[0].outcome,"succeeded");
  ASSERT_EQ(evaluated.paths.size(),1U);const auto& p=evaluated.paths[0];
  EXPECT_EQ(p.interval.identity,"confirmed");EXPECT_EQ(p.interval.start_quality,"confirmed");EXPECT_EQ(p.interval.end_quality,"confirmed");
  ASSERT_TRUE(p.metrics.at("lateral").rmse);EXPECT_NEAR(*p.metrics.at("lateral").rmse,.1,1e-9);
  EXPECT_NEAR(*p.metrics.at("vx_residual").rmse,.02,1e-9);
  ASSERT_TRUE(p.metrics.at("goal_position_error").value);EXPECT_NEAR(*p.metrics.at("goal_position_error").value,std::hypot(.8,.1),1e-9);
}
TEST(Pipeline, TrackingRawVelocityPreservesTimeGapsAndExistingMetrics) {
  for(bool stamped:{false,true}) {
    Fixture f(true,stamped);Config c;const auto data=readBag(f.bag,f.log,c);
    ASSERT_EQ(data.tracking_raw.size(),38U);ASSERT_EQ(data.commands.size(),41U);
    const auto& first=data.tracking_raw.front();
    EXPECT_EQ(first.stamp.source,"/tracking_cmd_vel_raw");
    EXPECT_EQ(first.stamp.ns,f.origin-(stamped?10000000:0));
    EXPECT_EQ(first.stamp.header_ns,stamped?f.origin-10000000:0);
    EXPECT_EQ(first.stamp.record_ns,f.origin);EXPECT_EQ(first.frame,c.base);EXPECT_TRUE(first.valid);
    EXPECT_DOUBLE_EQ(first.vx,.3);EXPECT_DOUBLE_EQ(first.vy,-.2);EXPECT_DOUBLE_EQ(first.omega,.4);
    EXPECT_DOUBLE_EQ(data.commands.front().vx,.1);
    const auto analysis=makeAnalysis(data,evaluate(data,c),c,captureDisplayClock(data.start,data.end,true));
    const auto& signal=analysis["signals"]["tracking_raw"];ASSERT_EQ(signal.size(),38U);
    EXPECT_EQ(signal[0][5],std::to_string(first.stamp.ns));EXPECT_EQ(analysis["raw"]["tracking_raw"].size(),38U);
    EXPECT_EQ(analysis["capabilities"]["tracking_raw"],true);
    EXPECT_NEAR(signal[9][6].get<double>()-signal[9][0].get<double>(),.2,1e-9);
    EXPECT_LT(signal[9][6].get<double>(),signal[10][0].get<double>());
    EXPECT_EQ(signal[17][4],false);EXPECT_EQ(signal[17][7],"non_finite_velocity");
    if(stamped){EXPECT_EQ(signal[27][4],false);EXPECT_EQ(signal[27][7],"missing_velocity_frame_transform");}
    auto baseline=data;baseline.tracking_raw.clear();
    const auto original=makeAnalysis(baseline,evaluate(baseline,c),c,captureDisplayClock(data.start,data.end,true));
    EXPECT_EQ(analysis["paths"],original["paths"]);EXPECT_EQ(analysis["navigation_metrics"],original["navigation_metrics"]);
    EXPECT_TRUE(original["signals"]["tracking_raw"].empty());EXPECT_EQ(original["capabilities"]["tracking_raw"],false);
    if(stamped) {
      const auto output=f.root/"raw_report";ASSERT_EQ(renderReport(analysis,output,TRACK_INSIGHT_RESOURCES),0);
      const auto svg=readText(output/"figures/P1_velocity.svg");
      EXPECT_NE(svg.find("Black: tracking raw command"),std::string::npos);
      EXPECT_NE(svg.find("stroke=\"#000000\""),std::string::npos);
    }
  }
}
TEST(Pipeline, DownstreamUsesRecordTimeDespiteReusedOrStaleHeaders) {
  for(bool stamped:{false,true}) {
    Fixture f(false,stamped,true);Config c;const auto data=readBag(f.bag,f.log,c);
    ASSERT_EQ(data.downstream.size(),38U);
    for(const auto& v:data.downstream) {
      EXPECT_EQ(v.stamp.ns,v.stamp.record_ns);EXPECT_EQ(v.stamp.clock_domain,"bag_record");
      EXPECT_TRUE(v.stamp.valid);
      EXPECT_EQ(v.stamp.header_ns,stamped?f.origin-(v.stamp.ns>=f.origin+1500000000?10*kSecond:50000000):0);
    }
    const auto clock=captureDisplayClock(data.start,data.end,true);
    const auto analysis=makeAnalysis(data,evaluate(data,c),c,clock);
    EXPECT_EQ(analysis["meta"]["downstream_time_basis"],"bag_record");
    const auto& signal=analysis["signals"]["downstream"];ASSERT_EQ(signal.size(),38U);
    for(std::size_t i=0;i<signal.size();++i) {
      EXPECT_EQ(signal[i][5],std::to_string(data.downstream[i].stamp.record_ns));
      EXPECT_NEAR(signal[i][0].get<double>(),double(data.downstream[i].stamp.record_ns-clock.origin_ns)/kSecond,1e-9);
      EXPECT_EQ(signal[i][4],i!=17); // non-finite sample stays invalid, independent of header policy
    }
    EXPECT_NEAR(signal[9][6].get<double>()-signal[9][0].get<double>(),.2,1e-9);
    EXPECT_LT(signal[9][6].get<double>(),signal[10][0].get<double>());
    auto baseline=data;baseline.downstream.clear();
    const auto original=makeAnalysis(baseline,evaluate(baseline,c),c,clock);
    EXPECT_EQ(analysis["paths"],original["paths"]);EXPECT_EQ(analysis["navigation_metrics"],original["navigation_metrics"]);
    if(stamped) {
      const auto output=f.root/"downstream_report";ASSERT_EQ(renderReport(analysis,output,TRACK_INSIGHT_RESOURCES),0);
      const auto svg=readText(output/"figures/P1_velocity.svg");
      EXPECT_NE(svg.find("/ctrl/velocity/navi/clct (bag record time)"),std::string::npos);
      EXPECT_NE(svg.find("stroke=\"#FFD700\""),std::string::npos);
      auto legacy=analysis;legacy["meta"].erase("downstream_time_basis");
      ASSERT_EQ(renderReport(legacy,output,TRACK_INSIGHT_RESOURCES,true),0);
      EXPECT_EQ(readText(output/"figures/P1_velocity.svg").find("stroke=\"#FFD700\""),std::string::npos);
    }
  }
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
  const auto output=f.root/"report";std::vector<double> progress;
  auto observe=[&](double value){progress.push_back(value);if(value==1){EXPECT_TRUE(std::filesystem::exists(output/"report.html"));}};
  EXPECT_EQ(renderReport(Json::parse(readText(saved)),output,TRACK_INSIGHT_RESOURCES,false,observe),0);
  ASSERT_GE(progress.size(),5U);EXPECT_EQ(progress.front(),0);EXPECT_EQ(progress.back(),1);
  EXPECT_TRUE(std::is_sorted(progress.begin(),progress.end()));
  const auto manifest=Json::parse(readText(output/"manifest.json"));EXPECT_EQ(manifest["status"],"complete");
  EXPECT_TRUE(std::filesystem::exists(output/"figures/P1_trajectory.png"));
  EXPECT_NE(readText(output/"report.html").find("Plotly"),std::string::npos);
  progress.clear();EXPECT_THROW(renderReport(a,output,TRACK_INSIGHT_RESOURCES,false,observe),std::exception);
  EXPECT_EQ(std::count(progress.begin(),progress.end(),1.0),0);
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
  const auto config=loadConfig(file);EXPECT_EQ(config.tracking_raw_topic,"/tracking_cmd_vel_raw");ASSERT_TRUE(config.lateral_threshold);EXPECT_DOUBLE_EQ(*config.lateral_threshold,.05);
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
