"""A minimal Thalamus Task Controller task.

This is the smallest useful behavioral task: it draws a square, waits for it to be
touched, logs the trial, and reports success.  It is meant to be loaded by the Task
Controller (it is not a standalone script) -- either drop it into
``thalamus/task_controller/`` and register it in ``tasks.py``, or load it at runtime:

    python -m thalamus.task_controller --ext examples/hello_world_task.py

See https://cajigaslab.github.io/Thalamus/task_controller.html for the full task API.
"""
import datetime

from thalamus.task_controller.util import TaskContextProtocol, TaskResult
from thalamus.qt import QWidget, QVBoxLayout, QLabel, QColor, QRect


def create_widget(task_config) -> QWidget:
    """Build the task's configuration widget for the control window."""
    widget = QWidget()
    layout = QVBoxLayout(widget)
    layout.addWidget(QLabel("Hello World task — touch the square."))
    return widget


async def run(context: TaskContextProtocol) -> TaskResult:
    """Run one trial: show a target, wait for a touch (or time out), return result."""
    target = QRect(100, 100, 80, 80)
    hit = False

    def renderer(painter):
        painter.fillRect(target, QColor(41, 171, 226))   # cyan square

    def on_touch(point):
        nonlocal hit
        hit = target.contains(point)

    context.widget.renderer = renderer
    context.widget.touch_listener = on_touch
    context.widget.update()

    await context.log("BehavState=start")
    # Finish as soon as the target is touched, or after a 5 second timeout.
    await context.any(context.until(lambda: hit),
                      context.sleep(datetime.timedelta(seconds=5)))

    if hit:
        await context.log("BehavState=success")
        return TaskResult(True)
    await context.log("BehavState=timeout")
    return TaskResult(False)
