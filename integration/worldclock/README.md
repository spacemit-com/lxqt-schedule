# worldclock 日程联动

`lxqt-schedule` 提供 D-Bus 服务后，`worldclock` 面板插件的日历弹层即可展示
日程：在含日程的日期下方画圆点，并随日程增删（`SchedulesChanged` 信号）自动刷新。

## 打补丁

在 `plugin-worldclock` 目录执行（补丁基于当前源码生成）：

```bash
cd /path/to/lxqt-panel/plugin-worldclock
patch -p1 < /path/to/lxqt-schedule/integration/worldclock/worldclock-schedule-integration.patch
```

补丁做了三件事：

1. `lxqtworldclock.h`
   - `CustomCalendarWidget` 增加 `setScheduleDates(const QSet<QDate>&)` 与
     `m_scheduleDates` 成员
   - `LXQtWorldClock` 增加私有槽 `refreshScheduleDates()`
2. `lxqtworldclock.cpp` 构造时订阅 `org.lxqt.Schedule` 的 `SchedulesChanged` 信号
3. 日历弹层打开时调用一次 `refreshScheduleDates()`；该函数通过 D-Bus 读取
   `ListSchedules`（JSON），把含日程的日期传给日历，在日期格下方画圆点

## 依赖

`worldclock` 已经链接 Qt6DBus（通过面板插件基类），无需改 CMakeLists。
若提示缺少 `QDBusInterface` / `QJsonDocument` 头，确认 CMake 里
`find_package(Qt6 COMPONENTS ... DBus ...)` 已启用。

## 验证

1. 启动 `lxqt-schedule` 守护进程
2. `lxqt-schedule-ctl add "开会" "$(date -d 'tomorrow 15:00' +%s)"`
3. 点击面板时钟打开日历弹层，`开会` 对应日期下方应出现圆点
4. 删除该日程后圆点消失
