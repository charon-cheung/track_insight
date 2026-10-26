# Track Insight v1 Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` to implement this plan in the current session after plan review. User rules override skill defaults: no commits, branches, worktrees, or edits to other packages. Steps use checkboxes to preserve progress.

**Goal:** Build a ROS 2 Jazzy offline CLI that reads one recorded test, reports defensible tracking metrics, and generates an independently viewable HTML report and automatic SVG/PNG figures.

**Architecture:** One new `track_insight` package containing `track_insight_ros_io`, `track_insight_core`, `track_insight_report`, and the `track_insight` executable. Core calculations use ordinary C++ domain objects. Reports consume saved analysis results and never independently infer controller behavior.

**Tech Stack:** C++17, ament_cmake, rosbag2_cpp/storage/MCAP, tf2, ROS message definitions, libarchive, nlohmann_json, yaml-cpp, fixed local Plotly.js, rsvg-convert, ament_cmake_gtest.

**Spec:** `/home/CNF2026987648/Downloads/track_insight_architecture_v1.docx`, together with the seven review clarifications accepted in the conversation on 2026-10-23. This plan records those clarifications below. The DOCX remains unchanged.

## Global Constraints

- Create and edit source, configuration, tests, resources, and documentation only under `/home/CNF2026987648/s100_docker/src/track_insight/`.
- Do not modify any existing package, especially the production tracking controller, navigation configuration, or their tests.
- Do not create Git commits, branches, or worktrees; do not initialize a Git repository merely to support tooling. The workspace `src/` is not a Git repository.
- Do not inspect or modify `LICENSE` or `CONTRIBUTING.md`. Do not create or modify README files as part of this work.
- No ROS Node, executor, publisher, subscriber, bag playback, navigation startup, HTTP server, video processing, map display, or composite tracking score.
- Standard inputs are bag ZIP and nav2.log. Also accept a bag directory or direct MCAP. Missing or mismatched logs degrade capability rather than invalidate unrelated bag evidence.
- All visible timestamps have at most millisecond precision. Preserve integer nanoseconds and source sequence internally. Freeze the generating system's local-time display rules; do not show a timezone name or use the viewing computer's timezone.
- Confirmed path identity and execution-boundary certainty are separate. Never turn missing data into zero or a recording end into navigation success.
- Use the existing workspace build/install bases and build only `track_insight`, with `MAKEFLAGS="-j12"`. No clean or temporary build workspace. Check for running RBS navigation before builds/tests and use the existing `kill_ros.sh --rbs` only if relevant navigation is running.
- Source `/opt/ros/jazzy/setup.bash` in every shell invoking a ROS build, test, CLI, or ROS-dependent executable; source workspace `install/setup.bash` when needed.
- Batch related tests and source edits. Architecture-task budget: at most seven explicit build invocations unless a concrete correctness or build issue requires more. Runtime/environment-only failures do not trigger rebuilds.
- Dependency installation outside the new package is not authorized by this plan. Resolve missing system dependencies explicitly before dependent build steps.

## Audit Evidence and Open Inputs

- No existing `track_insight` source, build, or install directory was found before preparation.
- Existing `pid_tracking_controller` and `robot_navigation` repositories contain user changes. They are read-only inputs to this work.
- Jazzy rosbag2_cpp, rosbag2_storage, rosbag2_storage_mcap, rclcpp, tf2, message packages, and ament_cmake_gtest are present. System nlohmann_json and yaml-cpp development files are present.
- `libarchive-dev` is absent; the libarchive runtime is present. `rsvg-convert` is absent. These are prerequisites for the complete ZIP/PNG implementation, not reasons to replace the accepted architecture.
- A local Plotly.js distribution was not found in the checked packages or Python environment. Obtain a fixed upstream release into this package's resources and record its version/checksum/embedded attribution before viewer implementation.
- Real input: `/home/CNF2026987648/test/pid_bags/10-22-1/pid_test_1022_155828.zip`; an already extracted bag exists alongside it. Metadata reports MCAP, 56,546 messages, 10 `/plan` messages, 2,539 `/cmd_vel_nav`, 8,484 `/ctrl/velocity/navi/clct`, 25,698 `/dev/odometry/raw/dist`, 17,979 `/tf`, and 3 `/tf_static`. Revalidate payloads with the implemented reader; do not hardcode these counts into production behavior.
- The real input's directory contains no nav2.log. Do not substitute the current simulation log as if it belonged to that recording. The simulation log is useful only as a separately identified log-format sample.
- The checked local behavior tree passes `{path}` from ComputePathToPose to FollowPath. That alone does not prove the recorded robot used the identical code/configuration or establish each `/plan` to controller revision mapping.
- Local configuration identifies `/ctrl/velocity/navi/clct` as an output topic, but the recorded run's precise publisher chain remains unverified. Keep its role as a downstream command candidate, never measured feedback.
- Current controller code resets `plan_revision_`, increments it on prepared paths, and records handoff/state events. It does not give every recorded P a directly shared identity.

## Review Focus

1. Restarted controller revisions and reset clocks must not join unrelated sessions. Exercise this in Tasks 1 and 3.
2. A correct path identity with uncertain activation time must not produce falsely confirmed interval metrics. Exercise this in Task 3.
3. A long data gap must reduce coverage rather than shrink the denominator or become a long held command. Exercise this in Task 4.
4. Re-rendering after removing source inputs must retain log evidence and time labels. Exercise this in Tasks 5 and 6.
5. A failed PNG conversion or interrupted repeated run must not present stale figures as complete new outputs. Exercise this in Task 5.

## Accepted Calculation and Delivery Contracts

- Preserve source timestamps and a canonical timestamp separately, with clock-domain, session/epoch, and sequence identity. Confirmed clock resets produce separate segments; small out-of-order arrivals are not automatically new sessions. Ambiguous discontinuities remain flagged and cannot be silently bridged.
- C revisions are scoped to a controller instance/session. P identifies every recorded path publication, including duplicate geometry; R identifies a lattice planner request and may have no P.
- Execution boundaries use `[start, end)`. Store identity confidence, start-boundary confidence, end-boundary confidence, evidence, and termination reason separately. Consecutive intervals must not double-count boundary samples.
- For time-weighted scalar summaries, use valid bounded analysis cells. A sample covers only its defined cell within the requested interval and within configured validity limits. Unknown trailing time, clock transitions, invalid interpolation brackets, and gaps have no valid weight.
- Coverage denominator is the selected candidate interval/stage duration before excluding missing or invalid measurements. Each metric reports its own valid duration, candidate duration, exclusions, and null reason. A zero-duration candidate has unavailable coverage, not 100%.
- Use the pose timeline for geometry samples. Velocity residual cells additionally split at command changes and relevant feedback timestamps; commands use bounded causal zero-order hold, feedback uses bounded interpolation. Preserve the sampling/integration method in metric_version and test convergence under changed sampling rates.
- Weighted P95 is the smallest absolute error whose cumulative valid time reaches at least 95% of total valid time. RMSE, MAE, bias, extrema, and exceedance durations use the same documented valid cells. Navigation summaries are recomputed from non-overlapping eligible cells, not averaged from P summaries.
- Keep geometric signed lateral error, distance to the selected segment, and endpoint error distinct. Ambiguous branches are unavailable for affected metrics. Progress constraints reset at path/session boundaries; rotation segments retain their own identity.
- Target yaw must have recorded or explicitly verified semantics. Path tangents are not body-yaw targets. Sparse controller logs constrain only their supported temporal neighborhood and retain source age.
- Navigation transitions, data-quality events, and tracking anomalies use separate categories/counts. Ordinary replanning is an event, not automatically a tracking anomaly. Threshold-dependent findings require explicit evaluation thresholds; no default pass/fail certification.
- `analysis.json` contains all data needed for supported re-rendering: snapshots, analysis/display series, validity gaps, events, log excerpts, metrics, provenance, and frozen display-clock information. It does not require the original bag/log at render time.
- Rendering may change presentation options only. Analysis-affecting changes require `analyze` again.
- Output status distinguishes complete, partial, and failed generation, separately from analysis evidence quality. CLI returns 0 for completed requested generation, 2 for partial generation, and 1 for fatal errors. Log unavailability is represented in capability/metric quality and does not alone imply a renderer failure.
- Render into a new staging directory and publish a coherent completed output set; refuse unsafe overwrite or mixing stale files. Allow deliberate replacement of an identified previous report only through an explicit CLI option with clear ownership checks.
- Static SVG and interactive Plotly rendering share data/scene semantics, not a pixel-identical layout contract. Support a small fixed set of figure templates; do not build a general plotting framework.

## Task 1: Time and Metric Domain Foundation

**Files:** Create `CMakeLists.txt`, `package.xml`, `include/track_insight/domain.hpp`, `include/track_insight/time.hpp`, `src/core/time.cpp`, `test/test_time.cpp`, `test/test_statistics.cpp`, and `config/default.yaml`.

**Interfaces:** Define `TimestampNs`, `SourceStamp`, `TimeSegment`, `DisplayClock`, `Interval`, `Evidence`, `Quality`, and `MetricResult`. Expose `TimestampNs parseTimestampNs(std::string_view)`, `std::string formatLocalTimestamp(TimestampNs, const DisplayClock&)`, and `WeightedSummary summarize(const std::vector<WeightedSample>&, const Interval&)`. Keep ROS types out of these declarations.

- [ ] Write behavior tests for exact nanosecond parsing, same-visible-millisecond ordering, midnight boundaries, negative/invalid timestamps, zero-duration intervals, and frozen local-time formatting.
- [ ] Write weighted-statistics fixtures with exact expectations: constant 0.1 m error gives MAE/RMSE/P95 0.1 m; 10 s valid out of 60 s candidate gives coverage 1/6; gaps split longest-exceedance duration.
- [ ] Implement domain/time/statistics primitives without adding ROS runtime objects. Validate invalid or non-finite inputs explicitly.
- [ ] Run the targeted foundation tests in the first meaningful incremental build batch; inspect failures for the intended invariant rather than manufacturing separate red cycles per case.

## Task 2: Bag, ZIP, Log, and Historical TF Input

**Files:** Create `include/track_insight/input.hpp`, `src/io/input_package.cpp`, `src/io/bag_reader.cpp`, `src/io/tf_reconstructor.cpp`, `src/core/log_parser.cpp`, `test/test_input.cpp`, `test/test_log_parser.cpp`, and `test/test_tf.cpp`.

**Interfaces:** `InputData readBag(const InputOptions&)`; `LogData parseLog(std::istream&, const LogOptions&)`; `PoseSeries reconstructPoses(const InputData&, const AnalysisConfig&)`. `InputData` retains all P snapshots, selected signal arrays, source stamps, topic/type information, and diagnostics. ZIP cleanup uses RAII and only owns its own extracted directory.

- [ ] Resolve the libarchive development dependency within the user's permitted environment before building the ZIP reader. Obtain the fixed Plotly resource separately; it is not a core dependency.
- [ ] Add fixtures for ZIP path traversal/links/size limits, multiple ambiguous bags, duplicate publications, unsupported topic types, missing logs, invalid numeric fields, and log clock incompatibility.
- [ ] Decode Path, TwistStamped/Twist, Odometry, and TF messages; retain recording/send/header times without treating their meanings as interchangeable. Recognize available navigation action/status evidence without requiring it in historical bags.
- [ ] Reconstruct only requested frames/times with adequate historical TF retention. Check each dynamic chain edge's validity; reject gaps, invalid transforms, and unsupported reference-point conversions.
- [ ] Parse the observed navigation/lattice/controller log families, retaining original text, line, source, time, and controller-session evidence. Preserve unknown events without inventing navigation outcomes.
- [ ] Verify generated MCAP fixtures and ZIP-versus-directory equivalence. Recheck real metadata counts through the reader, without presuming matching logs.

## Task 3: Navigation, Path Identity, and Execution Intervals

**Files:** Create `include/track_insight/correlation.hpp`, `src/core/correlation.cpp`, `src/core/motion_stage.cpp`, `test/test_correlation.cpp`, and `test/test_motion_stage.cpp`.

**Interfaces:** `CorrelationResult correlate(const InputData&, const LogData&, const AnalysisConfig&)`; `StageSeries detectStages(const CorrelationResult&, const InputData&, const AnalysisConfig&)`. Return independent N/R/P/C records and execution intervals carrying separate identity/boundary evidence.

- [ ] Test repeated C numbers across sessions, failed R without P, duplicate P content, missing first-command evidence, identical goals, overlapping candidates, cancellation, replacement, and truncated recording.
- [ ] Implement conservative evidence rules. Timing proximity alone never creates confirmed identity; conflicts remain visible. A fallback publication window is inferred and clipped to available lifecycle boundaries.
- [ ] Classify movement stages from supported controller evidence or explicitly marked kinematic inference. Unknown stages remain unknown; sparse state logs have bounded validity.
- [ ] Verify normal replanning is a navigation event and is excluded from tracking-anomaly counts.

## Task 4: Geometry, Velocity, and Tracking Evaluation

**Files:** Create `include/track_insight/evaluation.hpp`, `src/core/tracking_matcher.cpp`, `src/core/velocity_aligner.cpp`, `src/core/evaluator.cpp`, `src/core/anomalies.cpp`, `test/test_tracking.cpp`, `test/test_velocity.cpp`, and `test/test_evaluator.cpp`.

**Interfaces:** `TrackingSeries matchTracking(const PlanSnapshot&, const PoseSeries&, const ExecutionInterval&, const AnalysisConfig&)`; `VelocityComparison alignVelocity(const VelocitySeries&, const VelocitySeries&, const ExecutionInterval&, const AnalysisConfig&)`; `EvaluationResult evaluate(const CorrelationResult&, const InputData&, const AnalysisConfig&)`.

- [ ] Add invariant fixtures for constant lateral offset, endpoint overshoot, curves, crossing/repeated XY branches, reverse traversal, pure rotation, and sideways travel with fixed body yaw.
- [ ] Implement history-constrained segment projection and ambiguity handling. Report controller-local reference errors separately from geometric errors if both exist.
- [ ] Align command/feedback timestamps and body reference points. Test zero-order hold expiry, repeated source stamps with distinct delivery evidence, interpolation gaps, frame rotations, and offset-reference rotational velocity.
- [ ] Compute per-P/per-N/per-stage metrics, terminal errors where supported, coverage, exceedance intervals, and observable anomalies. Do not infer missing yaw targets or target schedules.
- [ ] Verify unequal sample-rate fixtures give explained convergent results and that navigation aggregation avoids overlap and does not average percentiles. Maintain separate confirmed and inferred aggregates.

## Task 5: Saved Analysis and Automatic Static Reports

**Files:** Create `include/track_insight/report.hpp`, `src/report/serialization.cpp`, `src/report/plot_scene.cpp`, `src/report/svg_renderer.cpp`, `src/report/output_writer.cpp`, `test/test_serialization.cpp`, `test/test_report.cpp`, and `test/test_output.cpp`.

**Interfaces:** `AnalysisDocument analyze(const InputData&, const LogData&, const AnalysisConfig&)`; `void saveAnalysis(const AnalysisDocument&, const std::filesystem::path&)`; `AnalysisDocument loadAnalysis(const std::filesystem::path&)`; `ReportResult renderReport(const AnalysisDocument&, const ReportOptions&)`.

- [ ] Test exact timestamp string round trips, schema validation, null-plus-reason values, non-finite value rejection, and frozen display-clock persistence.
- [ ] Build the fixed trajectory/velocity/error scenes from one result model. Preserve units, paths, interval boundaries, timestamps, direction/orientation markers, start/end, extrema, and missing-data breaks.
- [ ] Implement SVG output and safe process execution of rsvg-convert without shell interpolation. Resolve PNG dependency before claiming complete figure delivery.
- [ ] Generate metrics.json, metrics.csv, manifest.json, and complete analysis.json; embed needed log excerpts. Protect CSV/HTML/XML text handling.
- [ ] Test re-rendering with unavailable original inputs, missing converter behavior, interrupted generation, owned-output replacement, and protection against stale outputs or accidental overwrite.

## Task 6: Self-contained Interactive HTML

**Files:** Create `resources/viewer/index.html`, `resources/viewer/style.css`, `resources/viewer/app.js`, `resources/viewer/plotly.min.js`, `src/report/html_renderer.cpp`, and package-local viewer test fixtures/scripts.

**Interfaces:** The viewer receives one embedded, versioned display model derived from `AnalysisDocument`/`PlotScene`. It maintains selection `{navigation_id, plan_ids, interval_id, time_segment_id, cursor_offset_ms, visible_range}`. It does not calculate authoritative metrics.

- [ ] Embed a fixed upstream Plotly.js distribution with recorded checksum/version and retained embedded attribution. Do not require external scripts, fonts, fetch of adjacent JSON, a server, or Node.js at runtime.
- [ ] Implement N/P selection, unassociated records, multi-path overlays, equal-axis XY view, three-axis velocity and error plots, and existing metric/quality tables.
- [ ] Implement synchronized hover/click/cursor, time-based playback and speed selection, event jumps, log excerpts, gap handling, and multiple-time candidates for repeated XY positions.
- [ ] Keep display decimation separate from full analysis data and preserve extrema/events/gaps. Manual image export supplements, not replaces, automatic images.
- [ ] Verify file:// loading with networking unavailable, browser console errors, relocated reports, timezone changes, missing-data cursor states, and linked selection behavior using a local browser. Keep all test artifacts under the new package or its permitted build output.

## Task 7: CLI, End-to-end Verification, and Final Review

**Files:** Create `src/main.cpp`, `src/config.cpp`, `test/test_cli.cpp`, and `docs/verification.md`; finalize package installation rules. No README changes.

**Interfaces:** `track_insight inspect --bag INPUT [--log LOG]`; `track_insight analyze --bag INPUT [--log LOG] --output OUTPUT [--config CONFIG]`; `track_insight render --analysis ANALYSIS_JSON --output OUTPUT`. Record presentation-only options separately from analysis configuration.

- [ ] Complete argument validation, capability output, generation statuses/exit codes, installed-resource lookup, and safe output ownership handling.
- [ ] Run synthetic end-to-end cases with fully known answers, including accurate navigation association and deliberate missing/mismatched logs.
- [ ] Analyze the identified 10-22 bag using the reader's results; without its matching nav2.log, verify honest bag-only/inferred output and do not claim confirmed task outcomes. If matching data becomes available, add its real association check without changing the metric contract.
- [ ] Build and run the package's meaningful unit/integration tests, then inspect the generated HTML and SVG/PNG figures. Re-render a saved report without original input paths.
- [ ] Perform a fresh code review of the completed new package if skill-authorized review tooling is used. Review all findings against behavior and the accepted scope; keep source changes confined to this package.
- [ ] Review the complete task diff/file inventory, whitespace, file growth, and dependencies. Since the new package is not yet a Git repository, use no-index diffs/file counts as needed; do not alter another repository merely to get Git status output.
- [ ] Report files/major responsibilities, build invocation count, test evidence, actual-data coverage limits, dependency state, and any remaining risks. Do not claim a working complete v1 from compilation alone.

## Verification Commands and Build Batches

Run from `/home/CNF2026987648/s100_docker`, after the navigation-process check and any required stop:

```bash
source /opt/ros/jazzy/setup.bash
MAKEFLAGS="-j12" colcon build --packages-select track_insight --cmake-args -DBUILD_TESTING=ON
```

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash
ROS_LOG_DIR=/home/CNF2026987648/s100_docker/build/track_insight/test_ros_logs colcon test --packages-select track_insight --event-handlers console_direct+
colcon test-result --test-result-base build/track_insight --verbose
```

Planned build batches: foundation; input/correlation; evaluation; reporting/viewer/CLI; final substantive corrections if needed. This is a ceiling-oriented grouping, not a requirement to perform five builds. Tests of existing binaries and non-semantic formatting checks do not justify additional builds.

## Preparation Status

- [x] Read the supplied architecture and accepted review clarifications.
- [x] Check workspace boundaries, existing user changes, installed dependencies, and candidate real input locations.
- [x] Confirm metadata of the relevant real bag and absence of nav2.log in its directory.
- [x] Write and self-review this implementation plan within the new package only.
- [x] User reviews this concrete plan before implementation, as required by the writing-plans workflow.
- [x] Resolve dependency installation scope for libarchive-dev and rsvg-convert.
- [x] Implement and verify Tasks 1–7.

Final execution evidence is recorded in ../../progress.md and ../../verification.md. APIs and test files were consolidated by responsibility as ledgered; no other package was modified.
