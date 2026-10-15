# lxqt-schedule 架构

## 组成

```
lxqt-schedule        Qt6 Core+DBus 守护进程
  ├─ org.lxqt.Schedule    D-Bus 服务（/org/lxqt/Schedule）
  ├─ 持久化 ~/.config/lxqt/schedule.json
  └─ QTimer 到期触发 → org.freedesktop.Notifications 弹通知

lxqt-schedule-ctl   CLI：add / remove / list / clear / ping

bibit-intent skill  skills/schedule/{skill.yaml,handlers.py}
  └─ 自然语言 → parse_when → lxqt-schedule-ctl

worldclock 补丁     日历上标记含日程的日期（圆点）
```

## D-Bus 契约

| 方法 | 入参 | 出参 |
|------|------|------|
| `AddSchedule` | title(s), due_epoch(x), repeat(s) | id(x) |
| `RemoveSchedule` | id(x) | ok(b) |
| `RemoveAll` | — | — |
| `ListSchedules` | — | json(s) |

信号：`SchedulesChanged()`、`ScheduleTriggered(id, title)`。

`due_epoch` 为 Unix 秒（UTC）。`repeat` 取值 `""`（一次）、`daily`、`weekly`、
`monthly`、`yearly`。

## 到期流程

1. `ScheduleDaemon::checkDue()` 找出 `due_epoch <= now` 的条目
2. 发 `ScheduleTriggered` 信号 + `org.freedesktop.Notifications.Notify`
3. 一次性日程删除；重复日程按规则推进到下一次到期时间
4. 落盘 + 发 `SchedulesChanged`

## 联动关系

- `lxqt-notificationd`：实现标准 freedesktop 通知服务，日程到期自动弹窗，无需改它
- `bibit-intent`：通过新增 Skill 调 `lxqt-schedule-ctl` 创建/删除/查看
- `worldclock`：订阅 `SchedulesChanged`，日历上画日程圆点
