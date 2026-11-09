#include <gtest/gtest.h>
#include "track_insight/input.hpp"
#include "tf_history.hpp"
#include "velocity_clock.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <unistd.h>
#include <fstream>
#include <algorithm>

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

TEST(Tf, ConfiguredClockLimitsControlDiscrepancyAndReset) {
  Config c;c.base="base";Stamp source;source.record_ns=3*kSecond;
  auto discrepancy=[&](const Config& config){TfHistory tf(config,10*kSecond);
    tf.add(transform(1,"map","base",0),false,0,source);tf.finish();return tf.poses();};
  EXPECT_TRUE(discrepancy(c)[0].valid);
  c.clock_domain_tolerance=2*kSecond;EXPECT_TRUE(discrepancy(c)[0].valid);
  c.clock_domain_tolerance=2*kSecond-1;EXPECT_FALSE(discrepancy(c)[0].valid);
  auto reset=[&](const Config& config){TfHistory tf(config,10*kSecond);
    tf.add(transform(2,"map","base",0),false,0);tf.add(transform(1.5,"map","base",0),false,0);
    tf.finish();return tf.hasClockReset();};
  EXPECT_FALSE(reset(c));c.clock_reset_tolerance=500000000;EXPECT_FALSE(reset(c));
  c.clock_reset_tolerance=499999999;EXPECT_TRUE(reset(c));
}

namespace {
Velocity clockSample(int index,TimestampNs shift=0,int epoch=0,const std::string& source="/feedback") {
  Velocity v;v.stamp.record_ns=100*kSecond+index*100000000LL;
  v.stamp.ns=v.stamp.header_ns=v.stamp.record_ns+shift;v.stamp.sequence=index;
  v.stamp.epoch=epoch;v.stamp.source=source;v.stamp.clock_domain="message_header";
  v.frame=v.reference_point="base";v.vx=.1;return v;
}
}
TEST(VelocityClock, PrefixTailAndConsecutiveErrorsAreLocalized) {
  for(const auto& indices:{std::vector<int>{0},std::vector<int>{4},std::vector<int>{1,2}}) {
    std::vector<Velocity> values;std::vector<Diagnostic> diagnostics;
    for(int i=0;i<5;++i)values.push_back(clockSample(i,
      std::find(indices.begin(),indices.end(),i)!=indices.end()?10*kSecond:-2*kSecond));
    isolateVelocityClocks(values,Config{},diagnostics);
    for(int i=0;i<5;++i)EXPECT_EQ(values[i].stamp.valid,std::find(indices.begin(),indices.end(),i)==indices.end());
    ASSERT_EQ(diagnostics.size(),1U);EXPECT_EQ(diagnostics[0].ns,values[indices[0]].stamp.record_ns);
    EXPECT_LE(values[indices[0]].stamp.ns,values.front().stamp.ns+400000000);
    if(indices[0]==4){EXPECT_NE(diagnostics[0].message.find("end_ns=unrecovered"),std::string::npos);}
  }
}
TEST(VelocityClock, PersistentResetDoesNotRecoverByCatchingOldHeader) {
  std::vector<Velocity> values;std::vector<Diagnostic> diagnostics;
  for(int i=0;i<40;++i)values.push_back(clockSample(i,i>=10?-2*kSecond:0));
  isolateVelocityClocks(values,Config{},diagnostics);
  for(int i=0;i<40;++i)EXPECT_EQ(values[i].stamp.valid,i<10);
  ASSERT_EQ(diagnostics.size(),1U);
  EXPECT_NE(diagnostics[0].message.find("rejected=30"),std::string::npos);
}
TEST(VelocityClock, RecoveryAndRecordEpochsKeepIndependentEvidence) {
  std::vector<Velocity> values{clockSample(0),clockSample(1,2*kSecond),clockSample(2),
    clockSample(3,2*kSecond,0,"/command"),clockSample(4,2*kSecond,1),clockSample(5,2*kSecond,1)};
  std::vector<Diagnostic> diagnostics;isolateVelocityClocks(values,Config{},diagnostics);
  for(std::size_t i=0;i<values.size();++i)EXPECT_EQ(values[i].stamp.valid,i!=1);
  ASSERT_EQ(diagnostics.size(),1U);EXPECT_EQ(diagnostics[0].source,"/feedback");
}
TEST(VelocityClock, StableDelayZeroHeadersAndRecordTimedOutputRemainSupported) {
  std::vector<Velocity> values{clockSample(0,-2*kSecond),clockSample(1,-2*kSecond),clockSample(2,-2*kSecond)};
  values[1].stamp.header_ns=0;values[1].stamp.ns=values[1].stamp.record_ns;values[1].stamp.clock_domain="record_fallback";
  auto output=clockSample(3,10*kSecond);output.stamp.ns=output.stamp.record_ns;output.stamp.clock_domain="bag_record";values.push_back(output);
  std::vector<Diagnostic> diagnostics;isolateVelocityClocks(values,Config{},diagnostics);
  for(const auto& v:values)EXPECT_TRUE(v.stamp.valid);
  EXPECT_TRUE(diagnostics.empty());EXPECT_EQ(values[3].stamp.header_ns,output.stamp.header_ns);
  // A zero header does not establish that a persistent changed header clock recovered.
  values={clockSample(0),clockSample(1,2*kSecond),clockSample(2),clockSample(3,2*kSecond)};
  values[2].stamp.header_ns=0;values[2].stamp.clock_domain="record_fallback";
  isolateVelocityClocks(values,Config{},diagnostics);
  EXPECT_TRUE(values[0].stamp.valid);EXPECT_FALSE(values[1].stamp.valid);
  EXPECT_TRUE(values[2].stamp.valid);EXPECT_FALSE(values[3].stamp.valid);
}
TEST(VelocityClock, ConfiguredLimitsHaveInclusiveBoundaries) {
  Config c;c.clock_reset_tolerance=500000000;
  for(const auto extra:{TimestampNs(0),TimestampNs(1)}) {
    std::vector<Velocity> values{clockSample(0,-kSecond),clockSample(1,-kSecond+c.clock_reset_tolerance+extra)};
    std::vector<Diagnostic> diagnostics;isolateVelocityClocks(values,c,diagnostics);
    EXPECT_TRUE(values[0].stamp.valid);EXPECT_EQ(values[1].stamp.valid,extra==0);
    values={clockSample(0,c.clock_domain_tolerance+extra)};diagnostics.clear();
    isolateVelocityClocks(values,c,diagnostics);EXPECT_EQ(values[0].stamp.valid,extra==0);
  }
}

TEST(VelocityClock, CommandDuplicateDeliveryCannotCrossClockFence) {
  RunData data;Config c;std::vector<Diagnostic> diagnostics;
  for(int i=0;i<6;++i) {
    data.commands.push_back(clockSample(i));data.feedback.push_back(clockSample(i,0,0,"/odom"));
  }
  data.commands[2].stamp.ns=data.commands[2].stamp.header_ns=data.commands[1].stamp.ns;
  data.commands[3].stamp.ns=data.commands[3].stamp.header_ns+=10*kSecond;
  isolateVelocityClocks(data.commands,c,diagnostics);
  const auto causal=causalCommands(data.commands);
  for(const auto& v:causal)if(!v.stamp.valid){EXPECT_EQ(v.stamp.ns,data.commands[2].stamp.record_ns);}
  Interval interval;interval.start=100*kSecond;interval.end=100*kSecond+500000000;
  for(const auto& row:alignVelocity(data,interval,c)) {
    const bool gap=row.ns>=100*kSecond+200000000&&row.ns<100*kSecond+400000000;
    EXPECT_EQ(row.valid,!gap)<<row.ns-100*kSecond;
  }
}

TEST(VelocityClock, DelayedFeedbackRecoversAtFirstGoodSampleAfterBadPrefix) {
  RunData data;Config c;std::vector<Diagnostic> diagnostics;
  data.feedback={clockSample(0,10*kSecond),clockSample(1,-2*kSecond),clockSample(2,-2*kSecond)};
  isolateVelocityClocks(data.feedback,c,diagnostics);
  std::stable_sort(data.feedback.begin(),data.feedback.end(),[](const auto& a,const auto& b){return a.stamp.ns<b.stamp.ns;});
  data.commands={clockSample(1,-2*kSecond)};
  Interval interval;interval.start=data.commands[0].stamp.ns;interval.end=interval.start+100000000;
  const auto rows=alignVelocity(data,interval,c);ASSERT_EQ(rows.size(),1U);
  EXPECT_TRUE(rows[0].valid)<<rows[0].reason;
  EXPECT_TRUE(data.feedback[1].stamp.valid);EXPECT_TRUE(data.feedback[2].stamp.valid);
}
