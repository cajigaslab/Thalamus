Task Controller
===============

The **Task Controller** is Thalamus's behavioral-task runtime.  It runs trial-based
experiments (reaches, saccades, fixation, stimulation, ...) alongside the data
pipeline, drawing stimuli to a screen, reading touch/gaze input, delivering
reward/stimulation, and logging every trial into the recording.

It is the system behind the :doc:`TASK_CONTROLLER <nodes/task_controller>` node: the
node starts/stops the controller, while the controller hosts the tasks themselves.

Running the controller
----------------------

.. code-block::

   python -m thalamus.task_controller [options]

Common options:

* ``-c, --config PATH`` -- load a saved configuration (nodes + task clusters).
* ``-p, --port PORT`` -- data-pipeline gRPC port (default ``50050``).
* ``-u, --ui-port PORT`` -- UI gRPC port (default ``50051``).
* ``-y, --pypipeline`` -- use the Python pipeline instead of the native one.
* ``-l, --log-level LEVEL`` -- ``trace`` / ``debug`` / ``info`` / ``warning`` /
  ``error`` / ``fatal``.
* ``--ext MODULE`` -- load an extension module that adds custom tasks/widgets.

The controller opens a **control window** (where you assemble *task clusters* and a
run queue) and a **subject window** (the stimulus display).  An optional
**operator view** mirrors the subject display with extra operator-only overlays
(gaze/touch traces and any task-provided controls).

Tasks and task clusters
-----------------------

A **task** is one trial paradigm.  Thalamus ships a library of tasks, registered in
``thalamus/task_controller/tasks.py``, including ``simple``, ``delayed_reach``,
``delayed_saccade``, ``delayed_reach_and_saccade``, ``double_step_reach``,
``context_dependent_reach``, ``distractor_suppression_reach``, ``gaze_anchoring``,
``ceci_stim_task`` (video + synchronized stimulation), ``stim_task``, ``null``, and
more.

In the control window you build **task clusters** -- weighted groups of tasks -- and
the controller samples from them to schedule trials.  Each task exposes a
configuration widget for its parameters (timeouts, target positions, colors, ...).

Reproducibility
---------------

When a recording is running (a :doc:`STORAGE2 <nodes/storage2>` node), the controller
copies the **source file of each task** that executes into the recording's output
directory the first time it runs.  Together with the build/version/commit metadata
that STORAGE2 writes, this means a recording archives the exact task code that
produced it.

Writing a task
--------------

A task is a Python module that exports two things:

* ``create_widget(task_config) -> QWidget`` -- builds the Qt widget used to edit the
  task's parameters in the control window.
* ``async def run(context) -> TaskResult`` -- the trial itself: an async coroutine
  that draws stimuli, waits on input/timers, logs events, and returns a
  :class:`TaskResult` (success/failure).

The ``context`` (a ``TaskContextProtocol``, in
``thalamus/task_controller/util.py``) is how a task interacts with the system:

* **Timing** -- ``await context.sleep(timedelta(...))`` and
  ``await context.until(lambda: condition)``.
* **Parameters** -- ``context.get_value(key, default)``,
  ``context.get_target_value(itarg, key, default)`` and
  ``context.get_color(key, default)`` read (and randomize within ranges) the values
  configured in the task's widget.
* **Drawing & input** -- assign ``context.widget.renderer``,
  ``context.widget.touch_listener`` and ``context.widget.gaze_listener`` to a
  function; call ``context.widget.update()`` to repaint.
* **Logging** -- ``await context.log('BehavState=...')`` writes trial events into the
  recording.

For tasks that animate continuously, decorate ``run`` with ``@animate(frequency)``
(from ``util.py``) to repaint the canvas at a fixed rate.

A minimal task
^^^^^^^^^^^^^^

.. code-block:: python

   import datetime
   from thalamus.task_controller.util import TaskContextProtocol, TaskResult
   from thalamus.qt import QWidget, QVBoxLayout, QLabel, QColor, QRect

   def create_widget(task_config):
       w = QWidget()
       layout = QVBoxLayout(w)
       layout.addWidget(QLabel("Hello World task"))
       return w

   async def run(context: TaskContextProtocol) -> TaskResult:
       hit = False

       def renderer(painter):
           painter.fillRect(QRect(100, 100, 80, 80), QColor(41, 171, 226))

       def on_touch(point):
           nonlocal hit
           hit = QRect(100, 100, 80, 80).contains(point)

       context.widget.renderer = renderer
       context.widget.touch_listener = on_touch
       context.widget.update()

       await context.log('BehavState=start')
       await context.until(lambda: hit)        # wait for the target to be touched
       await context.log('BehavState=success')
       return TaskResult(True)

Register a new task by adding a ``TaskDescription`` entry to
``thalamus/task_controller/tasks.py`` (or load it at runtime with ``--ext``).

Operator controls
-----------------

A task can surface its own operator-facing control by calling
``context.set_operator_widget(widget)``; the control window mounts it in the operator
view for the duration of the task.  This is how experimenters get task-specific
buttons/inputs without baking them into the controller.
