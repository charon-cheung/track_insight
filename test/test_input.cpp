#include <gtest/gtest.h>
#include "track_insight/input.hpp"
#include "tf_history.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <unistd.h>
#include <fstream>

using namespace track_insight;
namespace {
struct Temp {
  std::filesystem::path path;
  Temp(){auto s=(std::filesystem::temp_directory_path()/"track_insight_test_XXXXXX").string();
    std::vector<char>b(s.begin(),s.end());b.push_back(0);path=mkdtemp(b.data());}
  ~Temp(){std::filesystem::remove_all(path);}
};
void zip(const std::filesystem::path& path,const std::string& name,const std::string& data) {
  archive* a=archive_write_new();archive_write_set_format_zip(a);
  ASSERT_EQ(archive_write_open_filename(a,path.c_str()),ARCHIVE_OK);
  archive_entry* e=archive_entry_new();archive_entry_set_pathname(e,name.c_str());
  archive_entry_set_size(e,data.size());archive_entry_set_filetype(e,AE_IFREG);archive_entry_set_perm(e,0600);
  ASSERT_EQ(archive_write_header(a,e),ARCHIVE_OK);archive_write_data(a,data.data(),data.size());
  archive_entry_free(e);archive_write_close(a);archive_write_free(a);
}
geometry_msgs::msg::TransformStamped transform(double t,const std::string& parent,const std::string& child,double x) {
  geometry_msgs::msg::TransformStamped r;r.header.stamp.sec=static_cast<int>(t);
  r.header.stamp.nanosec=static_cast<unsigned>((t-static_cast<int>(t))*1e9);
  r.header.frame_id=parent;r.child_frame_id=child;r.transform.rotation.w=1;r.transform.translation.x=x;return r;
}
}
TEST(Input, ZipTraversalAndExpansionAreRejected) {
  Temp t;auto p=t.path/"bad.zip";zip(p,"../escape.mcap","abc");
  EXPECT_THROW(InputPackage(p,Config{}),std::exception);
  zip(p,"data.mcap",std::string(10000,'x'));Config c;c.max_archive_bytes=100;
  EXPECT_THROW(InputPackage(p,c),std::exception);
}
TEST(Input, SingleMcapExtractionIsScoped) {
  Temp t;auto p=t.path/"bag.zip";zip(p,"nested/data.mcap","mcap");std::filesystem::path extracted;
  {InputPackage input(p,Config{});extracted=input.bagPath();EXPECT_TRUE(std::filesystem::exists(extracted));}
  EXPECT_FALSE(std::filesystem::exists(extracted));
}
TEST(Tf, MissingAndLongGapAreUnavailable) {
  Config c;c.base="base";TfHistory tf(c,10*kSecond);
  tf.add(transform(1,"map","odom",0),false,0);tf.add(transform(3,"map","odom",0),false,0);
  tf.add(transform(2,"odom","base",0),false,0);tf.finish();
  const auto poses=tf.poses();ASSERT_EQ(poses.size(),3U);
  for(const auto& p:poses)EXPECT_FALSE(p.valid);
}
TEST(Tf, StaticOffsetChangesTwistReferencePoint) {
  Config c;c.base="base";TfHistory tf(c,kSecond);
  tf.add(transform(0,"base","sensor",1),true,0);tf.finish();
  Velocity v;v.frame="sensor";v.reference_point="sensor";v.stamp.ns=kSecond;v.omega=1;v.vy=1;
  tf.convert(v);EXPECT_TRUE(v.valid);EXPECT_EQ(v.frame,"base");EXPECT_NEAR(v.vy,0,1e-12);
}
TEST(Tf, LocalizationDiscontinuityInvalidatesBoundary) {
  Config c;c.base="base";TfHistory tf(c,kSecond);
  tf.add(transform(1,"map","base",0),false,0);tf.add(transform(1.01,"map","base",1),false,0);tf.finish();
  const auto poses=tf.poses();ASSERT_EQ(poses.size(),2U);
  EXPECT_FALSE(poses[0].valid);EXPECT_FALSE(poses[1].valid);
}

TEST(Tf, HeaderResetWithMonotonicRecordTimeCannotMergeEpochs) {
  Config c;c.base="base";TfHistory tf(c,10*kSecond);Stamp s;s.record_ns=4*kSecond;
  tf.add(transform(4,"map","base",0),false,0,s);s.record_ns=5*kSecond;
  tf.add(transform(2,"map","base",1),false,0,s);tf.finish();EXPECT_TRUE(tf.hasClockReset());
  for(const auto& p:tf.poses())EXPECT_FALSE(p.valid);
}
