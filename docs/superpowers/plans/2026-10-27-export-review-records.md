# Export consistency and review records implementation plan

> Execute locally in the current session; no commits, branches, worktrees or unrelated package changes.

Goal: implement the user's selected audit items 6 and 8 only.
Architecture: share plot presentation data between C++ SVG/PNG generation and the embedded Plotly viewer. Keep browser review records in a separate module, preserving existing storage keys. Formal analysis and metrics remain unchanged.

- [x] Add image regression coverage for colors, star/circle endpoints, direction colors, speed defaults and missing evidence.
- [x] Add resources/plot_style.json, embed it in HTML, and consume it in viewer.js and figures.cpp. Use current webpage labels/colors; raw commands remain available but hidden by default in static exports too.
- [x] Add review_state.js for favorites, hidden items, JSON export/import and recovery. Reuse existing same-run localStorage keys. Import is additive, validates schema/run/case IDs before mutation, and reports errors without altering current records.
- [x] Add a compact review-records dialog: hidden list, per-item/all restoration, import/export, storage feedback. Report-mode contents and metric inputs remain unchanged.
- [x] Use incremental builds (at most five), report/archive tests and browser checks. The three known MotionWindow failures are outside this task.
- [x] Regenerate the two existing pid_reports rounds using saved analysis and --archive --replace. Verify metrics unchanged and unrelated rounds preserved.

Review focus: existing saved records survive; all-hidden lists recover; foreign/malformed records do not mutate data; storage-unavailable sessions still work; exports use consistent visual semantics. No manual-intervention annotations or report-version feature.

Verification: incremental build passed; 10 pipeline/report tests and 7 archive tests passed. Offline browser acceptance passed, including saved-record compatibility, restore-all, additive import, invalid/foreign record rejection and unavailable-storage behavior. Both archived reports were rendered in place; analysis (excluding generation metadata), metrics.json and archive catalog hashes/data remain unchanged.
