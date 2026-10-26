# Track Insight implementation ledger

Plan: docs/superpowers/plans/2026-10-23-track-insight-v1.md

- User approved the concrete plan and inline execution; system dependencies also approved.
- Source scope is exclusively this package. Existing packages remain read-only. No Git operations that change history or working context.
- User installed libarchive-dev and librsvg2-bin successfully. Verified pkg-config and rsvg-convert. Local .deps fallback is unused and removed.
- Initial metadata audit located the 10-22 bag; matching nav2.log is absent in that directory. Real-data confirmed association cannot be claimed without additional evidence.
- Build count: 9. First configure exposed mixed ament link signatures and invalid placeholder maintainer email; both corrected. No clean build.
- Implementation complete. Verification is grouped by observable invariants.

- Build 2 and 3 succeeded. First test batch: 13/14 passed; relative-time rounding discrepancy fixed.
- Real ZIP generated 10 inferred path windows and 60 SVG/PNG files. User confirmed no matching log exists.
- Ruling: consolidate small interfaces/tests while keeping core, ROS IO, report responsibilities separate; no controller packages changed.
- Ruling: raw command and feedback remain visible independently, residual metrics require valid pairs.
- Third build omitted newly written pipeline target because the target-edit command used the workspace root rather than src; fixed path before build 4. No clean build.

- Build 4: full 28 gtest cases passed (colcon reports 31 including 3 CTest wrappers).
- Browser passed offline file loading, timezone override, playback and linked ranges; zero JS exceptions.
- Fresh read-only review identified 6 important boundary issues. Fix batch covers TF reset exclusion, task epochs, causal duplicate commands, action/log deduplication, unknown-stage duration, explicit --replace.
- Review findings about navigation aggregate visibility and generation/evidence statuses were regraded important because they affect user interpretation; included in the same fix batch.
- Ruling: TF header resets invalidate affected chains rather than infer physical timing; this sacrifices coverage to avoid cross-session geometry.
- Review exclusions: runtime/browser/data checks stay with the executor; unavailable real logs cannot prove adoption; generated artifacts, vendor internals and unrelated packages remain outside code review.

- Build 7 and all 35 gtest cases passed (38 in colcon including wrappers). Final offline browser acceptance passed.
- Visual QA found the static trajectory x-axis caption overlapping tick labels. A narrowly scoped layout fix requires build 8; this exceeds the normal 7-build ceiling for a concrete output-layout defect. No clean build or unrelated targets.

- Final input audit identified that malformed serialized Path messages were diagnosed but did not retain a P identity. Added an unavailable snapshot and a corrupt-MCAP regression; build 9 is required for this concrete data-identity invariant.

- Final verification: 36/36 gtest cases passed (39 with CTest wrappers), plus 6/6 CLI boundary fixtures.
- Tasks 1–7: complete. Installed CLI inspect/analyze/render, core / ROS IO / report libraries, offline viewer, automatic 60-figure real report, saved-analysis re-render and usage/metric documentation delivered.
- Final review fixes verified by focused regressions and full green suite. No review findings deferred. No second reviewer used.
- Real report: reports/pid_test_1022_155828/report.html. UTC re-render retained identical frozen clock and path/metric results.
- Final scope: 39 new package files (including fixed Plotly resource); no existing package modified; C++ files remain below 200 lines. No commits, branches, worktrees or clean builds.
