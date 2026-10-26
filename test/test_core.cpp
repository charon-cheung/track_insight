#include <gtest/gtest.h>
#include <cmath>
#include "track_insight/domain.hpp"

using namespace track_insight;

TEST(Time, KeepsNanosecondsAndRejectsOverflow) {
  EXPECT_EQ(parseTimestampNs("1792655909.073718851"), 1792655909073718851LL);
  EXPECT_EQ(parseTimestampNs("-0.000000001"), -1);
  EXPECT_THROW(parseTimestampNs("1.1234567890"), std::exception);
  EXPECT_THROW(parseTimestampNs("99999999999999999"), std::exception);
}

TEST(Time, FrozenOffsetAndSameMillisecond) {
  DisplayClock clock{0, true, {{0, 3600}}};
  EXPECT_EQ(formatTimestamp(1234567890, clock), "1970-01-01 01:00:01.234");
  EXPECT_EQ(formatTimestamp(1234999999, clock), "1970-01-01 01:00:01.234");
  EXPECT_EQ(formatTimestamp(86399999999999LL, clock), "1970-01-02 00:59:59.999");
  clock.wall_clock = false;
  EXPECT_EQ(formatTimestamp(1234567890, clock), "+1.234 s");
}

TEST(Metrics, WeightsByTimeAndKeepsMissingDuration) {
  Interval interval; interval.end = 60 * kSecond;
  const auto result = summarize({{0, 10*kSecond, -0.1, true, ""}}, interval, "m", 0.05);
  ASSERT_TRUE(result.rmse);
  EXPECT_NEAR(*result.rmse, 0.1, 1e-12);
  EXPECT_NEAR(*result.mean, -0.1, 1e-12);
  EXPECT_NEAR(*result.p95, 0.1, 1e-12);
  EXPECT_NEAR(*result.coverage, 1.0/6.0, 1e-12);
  EXPECT_DOUBLE_EQ(*result.longest_exceedance_s, 10.0);
}

TEST(Metrics, GapsAndHalfOpenIntervalsDoNotDoubleCount) {
  Interval interval; interval.start=kSecond; interval.end=5*kSecond;
  auto result=summarize({{0,2*kSecond,2,true,""},{3*kSecond,6*kSecond,2,true,""}},interval,"m",1);
  EXPECT_DOUBLE_EQ(result.valid_s,3);
  EXPECT_DOUBLE_EQ(*result.longest_exceedance_s,2);
  interval.end=interval.start;
  EXPECT_FALSE(summarize({},interval,"m").coverage);
}

TEST(Metrics, PercentileUsesDurationNotNumberOfSamples) {
  Interval interval; interval.end=100*kSecond;
  auto result=summarize({{0,96*kSecond,1,true,""},{96*kSecond,100*kSecond,9,true,""}},interval,"m");
  EXPECT_DOUBLE_EQ(*result.p95,1);
  EXPECT_NEAR(*result.rmse,std::sqrt(4.2),1e-12);
}

TEST(Correlation, RecordingEndNeverBecomesNavigationSuccess) {
  RunData data; data.start=kSecond; data.end=10*kSecond; data.log_compatible=true;
  data.events.push_back({2*kSecond,1,0,"navigation_start","navigation","start","log",{}});
  auto tasks=navigationTasks(data);
  ASSERT_EQ(tasks.size(),1U);
  EXPECT_EQ(tasks[0].outcome,"unknown");
  EXPECT_EQ(tasks[0].end,data.end);
}

TEST(Correlation, SameGoalAndDuplicatePathsDoNotProveAdoption) {
  RunData data; data.start=kSecond; data.end=10*kSecond;
  Plan p; p.id="P1"; p.frame="map"; p.stamp.ns=2*kSecond; p.stamp.record_ns=2*kSecond;
  p.poses={{0,0,0},{1,0,0}}; data.plans.push_back(p);
  p.id="P2";p.stamp.ns=5*kSecond;p.stamp.record_ns=5*kSecond;data.plans.push_back(p);
  auto intervals=executionIntervals(data,{});
  ASSERT_EQ(intervals.size(),2U);
  EXPECT_EQ(intervals[0].identity,"inferred");
  EXPECT_EQ(intervals[0].end,intervals[1].start);
}

TEST(Tracking, SeparatesLateralErrorFromEndpointOvershoot) {
  RunData data; Plan plan;plan.id="P1";plan.poses={{0,0,0},{1,0,0}};
  Interval interval;interval.end=kSecond;
  PoseSample p;p.stamp.ns=0;p.pose={1.2,0.1,0};data.poses.push_back(p);
  p.stamp.ns=100000000;data.poses.push_back(p);
  auto samples=matchTracking(plan,data,interval,Config{});
  ASSERT_FALSE(samples.empty());ASSERT_TRUE(samples.front().valid);
  EXPECT_NEAR(samples.front().lateral,0.1,1e-9);
  EXPECT_NEAR(samples.front().distance,std::sqrt(0.05),1e-9);
  EXPECT_FALSE(samples.front().yaw_error);
}

TEST(Velocity, HoldsCommandCausallyAndExpiresIt) {
  RunData data;Interval interval;interval.end=kSecond;
  Velocity v;v.stamp.ns=0;v.frame="base";v.vx=1;data.commands.push_back(v);
  for(int i=0;i<=10;i++){v.stamp.ns=i*100000000LL;v.vx=0.8;data.feedback.push_back(v);}
  Config config;config.command_age=200000000;
  auto samples=alignVelocity(data,interval,config);
  ASSERT_FALSE(samples.empty()); EXPECT_TRUE(samples.front().valid);
  bool expired=false;for(const auto& s:samples) if(s.ns>=200000000 && !s.valid) expired=true;
  EXPECT_TRUE(expired);
}


TEST(Tracking, CrossingBranchIsUnavailableWithoutHistory) {
  Plan p;p.poses={{-1,-1,0},{1,1,0},{-1,1,0},{1,-1,0}};
  RunData d;PoseSample x;x.stamp.ns=kSecond;x.pose={0,0,0};d.poses.push_back(x);
  Interval i;i.start=kSecond;i.end=2*kSecond;
  const auto result=matchTracking(p,d,i,Config{});
  ASSERT_EQ(result.size(),1U);EXPECT_FALSE(result[0].valid);EXPECT_EQ(result[0].reason,"ambiguous_path_branch");
}
TEST(Tracking, RotationHasPointDistanceButNoLateralDirection) {
  Plan p;p.poses={{0,0,0},{0,0,1.57}};RunData d;PoseSample x;x.stamp.ns=kSecond;x.pose={0.1,0,0};d.poses.push_back(x);
  Interval i;i.start=kSecond;i.end=2*kSecond;
  const auto result=matchTracking(p,d,i,Config{});ASSERT_EQ(result.size(),1U);
  EXPECT_TRUE(result[0].valid);EXPECT_FALSE(result[0].lateral_valid);EXPECT_NEAR(result[0].distance,.1,1e-9);
}
TEST(Tracking, SidewaysBodyYawIsNotPathTangent) {
  Plan p;p.poses={{0,0,0},{0,2,0}};RunData d;d.log_compatible=true;
  PoseSample x;x.stamp.ns=kSecond;x.pose={.1,1,0};d.poses.push_back(x);
  Interval i;i.start=kSecond;i.end=2*kSecond;i.identity="confirmed";i.controller_session=2;i.controller_revision="1";
  LogEvent e;e.ns=kSecond;e.kind="tracking_state";e.session=2;e.fields={{"target_yaw","0"},{"plan_revision","1"}};d.events.push_back(e);
  auto result=matchTracking(p,d,i,Config{});ASSERT_TRUE(result[0].yaw_error);EXPECT_NEAR(*result[0].yaw_error,0,1e-9);
  d.events[0].session=1;result=matchTracking(p,d,i,Config{});EXPECT_FALSE(result[0].yaw_error);
}
TEST(Tracking, ReverseMotionCanFollowOrderedReversePath) {
  Plan p;p.poses={{1,0,0},{0,0,0}};RunData d;Interval i;i.end=kSecond;
  for(int n=0;n<5;n++){PoseSample x;x.stamp.ns=n*100000000LL;x.pose={1-n*.1,.1,0};d.poses.push_back(x);}
  auto result=matchTracking(p,d,i,Config{});ASSERT_EQ(result.size(),5U);
  for(const auto& s:result){EXPECT_TRUE(s.valid);EXPECT_NEAR(s.lateral,-.1,1e-9);}
}
TEST(Correlation, IdentityDoesNotConfirmActivationWithoutCommandEvidence) {
  RunData d;d.start=kSecond;d.end=3*kSecond;d.log_compatible=true;
  Plan p;p.id="P1";p.frame="map";p.stamp.record_ns=kSecond;p.stamp.header_ns=kSecond;p.poses={{0,0,0},{1,0,0}};d.plans.push_back(p);
  LogEvent e;e.ns=kSecond;e.kind="handoff";e.fields={{"path_stamp_ns",std::to_string(kSecond)},{"poses","2"},{"frame","map"},{"plan_revision","1"}};d.events.push_back(e);
  auto windows=executionIntervals(d,{});ASSERT_EQ(windows.size(),1U);EXPECT_EQ(windows[0].identity,"confirmed");EXPECT_EQ(windows[0].start_quality,"inferred");
  e.kind="tracking_state";e.fields["command_stamp_ns"]=std::to_string(kSecond+10000000);d.events.push_back(e);
  Velocity v;v.stamp.ns=kSecond+10000000;d.commands.push_back(v);
  windows=executionIntervals(d,{});EXPECT_EQ(windows[0].start_quality,"confirmed");EXPECT_EQ(windows[0].end_quality,"inferred");
}
TEST(Correlation, ResultFromAnotherSessionOrUuidIsNotApplied) {
  RunData d;d.start=kSecond;d.end=10*kSecond;d.log_compatible=true;
  d.events.push_back({2*kSecond,1,1,"navigation_start","navigation","","log",{{"uuid","a"}}});
  d.events.push_back({3*kSecond,2,2,"navigation_succeeded","navigation","","log",{{"uuid","a"}}});
  d.events.push_back({4*kSecond,3,1,"navigation_succeeded","navigation","","log",{{"uuid","b"}}});
  EXPECT_EQ(navigationTasks(d).front().outcome,"unknown");
}
TEST(Velocity, FrameMismatchAndFeedbackGapHaveNoWeight) {
  RunData d;Interval i;i.end=kSecond;Velocity c;c.stamp.ns=0;c.frame="base";d.commands.push_back(c);
  c.frame="other";d.feedback.push_back(c);c.stamp.ns=kSecond;d.feedback.push_back(c);
  auto rows=alignVelocity(d,i,Config{});for(const auto& r:rows)EXPECT_FALSE(r.valid);
}
TEST(Velocity, LinearFeedbackConvergesUnderFinerSampling) {
  auto error=[](int count){RunData d;Interval i;i.end=kSecond;Config c;c.command_age=2*kSecond;c.velocity_gap=2*kSecond;
    Velocity v;v.frame="base";d.commands.push_back(v);
    for(int j=0;j<=count;j++){v.stamp.ns=j*kSecond/count;v.vx=double(j)/count;d.feedback.push_back(v);}
    std::vector<WeightedSample> cells;for(const auto& s:alignVelocity(d,i,c))cells.push_back({s.ns,s.end_ns,s.feedback.vx,s.valid,s.reason});
    return *summarize(cells,i,"m/s").rmse;};
  const double exact=std::sqrt(1./3);EXPECT_LT(std::abs(error(100)-exact),std::abs(error(10)-exact));EXPECT_NEAR(error(100),exact,.005);
}
TEST(Terminal, TruncatedNavigationNeverHasTerminalSuccessMetrics) {
  RunData d;Task task;task.start=kSecond;task.end=2*kSecond;task.goal=Pose{0,0,0};
  Interval i;i.end=task.end;const auto m=terminalMetrics(d,&task,i,Config{});
  EXPECT_FALSE(m.at("goal_position_error").value);EXPECT_EQ(m.at("goal_position_error").reason,"navigation_result_unavailable");
  EXPECT_FALSE(m.at("stopped_drift").value);
}
TEST(Terminal, KnownStoppedWindowReportsDriftSeparately) {
  RunData d;Task task;task.start=kSecond;task.end=2*kSecond;task.goal=Pose{1,0,0};task.reason="recorded_navigation_result";
  Interval interval;interval.end=task.end;Config c;
  for(int j=0;j<=15;j++){PoseSample p;p.stamp.ns=task.end+j*100000000LL;p.pose={1.1+j*.001,0,0};d.poses.push_back(p);
    Velocity v;v.stamp.ns=p.stamp.ns;d.feedback.push_back(v);}
  const auto m=terminalMetrics(d,&task,interval,c);
  ASSERT_TRUE(m.at("goal_position_error").value);EXPECT_NEAR(*m.at("goal_position_error").value,.1,1e-9);
  ASSERT_TRUE(m.at("stopped_drift").value);EXPECT_NEAR(*m.at("stopped_drift").value,.01,1e-9);
  EXPECT_FALSE(m.at("settling_time").value);
}

TEST(Velocity, RepeatedHeaderUsesDeliveryEvidenceWithoutLookingAhead) {
  RunData d;Interval i;i.start=kSecond;i.end=kSecond+200000000;Config c;c.command_age=kSecond;
  Velocity v;v.frame="base";v.stamp.ns=kSecond;v.stamp.record_ns=kSecond;v.vx=0;d.commands.push_back(v);
  v.stamp.record_ns=kSecond+100000000;v.stamp.sequence=1;v.vx=1;d.commands.push_back(v);
  for(int j=0;j<=4;j++){v.stamp.ns=kSecond+j*50000000LL;v.vx=0;d.feedback.push_back(v);}
  const auto rows=alignVelocity(d,i,c);ASSERT_EQ(rows.size(),4U);
  for(const auto& row:rows){ASSERT_TRUE(row.valid);EXPECT_DOUBLE_EQ(row.command.vx,row.ns<kSecond+100000000?0:1);}
}
TEST(Correlation, OverlappingClockEpochCannotInheritOldTask) {
  RunData d;d.end=10*kSecond;Plan p;p.id="P1";p.stamp.record_ns=kSecond;p.stamp.epoch=1;p.poses={{0,0,0},{1,0,0}};d.plans.push_back(p);
  Task task;task.id="N1";task.start=0;task.end=5*kSecond;task.epoch=0;task.outcome="succeeded";
  const auto intervals=executionIntervals(d,{task});ASSERT_EQ(intervals.size(),1U);
  EXPECT_TRUE(intervals[0].navigation_id.empty());EXPECT_EQ(intervals[0].end_quality,"inferred");
}
TEST(Correlation, ActionAndLogDescribeOneTask) {
  RunData d;d.start=kSecond;d.end=5*kSecond;d.log_compatible=true;
  d.events.push_back({kSecond,1,0,"navigation_start","navigation","","log",{{"goal_x","1"},{"goal_y","2"}}});
  d.events.push_back({kSecond,0,0,"navigation_start","navigation","","bag_action_status",{{"uuid","abc"}}});
  d.events.push_back({3*kSecond,0,0,"navigation_succeeded","navigation","","bag_action_status",{{"uuid","abc"}}});
  const auto tasks=navigationTasks(d);ASSERT_EQ(tasks.size(),1U);EXPECT_EQ(tasks[0].outcome,"succeeded");
  ASSERT_TRUE(tasks[0].goal);EXPECT_DOUBLE_EQ(tasks[0].goal->x,1);EXPECT_EQ(tasks[0].epoch,0);
}
TEST(Metrics, StageCandidatesRetainAllUnknownGapTime) {
  RunData d;d.end=10*kSecond;Plan p;p.id="P1";p.poses={{0,0,0},{1,0,0}};d.plans.push_back(p);
  for(auto ns:{0LL,100000000LL,2000000000LL,2100000000LL}){PoseSample s;s.stamp.ns=ns;s.pose={.1,.1,0};d.poses.push_back(s);}
  const auto result=evaluate(d,Config{});ASSERT_EQ(result.paths.size(),1U);
  double candidate=0;for(const auto& stage:result.paths[0].stages)candidate+=stage.second.at("lateral").candidate_s;
  EXPECT_NEAR(candidate,10,1e-9);EXPECT_NEAR(result.paths[0].stages.at("unknown").at("lateral").candidate_s,10,1e-9);
}


namespace {
RunData motionWindowFixture() {
  RunData data;data.end=12*kSecond;
  Plan plan;plan.id="P1";plan.poses={{0,0,0},{1,0,0}};data.plans.push_back(plan);
  for(int j=0;j<=120;++j) {
    const bool moving=(j>=20&&j<40)||(j>=60&&j<70);
    Velocity v;v.stamp.ns=j*100000000LL;v.frame="rbs_base_link";
    v.vx=moving?.1:0;data.commands.push_back(v);data.feedback.push_back(v);
    PoseSample p;p.stamp=v.stamp;p.pose={.5,j>=20&&j<80?.1:0,0};data.poses.push_back(p);
  }
  return data;
}
}
TEST(MotionWindow, RemovesIdleEdgesFromMetricsButKeepsIntermediateStop) {
  const auto result=evaluate(motionWindowFixture(),Config{});ASSERT_EQ(result.paths.size(),1U);
  const auto& path=result.paths.front();
  EXPECT_EQ(path.interval.start,2*kSecond);EXPECT_EQ(path.interval.end,8*kSecond);
  EXPECT_DOUBLE_EQ(path.metrics.at("lateral").candidate_s,6);
  ASSERT_TRUE(path.metrics.at("lateral").rmse);EXPECT_NEAR(*path.metrics.at("lateral").rmse,.1,1e-9);
  EXPECT_DOUBLE_EQ(path.stages.at("inferred:stationary").at("lateral").candidate_s,3);
  EXPECT_TRUE(result.tasks.empty());EXPECT_EQ(path.interval.end_quality,"inferred");
}
TEST(MotionWindow, PreservesCommandedStallAndSlowTerminalAdjustment) {
  auto data=motionWindowFixture();
  for(auto& v:data.commands)if(v.vx>0)v.vx=.005;
  for(auto& v:data.feedback)v.vx=0;
  const auto result=evaluate(data,Config{});
  EXPECT_EQ(result.paths.front().interval.start,2*kSecond);
  EXPECT_EQ(result.paths.front().interval.end,8*kSecond);
}
TEST(MotionWindow, IncludesAngularMotionAndActualFeedbackOverrun) {
  auto data=motionWindowFixture();
  for(auto& v:data.feedback){v.omega=v.vx;v.vx=0;if(v.stamp.ns>=7*kSecond&&v.stamp.ns<9*kSecond)v.omega=.1;}
  const auto result=evaluate(data,Config{});
  EXPECT_EQ(result.paths.front().interval.start,2*kSecond);
  EXPECT_EQ(result.paths.front().interval.end,10*kSecond);
}
TEST(MotionWindow, MissingFeedbackCannotProveStoppedTail) {
  auto data=motionWindowFixture();data.feedback.resize(70);
  const auto result=evaluate(data,Config{});
  EXPECT_EQ(result.paths.front().interval.end,12*kSecond);
  data.commands.clear();data.feedback.clear();
  const auto missing=evaluate(data,Config{});
  EXPECT_EQ(missing.paths.front().interval.start,0);EXPECT_EQ(missing.paths.front().interval.end,12*kSecond);
}
TEST(MotionWindow, ActionLifecycleTakesPriorityOverVelocityInference) {
  auto data=motionWindowFixture();
  data.events.push_back({0,0,0,"navigation_start","navigation","","bag_action_status",{{"uuid","goal"}}});
  data.events.push_back({10*kSecond,0,0,"navigation_succeeded","navigation","","bag_action_status",{{"uuid","goal"}}});
  const auto result=evaluate(data,Config{});ASSERT_EQ(result.tasks.size(),1U);
  EXPECT_EQ(result.tasks.front().outcome,"succeeded");
  EXPECT_EQ(result.paths.front().interval.start,0);EXPECT_EQ(result.paths.front().interval.end,10*kSecond);
  EXPECT_EQ(result.paths.front().interval.end_quality,"confirmed");
}
