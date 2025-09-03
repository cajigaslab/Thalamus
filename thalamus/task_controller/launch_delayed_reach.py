# ─────────────────────────────────────────────────────────────────────────────
# File: launch_delayed_reach.py
#
# Example “launcher” that uses your real TaskContext, Canvas, widgets, and util.
# ─────────────────────────────────────────────────────────────────────────────

import sys
import asyncio
import datetime

# 1) Pick PyQt5 or PyQt6 based on what you have installed. 
#    In your folder you have “qt.py” that wraps both. We’ll import via that.
from ..qt import QApplication, QMainWindow
from qasync import QEventLoop

# Relative import of the sibling module delayed_reach_task_joystick.py:
from .delayed_reach_task_joystick import create_widget, run as run_task

# Import your Canvas and TaskContext from one level up:
from .canvas import Canvas
from .task_context import TaskContext

# 5) (Optional) If you want a configuration pane next to the Canvas:
#    create_widget(...) returns a QWidget that hosts all the Form fields for task_config.
#    Later, you might want to show that in a split view, but for now we can just hide it
#    or keep it in a small area.

async def main():
    # ─────────────────────────────────────────────────────────────────────────
    # A) Create the Qt application and main window
    # ─────────────────────────────────────────────────────────────────────────
    app = QApplication(sys.argv)
    window = QMainWindow()
    window.setWindowTitle("Delayed Center-Out Reach (Joystick) Task")

    # ─────────────────────────────────────────────────────────────────────────
    # B) Build your “task_config” dict or ObservableCollection
    #    The code in delayed_reach_task_joystick.py expects task_config to have:
    #      • Timeouts: 'intertrial_timeout', 'start_timeout', etc., each as a dict {'min':X,'max':Y}
    #      • 'is_choice' as a bool
    #      • 'state_indicator_x', 'state_indicator_y', 'stim_phase', etc.
    #      • A list under key 'targets' with at least one fixation target and one peripheral target.
    #
    #    You can (1) write this dict by hand, or (2) load JSON/ObservableCollection from disk.
    #    Below is a minimal‐working example with two targets: one fixation at center (radius=0)
    #    and one peripheral at 10°/45° so that the task can at least draw two circles.
    # ─────────────────────────────────────────────────────────────────────────
    task_config = {
        # ─── The timeouts: you can pick any small value (in seconds) so the task doesn’t immediately bail.
        'intertrial_timeout': {'min': 1, 'max': 1},
        'start_timeout':     {'min': 1, 'max': 1},
        'baseline_timeout':  {'min': 1, 'max': 1},
        'cue_timeout':       {'min': 1, 'max': 1},
        'reach_timeout':     {'min': 1, 'max': 1},
        'hold_timeout':      {'min': 1, 'max': 1},
        'blink_timeout':     {'min': 1, 'max': 1},
        'fail_timeout':      {'min': 1, 'max': 1},
        'success_timeout':   {'min': 1, 'max': 1},

        'is_choice': False,           # we’ll do a single target (no choice)
        'state_indicator_x': 180,
        'state_indicator_y': 0,
        'stim_phase': 'FAIL',         # no stimulation by default
        'stim_start': 1,
        'intan_cfg': 2,
        'pulse_count': 1,
        'pulse_frequency': 1,
        'pulse_width': 0,

        # ─── Now define two “targets”: one fixation (“is_fixation=True”) at center,
        #      and one peripheral (“is_fixation=False”).
        'targets': [
            {
                'name': 'Fixation',
                'is_fixation': True,
                'radius': 0,        # degrees of visual angle
                'angle': 0,         # degrees (irrelevant when radius=0)
                'width': 5,         # deg wide
                'height': 5,        # deg tall
                'window_size': 10,  # ±10° tolerance on “touch”
                'color': [255, 255, 255],
                'on_luminance': 1,
                'off_luminance': 0,
                'reward_channel': 0,
                'shape': 'box',
                'audio_scale_left': 0,
                'audio_scale_right': 0,
                'audio_only_if_high': False,
                'play_in_ear': False,
                'stl_file': ''
            },
            {
                'name': 'Periph1',
                'is_fixation': False,
                'radius': 10,       # 10° eccentricity
                'angle': 45,        # upper-right quadrant
                'width': 5,
                'height': 5,
                'window_size': 10,
                'color': [0, 255, 0],
                'on_luminance': 1,
                'off_luminance': 0,
                'reward_channel': 0,
                'shape': 'box',
                'audio_scale_left': 0,
                'audio_scale_right': 0,
                'audio_only_if_high': False,
                'play_in_ear': False,
                'stl_file': ''
            }
        ]
    }

    # ─────────────────────────────────────────────────────────────────────────
    # C) Choose how to lay out the form + canvas:
    #
    #    Option 1: If you want to see the Form (configuration GUI) + the Canvas *together*:
    #      1. Call create_widget(task_config) → this returns a QWidget containing all of your
    #         “Form fields” for every parameter in task_config.
    #      2. You could put that in a QSplitter or a QHBoxLayout, and then put the Canvas next to it.
    #
    #    Option 2: If you only care about the Canvas right now, you can skip the form entirely,
    #      hide it, and just put Canvas into the main window.
    #
    #    Below, we’ll show Option 2 (just Canvas).  If you want Option 1, uncomment the
    #    create_widget(...) lines and place both widgets in a splitter.
    # ─────────────────────────────────────────────────────────────────────────

    # --------------------------
    # Option 2 (just the Canvas):
    task_canvas = Canvas()
    window.setCentralWidget(task_canvas)

    # --------------------------
    # (Option 1, if desired): show the configuration pane on the left, Canvas on the right
    #
    #    config_widget = create_widget(task_config)
    #    from PyQt5.QtWidgets import QSplitter
    #    splitter = QSplitter()
    #    splitter.addWidget(config_widget)
    #    splitter.addWidget(Canvas())
    #    splitter.setSizes([250, 750])
    #    window.setCentralWidget(splitter)
    #
    #    # You must store a reference so that run(…) can reach “context.widget”:
    #    task_canvas = splitter.widget(1)
    #
    #    # If you want the form to remain visible while the task is running,
    #    # simply do NOT call config_widget.hide().  Otherwise, call config_widget.hide()
    #
    # For now, we’ll stick with Option 2: just Canvas.

    window.resize(800, 600)
    window.show()

    # ─────────────────────────────────────────────────────────────────────────
    # D) Build your real TaskContext (from task_context.py), passing in:
    #      • task_canvas  (the Canvas() instance)  
    #      • task_config  (the dict we built above)
    #
    #    In your own codebase, TaskContext likely takes additional arguments (e.g. a gRPC channel,
    #    maybe a reference to “thalamus address” or “recorder address”).  If so, adjust this line:
    # ─────────────────────────────────────────────────────────────────────────
    context = TaskContext(
        widget=task_canvas,
        task_config=task_config,
        # … any other keyword args your real TaskContext constructor expects …
    )

    # ─────────────────────────────────────────────────────────────────────────
    # E) Finally, hand off to the task’s “run(…)” coroutine.  Because run() is decorated
    #    with @animate(30), it will attempt to repaint at 30 Hz.  qasync’s loop integration
    #    lets “await asyncio.sleep(...)” inside run() work nicely with Qt’s event loop.
    # ─────────────────────────────────────────────────────────────────────────
    await run_task(context)

    print("Task ended. Final behavior result was:", context.behav_result)
    app.quit()


if __name__ == "__main__":
    # ─────────────────────────────────────────────────────────────────────────
    # Initialize qasync so that Qt <–> asyncio can co-run in one thread.
    # ─────────────────────────────────────────────────────────────────────────
    app = QApplication(sys.argv)
    loop = QEventLoop(app)
    asyncio.set_event_loop(loop)

    with loop:
        loop.run_until_complete(main())
