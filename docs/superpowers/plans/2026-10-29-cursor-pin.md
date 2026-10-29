# Cursor Pin Implementation Plan

> Execution: superpowers:executing-plans, inline. User approved the interaction and direct implementation. No commits, branches or worktrees.

**Goal:** Click a time plot to pin the shared cursor; click again to change the pinned time; Escape or a visible button restores hover following.
**Architecture:** Keep pin state alongside the existing shared cursor in viewer.js. Use primary pointer clicks on time-plot drag areas, rejecting drags; keep existing Plotly zoom and trajectory selection behavior. Add release controls in the playback bar and maximized velocity header.
**Tech Stack:** Existing JavaScript/Plotly, self-contained HTML, Python Chrome CDP acceptance check.
**Spec:** User-approved interaction in this conversation: click pins and pauses; another click repins; Escape/button releases; steps/timeline preserve pin; play/case switch release; drags still zoom. In maximum view, Escape releases a pin before closing the dialog on a subsequent Escape.

## Constraints / Review Focus
- Preserve pre-existing interval fixes and tooltip changes; no metric, signal-time or controller changes.
- A click in empty plot space must work on each of the three velocity/error subplots.
- Pointer drags (including moving away then back), right clicks, legends and toolbar controls must not pin.
- Hover must not move either the shared cursor or Plotly's temporary spike while pinned.
- Playback must stop on pin; stepping/seeking must pause playback and retain pin state.
- Maximize, resize and mode switch preserve the pin; selecting another case clears it.
- Update the current round from analysis.json, preserving analysis and other rounds.

### Task 1: Implement and verify cursor pinning
Files: resources/viewer/viewer.js, resources/viewer/index.html, resources/viewer/viewer.css; test/check_cursor_pin.py.
- [x] Add a focused browser acceptance check using real mouse/key events; confirm pinning fails before implementation.
- [x] Add pin state, pause/release operations and primary-pointer click/drag discrimination to existing cursor owner.
- [x] Add visible release controls; intercept Escape only while pinned, without closing a maximized dialog.
- [x] Verify pins/repins, hover freeze, steps/seek/play, case change, zoom and maximum view using the browser check.
- [x] Render the current archived round, verify unchanged data and final diff; get one independent code review.

Pre-flight: one cohesive task, no cross-task interfaces. No C++ build needed: installed resources are source symlinks. Browser verification exercises the retained UI behavior directly.

Verification: original report fails the real-pointer frozen-state assertion as intended. Initial implementation passes six subplot clicks, synchronized freeze, steps/seeking/play, zoom, mode/maximize/Escape/case reset and has no JavaScript exceptions. Expanded checks also pass for right click, drag-away-and-back, toolbar clicks and hover-spike suppression.

Independent review found one issue: held Escape releases the pin and then closes the dialog via key repeat. Added a real autoRepeat regression; confirmed RED at the modal-open assertion, then changed Escape handling to consume that press until keyup (reset on window blur). Final verification pending. No other concrete issue. OS focus-loss pointer sequences and touch/browser compatibility beyond Chrome were not reproduced/assessed; no speculative changes added.

Final verification: the held-Escape regression passes, including a subsequent separate Escape closing the maximized view. The full focused real-browser check passed on the updated 10-28-7 archive report, including prior three-space absolute timestamp tooltip formatting after release. One earlier successful behavior run encountered Chrome temporary-profile cleanup racing after process termination; the harness now requests Browser.close and treats only disposable-profile cleanup errors as non-behavioral. No production change was needed for that environment issue.

Current analysis.json SHA256 and all eight other report HTML hashes match their pre-task values. git diff --check passed. This task changes three frontend resources (+53/-7 lines relative to pre-task snapshots), adds the focused browser check and this plan. viewer.js is 441 lines; no oversized file growth. No C++ build or full C++ test rerun was needed for browser-only changes. Existing uncommitted parser, correlation, C++ tests, tooltip changes and .gitignore were preserved.

Final review ruling: the concrete held-Escape defect was fixed and verified RED to GREEN. No speculative pointer behavior across OS focus loss or untested touch/browser support is claimed; the supported verification target here is desktop Chrome with mouse/keyboard. No deferred concrete defect remains.
