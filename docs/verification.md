# 验收记录

环境：Ubuntu 24.04 / ROS 2 Jazzy。源码修改范围仅为 track_insight。没有修改其他 package，没有提交、切换分支、创建 worktree 或清理既有工作区。

## 验证范围

- C++ 核心：纳秒解析、冻结本地时间、持续时间加权统计、P95、覆盖率、缺口、区间边界。
- 几何：恒定偏差、越过终点、交叉分支、倒行路径、固定车身朝向横移、纯旋转。
- 关联：未结束任务、重复发布、独立身份/首指令证据、会话/UUID/epoch 隔离、action 与日志去重。
- 速度：因果保持及过期、坐标参考点、插值缺口、重复 header 的交付顺序、采样收敛。
- TF/输入：ZIP 穿越与容量限制、临时文件归属、静态参考点、长 TF 缺口、定位跳变、TF header 回退。
- 输出：合成 MCAP 与 ZIP 等价、已知精确指标、无源文件重渲染、既有目录保护、显式替换、PNG 失败不沿用旧图、生成质量与证据质量分离。
- 浏览器：独立 Chrome 配置，file://，禁用网络，切换查看时区，路径选择、播放、游标和时间范围联动、无脚本异常。
- 配置：显式阈值读取和持续时间溢出保护。

测试命令：

~~~bash
cd /home/CNF2026987648/s100_docker
source /opt/ros/jazzy/setup.bash
source install/setup.bash
ROS_LOG_DIR=/home/CNF2026987648/s100_docker/build/track_insight/test_ros_logs \
  colcon test --packages-select track_insight --event-handlers console_direct+
colcon test-result --test-result-base build/track_insight --verbose
~~~

详细执行状态及构建计数见 progress.md。测试输出在 build/track_insight/test_run.log；不通过仅编译成功宣称算法正确。

## 实车输入

/home/CNF2026987648/test/pid_bags/10-22-1/pid_test_1022_155828.zip

用户确认无完全对应的 nav2.log。不得替换为其他轮次日志。

读取到 10 条 /plan、2,539 条控制器指令、25,698 条里程计反馈、17,979 个动态 TF 位姿时间点。报告中 N=0、P=10，均为推定发布窗口；不会给出未经证实的成功结果或控制器目标姿态。

自动导出 10 × 3 组图，每组 SVG 与 PNG，共 60 个图片文件。其余产物为独立 report.html、analysis.json、metrics.json、metrics.csv、manifest.json。

## 独立代码审查

一次独立只读审查覆盖时间重置、任务会话、命令因果性、阶段缺口分母、输出替换及重渲染。发现项已在单批修正中加入回归覆盖。

TF header 回退的处理采用排除受影响变换链，代价是降低数据覆盖率；不推断无法验证的时钟映射。页面分别提供 confirmed 与 inferred 导航汇总，生成成功与证据质量分开记录。

实际匹配日志已经缺失，因此真实数据的确认关联不能验收。确认路径和执行边界通过具有明确共享身份与指令证据的合成 MCAP/日志测试验证，不用它替代真实数据证明。

## 最终结果

- 36 个 C++ 行为测试通过；colcon 计入 3 个 CTest 包装项后显示 39 项、0 失败。
- check_cli_boundaries.py 的 6 个输入边界案例通过。
- check_viewer.py 的断网、时区、播放及联动检查通过，0 JavaScript 异常。
- UTC 下 render 后，冻结时间规则、路径结果及指标与原分析一致。
- 共 9 次受影响包增量构建；额外两次分别用于静态图文字碰撞和损坏 Path 身份保留修正。
