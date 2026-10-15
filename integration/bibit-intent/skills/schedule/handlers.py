"""
skills/schedule/handlers.py — 日程管理 Skill 的执行实现

通过 `lxqt-schedule-ctl` 调用 org.lxqt.Schedule D-Bus 服务。自然语言时间
（"明天下午三点""十分钟后""每周五早上八点"）在这里解析为 Unix 时间戳，
解析逻辑不占端侧模型 prompt。

对照 design.md §12 安全：
  - 参数化 subprocess（列表形式），绝不 shell 字符串拼接
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Literal

from bibit_intent.execution.cancellation import run_command

_CTL = "lxqt-schedule-ctl"

_REPEAT_RULES = frozenset({"daily", "weekly", "monthly", "yearly"})

_CN_DIGITS = {
    "零": 0, "〇": 0, "一": 1, "二": 2, "两": 2, "三": 3, "四": 4,
    "五": 5, "六": 6, "七": 7, "八": 8, "九": 9,
}

_WEEKDAY = {
    "一": 0, "二": 1, "三": 2, "四": 3, "五": 4, "六": 5, "日": 6, "天": 6,
}


@dataclass
class ExecResult:
    ok: bool
    message: str
    detail: str = ""
    detail_format: Literal["text", "markdown"] = "markdown"


def _run(cmd: list[str], timeout: int = 10) -> ExecResult:
    try:
        p = run_command(cmd, timeout=timeout)
        if p.returncode == 0:
            return ExecResult(True, "ok", p.stdout.strip(), detail_format="text")
        return ExecResult(False, f"命令失败(码{p.returncode})", p.stderr.strip(), detail_format="text")
    except FileNotFoundError:
        return ExecResult(False, f"命令不存在：{cmd[0]}（请确认已安装 lxqt-schedule）")
    except Exception as e:  # noqa: BLE001 - 兜底，含超时/取消
        return ExecResult(False, f"命令执行失败：{e}")


# ── 中文数字/时间解析 ────────────────────────────────────────────────

def _cn_to_int(s: str) -> int | None:
    s = s.strip()
    if not s:
        return None
    if s.isdigit():
        return int(s)
    if "十" in s:
        left, _, right = s.partition("十")
        tens = _CN_DIGITS.get(left, 1) if left else 1
        total = tens * 10
        if right:
            ones = _cn_to_int(right)
            if ones is None:
                return None
            total += ones
        return total
    return _CN_DIGITS.get(s)


def _extract_clock(text: str) -> tuple[int, int] | None:
    """Return (hour, minute) in 24h from a natural-language time phrase."""
    # 24h "HH:MM" / "HH：MM"
    m = re.search(r"(?<!\d)(\d{1,2})[:：](\d{1,2})(?!\d)", text)
    if m:
        hour, minute = int(m.group(1)), int(m.group(2))
        if 0 <= hour <= 23 and 0 <= minute <= 59:
            return hour, minute

    # "早上/上午/中午/下午/傍晚/晚上/夜里/凌晨 + X点[半|一刻|三刻|Y分]"
    m = re.search(
        r"(早上|上午|中午|下午|傍晚|晚上|夜里|夜间|凌晨)?\s*"
        r"([零〇一二两三四五六七八九十\d]{1,4})\s*点\s*"
        r"(半|一刻|三刻|一刻钟|([零〇一二两三四五六七八九十\d]{1,2})\s*分?)?",
        text,
    )
    if not m:
        return None

    meridiem = m.group(1) or ""
    hour = _cn_to_int(m.group(2))
    if hour is None or hour > 23:
        return None

    minute_word = m.group(3) or ""
    minute = 0
    if minute_word in ("半",):
        minute = 30
    elif minute_word in ("一刻", "一刻钟"):
        minute = 15
    elif minute_word == "三刻":
        minute = 45
    elif m.group(4):
        minute = _cn_to_int(m.group(4))
        if minute is None or minute >= 60:
            return None

    if meridiem in ("下午", "傍晚", "晚上", "夜里", "夜间"):
        if hour < 12:
            hour += 12
    elif meridiem == "中午":
        if hour < 11:
            hour += 12
    elif meridiem == "凌晨":
        if hour == 12:
            hour = 0
    # 早上/上午 keep as-is (already AM).

    return hour, minute


def _extract_date(text: str, now: datetime) -> datetime | None:
    """Return a date (midnight, naive local) for an absolute date phrase."""
    today = now.date()

    if "大后天" in text:
        return datetime.combine(today + timedelta(days=3), datetime.min.time())
    if "后天" in text:
        return datetime.combine(today + timedelta(days=2), datetime.min.time())
    if "明天" in text or "明日" in text:
        return datetime.combine(today + timedelta(days=1), datetime.min.time())
    if "今天" in text or "今日" in text:
        return datetime.combine(today, datetime.min.time())

    # "X月X日/号"
    m = re.search(r"(\d{1,2})\s*月\s*(\d{1,2})\s*[日号]", text)
    if m:
        month, day = int(m.group(1)), int(m.group(2))
        if 1 <= month <= 12 and 1 <= day <= 31:
            return datetime(now.year, month, day)
        return None

    # "X日/号"（当月）
    m = re.search(r"(?<!\d)(\d{1,2})\s*[日号](?!\d)", text)
    if m:
        day = int(m.group(1))
        if 1 <= day <= 31:
            return datetime(now.year, now.month, day)
        return None

    # "周X / 星期X / 礼拜X"，可选"下"前缀
    m = re.search(r"(下)?(?:周|星期|礼拜)([一二三四五六日天])", text)
    if m:
        next_week = bool(m.group(1))
        target = _WEEKDAY[m.group(2)]
        days_ahead = (target - today.weekday()) % 7
        if days_ahead == 0:
            days_ahead = 7
        if next_week:
            days_ahead += 7
        return datetime.combine(today + timedelta(days=days_ahead), datetime.min.time())

    return None


def parse_when(text: str, now: datetime | None = None) -> int | None:
    """把自然语言时间解析为 Unix 时间戳（秒）；解析失败返回 None。"""
    now = now or datetime.now()
    text = (text or "").strip()
    if not text:
        return None

    # 纯数字 → 直接当作时间戳
    if text.isdigit():
        ts = int(text)
        return ts if ts > 0 else None

    # ISO "YYYY-MM-DD[ HH:MM[:SS]]"
    iso = re.sub(r"[Tt]", " ", text).strip()
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d"):
        try:
            dt = datetime.strptime(iso, fmt)
            return int(dt.timestamp())
        except ValueError:
            continue

    # 相对时间："N分钟后/小时后/天后/周后/月后/年后"、"半小时后"、"一刻钟后"
    # "X个半小时" = (X+0.5) 小时
    m = re.search(r"([零〇一二两三四五六七八九十\d]+)\s*个\s*半\s*(小时|钟头)", text)
    if m:
        n = _cn_to_int(m.group(1))
        if n is None:
            return None
        return int((now + timedelta(minutes=int((n + 0.5) * 60))).timestamp())

    # "半小时" = 30 分钟
    if re.search(r"半\s*(?:个)?\s*(小时|钟头)", text):
        return int((now + timedelta(minutes=30)).timestamp())

    # "一刻钟"/"三刻钟" = 15/45 分钟
    m = re.search(r"([零〇一二两三四五六七八九十\d]+)?\s*刻\s*钟?", text)
    if m:
        n = _cn_to_int(m.group(1)) if m.group(1) else 1
        if n is None:
            return None
        return int((now + timedelta(minutes=15 * n)).timestamp())

    rel = re.search(
        r"([零〇一二两三四五六七八九十\d]+)\s*个?\s*(分钟|小时|钟头|天|周|星期|礼拜|个月|月|年)\s*(?:后|之后|以后)",
        text,
    )
    if rel:
        amount = _cn_to_int(rel.group(1))
        if amount is None:
            return None
        unit = rel.group(2)
        if unit == "分钟":
            delta = timedelta(minutes=amount)
        elif unit in ("小时", "钟头"):
            delta = timedelta(hours=amount)
        elif unit == "天":
            delta = timedelta(days=amount)
        elif unit in ("周", "星期", "礼拜"):
            delta = timedelta(weeks=amount)
        elif unit in ("个月", "月"):
            delta = timedelta(days=30 * amount)
        else:  # 年
            delta = timedelta(days=365 * amount)
        return int((now + delta).timestamp())

    # 立即
    if text in ("现在", "马上", "立刻", "立即", "now"):
        return int((now + timedelta(seconds=5)).timestamp())

    clock = _extract_clock(text)
    date_base = _extract_date(text, now)

    if clock is None and date_base is None:
        return None

    # 只有日期、没写时间 → 默认 09:00
    if clock is None:
        clock = (9, 0)
    # 只有时间、没写日期 → 今天
    if date_base is None:
        date_base = now

    result = date_base.replace(hour=clock[0], minute=clock[1], second=0, microsecond=0)

    # 纯时间但已过（"3点"而现在是下午4点）→ 顺延一天
    if _extract_date(text, now) is None and result <= now:
        result += timedelta(days=1)
    # 显式日期但已过（"10月5日"而今天是10月8日）→ 顺延一年
    elif result <= now:
        result = result.replace(year=result.year + 1)

    return int(result.timestamp())


# ── 工具实现 ─────────────────────────────────────────────────────────

def schedule_add(title: str = "", when: str = "", repeat: str = "", **_) -> ExecResult:
    title = (title or "").strip()
    when = (when or "").strip()
    repeat = (repeat or "").strip().lower()

    if not title:
        return ExecResult(False, "请告诉我日程的内容")

    epoch = parse_when(when)
    # 模型可能把"明天下午三点开会"整体塞进 title 而没填 when —— 尝试从 title 解析
    if epoch is None and parse_when(title) is not None:
        epoch = parse_when(title)
    if epoch is None:
        return ExecResult(False, f"无法识别时间：{when or title}（例如“明天下午三点”“10分钟后”）")

    if repeat and repeat not in _REPEAT_RULES:
        return ExecResult(False, f"不支持的重复规则：{repeat}（可选 daily/weekly/monthly/yearly）")

    cmd = [_CTL, "add", title, str(epoch)]
    if repeat:
        cmd += ["--repeat", repeat]

    r = _run(cmd)
    if not r.ok:
        return r

    due = datetime.fromtimestamp(epoch).strftime("%Y-%m-%d %H:%M")
    repeat_cn = {"daily": "每天", "weekly": "每周", "monthly": "每月", "yearly": "每年"}.get(repeat, "")
    suffix = f"（{repeat_cn}重复）" if repeat_cn else ""
    return ExecResult(True, f"已创建日程：{title} · {due}{suffix}")


def schedule_remove(id: str = "", title: str = "", **_) -> ExecResult:
    id = str(id or "").strip()
    title = (title or "").strip()

    if id:
        try:
            sid = int(id)
        except ValueError:
            sid = -1
        if sid > 0:
            r = _run([_CTL, "remove", str(sid)])
            if r.ok:
                return ExecResult(True, f"已删除日程 #{sid}")
            if r.detail and "not found" in r.detail:
                return ExecResult(False, f"没有找到日程 #{sid}")
            return r

    if not title:
        return ExecResult(False, "请提供要删除的日程 id 或标题")

    lst = _run([_CTL, "list", "--json"])
    if not lst.ok:
        return lst

    try:
        schedules = json.loads(lst.detail or "{}").get("schedules", [])
    except (ValueError, AttributeError):
        schedules = []

    if not schedules:
        return ExecResult(False, "当前没有日程")

    exact = [s for s in schedules if s.get("title", "").strip() == title]
    fuzzy = [s for s in schedules if title in s.get("title", "")]
    matches = exact or fuzzy

    if not matches:
        return ExecResult(False, f"没有找到标题含“{title}”的日程")

    if len(matches) > 1:
        lines = "\n".join(f"#{s['id']} {s['title']}（{_fmt_due(s)}）" for s in matches)
        return ExecResult(False, f"找到多个匹配的日程，请用 id 删除：\n{lines}")

    sid = int(matches[0]["id"])
    r = _run([_CTL, "remove", str(sid)])
    if r.ok:
        return ExecResult(True, f"已删除日程：{matches[0]['title']}")
    return r


def _fmt_due(s: dict) -> str:
    try:
        return datetime.fromtimestamp(int(s.get("due_epoch", 0))).strftime("%Y-%m-%d %H:%M")
    except (ValueError, OSError):
        return "?"


def schedule_list(**_) -> ExecResult:
    r = _run([_CTL, "list", "--json"])
    if not r.ok:
        return r

    try:
        schedules = json.loads(r.detail or "{}").get("schedules", [])
    except (ValueError, AttributeError):
        schedules = []

    if not schedules:
        return ExecResult(True, "当前没有日程提醒")

    rows = sorted(schedules, key=lambda s: s.get("due_epoch", 0))
    lines = []
    for s in rows:
        repeat = s.get("repeat") or ""
        repeat_cn = {"daily": "每天", "weekly": "每周", "monthly": "每月", "yearly": "每年"}.get(repeat, "")
        note = f"（{repeat_cn}）" if repeat_cn else ""
        lines.append(f"#{s['id']} {_fmt_due(s)} {s['title']}{note}")
    return ExecResult(True, f"共 {len(rows)} 个日程", "\n".join(lines), detail_format="text")


HANDLERS = {
    "schedule_add": schedule_add,
    "schedule_remove": schedule_remove,
    "schedule_list": schedule_list,
}


if __name__ == "__main__":
    # 轻量自检（无需 D-Bus 服务）：python3 handlers.py
    base = datetime(2026, 10, 8, 10, 0, 0)
    cases = [
        "明天下午三点", "十分钟后", "半小时后", "2026-10-09 15:00",
        "10月9日 15:00", "每周五下午五点", "明天 08:30", "今天 23:00",
    ]
    for c in cases:
        print(f"{c!r:28} -> {parse_when(c, base)}")
