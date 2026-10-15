# lxqt-schedule

A schedule and reminder module for LXQt, consisting of a local daemon and a
command-line client. It integrates with `lxqt-notificationd`, the
`lxqt-panel/plugin-worldclock` calendar, and `bibit-intent`.

## Features

- Create, update, remove, and list schedules through D-Bus or the CLI.
- Display due reminders through `org.freedesktop.Notifications`, using
  `lxqt-notificationd` or another compatible notification service.
- Support one-time reminders and daily, weekly, monthly, or yearly recurrence.
- Update individual fields while preserving unspecified values, including
  changing or clearing recurrence.
- Mark schedules with identical due timestamps as conflicting in listings.
- Persist schedules in `~/.config/lxqt/schedule.json`.
- Allow natural language schedule management through the `bibit-intent`
  integration.
- Mark scheduled dates in the worldclock calendar through the panel integration.

## Build

Requirements: CMake 3.16 or later, a C++17 compiler, and the Qt 6 Core and D-Bus
development libraries. At runtime, the daemon and CLI use the same D-Bus session
bus. Desktop notifications require a compatible notification service.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

An XDG autostart entry is installed for desktop sessions.

## Usage

```bash
# Start the daemon manually if it is not already running through autostart.
lxqt-schedule &

# Check that the daemon is reachable.
lxqt-schedule-ctl ping

# Create a one-time reminder (GNU date syntax).
lxqt-schedule-ctl add "Team meeting" "$(date -d 'tomorrow 15:00' +%s)"

# Create a recurring reminder.
lxqt-schedule-ctl add "Weekly stand-up" "$(date -d 'next mon 09:00' +%s)" --repeat weekly

# List schedules as text or JSON.
lxqt-schedule-ctl list
lxqt-schedule-ctl list --json

# Update a schedule using its ID from the list output.
lxqt-schedule-ctl update 1 --title "Project meeting" --when "2027-01-15 15:30"
lxqt-schedule-ctl update 1 --repeat monthly
lxqt-schedule-ctl update 1 --clear-repeat

# Remove a schedule or clear all schedules.
lxqt-schedule-ctl remove 1
lxqt-schedule-ctl clear
```

The `add` command and the `update --when` option accept Unix epoch seconds or a
local date and time in `YYYY-MM-DD HH:MM[:SS]` format. Replace example IDs and dates
with values appropriate for your schedules. `--repeat` and `--clear-repeat` are
mutually exclusive when updating a schedule.

## Integration documentation

- [Architecture and D-Bus contract](docs/architecture.md)
- [bibit-intent integration](integration/bibit-intent/README.md)
- [worldclock integration](integration/worldclock/README.md)
