Task Controller
===============

The **Task Controller** is Thalamus's behavioral-task runtime.  It runs trial-based
experiments (reaches, saccades, fixation, stimulation, ...) alongside the data
pipeline, drawing stimuli to a screen, reading touch/gaze input, delivering
reward/stimulation, and logging every trial into the recording.

It is the system behind the :doc:`TASK_CONTROLLER <nodes/task_controller>` node: the
node starts/stops the controller, while the controller hosts the tasks themselves.

.. admonition:: When would I use this?

   Reach for the Task Controller when your experiment isn't just *recording* signals
   but *running a paradigm* -- presenting stimuli, requiring the subject to fixate /
   reach / choose, and delivering reward or stimulation -- all synchronized with the
   data pipeline so every trial is timestamped into the recording.

Running the controller
----------------------

.. code-block::

   python -m thalamus.task_controller [options]

Common options:

* ``-c, --config PATH`` -- load a saved configuration (nodes + task clusters).
* ``-p, --port PORT`` -- data-pipeline gRPC port (default ``50050``).
* ``-u, --ui-port PORT`` -- UI gRPC port (default ``50051``).
* ``-d, --dotnet-port PORT`` -- gRPC port used to talk to the .NET sidecar process
  (default ``50052``); see :ref:`dotnet-runtime` below.
* ``-y, --pypipeline`` -- use the Python pipeline instead of the native one.
* ``-l, --log-level LEVEL`` -- ``trace`` / ``debug`` / ``info`` / ``warning`` /
  ``error`` / ``fatal``.
* ``--ext MODULE`` -- load an extension module that adds custom tasks/widgets.
* ``--wait-for-pipeline`` -- don't start the data pipeline; wait for something else
  to launch it.  Use this when another process (e.g. a separately managed Thalamus
  pipeline, or a remote launcher) owns the pipeline and the task controller should
  only attach to it.  Has no effect with ``-y/--pypipeline``, which never launches
  the native pipeline subprocess.  The same flag exists on
  ``python -m thalamus.pipeline``.
* ``--open`` -- bind the gRPC servers (and the native pipeline process) to
  ``0.0.0.0`` instead of localhost only.  As of this release the default is
  localhost-only, so pass ``--open`` on the machine you want to reach from a
  :doc:`REMOTE <nodes/remote>` / :doc:`RUNNER2 <nodes/runner2>` node on another
  machine.  The same flag exists on ``python -m thalamus.pipeline``.
* ``-r, --remote-executor`` -- send task execution to a remote executor process
  instead of running tasks locally.

.. _dotnet-runtime:

.NET sidecar (Windows)
-----------------------

On Windows, some nodes (e.g. :doc:`DELSYS <nodes/delsys>`) are implemented by a
separate ``dotnet.exe`` process launched alongside the controller/pipeline.  Before
starting it, Thalamus checks that the required .NET 8 ASP.NET Core runtime is
installed; if it's missing, those nodes are unavailable and a one-time dialog
prompts you to install it (with a "don't show this again" option), rather than
crashing on launch.

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
  ``await context.until(lambda: condition)``.  For fixation/hold logic, the helper
  ``wait_for_hold`` (also in ``util.py``) waits for a predicate to stay true for a
  hold duration while tolerating blinks (see below).
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

Holds and blinks
^^^^^^^^^^^^^^^^

Fixation-style tasks usually require the subject to *hold* a state (gaze on a
target, hand on a pad) for some duration, while briefly losing it -- a blink, a
tracking dropout -- shouldn't necessarily fail the trial.  ``util.wait_for_hold``
implements this:

.. code-block:: python

   from thalamus.task_controller.util import wait_for_hold

   held = await wait_for_hold(
       context,
       is_held=lambda: gaze_on_target,          # predicate to hold
       hold_duration=datetime.timedelta(seconds=1),
       blink_duration=datetime.timedelta(milliseconds=200),
       include_blink=False,
       blink_resets=False)
   # held is True if the hold completed, False if a blink lasted too long

* ``hold_duration`` -- how long ``is_held`` must remain true.
* ``blink_duration`` -- the longest interruption tolerated before the hold fails.
  May be ``None`` to wait indefinitely for reacquisition.
* ``include_blink`` -- when true, time spent blinking counts toward the hold; when
  false (default), the hold is extended by the time spent blinking.
* ``blink_resets`` -- when true, any blink resets the hold timer entirely: the
  subject must produce a *continuous* ``hold_duration`` after reacquiring.  Use
  this for paradigms where an uninterrupted fixation is required rather than a
  cumulative one.

``wait_for(context, condition, timeout)`` is the simpler building block behind
``wait_for_hold``: it waits until ``condition()`` is true or ``timeout`` elapses,
returning whether the condition was met (pass ``timeout=None`` to wait
indefinitely).  ``wait_for_dual_hold`` follows the ``wait_for_hold`` pattern for two
simultaneous hold conditions (e.g. two touch points), with independent
``blink1_duration`` / ``blink2_duration``.

.. note::

   The ``stimulator(...)`` context manager in ``util.py`` is currently disabled
   (a no-op) -- it no longer starts background stimulation.  Tasks that deliver
   stimulation should drive a local ``do_stimulation()`` coroutine directly (see
   ``stim_task.py`` / ``gaze_anchoring_stim_task.py`` for the current pattern)
   rather than relying on the ``stimulator`` context manager.

Task errors
^^^^^^^^^^^

An unhandled exception raised inside a task's ``run`` is caught by the controller:
the run queue is cleared (no further scheduled trials run), the full traceback is
logged, and a **Task Error** dialog shows the traceback to the operator.  Loading a
task cluster that references an unregistered task code (e.g. defined in an
extension module that wasn't loaded with ``--ext``) similarly shows an **Unknown
Task** dialog naming the missing code instead of crashing the control window.

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

Tutorial: run your first task
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Using the ready-made ``examples/hello_world_task.py`` (the task above), end to end:

#. **Launch** the controller with the task loaded::

      python -m thalamus.task_controller --ext examples/hello_world_task.py

#. **Add a STORAGE2 node** and start it recording, so the trial events are saved.
#. **Build a task cluster.** In the control window, create a cluster and add the
   *Hello World* task to it; the controller schedules trials by sampling from your
   clusters.
#. **Run.** Start the run queue.  The subject window shows the cyan square; touching
   it (or clicking, with a touch device) completes the trial successfully, otherwise
   it times out after 5 seconds.
#. **Verify.** Stop recording and confirm the logged trial states landed in the
   capture (see :doc:`tools`)::

      python -m thalamus.dataframe -n task_controller -t text -i recording.tha -f csv

   You should see the ``BehavState=start`` / ``BehavState=success`` events your task
   logged.

From here, edit ``run`` to add stimuli, timing, and reward, and read parameters from
``create_widget`` via ``context.get_value`` so the task is configurable.

Operator controls
-----------------

A task can surface its own operator-facing control by calling
``context.set_operator_widget(widget)``; the control window mounts it in the operator
view for the duration of the task.  This is how experimenters get task-specific
buttons/inputs without baking them into the controller.

Verifying a trial was recorded
------------------------------

With a STORAGE2 node running, each ``await context.log('BehavState=...')`` becomes a
``text`` record in the capture file.  After a run you can confirm trials were logged
by exporting that node's text channel (see :doc:`tools`):

.. code-block::

   python -m thalamus.dataframe -n task_controller -t text -i recording.tha -f csv
