# Track Insight

离线分析机器人路径跟踪数据，生成可直接打开的网页报告。支持 MCAP bag 目录、单个 `.mcap` 或包含 bag 的 ZIP；无需启动导航或回放 bag。

## 1. 准备环境

使用 Ubuntu 24.04、ROS 2 Jazzy。以下路径均为示例，请替换为自己的路径。

编译前停止正在运行的 RBS 导航。包目录若有 `COLCON_IGNORE`，需先解除忽略才能编译。依赖包括 rosbag2/MCAP、tf2、libarchive、nlohmann-json、yaml-cpp、OpenSSL；导出 PNG 还需 `librsvg2-bin`（提供 `rsvg-convert`）。

## 2. 生成与更新报告

**新增一轮测试到归档：**

```bash
ros2 run track_insight track_insight analyze \
  --bag /path/to/pid_test_bag \
  --log /path/to/nav2.log \
  --archive /path/to/pid_reports \
  --name "11-3-3"
```

- `--log` 必须对应这一轮；没有日志时可省略。
- 提示 `round already exists` 时，在原命令末尾加 `--replace`，只替换同一 bag 对应的轮次；更改名称不会新建同一 bag 的副本。
- 只生成独立报告：将 `--archive ... --name ...` 换为 `--output /path/to/report`。`--archive` 和 `--output` 不能同时使用。
- 自定义话题、坐标系或分析阈值：增加 `--config /path/to/config.yaml`，参考 [默认配置](config/default.yaml)。

**只更新已有报告的网页和图片：**

```bash
ros2 run track_insight track_insight render \
  --analysis /path/to/pid_reports/runs/run_xxx/analysis.json \
  --archive /path/to/pid_reports \
  --replace
```

| 需要做什么                             | 使用方式                         |
| -------------------------------------- | -------------------------------- |
| 更新页面布局、文字或图表展示           | `render`，不需要原 bag/log       |
| 修正时间区间、时钟处理或指标算法后重算 | `analyze`，需要原 bag 和对应日志 |
| 更新已有同轮报告                       | 在对应命令末尾加 `--replace`     |

独立报告重渲染时，用 `--output /path/to/report` 替换 `--archive ...`。归档内的轮次应通过 `--archive` 更新，以保持测试列表同步。

## 3. 查看与操作

命令结束后会打印网页路径，直接用浏览器打开，无需 Web 服务。

| 文件                           | 用途                                       |
| ------------------------------ | ------------------------------------------ |
| 归档根目录的 `index.html`      | 所有测试列表与跨轮比较                     |
| 每轮的 `report.html`           | 报告模式看指标；综合模式看轨迹、速度和误差 |
| `analysis.json`                | 保存分析结果，供以后重新渲染               |
| `metrics.json` / `metrics.csv` | 导出指标数据                               |
| `figures/`                     | 静态 SVG/PNG 图片                          |

表头可按横向 RMSE、P95、最大横向误差排序。点击曲线可固定时刻，再点另一位置可换时刻；点击“已固定”或按 Esc 解除。红星表示好 case，黑星表示坏 case；黑星 case 的问题备注输入后自动保存。

## 4. 注意事项

- **保留原始数据。** `report.html` 可单独复制、离线查看；`analysis.json` 可用于重渲染。但重新计算正式指标仍需要原 bag/log。
- **参数只取测试日志。** 必须有可匹配的完整 PID 运行参数快照，否则显示“测试时参数未知”；不再用 `--pid-config` 读取本机配置代替。
- **删除是浏览器内的复盘操作。** 会更新单轮报告的最好/最差和均值，不改原始数据或归档首页统计。星标、备注和删除记录也不会自动同步到其他浏览器或设备。
- **部分 case 会被过滤。** 规划路径相对首尾线段的最大偏离不超过 2 cm 时，两种模式均排除；报告总览还排除 failed/replaced case。
- **不可用不等于零。** 定位轨迹不是独立真值；缺少可信目标朝向时，姿态误差不可用。推定区间也不等于已确认执行区间。
- **输出目录只放工具产物。** 不要手动塞入其他文件，以免替换时归属检查失败。当前不支持 sqlite3（`.db3`）bag。
- **生成完成不代表精度合格。** 退出码 `0` 表示生成完成，`2` 表示部分产物失败，`1` 表示致命错误；详细原因见终端和 `manifest.json`。
