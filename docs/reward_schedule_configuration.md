# Reward Schedule Configuration

This note documents how reward is currently configured in the task controller.

## Short Version

Rewards are looked up by channel from `config["reward_schedule"]`.

Each task decides which reward channel to use. The shared task context turns that channel into a reward duration in milliseconds by reading the current row of the reward schedule.

## Schedule Shape

The reward schedule lives in the top-level task controller config:

```json
{
  "reward_schedule": {
    "schedules": [
      [0, 100, 100, 150],
      [0, 200, 200, 250]
    ],
    "index": 0
  }
}
```

`schedules` is organized as:

- `schedules[0]`: reward channel 0 values over time
- `schedules[1]`: reward channel 1 values over time
- `schedules[N]`: reward channel N values over time

`index` selects the current position in every channel schedule.

For example, if `index == 2` and a task requests reward channel `1`, the returned value is:

```python
config["reward_schedule"]["schedules"][1][2]
```

That value is interpreted as milliseconds of reward.

## Loading A Schedule File

The current task controller menu item is `File -> Load Reward Schedule`.

Despite older references to Excel-style reward schedule files, the current loader only accepts CSV:

```python
QFileDialog.getOpenFileName(self, "Load Reward Schedule", "", "*.csv")
```

The loader uses:

```python
numpy.loadtxt(filename, delimiter=",", unpack=True)
```

Because `unpack=True` is used, CSV columns become reward channels.

Example CSV:

```csv
0,0,0
100,200,300
100,200,300
150,250,350
```

This becomes:

- channel 0: `[0, 100, 100, 150]`
- channel 1: `[0, 200, 200, 250]`
- channel 2: `[0, 300, 300, 350]`

If you maintain the schedule in Excel, LibreOffice, or an `.ods` file, export it to CSV before loading it through the task controller.

## Runtime Lookup

The shared lookup happens in `TaskContext.get_reward(channel)`:

```python
current_index = self.config["reward_schedule"]["index"]
reward = float(self.config["reward_schedule"]["schedules"][int(channel)][current_index])
return max(reward, 0.0)
```

Negative values are clamped to `0.0`.

The returned reward is also recorded in `trial_summary_data.used_values["reward"]`.

## When The Schedule Advances

The schedule index advances only after a non-cancelled successful trial.

After success, the task context does:

```python
current_index = config["reward_schedule"]["index"]
modulus = min(len(s) for s in config["reward_schedule"]["schedules"])
config["reward_schedule"]["index"] = (current_index + 1) % modulus
```

Failed trials do not advance the schedule. Cancelled trials do not advance it.

The modulo uses the shortest channel schedule length, so channels with longer schedules will wrap according to the shortest channel.

## How Tasks Choose Channels

Most tasks expose a `Reward Channel` or `Reward Channel (if correct)` setting in the task/target config.

Common pattern:

```python
all_reward_channels = [
  context.get_target_value(i, "reward_channel", None)
  for i in range(ntargets)
]
on_time_ms = int(context.get_reward(all_reward_channels[successful_target_index]))
```

For target-based tasks, the successful target usually determines which reward channel is requested.

## How Reward Is Delivered

The schedule only determines the duration. The individual task still decides how to deliver the reward.

Many non-ROS tasks inject an analog pulse named `Reward`:

```python
signal = thalamus_pb2.AnalogResponse(
  data=[5, 0],
  spans=[thalamus_pb2.Span(begin=0, end=2, name="Reward")],
  sample_intervals=[1_000_000 * on_time_ms],
)
await context.inject_analog("Reward", signal)
```

Some ROS-based reach tasks publish a `RewardDeliveryCmd` instead:

```python
reward_message.on_time_ms = int(context.get_reward(channel))
context.publish(RewardDeliveryCmd, "deliver_reward", reward_message)
```

## Joystick Intro Notes

`joystick_intro.py` uses the same shared reward schedule lookup.

The helper `deliver_reward(channel)` does:

```python
on_time_ms = int(context.get_reward(channel))
```

If the result is `0` or less, reward is skipped. Otherwise it injects the analog `Reward` pulse.

In target-guided mode, each target can have its own `reward_channel`; if a target does not specify one, it falls back to the task-level `reward_channel`.

In free-play mode, the first-touch, bout-start, and sustain rewards can each use their own configured reward channel.

## Important Caveats

- The current runtime loader is CSV-only.
- Reward channel numbers are indexes into `reward_schedule["schedules"]`.
- A task requesting a channel that does not exist in `schedules` will fail at runtime.
- The schedule advances on successful trials, not on every reward delivery. Extra bonus rewards within a successful joystick trial use the same current schedule index.
- The `RewardSchedule` dock is only a visualization of the current schedule and index; the actual lookup happens in `TaskContext.get_reward`.
