#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace track_insight {

using TimestampNs = std::int64_t;
constexpr TimestampNs kSecond = 1000000000LL;

struct Stamp {
  TimestampNs ns{0}, record_ns{0}, header_ns{0}, send_ns{0};
  std::uint64_t sequence{0};
  int epoch{0};
  std::string source, clock_domain{"recorded"};
  bool valid{true};
};

struct Pose { double x{0}, y{0}, yaw{0}; };
struct PoseSample { Stamp stamp; Pose pose; bool valid{true}; std::string reason; };
struct Velocity {
  Stamp stamp;
  double vx{0}, vy{0}, omega{0};
  std::string frame, reference_point;
  bool valid{true};
  std::string reason;
};
struct Plan {
  std::string id, frame, fingerprint;
  Stamp stamp;
  std::vector<Pose> poses;
  bool valid{true};
  std::string reason;
};
struct LogEvent {
  TimestampNs ns{0};
  std::size_t line{0};
  int session{0};
  std::string kind, category{"navigation"}, raw, source;
  std::map<std::string, std::string> fields;
};
struct Diagnostic { std::string code, message, source; TimestampNs ns{0}; };
struct Topic { std::string name, type; std::size_t count{0}; };
struct RunData {
  std::string input_path, log_path, input_hash, log_hash, frame{"map"}, base{"rbs_base_link"};
  TimestampNs start{0}, end{0};
  std::vector<Topic> topics;
  std::vector<Plan> plans;
  std::vector<PoseSample> poses;
  std::vector<Velocity> commands, feedback, downstream, tracking_raw;
  std::vector<LogEvent> events;
  std::vector<Diagnostic> diagnostics;
  bool wall_clock{true}, log_compatible{false};
};
struct Task {
  std::string id, outcome{"unknown"}, reason, uuid;
  int epoch{-1}, session{-1};
  TimestampNs start{0}, end{0};
  std::optional<Pose> goal;
  std::optional<double> goal_yaw;
  std::vector<std::size_t> evidence_lines;
};
struct Interval {
  std::string id, plan_id, navigation_id;
  TimestampNs start{0}, end{0};
  int epoch{0}, controller_session{-1};
  std::string controller_revision;
  std::string identity{"inferred"}, start_quality{"inferred"}, end_quality{"inferred"};
  std::string reason, termination{"recording_end"};
  std::vector<std::size_t> evidence_lines;
};
struct WeightedSample {
  TimestampNs start{0}, end{0};
  double value{0};
  bool valid{true};
  std::string reason;
};
struct Metric {
  std::optional<double> value;
  std::optional<double> mean, mae, rmse, p95, maximum, coverage, exceedance_s, longest_exceedance_s;
  TimestampNs maximum_ns{0};
  double valid_s{0}, candidate_s{0};
  std::size_t count{0};
  std::string unit, quality{"unavailable"}, reason;
  std::map<std::string, std::size_t> excluded;
};
struct TrackingSample {
  TimestampNs ns{0}, end_ns{0};
  Pose actual, projection;
  double progress{0}, lateral{0}, distance{0}, endpoint{0};
  std::optional<double> yaw_error;
  std::size_t segment{0};
  bool valid{false}, lateral_valid{true};
  std::string reason, stage{"unknown"};
};
struct VelocitySample {
  TimestampNs ns{0}, end_ns{0};
  Velocity command, feedback;
  bool valid{false};
  std::string reason;
};
struct Finding {
  std::string category, kind, message, interval_id;
  TimestampNs start{0}, end{0};
  std::optional<double> peak;
};
struct PathResult {
  Interval interval;
  std::vector<TrackingSample> tracking;
  std::vector<VelocitySample> velocity;
  std::map<std::string, Metric> metrics;
  std::map<std::string, std::map<std::string, Metric>> stages;
  std::vector<Finding> findings;
};
struct Evaluation {
  std::vector<Task> tasks;
  std::vector<PathResult> paths;
  std::map<std::string, std::map<std::string, Metric>> navigation_metrics;
};
struct Config {
  std::string plan_topic{"/plan"}, command_topic{"/cmd_vel_nav"};
  std::string tracking_raw_topic{"/tracking_cmd_vel_raw"};
  std::string feedback_topic{"/dev/odometry/raw/dist"}, downstream_topic{"/ctrl/velocity/navi/clct"};
  std::string frame{"map"}, base{"rbs_base_link"}, command_frame{"rbs_base_link"};
  TimestampNs tf_gap{200000000}, velocity_gap{200000000}, command_age{200000000}, log_age{100000000};
  double max_position_jump_speed{5.0}, jump_distance{0.15}, progress_window{2.0};
  double stopped_linear{0.02}, stopped_angular{0.03}, settle_duration{1.0};
  std::optional<double> lateral_threshold, velocity_threshold, angular_velocity_threshold, terminal_tolerance;
  std::size_t max_archive_files{10000};
  std::uint64_t max_archive_bytes{8ULL * 1024 * 1024 * 1024};
  double max_expansion_ratio{1000};
};
struct OffsetChange { TimestampNs from_ns{0}; int seconds{0}; };
struct DisplayClock { TimestampNs origin_ns{0}; bool wall_clock{true}; std::vector<OffsetChange> offsets; };

double wrapAngle(double angle);
TimestampNs parseTimestampNs(const std::string& text);
DisplayClock captureDisplayClock(TimestampNs start, TimestampNs end, bool wall_clock);
std::string formatTimestamp(TimestampNs ns, const DisplayClock& clock, bool full = true);
Metric summarize(const std::vector<WeightedSample>& samples, const Interval& interval,
  const std::string& unit, std::optional<double> threshold = std::nullopt);
std::vector<LogEvent> parseLog(const std::filesystem::path& path);
std::vector<Task> navigationTasks(const RunData& data);
std::vector<Interval> executionIntervals(const RunData& data, const std::vector<Task>& tasks);
std::vector<TrackingSample> matchTracking(const Plan& plan, const RunData& data,
  const Interval& interval, const Config& config);
std::vector<Velocity> causalCommands(const std::vector<Velocity>& input);
std::vector<VelocitySample> alignVelocity(const RunData& data, const Interval& interval, const Config& config);
void addObservations(PathResult&,const Config&);
std::map<std::string,Metric> terminalMetrics(const RunData&,const Task*,const Interval&,const Config&);
Evaluation evaluate(const RunData& data, const Config& config);
Config loadConfig(const std::filesystem::path& path);

}  // namespace track_insight
