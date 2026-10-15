# bibit-intent 日程 Skill 集成

`skills/schedule/` 是给 `bibit-intent` 用的日程 Skill，通过 `lxqt-schedule-ctl`
调用 `org.lxqt.Schedule` D-Bus 服务，实现自然语言创建/删除/查看日程。

## 安装

把 `skills/schedule/` 整个目录放入 `bibit-intent` 的 skills 目录：

```bash
BIBIT=/path/to/bibit-intent
cp -r integration/bibit-intent/skills/schedule \
      "$BIBIT/src/bibit_intent/resources/skills/schedule"
```

`bibit-intent` 的 `skill_loader.py` 会在启动时扫描 `skills/`，自动注册
`schedule_add` / `schedule_remove` / `schedule_list` 三个工具。

前提：目标设备已安装 `lxqt-schedule`（`lxqt-schedule-ctl` 在 `PATH` 中）。
`skill.yaml` 的 `requires: [lxqt-schedule-ctl]` 会在加载时做存在性检查。

## 已支持的意图

- 创建：「提醒我明天下午三点开会」「十分钟后提醒我喝水」「每周五早上八点提醒我晨会」
- 删除：「删除日程」「取消提醒」「删除标题为开会的日程」「删除提醒 12」
- 查看：「查看日程」「有哪些提醒」

## 时间解析范围

`handlers.py::parse_when` 支持：

- 相对时间：`N分钟/小时/天/周/月/年后`、`半小时后`、`一刻钟后`、`一个半小时后`
- 绝对时间：`今天/明天/后天/大后天 + 时间`、`X月X日[号] + 时间`、`周X/星期X + 时间`
- 时钟：`HH:MM`、`早上/上午/中午/下午/晚上/夜里/凌晨 X点[半|一刻|三刻|Y分]`
- 裸时间（如「3点」）默认当天，已过则顺延到次日
- ISO 时间 `YYYY-MM-DD[ HH:MM[:SS]]` 与纯 Unix 时间戳

超出范围的表达会回退给端侧模型翻译成 `when` 参数，或提示用户换一种说法。

## 自检

不需要 D-Bus 服务也能跑时间解析自检：

```bash
python3 handlers.py
```
