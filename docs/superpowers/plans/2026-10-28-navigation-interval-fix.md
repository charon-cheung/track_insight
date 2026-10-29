# Navigation Interval Fix Implementation Plan

> Execution: superpowers:executing-plans, inline. User explicitly authorized review, proposal and direct implementation. No commits, branches or worktrees.

**Goal:** Correct 10-28-7 path/navigation associations and remove post-navigation waiting from plots and metrics.
**Architecture:** Keep parsing in log_parser.cpp and task lifecycle/correlation in correlation.cpp. Scope LogEvent.session by event kind: navigation lifecycle follows log-clock sessions; controller evidence additionally follows revision resets. A task is open only while its reason is recording_ended_without_result. Closed unknown tasks cannot be extended or receive late results.
**Tech Stack:** C++17, ROS 2 Jazzy, existing gtest and self-contained HTML.
**Spec:** User request in this conversation: review all 18 cases, propose and execute a fix. Independent expected intervals are in log/track_insight_1028_7_interval_audit/expected_intervals.json.

## Constraints and decisions
- Preserve raw data, controller behavior, UI and geometric/statistical definitions.
- End this round's path windows at their recorded navigation success; retain pauses within each window. No blanket zero-speed trimming.
- Keep controller revision isolation and navigation clock-reset/UUID protection.
- Update this round and its archive summary only; other rounds remain unchanged.
- Existing .gitignore is pre-existing untracked user work.
- Native session field keeps existing interface; its scope is documented at the model and parser.

## Review focus
- Revision reset between navigation begin and result must not split navigation; controller evidence must still split.
- Clock reset must continue separating navigation events.
- Closed unknown tasks must not grow on later starts or receive late results.
- Replacement, failure/cancel, UUID and no-log fallback behavior must remain valid.
- All 18 reanalyzed windows must match independent log evidence and retain all motion inside the window; raw data/other rounds must match baseline.

## Task 1: Correct session and task lifecycle semantics
Files: src/core/log_parser.cpp, src/core/correlation.cpp, include/track_insight/domain.hpp, test/test_core.cpp, test/test_pipeline.cpp.
- [x] Add pipeline reproduction with historical revision 11, current begin, revision 1 and success: one succeeded navigation, controller isolation preserved, 1.8 s execution window/metric weight.
- [x] Add clock-rollback isolation and closed-unknown/late-result regression tests; retain existing UUID/action/fallback tests.
- [x] Incrementally build affected package and run representative tests once before implementation; expect intended failures.
- [x] Separate navigation clock session from controller revision session, and only mutate tasks still open.
- [x] Incrementally build and run core/pipeline/archive/overview tests; all new regressions pass, with the three documented pre-existing MotionWindow failures unchanged.

## Task 2: Regenerate and verify real round
- [x] Save pre-change analysis, independent 18-case intervals and hashes of other reports.
- [x] Reanalyze the supplied bag/log and replace only 10-28-7 archive report.
- [x] Check all navigation starts/results, plan identities, intervals, raw data, and recomputed metrics; write before/after audit.
- [x] Browser-check P2 time ranges/cursor, all 18 selections, report overview and PNG-only trajectory control.
- [x] Final diff and independent review; no oversized file growth or unrelated changes.

## Execution notes
Pre-flight: Task 2 consumes Task 1 analysis; exact ns independent log boundaries are authoritative, not P-number coincidence. Existing report starts remain inferred publication times because strict controller activation evidence is unavailable. No forced activation confirmation.

Task 1 RED: new revision-reset pipeline test and closed-unknown correlation test reproduced the intended failures. Three existing MotionWindow tests also fail before any production edits (no-log inferred trimming is absent in HEAD); preserve these tests, report this baseline limitation, and do not expand this log-association fix into a new motion-window implementation.
Build invocation 1 accidentally used src cwd; confirmed its build/install were newly created, removed only those new directories, and preserved the existing src/log history. Invocation 2 uses workspace build/install. No further isolated builds.

Task 1 verification: invocation 3 incrementally rebuilt track_insight in the existing workspace successfully. Pipeline (12 tests), archive and overview passed; core passed 27/30, with only the same three pre-existing MotionWindow failures. No tests were disabled or weakened. The final independent code review found no issues requiring changes.

Task 2 verification: reanalyzed the supplied bag/log with --archive /home/CNF2026987648/pid_reports --replace. All 18 task start/result evidence pairs and path windows match the independently extracted log boundaries. P1-P17 lose 99.032 s of post-navigation waiting in total; P18 is unchanged. Raw data, signals, plans, configuration, clock, metadata and topics are identical; all eight other report HTML hashes are unchanged. Independently recomputed lateral/path-distance metrics agree with the regenerated metrics.

Browser verification passed all 18 time ranges, shared cursors, P2 header, report overview, mode switching, stepping, PNG-only trajectory controls and PNG rendering, with no JavaScript exceptions. Evidence and P2 screenshot are under /home/CNF2026987648/s100_docker/log/track_insight_1028_7_interval_audit/.

Final scope: five tracked files, 64 insertions and 5 deletions (including 56 regression-test lines and one domain comment), plus this plan. No oversized file grew; no controller algorithm, velocity time basis or error definition changed. git diff --check passed. The pre-existing untracked .gitignore remains untouched. Total build invocations: three.
