#pylint: skip-file
#type: ignore
"""
Implementation of the simple task
"""
import time
import math
import typing
import asyncio
import logging
import datetime
import numpy as np
import os

import stl

from thalamus.qt import *

from thalamus.task_controller import task_context
from thalamus.task_controller.widgets import Form, ListAsTabsWidget
from thalamus.task_controller.util import wait_for, wait_for_hold, RenderOutput, animate
from thalamus import thalamus_pb2
from thalamus import task_controller_pb2
from thalamus import config

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
    ('intertrial_timeout', datetime.timedelta),       # time between trials - black screen
    ('start_timeout', datetime.timedelta),            # time to initiate trial
    ('center_hold_timeout', datetime.timedelta),       # hold at center
    ('reach_timeout', datetime.timedelta),             # time to reach target
    ('target_hold_timeout', datetime.timedelta),       # hold at peripheral
    ('blink_timeout', datetime.timedelta),            # time allowed to break hold
    ('cue_delay', datetime.timedelta),                 # delay before target lights
    ('fail_timeout', datetime.timedelta),              # time after fail
    ('success_timeout', datetime.timedelta),           # time after success
    ('sequence_length', int),                          # how many reaches
    ('is_random_sequence', bool),                      # random vs fixed
    ('last_target_hold_timeout', datetime.timedelta)   # last target hold
])

RANDOM_DEFAULT = {'min': 1, 'max': 1}
COLOR_DEFAULT = [255, 255, 255]


class TargetWidget(QWidget):
    """
    Widget for managing a target config
    """
    def __init__(self, config_obj: config.ObservableCollection) -> None:
        super().__init__()
        if 'name' not in config_obj:
            config_obj['name'] = 'Untitled'

        layout = QGridLayout()
        self.setLayout(layout)

        layout.addWidget(QLabel('Name:'), 0, 0)

        name_edit = QLineEdit(config_obj['name'])
        name_edit.setObjectName('name_edit')
        name_edit.textChanged.connect(lambda v: config_obj.update({'name': v}))
        layout.addWidget(name_edit, 0, 1)

        def do_copy() -> None:
            if config_obj.parent:
                config_obj.parent.append(config_obj.copy())

        copy_button = QPushButton('Copy Target')
        copy_button.setObjectName('copy_button')
        copy_button.clicked.connect(do_copy)
        layout.addWidget(copy_button, 0, 2)

        fixed_form = Form.build(config_obj, ['Name:', 'Value:'],
            Form.Constant('Width', 'width', 10, '°'),
            Form.Constant('Height', 'height', 10, '°'),
            Form.Constant('Orientation', 'orientation', 0, '°'),
            Form.Constant('Window Size', 'window_size', 0, '°'),
            Form.Constant('Reward Channel', 'reward_channel', 0),
            Form.Constant('Audio Scale Left', 'audio_scale_left', 0),
            Form.Constant('Audio Scale Right', 'audio_scale_right', 0),
            Form.Color('Color', 'color', QColor(255, 255, 255)),
            Form.Bool('Is Center', 'is_center', False),
            Form.Choice('Shape', 'shape', [('Box', 'box'), ('Ellipsoid', 'ellipsoid')]),
            Form.File('Stl File (Overrides shape)', 'stl_file', '', 'Select Stl File', '*.stl'),
            Form.File('Audio File', 'audio_file', '', 'Select Audio File', '*.wav'),
            Form.Bool('Only Play If Channel Is High', 'audio_only_if_high'),
            Form.Bool('Play In Ear', 'play_in_ear')
        )
        layout.addWidget(fixed_form, 1, 1, 1, 2)

        random_form = Form.build(config_obj, ['Name:', 'Min:', 'Max:'],
            Form.Uniform('Radius', 'radius', 0, 5, '°'),
            Form.Uniform('Angle', 'angle', 0, 360, '°'),
            Form.Uniform('Audio Volume', 'volume', 0, 0),
            Form.Uniform('Auditory Temporal Jitter', 'auditory_temporal_jitter', 0, 0),
            Form.Uniform('Auditory Spatial Offset', 'auditory_spatial_offset', 0, 0),
            Form.Uniform('Auditory Spatial Offset Around Fixation', 'auditory_spatial_offset_around_fixation', 0, 0),
            Form.Uniform('On Luminance', 'on_luminance', 1, 1),
            Form.Uniform('Off Luminance', 'off_luminance', 0, 0)
        )
        layout.addWidget(random_form, 1, 3, 1, 2)


def create_widget(task_config: config.ObservableCollection) -> QWidget:
    """
    Creates a widget for configuring the simple task
    """
    result = QWidget()
    layout = QVBoxLayout()
    result.setLayout(layout)

    form = Form.build(task_config, ["Name:", "Min:", "Max:"],
        Form.Uniform('Intertrial Interval', 'intertrial_timeout', 1, 1, 's'),
        Form.Uniform('Start Interval', 'start_timeout', 5, 5, 's'),
        Form.Uniform('Center Hold Interval', 'center_hold_timeout', 1, 1, 's'),    
        Form.Uniform('Reach Timeout', 'reach_timeout', 2, 2, 's'),
        Form.Uniform('Target Hold Interval', 'target_hold_timeout', 0.3, 0.3, 's'),
        Form.Uniform('Last Target Hold', 'last_target_hold_timeout', 0.3, 0.3, 's'),
        Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
        Form.Uniform('Cue Delay', 'cue_delay', 0.4, 2.0, 's'),
        Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
        Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
        Form.Uniform('Sequence Length', 'sequence_length', 2, 2, ''),
        Form.Bool('Random Sequence', 'is_random_sequence', True),
        Form.Constant('State Indicator X', 'state_indicator_x', 180),
        Form.Constant('State Indicator Y', 'state_indicator_y', 0),
    )
    layout.addWidget(form)

    new_target_button = QPushButton('Add Target')
    new_target_button.setObjectName('new_target_button')
    new_target_button.clicked.connect(lambda: task_config['targets'].append({}) and None)
    layout.addWidget(new_target_button)

    if 'targets' not in task_config:
        task_config['targets'] = []
    target_config_list = task_config['targets']
    target_tabs = ListAsTabsWidget(target_config_list, TargetWidget, lambda t: str(t.get('name', 'Untitled')))
    layout.addWidget(target_tabs)

    return result


def pol_to_cart2d(radius, angle):
    xcart = radius * np.cos(np.radians(angle))
    ycart = radius * np.sin(np.radians(angle))
    return xcart, ycart


def ecc_to_px(ecc, dpi):
    """
    Converts degrees of eccentricity to pixels relative to the optical center.
    """
    d_m = 0.4  # meters (approximate)
    x_m = d_m * np.tan(np.radians(ecc))
    x_inch = x_m / 0.0254
    x_px = x_inch * dpi
    return x_px


def get_target_rectangles(context, dpi):
    all_target_rects = []
    ntargets = len(context.task_config['targets'])
    canvas = context.widget

    for itarg in range(ntargets):
        x_ecc, y_ecc = pol_to_cart2d(
            context.get_target_value(itarg, 'radius'),
            context.get_target_value(itarg, 'angle')
        )

        targ_width_px = ecc_to_px(context.get_target_value(itarg, 'width'), dpi)
        targ_height_px = ecc_to_px(context.get_target_value(itarg, 'height'), dpi)

        ecc = np.array([x_ecc, y_ecc])
        pos_vis = ecc_to_px(ecc, dpi)
        t = np.array([canvas.frameGeometry().width() / 2, canvas.frameGeometry().height() / 2])
        Rvec = np.array([1.0, -1.0])

        p_win = Rvec * pos_vis + t
        all_target_rects.append(
            QRect(
                int(p_win[0] - targ_width_px / 2),
                int(p_win[1] - targ_height_px / 2),
                int(targ_width_px),
                int(targ_height_px)
            )
        )

    return all_target_rects


def distance(lhs, rhs):
    return ((lhs.x() - rhs.x()) ** 2 + (lhs.y() - rhs.y()) ** 2) ** 0.5


@animate(30)
async def run(context: task_context.TaskContextProtocol) -> task_context.TaskResult:
    """
    Implementation of the state machine for the random sequence task
    """
    success_sound = QSound(os.path.join(os.path.dirname(__file__), 'success_clip.wav'))
    fail_sound = QSound(os.path.join(os.path.dirname(__file__), 'failure_clip.wav'))
    show_touch_pos_feedback = False

    # Instantiating NamedTuple using keyword arguments to protect item placement
    config = Config(
        intertrial_timeout=datetime.timedelta(seconds=context.get_value('intertrial_timeout', 1.0)),
        start_timeout=datetime.timedelta(seconds=context.get_value('start_timeout', 5.0)),
        center_hold_timeout=datetime.timedelta(seconds=context.get_value('center_hold_timeout', 1.0)),
        reach_timeout=datetime.timedelta(seconds=context.get_value('reach_timeout', 2.0)),
        target_hold_timeout=datetime.timedelta(seconds=context.get_value('target_hold_timeout', 0.3)),
        blink_timeout=datetime.timedelta(seconds=context.get_value('blink_timeout', 0.1)),
        cue_delay=datetime.timedelta(seconds=context.get_value('cue_delay', 0.4)),
        fail_timeout=datetime.timedelta(seconds=context.get_value('fail_timeout', 1.0)),
        success_timeout=datetime.timedelta(seconds=context.get_value('success_timeout', 1.0)),
        sequence_length=int(context.get_value('sequence_length', 2)),
        is_random_sequence=context.get_value('is_random_sequence', True),
        last_target_hold_timeout=datetime.timedelta(seconds=context.get_value('last_target_hold_timeout', 0.3))
    )

    custom_display_state_x = int(context.task_config['state_indicator_x'])
    custom_display_state_y = int(context.task_config['state_indicator_y'])

    ntargets = len(context.task_config['targets'])
    i_center_target = None
    i_periph_targs = []

    for i in range(ntargets):
        is_center = context.get_target_value(i, 'is_center', False)
        if is_center:
            i_center_target = int(i)
        else:
            i_periph_targs.append(int(i))

    if i_center_target is None:
        i_center_target = 0
        i_periph_targs = [int(x) for x in range(1, ntargets)]

    dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()

    all_target_rects = get_target_rectangles(context, dpi)
    all_target_windows = [ecc_to_px(context.get_target_value(itarg, 'window_size'), dpi) for itarg in range(ntargets)]
    all_target_colors = [context.get_target_color(itarg, 'color', COLOR_DEFAULT) for itarg in range(ntargets)]
    all_target_stls = []

    def load_stl(filename: str) -> typing.Optional[stl.mesh.Mesh]:
        if not filename:
            return None
        return stl.mesh.Mesh.from_file(filename)

    for i in range(ntargets):
        all_target_stls.append(load_stl(context.get_target_value(i, 'stl_file')))

    all_reward_channels = [context.get_target_value(i, 'reward_channel', None) for i in range(ntargets)]

    if config.is_random_sequence:
        sequence = np.random.choice(i_periph_targs, size=config.sequence_length, replace=True)
        sequence = sequence.tolist()
    else:
        sequence = i_periph_targs[:config.sequence_length]

    behav_result = {
        'sequence': sequence,
        'selected_targets': [],
        'completed_steps': 0,
        'is_random_sequence': config.is_random_sequence
    }

    center_acquired = False
    target_acquired = False
    i_selected_target = None
    touch_pos = QPoint()
    center_brightness = 255
    show_all_targets = True
    current_target_to_highlight = None
    state_brightness = 0

    def touch_handler(cursor: QPoint) -> None:
        nonlocal center_acquired, target_acquired, i_selected_target, touch_pos

        touch_pos = cursor

        # Center always takes priority: being inside the center window counts
        # as "at center" even if a peripheral window overlaps that same spot.
        # Peripheral targets are only considered once the touch is outside
        # the center window, so center_acquired/target_acquired stay mutually
        # exclusive no matter how much the windows overlap.
        center_acquired = distance(all_target_rects[i_center_target].center(), cursor) < all_target_windows[i_center_target]

        target_acquired = False
        i_selected_target = None

        if not center_acquired:
            for i in i_periph_targs:
                if distance(all_target_rects[i].center(), cursor) < all_target_windows[i]:
                    target_acquired = True
                    i_selected_target = i
                    break

    context.widget.touch_listener = touch_handler
    state_brightness = 0

    def renderer(painter: QPainter) -> None:
        nonlocal center_brightness, show_all_targets, current_target_to_highlight, state_brightness
        #window = all_target_windows[0]
        center_color = QColor(center_brightness, center_brightness, center_brightness)
        stl_mesh = all_target_stls[i_center_target]
        if stl_mesh:
            painter.render_stl(stl_mesh)
        else:
            painter.fillRect(all_target_rects[i_center_target], center_color)

        if show_all_targets:
            for i in i_periph_targs:
                if i == current_target_to_highlight:
                    color = all_target_colors[i]
                else:
                    color = QColor(10, 10, 10)

                stl_mesh = all_target_stls[i]
                if stl_mesh:
                    painter.render_stl(stl_mesh)
                else:
                    painter.fillRect(all_target_rects[i], color)

        with painter.masked(RenderOutput.OPERATOR):
            path = QPainterPath()
            for rect, window in zip(all_target_rects, all_target_windows):
                path.addEllipse(QPointF(rect.center()), window, window)
            painter.fillPath(path, QColor(255, 255, 255, 50))

        state_color = QColor(state_brightness, state_brightness, state_brightness)
        state_width = 40
        custom_display_state_pos = True
        custom_display_state_width = 70

        if custom_display_state_pos:
            painter.fillRect(custom_display_state_x, custom_display_state_y,
                             custom_display_state_width, custom_display_state_width, state_color)
        else:
            painter.fillRect(context.widget.width() - state_width,
                             context.widget.height() - state_width,
                             state_width, state_width, state_color)

        if show_touch_pos_feedback:
            cursor_color = QColor(255, 0, 0)
            cursor_width = 20
            voltage = context.widget.last_voltage
            painter.fillRect(touch_pos.x() - int(cursor_width / 2),
                             touch_pos.y() - int(cursor_width / 2),
                             cursor_width, cursor_width, cursor_color)
            painter.setPen(QColor(255, 0, 0, 255))
            painter.drawText(touch_pos, '   x: %d, y: %d, Vx: %0.2f, Vy: %0.2f' %
                             (touch_pos.x(), touch_pos.y(), voltage.x(), voltage.y()))

    context.widget.renderer = renderer

    async def fail_trial(reason: str):
        nonlocal center_brightness, current_target_to_highlight, state_brightness, show_all_targets
        await context.log(f'BehavState=fail_{reason}')
        context.behav_result = behav_result
        center_brightness = 0
        show_all_targets = False
        current_target_to_highlight = None
        state_brightness = 0
        fail_sound.play()
        context.widget.update()
        await context.sleep(config.fail_timeout)

    # ITI phase
    await context.log('BehavState=intertrial')
    state_brightness = 0
    center_brightness = 0
    current_target_to_highlight = None
    show_all_targets = False
    context.widget.update()
    await context.sleep(config.intertrial_timeout)

    show_all_targets = True
    center_brightness = 255
    context.widget.update()

    # Sequence Loop
    for step_idx, target_idx in enumerate(sequence):
        target_acquired = False
        i_selected_target = None
        center_acquired = False

        if step_idx == 0:
            while True:
                await context.log(f'BehavState=step_{step_idx}_wait_center')
                center_brightness = 255
                current_target_to_highlight = None
                state_brightness = 255
                show_all_targets = True
                context.widget.update()

                wrong_touch_occurred = False

                def check_touch():
                    nonlocal wrong_touch_occurred
                    if target_acquired:
                        wrong_touch_occurred = True
                        return True
                    return center_acquired

                acquired = await wait_for(context, check_touch, config.start_timeout)

                if wrong_touch_occurred:
                    await fail_trial('wrong_touch_initiation')
                    if i_selected_target is not None:
                        behav_result['selected_targets'].append(int(i_selected_target))
                    return task_context.TaskResult(False)
                elif acquired:
                    break
                else:
                    await context.log('BehavState=no_initiation')
                    center_brightness = 0
                    show_all_targets = False
                    current_target_to_highlight = None
                    state_brightness = 0
                    context.widget.update()
                    await context.sleep(config.fail_timeout)
        else:
            await context.log(f'BehavState=step_{step_idx}_wait_center_return')
            center_brightness = 255
            current_target_to_highlight = None
            state_brightness = 255
            show_all_targets = True
            context.widget.update()

            def center_check():
                return center_acquired and not target_acquired

            acquired = await wait_for(context, center_check, config.reach_timeout)
            if not acquired:
                await fail_trial('no_center_return')
                return task_context.TaskResult(False)

        await context.log(f'BehavState=step_{step_idx}_center_hold')
        center_hold_duration = config.center_hold_timeout if step_idx == 0 else config.target_hold_timeout
        success = await wait_for_hold(context, lambda: center_acquired, center_hold_duration, config.blink_timeout)
        if not success:
            await fail_trial('center_hold_break')
            return task_context.TaskResult(False)

        await context.log(f'BehavState=step_{step_idx}_cue_delay')
        center_brightness = 255
        current_target_to_highlight = target_idx
        context.widget.update()

        success = await wait_for_hold(context, lambda: center_acquired, config.cue_delay, config.blink_timeout)
        if not success:
            await fail_trial('center_hold_break_during_delay')
            return task_context.TaskResult(False)

        await context.log(f'BehavState=step_{step_idx}_go_cue')
        center_brightness = 10
        context.widget.update()

        await context.log(f'BehavState=step_{step_idx}_reach')
        acquired = await wait_for(context, lambda: target_acquired, config.reach_timeout)
        if not acquired:
            await fail_trial('no_target_touch')
            return task_context.TaskResult(False)

        if i_selected_target != target_idx:
            await fail_trial('wrong_target')
            if i_selected_target is not None:
                behav_result['selected_targets'].append(int(i_selected_target))
            return task_context.TaskResult(False)

        behav_result['selected_targets'].append(i_selected_target)

        await context.log(f'BehavState=step_{step_idx}_target_hold')
        target_hold_check = lambda: (i_selected_target == target_idx and target_acquired)
        hold_duration = config.last_target_hold_timeout if step_idx == len(sequence) - 1 else config.target_hold_timeout
        success = await wait_for_hold(context, target_hold_check, hold_duration, config.blink_timeout)
        if not success:
            await fail_trial('target_hold_break')
            return task_context.TaskResult(False)

        behav_result['completed_steps'] = step_idx + 1
        if step_idx < len(sequence) - 1:
            center_brightness = 255
            current_target_to_highlight = None
            context.widget.update()

    context.widget.update()

    await context.log('BehavState=success')
    state_brightness = 0
    center_brightness = 0
    show_all_targets = False
    current_target_to_highlight = None
    context.widget.update()
    final_target = int(sequence[-1])
    on_time_ms = int(context.get_reward(all_reward_channels[final_target]))

    signal = thalamus_pb2.AnalogResponse(
        data=[5, 0],
        spans=[thalamus_pb2.Span(begin=0, end=2, name='Reward')],
        sample_intervals=[1_000_000 * on_time_ms])

    await context.inject_analog('Reward', signal)
    success_sound.play()
    await context.sleep(config.success_timeout)

    context.behav_result = behav_result
    return task_context.TaskResult(True)
