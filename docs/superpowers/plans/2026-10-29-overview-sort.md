# Overview Accuracy Sorting Implementation Plan

> Execution: superpowers:executing-plans, inline; user requested implementation. No commits, branches or worktrees.

**Goal:** Sort the report overview case table by lateral RMSE, P95 or maximum error.
**Architecture:** overview.js owns table ordering and accessible sort-header buttons; overview.css owns their appearance. Reorder existing rows without modifying analysis, statistics, case identities or navigation callbacks.
**Spec:** User request in this conversation. First click ascending; repeated click toggles descending. New column starts ascending. Original order before any click; finite raw numbers sort numerically; missing/invalid values stay last in both directions; equal values retain original case order. Show active direction and aria-sort. Preserve current sort when revisiting the overview in the same page. CSV and summary calculations retain existing behavior.
**Tech stack:** Existing JavaScript/DOM, CSS and Chrome browser acceptance checks; no dependencies or C++ changes.

## Review focus
- Numeric comparisons must use raw values rather than rounded cell text.
- Zero is valid; missing values and NaN/Infinity belong last, also in descending order.
- Sorting must retain clickable case links and leave best/worst/average sections unchanged.
- Only the three requested columns sort; controls must be keyboard accessible.
- Empty tables keep their no-cases row when sorting.

### Task 1
- [x] Add a focused browser regression for ordering, ties, missing values, indicators, unchanged data and navigation; observe original failure.
- [x] Implement local sorting controls in overview.js and overview.css.
- [x] Run the regression and check the actual current report after rendering.
- [x] Review task-only diff and obtain one independent review; preserve pre-existing .gitignore.

Pre-flight: one cohesive frontend task. Current tracked working tree is clean; .gitignore is pre-existing untracked user work. Installed resources are source symlinks; no C++ build is needed.

Verification: pre-change Chrome regression failed because the three sort headers were absent. Post-change regression passed numeric ascending/descending, equal raw values, different raw values with the same displayed rounded text, zero, null/missing/NaN/Infinity, arrows/aria-sort, unchanged analysis/summary, clickable cases, retained ordering on return and empty-table placeholder.

Independent read-only review found no defect. Its only test-coverage observation (same rounded display with different raw values) was added and passed. CSV ordering intentionally retains the existing behavior.

Actual 10-28-7 report was rendered from its saved analysis. Mouse sorting was checked against all 18 original case metrics in both directions for all three columns. Native Space/Enter, case navigation and retained sort on return were verified. The initial CDP Enter check omitted the carriage-return text required to produce the native character event; supplying a complete key event resolved the test-harness failure with no production changes. No JavaScript exceptions occurred. Analysis SHA256 and all nine other report HTML hashes are unchanged.

Scope: overview.js +25 lines, overview.css +3 lines, focused browser regression +74 lines, and this plan. No oversized files, dependency changes, C++ changes, builds or unrelated refactoring. git diff --check passed; pre-existing untracked .gitignore preserved.
