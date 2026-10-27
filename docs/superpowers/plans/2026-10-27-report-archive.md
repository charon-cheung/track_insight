# 多轮测试报告归档 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans in the current session. 用户禁止提交、分支、worktree；不执行技能中的 Git 写入步骤。

**Goal:** 累计每轮报告并提供离线测试总览入口。
**Architecture:** 独立归档模块组合现有 renderReport；静态主页内嵌轻量索引；单轮报告使用相对返回链接。
**Tech Stack:** C++17、std::filesystem、nlohmann JSON、HTML/JavaScript、现有 ROS 2 Jazzy 工具链。
**Spec:** docs/superpowers/specs/2026-10-27-report-archive-design.md

## Global Constraints
- 只修改 track_insight；保留 .gitignore 等已有用户工作，不提交、不分支、不 worktree、不改 README。
- --output 兼容，归档不改统计口径、时间语义或速度曲线。
- 增量构建复用工作区 build/install，MAKEFLAGS=-j12；先检查导航运行状态。

## Review Focus
- 重复导入和替换只影响本轮，未指定名称备注时保留它们。
- 同秒不同 bag、空指标和 failed/replaced 均正确处理。
- 损坏索引、符号链接、非归档目录不能造成覆盖或路径越界。
- 生成失败保持旧首页和旧报告；并发生成不能交叉更新索引。
- file:// 可选择和返回；特殊字符不会注入 HTML；窄屏可用。

### Task 1: 归档模块与行为测试
**Files:** include/track_insight/archive.hpp, src/report/archive.cpp, test/test_archive.cpp, CMakeLists.txt
**Interfaces:** renderArchive(Json, path root, path resources, bool replace, optional<string> name, optional<string> note, progress) -> ArchiveResult{code, report, home}。
- [x] 添加真实临时目录行为测试：追加、去重、替换保留、失败保留、所有权、缺失统计与文本转义。
- [x] 实现独立归档模块，复用 makeOverview 和 renderReport。
- [x] 单轮目录及索引采用暂存和回滚；读写都验证目录身份。

### Task 2: CLI 和总览界面
**Files:** src/main.cpp, include/track_insight/report.hpp, src/report/report.cpp, resources/viewer/index.html, resources/archive/index.html
**Interfaces:** CLI --archive 与 --output 互斥；renderArchive 返回实际报告和首页路径。
- [x] 接入 analyze/render，名称备注可选；旧命令输出兼容。
- [x] 首页列表、轮次下拉框、均值/覆盖率/最差 case/备注、相对导航、无数据状态。
- [x] 归档报告增加返回入口，独立报告无归档导航。

### Task 3: 验证与交付
**Files:** test/check_archive.py（CLI + 浏览器验收）
- [x] 受影响包增量构建并运行 archive/overview/pipeline 相关测试。
- [x] 真实 analysis.json 导入，验证双轮选择、返回、离线与报告原有功能。
- [x] 对不同输入和非法参数执行 CLI 边界检查；旧 --output 仍可运行。
- [x] 完整审阅 diff、文件增长、独立最终审查；提供以后累计报告的命令。

## 执行记录
- 初始工作树仅有未跟踪 .gitignore，保持不动。
- 无导航进程运行；sandbox 启动错误，通过获批本地执行完成工具调用。
- 预检：Task 2 使用 Task 1 的 ArchiveResult 路径，不推断输出子目录；Task 3 使用相同 CLI 和 file:// 链接。接口无冲突。
- 用户本轮已明确要求实施，沿用已确认方案，不重新请求设计或执行确认。

## 最终验证记录
- 归档接口 Red：旧 CLI 拒绝 --archive；已在当前新 CLI 跑通。
- 独立审查 archive_review 完成；目录末尾 /、索引哈希与 manifest 不一致两个重要问题已在临时目录复现并修正，加入 TrailingSlashAndDotReferToTheSameArchive / SwappedIdentityAndMalformedSummaryNeverReplaceAnotherRound。
- 特殊名称中的模板字面量最后按数据插入；浏览器验证 __TRACK_DATA__、__TRACK_SCRIPT__ 和 HTML 片段均原样显示，无脚本执行。
- 归档事务的追加/替换、并发锁、失败回滚、所有权保护、空指标、特殊字符：test_archive 7/7。
- test_pipeline 9/9、test_overview 3/3、test_input 6/6 通过。
- 完整 CTest 53 个测试中 50 通过，3 项既有失败（test_core）。失败二进制与 core 静态库时间分别为 2026-10-26 15:19:51 / 15:19:49，本轮 src/core 和 test/test_core.cpp 无差异：
  - MotionWindow.RemovesIdleEdgesFromMetricsButKeepsIntermediateStop
  - MotionWindow.PreservesCommandedStallAndSlowTerminalAdjustment
  - MotionWindow.IncludesAngularMotionAndActualFeedbackOverrun
  这三项预期裁剪停车区间，现有实现返回完整区间；本轮不修改统计行为，也未将其计作归档测试通过。
- check_archive.py 真实 analysis 验证通过：双轮追加与切换、仅替换本轮、名称备注保留、互斥参数拒绝、旧 --output 兼容、指标 JSON 相等、离线打开返回、1366/1920/600 宽度。
- 真实 10-27 bag analyze --archive（带尾斜杠）成功：9 paths / 9 tasks，100% 进度；测试产物在 .work。
- 既有 check_viewer.py 在归档的 10-26 报告通过，0 浏览器错误：播放/步进/星标/删除项/缩放联动/显示选项/最大化/模式切换保持状态。
- 10-23、10-26、10-27 三份原 analysis 已导入 reports/test_archive，共 37 case。原报告不变；实际主页已截图检查。
- 共 4 次增量 build 调用（其中 1 次因编辑路径写错未产生修改，构建为空操作）；没有清理或新建 build/install。
- 现有文件改动 50 行新增 / 17 行删除；新增生产代码 archive.cpp 162 行、archive.hpp 12 行、主页 53 行，测试 229 行。无超过 1500 行文件增长。
- 无新增依赖，无 Git 提交/分支/worktree；已有未跟踪 .gitignore 未修改。

## 使用
新测试：`ros2 run track_insight track_insight analyze --bag BAG_DIR --log NAV2_LOG --archive /home/CNF2026987648/s100_docker/src/track_insight/reports/test_archive --name "轮次名称" --note "备注"`。
已有报告：`ros2 run track_insight track_insight render --analysis OLD_REPORT/analysis.json --archive ARCHIVE_DIR`。
同 bag 重生成：追加 `--replace`，仅更新匹配轮次；未传 --name/--note 时保留名称备注。
名称和备注随命令持久保存；归档可整体移动，内部相对链接仍有效。
普通异常会回滚；断电或强制结束进程可能留下锁/暂存目录，此时工具拒绝继续覆盖，需要先检查保留的事务目录。
