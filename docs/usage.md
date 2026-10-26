# Track Insight v1 使用说明

这是 Ubuntu 24.04 / ROS 2 Jazzy 上的离线 C++ 工具。无需运行 Nav2、ROS Node、bag playback、RViz、PlotJuggler 或 Web 服务。

## 构建

依赖：Jazzy 的 rosbag2_cpp、MCAP、tf2、标准消息包，以及 libarchive-dev、librsvg2-bin、nlohmann-json-dev、libyaml-cpp-dev、OpenSSL 开发库。

在工作区根目录运行；按项目约定，构建前先确认 RBS 导航未运行。

~~~bash
cd /home/CNF2026987648/s100_docker
source /opt/ros/jazzy/setup.bash
MAKEFLAGS="-j12" colcon build --packages-select track_insight --cmake-args -DBUILD_TESTING=ON
source install/setup.bash
~~~

## 分析实车数据

~~~bash
ros2 run track_insight track_insight inspect \
  --bag /home/CNF2026987648/test/pid_bags/10-22-1/pid_test_1022_155828.zip

ros2 run track_insight track_insight analyze \
  --bag /home/CNF2026987648/test/pid_bags/10-22-1/pid_test_1022_155828.zip \
  --output /home/CNF2026987648/s100_docker/src/track_insight/reports/pid_test_1022_155828
~~~

输入也接受已经解压的 bag 目录或单个 .mcap。若有同轮日志，增加 --log /path/to/nav2.log。该实车目录已确认没有完全对应的日志，不应传入其他轮次的日志冒充关联证据。

自定义话题、参考系、有效时间间隔、运动阈值及评价阈值，使用 --config /path/to/config.yaml。默认模板为 config/default.yaml。未配置误差阈值时不做合格/不合格判断。定位跳变和持续低反馈属于按明确配置识别的观测现象，不代表已经确定故障原因。

输出目录默认不能覆盖。确需替换由本工具生成的报告时增加 --replace；仍会检查目录归属、符号链接及用户额外加入的文件。不要在报告目录里存放其他文件。

## 输出与查看

- report.html：双击或用本地浏览器打开。内嵌固定 Plotly.js、数据、样式与脚本，断网可用，可单独复制到其他电脑。
- analysis.json：完整版本化分析存档，包含配置、来源时间戳、原始选定信号、日志摘录、路径、指标和展示时间规则。
- metrics.json / metrics.csv：指标、有效时长、候选时长、覆盖率、质量与不可用原因。
- manifest.json：生成状态、证据质量、文件清单、输入校验值、配置及前端依赖版本。
- figures/：每个可评价路径窗口的轨迹、速度和误差 SVG/PNG。PNG 使用 rsvg-convert 自动生成。

界面可选择路径或导航任务、叠加规划版本、播放或拖动时间游标、联动速度/误差范围、点击轨迹定位时间、跳转事件及查看原始日志。重复 XY 位置会提供多个时间候选。正式统计由 C++ 完成；网页缩放和展示降采样不会重算指标。

没有日志或导航 action 证据时显示 P1、P2 等发布窗口，任务数可以为零。这不代表没有导航，也不代表路径已被控制器采用。报告区分确认身份、开始边界、结束边界，只有三者均确认才进入 confirmed 汇总。

## 从保存的分析重新生成报告

~~~bash
ros2 run track_insight track_insight render \
  --analysis /path/to/analysis.json \
  --output /path/to/new_report
~~~

render 不读取原 bag 或 nav2.log，也不重算指标。时间显示继续使用原分析保存的本地时间规则，不受查看电脑或重新渲染时区影响。

退出码：0 表示生成完成；2 表示图片转换等输出步骤部分失败；1 表示输入、配置或生成过程致命失败。证据质量另列于 manifest.json，不以生成成功代表控制效果合格。

## 已知输入限制

- map 轨迹是记录的定位估计，不是独立测量真值。
- 缺少原始目标 yaw 时姿态误差不可用；不会用路径切线代替车身目标朝向。
- 仅有发布时间时，执行区间为推定；路径终点距离不等于任务成功后的终点误差。
- 缺少导航目标、结果或停车后数据时，对应终点/稳定指标为 null，并说明原因。
- 不支持把未知或非平面的速度参考系转换假装成已知平面速度。
- TF header 时钟回退会使受影响变换链不可评价；不会拼接两个时间段。
