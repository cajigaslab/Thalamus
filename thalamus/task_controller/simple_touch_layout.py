"""
Operator config panel for the simple_touch_rust task: the task settings form, a
target table, and a visual Target Layout Editor (drag targets on a scaled
picture of the subject screen, generate rings/grids, presets, groups).

Adapted from joystick_intro.create_widget's table + layout editor, but in
simple_touch's own coordinate system: every target is placed by polar
eccentricity (radius/angle in degrees around the task center) and sized in
degrees, converted to pixels exactly like simple_touch_task.ecc_to_px and the
Rust executor (rust/joystick_task/src/touch.rs). There is no task region,
cursor, hold time per target, schedule, or active/inactive colors here.

Config compatibility: targets keep every key the legacy simple_touch_task
TargetWidget writes (audio/luminance/stl fields included, hidden in this UI)
so the Python Simple Touch task can still run the same config and the Rust
delegate's get_target_value() calls see the same keys. New keys: `enabled`
and `group` (honored by simple_touch_rust; the Python task ignores them),
`center_x`/`center_y` (task center as screen fractions, default 0.5/0.5), and
`state_indicator_anchor`. The Rust task draws the photodiode square like
joystick_intro: state_indicator_x/y are margins from the bottom-right corner,
NOT simple_touch_task's absolute top-left position.
"""

import copy
import math
import typing

from ..qt import *
from ..config import ObservableCollection
from ..util import PersistedGeometryWindow
from .widgets import Form

# Geometry shared with simple_touch_task.ecc_to_px and touch.rs.
VIEWING_DISTANCE_M = 0.4
STATE_INDICATOR_EDGE_PX = 70

DEFAULT_TARGET_COLOR = [255, 255, 255]
# joystick_intro's photodiode margins (right, bottom).
DEFAULT_STATE_INDICATOR_MARGINS = (30, 70)
DEFAULT_PREVIEW_SCREEN = {'width': 1920, 'height': 1080, 'dpi': 0}

# Every per-target key simple_touch_task.TargetWidget writes, with its
# defaults. Keys not editable here are still filled in so the Python task and
# the delegate's get_target_value() calls keep working on edited configs.
LEGACY_TARGET_DEFAULTS: typing.Dict[str, typing.Any] = {
  'name': 'Untitled',
  'width': 10.0,
  'height': 10.0,
  'orientation': 0,
  'window_size': 0.0,
  'reward_channel': 0,
  'audio_scale_left': 0,
  'audio_scale_right': 0,
  'color': list(DEFAULT_TARGET_COLOR),
  'is_fixation': False,
  'shape': 'box',
  'stl_file': '',
  'audio_file': '',
  'audio_only_if_high': False,
  'play_in_ear': False,
  'radius': {'min': 0, 'max': 5},
  'angle': {'min': 0, 'max': 360},
  'volume': {'min': 0, 'max': 0},
  'auditory_temporal_jitter': {'min': 0, 'max': 0},
  'auditory_spatial_offset': {'min': 0, 'max': 0},
  'auditory_spatial_offset_around_fixation': {'min': 0, 'max': 0},
  'on_luminance': {'min': 1, 'max': 1},
  'off_luminance': {'min': 0, 'max': 0},
}

GROUPLESS_LABEL = '(ungrouped)'

# Hover help for the settings form, keyed by the row label. Every widget in
# the row gets the same tooltip. Describes what touch.rs actually does.
UNIFORM_NOTE = (' Min/Max: a new value is drawn uniformly between them at the start of every '
                'trial (set both equal for a fixed value).')
SETTINGS_TOOLTIPS: typing.Dict[str, str] = {
  'Min:': 'Lower bound of the random range for the timing rows below.',
  'Max:': 'Upper bound of the random range for the timing rows below. Equal to Min = fixed value.',
  'Intertrial Interval': (
    'Blank screen (target hidden, photodiode dark) before the target appears. '
    'Also repeats, with the same value, when a start times out and "No start = fail?" is off.'
    + UNIFORM_NOTE),
  'Start Interval': (
    'How long the target stays up waiting for the first touch. Touching inside a target\'s '
    'window starts the hold. Touching blank space, or no touch before this runs out, '
    'fails the trial when "No start = fail?" is on; otherwise the target hides and the '
    'trial goes back to the intertrial interval.' + UNIFORM_NOTE),
  'Hold Interval': (
    'How long the touch must stay inside the target\'s window, counted from when it was '
    'acquired, to succeed. Time spent lifted (blinking) still counts toward it.' + UNIFORM_NOTE),
  'Blink Interval': (
    'During the hold, the longest the finger may leave the window and come back. Each lift '
    'logs BehavState=blink; not back within this time fails the trial.' + UNIFORM_NOTE),
  'Fail Interval': (
    'Time after a fail (target hidden, photodiode dark, fail sound) before the trial ends '
    'and the next one starts.' + UNIFORM_NOTE),
  'Success Interval': (
    'Time after a success (target hidden, reward delivered, success sound) before the '
    'trial ends and the next one starts.' + UNIFORM_NOTE),
  'Is Choice': (
    'On: every enabled target is shown at once and whichever the subject touches is '
    'rewarded on its own reward channel (if windows overlap, the earlier target in the list '
    'wins). Off: one enabled target is picked at random each trial and only it counts.'),
  'No start = fail?': (
    'On: touching blank space or not touching during the start interval fails the trial '
    '(fail interval, then a new trial with freshly drawn values). Off: the target hides and '
    'the same trial loops back to the intertrial interval.'),
  'Task Center X (fraction of width)': (
    'Horizontal position of the 0-eccentricity point that every target radius/angle is '
    'measured from, as a fraction of the screen width. 0.5 = middle.'),
  'Task Center Y (fraction of height, 0 = top)': (
    'Vertical position of the 0-eccentricity point, as a fraction of the screen height from '
    'the top. 0.5 = middle; lower values move every target up (0.4 = 108 px higher on 1080p).'),
  'State Indicator Right Margin': (
    'Photodiode square (70 px) distance from the right screen edge, in px. The square '
    'goes white when the target appears, dark again at success, and stays dark between '
    'trials and on fail.'),
  'State Indicator Bottom Margin': (
    'Photodiode square (70 px) distance from the bottom screen edge, in px.'),
  'Touch Node': (
    'Name of the Thalamus TOUCH_SCREEN node the Rust executor reads touches from '
    '(global screen pixels).'),
}

TABLE_COLUMNS = [
  'Enabled', 'Name', 'Radius (°)', 'Angle (°)', 'Width (°)',
  'Height (°)', 'Window (°)', 'Reward Channel', 'Color',
]
COL_ENABLED, COL_NAME, COL_RADIUS, COL_ANGLE, COL_WIDTH, COL_HEIGHT, COL_WINDOW, COL_REWARD, COL_COLOR = range(9)

BULK_FIELDS = ['Enabled', 'Name', 'Radius', 'Angle', 'Width', 'Height', 'Window', 'Reward Channel', 'Color']


# --- pure helpers (no Qt widgets; unit-testable) ---

def plain(value: typing.Any) -> typing.Any:
  """Deep, Observable-free copy of a config value."""
  if isinstance(value, ObservableCollection):
    value = value.unwrap()
  return copy.deepcopy(value)


def ecc_to_px(ecc: float, dpi: float) -> float:
  """simple_touch_task.ecc_to_px: degrees of eccentricity -> pixels."""
  return VIEWING_DISTANCE_M * math.tan(math.radians(ecc)) / 0.0254 * dpi


def px_to_ecc(px: float, dpi: float) -> float:
  """Inverse of ecc_to_px."""
  return math.degrees(math.atan(px / max(dpi, 1e-6) * 0.0254 / VIEWING_DISTANCE_M))


def range_bounds(value: typing.Any, default: float = 0.0) -> typing.Tuple[float, float]:
  """(min, max) of a Form.Uniform value: a number or {'min', 'max'}."""
  if isinstance(value, dict):
    try:
      lo = float(value.get('min', default))
      hi = float(value.get('max', lo))
    except (TypeError, ValueError):
      return default, default
    return lo, hi
  try:
    v = float(value)
  except (TypeError, ValueError):
    v = default
  return v, v


def range_mid(value: typing.Any, default: float = 0.0) -> float:
  lo, hi = range_bounds(value, default)
  return (lo + hi) / 2.0


def make_range(lo: float, hi: typing.Optional[float] = None) -> typing.Dict[str, float]:
  hi = lo if hi is None else hi
  return {'min': float(lo), 'max': float(hi)}


def _fmt(v: float) -> str:
  return f'{v:.3f}'.rstrip('0').rstrip('.') or '0'


def format_range(value: typing.Any) -> str:
  """'5' for a fixed value, '0..360' for a random range."""
  lo, hi = range_bounds(value)
  return _fmt(lo) if lo == hi else f'{_fmt(lo)}..{_fmt(hi)}'


def parse_range(text: str) -> typing.Dict[str, float]:
  """Inverse of format_range. Raises ValueError on bad input."""
  text = text.strip()
  if '..' in text:
    lo_text, hi_text = text.split('..', 1)
    lo, hi = float(lo_text), float(hi_text)
    if hi < lo:
      lo, hi = hi, lo
    return make_range(lo, hi)
  return make_range(float(text))


def normalize_rgb(value: typing.Any, default: typing.Sequence[int] = DEFAULT_TARGET_COLOR) -> typing.List[int]:
  if isinstance(value, QColor):
    return [value.red(), value.green(), value.blue()]
  if isinstance(value, (list, tuple)) and len(value) >= 3:
    try:
      return [max(0, min(255, int(value[i]))) for i in range(3)]
    except (TypeError, ValueError):
      pass
  return [int(c) for c in default]


def normalize_target(target: typing.Any) -> typing.Dict[str, typing.Any]:
  """Plain dict with every legacy key present and editable fields coerced.

  Unknown keys are preserved so nothing another tool wrote is lost.
  """
  t = plain(target) if isinstance(target, dict) else {}
  for key, default in LEGACY_TARGET_DEFAULTS.items():
    if key not in t:
      t[key] = copy.deepcopy(default)
  t['name'] = str(t.get('name', ''))
  t['enabled'] = bool(t.get('enabled', True))
  t['group'] = str(t.get('group', ''))
  for key in ('radius', 'angle'):
    lo, hi = range_bounds(t[key])
    t[key] = make_range(lo, hi)
  for key in ('width', 'height', 'window_size'):
    try:
      t[key] = max(0.0, float(t[key]))
    except (TypeError, ValueError):
      t[key] = float(LEGACY_TARGET_DEFAULTS[key])
  try:
    t['reward_channel'] = max(0, int(float(t['reward_channel'])))
  except (TypeError, ValueError):
    t['reward_channel'] = 0
  t['color'] = normalize_rgb(t.get('color'))
  return t


def make_default_target(index: int) -> typing.Dict[str, typing.Any]:
  target = normalize_target({
    'name': f'Target {index}',
    'width': 3.0,
    'height': 3.0,
    'window_size': 8.0,
    'radius': make_range(0.0),
    'angle': make_range(0.0),
  })
  return target


def target_center_ecc(target: typing.Dict[str, typing.Any]) -> typing.Tuple[float, float]:
  """(x, y) eccentricity of the target's mean position; y is up."""
  radius = range_mid(target.get('radius'))
  angle = math.radians(range_mid(target.get('angle')))
  return radius * math.cos(angle), radius * math.sin(angle)


def polar_from_ecc(x_ecc: float, y_ecc: float) -> typing.Tuple[float, float]:
  """(radius, angle in [0, 360)) degrees."""
  radius = math.hypot(x_ecc, y_ecc)
  angle = math.degrees(math.atan2(y_ecc, x_ecc)) % 360.0 if radius > 1e-9 else 0.0
  return radius, angle


def move_target_to_ecc(target: typing.Dict[str, typing.Any], x_ecc: float, y_ecc: float) -> None:
  """Move a target's mean position, keeping the width of any random range."""
  radius, angle = polar_from_ecc(x_ecc, y_ecc)
  radius = round(radius, 2)
  angle = round(angle, 1)
  r_lo, r_hi = range_bounds(target.get('radius'))
  a_lo, a_hi = range_bounds(target.get('angle'))
  r_half = (r_hi - r_lo) / 2.0
  a_half = (a_hi - a_lo) / 2.0
  target['radius'] = make_range(max(0.0, radius - r_half), max(0.0, radius + r_half))
  target['angle'] = make_range(angle - a_half, angle + a_half)


def screen_center_px(target: typing.Dict[str, typing.Any], dpi: float,
                     width: float, height: float,
                     center_x: float = 0.5, center_y: float = 0.5) -> typing.Tuple[float, float]:
  """Target center on the subject screen, as touch.rs places it (x and y
  eccentricities converted independently, y flipped) around the task center
  at (center_x * width, center_y * height)."""
  x_ecc, y_ecc = target_center_ecc(target)
  return width * center_x + ecc_to_px(x_ecc, dpi), height * center_y - ecc_to_px(y_ecc, dpi)


def annulus_positions(ring_count: int, points_per_ring: int, inner: float, outer: float,
                      angle_offset: float, include_center: bool) -> typing.List[typing.Tuple[float, float]]:
  """(radius, angle) degrees: concentric rings, outer rings get more points."""
  ring_count = max(1, int(ring_count))
  points_per_ring = max(1, int(points_per_ring))
  inner = max(0.0, float(inner))
  outer = max(inner, float(outer))
  radii = [outer] if ring_count <= 1 else [
    inner + (outer - inner) * (i / float(ring_count - 1)) for i in range(ring_count)]
  positions = [(0.0, 0.0)] if include_center else []
  for ring_index, radius in enumerate(radii):
    count = points_per_ring * (ring_index + 1)
    for k in range(count):
      positions.append((radius, (angle_offset + 360.0 * k / count) % 360.0))
  return positions


def grid_positions(rows: int, columns: int, spacing: float, exclusion: float,
                   hexagonal: bool, include_center: bool) -> typing.List[typing.Tuple[float, float]]:
  """(radius, angle) degrees: a grid centered on the screen center."""
  rows = max(1, int(rows))
  columns = max(1, int(columns))
  spacing = max(0.01, float(spacing))
  row_step = spacing * math.sqrt(3.0) / 2.0 if hexagonal else spacing
  positions = []
  for row in range(rows):
    y = (row - (rows - 1) / 2.0) * row_step
    offset = spacing / 2.0 if hexagonal and row % 2 == 1 else 0.0
    for column in range(columns):
      x = (column - (columns - 1) / 2.0) * spacing + offset
      radius, angle = polar_from_ecc(x, y)
      if radius < max(0.0, float(exclusion)):
        continue
      positions.append((round(radius, 3), round(angle, 2)))
  if include_center and not any(r < 1e-6 for r, _ in positions):
    positions.insert(0, (0.0, 0.0))
  return positions


def target_group_key(target: typing.Any) -> str:
  group = str(target.get('group', '')) if isinstance(target, dict) else ''
  return group or GROUPLESS_LABEL


def rebuild_group_toggle_rows(container: QWidget, container_layout: typing.Any,
                              targets_list: typing.Iterable[typing.Dict[str, typing.Any]],
                              on_set_group_enabled: typing.Callable[[str, bool], None]) -> None:
  """One enable/disable checkbox per preset group (hidden when ungrouped)."""
  while container_layout.count():
    item = container_layout.takeAt(0)
    widget = item.widget()
    if widget is not None:
      widget.setParent(None)
      widget.deleteLater()
  groups: typing.Dict[str, typing.List[bool]] = {}
  for target in targets_list:
    groups.setdefault(target_group_key(target), []).append(bool(target.get('enabled', True)))
  has_groups = any(key != GROUPLESS_LABEL for key in groups)
  container.setVisible(has_groups)
  if not has_groups:
    return
  for key in sorted(groups):
    flags = groups[key]
    on_count = sum(1 for flag in flags if flag)
    box = QCheckBox(f'{key} ({on_count}/{len(flags)} on)')
    box.setToolTip(f"Enable or disable every target in group '{key}' at once.")
    box.blockSignals(True)
    box.setChecked(on_count == len(flags))
    box.blockSignals(False)
    box.clicked.connect(lambda checked, group=key: on_set_group_enabled(group, bool(checked)))
    container_layout.addWidget(box)


def color_style(rgb: typing.Sequence[int]) -> str:
  return f'background-color: rgb({int(rgb[0])}, {int(rgb[1])}, {int(rgb[2])});'


def apply_row_tooltips(form: Form, tooltips: typing.Dict[str, str]) -> None:
  """Attach tooltips to every widget in each Form row whose label matches.

  Form has no tooltip support, so walk its grid: column 0 holds the row label.
  """
  grid = form.grid_layout
  for row in range(grid.rowCount()):
    label_item = grid.itemAtPosition(row, 0)
    label = label_item.widget() if label_item is not None else None
    if not isinstance(label, QLabel) or label.text() not in tooltips:
      continue
    text = tooltips[label.text()]
    for column in range(grid.columnCount()):
      item = grid.itemAtPosition(row, column)
      if item is not None and item.widget() is not None:
        item.widget().setToolTip(text)
  # The Min:/Max: headers share row 0 with 'Name:'.
  for column in range(grid.columnCount()):
    item = grid.itemAtPosition(0, column)
    if item is not None and isinstance(item.widget(), QLabel) and item.widget().text() in tooltips:
      item.widget().setToolTip(tooltips[item.widget().text()])


def block_wheel_edits(root: QWidget) -> None:
  """Stop the scroll wheel from changing spin boxes / combos under root."""
  wheel_filter = NoWheelChangeFilter(root)
  root._no_wheel_filter = wheel_filter  # type: ignore[attr-defined]
  for widget in root.findChildren(QSpinBox) + root.findChildren(QDoubleSpinBox) + root.findChildren(QComboBox):
    widget.installEventFilter(wheel_filter)
    # Wheel focus would let a scroll over a focused box still step it.
    widget.setFocusPolicy(Qt.FocusPolicy.StrongFocus)


class NoWheelChangeFilter(QWidget):
  """Scrolling the editor must not change spin boxes / combos under the mouse.

  A QWidget (not QObject) because the repo's qt wrapper does not export
  QObject; joystick_intro's filter does the same.
  """
  def eventFilter(self, watched: typing.Any, event: typing.Any) -> bool:
    if hasattr(event, 'angleDelta') and isinstance(watched, (QSpinBox, QDoubleSpinBox, QComboBox)):
      event.ignore()
      return True
    return super().eventFilter(watched, event)


class TouchLayoutPreview(QWidget):
  """Scaled picture of the subject screen with the targets drawn the way the
  executor draws them: filled target rect, dashed acceptance window, the
  photodiode square, and dotted eccentricity rings every 5 degrees around the
  task center.

  Targets with a random radius/angle range show a faint patch covering every
  position they can land on; dragging moves the range's center.
  """

  def __init__(self, targets_ref: typing.List[typing.Dict[str, typing.Any]],
               generated_getter: typing.Callable[[], typing.List[typing.Dict[str, typing.Any]]],
               screen_getter: typing.Callable[[], typing.Tuple[float, float, float]],
               indicator_getter: typing.Callable[[], typing.Tuple[float, float]],
               center_getter: typing.Callable[[], typing.Tuple[float, float]],
               select_callback: typing.Callable[[int], None],
               update_callback: typing.Callable[[], None],
               parent: typing.Optional[QWidget] = None) -> None:
    super().__init__(parent)
    self.targets_ref = targets_ref
    self.generated_getter = generated_getter
    self.screen_getter = screen_getter
    self.indicator_getter = indicator_getter
    self.center_getter = center_getter
    self.select_callback = select_callback
    self.update_callback = update_callback
    self.selected_index = -1
    self.hovered_index = -1
    self.drag_index = -1
    self.drag_enabled = True
    self.setMinimumSize(420, 260)
    self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
    self.setMouseTracking(True)

  def set_selected_index(self, index: int) -> None:
    self.selected_index = index
    self.update()

  def set_drag_enabled(self, enabled: bool) -> None:
    self.drag_enabled = bool(enabled)
    if not self.drag_enabled:
      self.drag_index = -1
    self.update()

  # --- coordinate mapping: subject-screen pixels <-> widget ---

  def _transform(self) -> typing.Tuple[float, float, float]:
    """(scale, offset_x, offset_y) mapping screen px into the widget."""
    screen_w, screen_h, _ = self.screen_getter()
    margin = 14.0
    avail_w = max(1.0, self.width() - 2.0 * margin)
    avail_h = max(1.0, self.height() - 2.0 * margin - 40.0)
    scale = min(avail_w / screen_w, avail_h / screen_h)
    offset_x = (self.width() - screen_w * scale) / 2.0
    offset_y = 40.0 + (self.height() - 40.0 - screen_h * scale) / 2.0
    return scale, offset_x, offset_y

  def _to_widget(self, x: float, y: float) -> QPointF:
    scale, ox, oy = self._transform()
    return QPointF(ox + x * scale, oy + y * scale)

  def _to_screen(self, pos: QPointF) -> typing.Tuple[float, float]:
    scale, ox, oy = self._transform()
    return (pos.x() - ox) / scale, (pos.y() - oy) / scale

  def _event_pos(self, event: typing.Any) -> QPointF:
    if hasattr(event, 'position'):
      return event.position()
    return QPointF(float(event.x()), float(event.y()))

  def _task_center(self) -> typing.Tuple[float, float]:
    """The 0-eccentricity point in screen px."""
    screen_w, screen_h, _ = self.screen_getter()
    center_x, center_y = self.center_getter()
    return screen_w * center_x, screen_h * center_y

  def _center(self, target: typing.Dict[str, typing.Any]) -> typing.Tuple[float, float]:
    screen_w, screen_h, dpi = self.screen_getter()
    center_x, center_y = self.center_getter()
    return screen_center_px(target, dpi, screen_w, screen_h, center_x, center_y)

  def _hit_test(self, pos: QPointF) -> int:
    _, _, dpi = self.screen_getter()
    scale, _, _ = self._transform()
    best_index, best_distance = -1, None
    for i, target in enumerate(self.targets_ref):
      cx, cy = self._center(target)
      center = self._to_widget(cx, cy)
      half = max(ecc_to_px(float(target.get('width', 0.0)), dpi),
                 ecc_to_px(float(target.get('height', 0.0)), dpi)) / 2.0
      reach = max(half, ecc_to_px(float(target.get('window_size', 0.0)), dpi)) * scale + 6.0
      distance = math.hypot(pos.x() - center.x(), pos.y() - center.y())
      if distance <= reach and (best_distance is None or distance < best_distance):
        best_index, best_distance = i, distance
    return best_index

  def _move_target_to(self, index: int, pos: QPointF) -> None:
    if not 0 <= index < len(self.targets_ref):
      return
    screen_w, screen_h, dpi = self.screen_getter()
    x, y = self._to_screen(pos)
    x = max(0.0, min(screen_w, x))
    y = max(0.0, min(screen_h, y))
    origin_x, origin_y = self._task_center()
    move_target_to_ecc(self.targets_ref[index],
                       px_to_ecc(x - origin_x, dpi), px_to_ecc(origin_y - y, dpi))
    self.update_callback()
    self.update()

  def mousePressEvent(self, event: typing.Any) -> None:
    if event.button() != Qt.MouseButton.LeftButton:
      return super().mousePressEvent(event)
    index = self._hit_test(self._event_pos(event))
    self.drag_index = index if self.drag_enabled else -1
    self.selected_index = index
    self.select_callback(index)
    self.update()

  def mouseMoveEvent(self, event: typing.Any) -> None:
    if self.drag_index >= 0:
      self._move_target_to(self.drag_index, self._event_pos(event))
      return
    hovered = self._hit_test(self._event_pos(event))
    if hovered != self.hovered_index:
      self.hovered_index = hovered
      self.update()
    super().mouseMoveEvent(event)

  def leaveEvent(self, event: typing.Any) -> None:
    if self.hovered_index != -1:
      self.hovered_index = -1
      self.update()
    super().leaveEvent(event)

  def mouseReleaseEvent(self, event: typing.Any) -> None:
    if event.button() == Qt.MouseButton.LeftButton:
      self.drag_index = -1
    super().mouseReleaseEvent(event)

  # --- painting ---

  def _range_patch(self, target: typing.Dict[str, typing.Any]) -> typing.Optional[QPainterPath]:
    """Outline of every center a ranged target can land on."""
    r_lo, r_hi = range_bounds(target.get('radius'))
    a_lo, a_hi = range_bounds(target.get('angle'))
    if r_lo == r_hi and a_lo == a_hi:
      return None
    _, _, dpi = self.screen_getter()
    origin_x, origin_y = self._task_center()
    steps = 24
    boundary = (
      [(r_lo, a_lo + (a_hi - a_lo) * k / steps) for k in range(steps + 1)]
      + [(r_lo + (r_hi - r_lo) * k / steps, a_hi) for k in range(steps + 1)]
      + [(r_hi, a_hi - (a_hi - a_lo) * k / steps) for k in range(steps + 1)]
      + [(r_hi - (r_hi - r_lo) * k / steps, a_lo) for k in range(steps + 1)]
    )
    path = QPainterPath()
    for k, (radius, angle) in enumerate(boundary):
      x_ecc = radius * math.cos(math.radians(angle))
      y_ecc = radius * math.sin(math.radians(angle))
      point = self._to_widget(origin_x + ecc_to_px(x_ecc, dpi), origin_y - ecc_to_px(y_ecc, dpi))
      if k == 0:
        path.moveTo(point)
      else:
        path.lineTo(point)
    path.closeSubpath()
    return path

  def _draw_target(self, painter: QPainter, index: int, target: typing.Dict[str, typing.Any],
                   generated: bool = False) -> None:
    _, _, dpi = self.screen_getter()
    scale, _, _ = self._transform()
    cx, cy = self._center(target)
    center = self._to_widget(cx, cy)
    width_px = ecc_to_px(float(target.get('width', 0.0)), dpi) * scale
    height_px = ecc_to_px(float(target.get('height', 0.0)), dpi) * scale
    window_px = ecc_to_px(float(target.get('window_size', 0.0)), dpi) * scale
    rect = QRectF(center.x() - width_px / 2.0, center.y() - height_px / 2.0, width_px, height_px)
    enabled = bool(target.get('enabled', True))
    rgb = target.get('color', DEFAULT_TARGET_COLOR)

    if generated:
      painter.setPen(QPen(QColor(120, 200, 255, 220), 1, Qt.PenStyle.DashLine))
      painter.setBrush(Qt.BrushStyle.NoBrush)
      painter.drawRect(rect)
      painter.drawEllipse(center, window_px, window_px)
      return

    patch = self._range_patch(target)
    if patch is not None:
      painter.setPen(Qt.PenStyle.NoPen)
      painter.setBrush(QColor(int(rgb[0]), int(rgb[1]), int(rgb[2]), 45 if enabled else 20))
      painter.drawPath(patch)

    # Acceptance window: what the subject must touch inside.
    painter.setBrush(Qt.BrushStyle.NoBrush)
    window_color = QColor(255, 255, 255, 140) if enabled else QColor(150, 150, 150, 90)
    painter.setPen(QPen(window_color, 1, Qt.PenStyle.DashLine))
    painter.drawEllipse(center, window_px, window_px)

    # The target itself, as the subject sees it (filled box).
    if enabled:
      painter.setPen(Qt.PenStyle.NoPen)
      painter.setBrush(QColor(int(rgb[0]), int(rgb[1]), int(rgb[2])))
    else:
      painter.setPen(QPen(QColor(150, 150, 150), 1, Qt.PenStyle.DashLine))
      painter.setBrush(Qt.BrushStyle.NoBrush)
    painter.drawRect(rect)

    is_selected = index == self.selected_index
    if is_selected:
      painter.setPen(QPen(QColor(255, 255, 255), 2, Qt.PenStyle.DashLine))
      painter.setBrush(Qt.BrushStyle.NoBrush)
      halo = max(window_px, max(width_px, height_px) / 2.0) + 6.0
      painter.drawEllipse(center, halo, halo)
    if is_selected or index == self.hovered_index:
      painter.setPen(QPen(QColor(255, 255, 255), 1))
      label = str(target.get('name', '')).strip() or f'T{index + 1}'
      if not enabled:
        label += ' (disabled)'
      painter.drawText(int(center.x() + max(window_px, width_px / 2.0) + 6.0),
                       int(center.y() - max(window_px, height_px / 2.0) - 4.0), label)

  def paintEvent(self, event: typing.Any) -> None:
    painter = QPainter(self)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    painter.fillRect(self.rect(), QColor(24, 24, 24))
    screen_w, screen_h, dpi = self.screen_getter()
    scale, ox, oy = self._transform()

    # Subject screen (black, like the executor's background).
    painter.setPen(QPen(QColor(90, 90, 90), 1))
    painter.setBrush(QColor(0, 0, 0))
    painter.drawRect(QRectF(ox, oy, screen_w * scale, screen_h * scale))
    # Everything on the subject screen stays inside it (the captions below
    # are drawn after restore()).
    painter.save()
    painter.setClipRect(QRectF(ox, oy, screen_w * scale, screen_h * scale))

    # Eccentricity rings every 5 degrees + crosshair, around the task center.
    origin_x, origin_y = self._task_center()
    center = self._to_widget(origin_x, origin_y)
    painter.setBrush(Qt.BrushStyle.NoBrush)
    ring_pen = QPen(QColor(70, 70, 70), 1, Qt.PenStyle.DotLine)
    max_ecc = px_to_ecc(math.hypot(max(origin_x, screen_w - origin_x), max(origin_y, screen_h - origin_y)), dpi)
    degrees = 5
    while degrees <= max_ecc:
      painter.setPen(ring_pen)
      rx = ecc_to_px(degrees, dpi) * scale
      painter.drawEllipse(center, rx, rx)
      painter.setPen(QPen(QColor(110, 110, 110), 1))
      painter.drawText(int(center.x() + rx + 2), int(center.y() - 2), f'{degrees}°')
      degrees += 5
    painter.setPen(QPen(QColor(90, 90, 90), 1))
    painter.drawLine(QPointF(center.x() - 8, center.y()), QPointF(center.x() + 8, center.y()))
    painter.drawLine(QPointF(center.x(), center.y() - 8), QPointF(center.x(), center.y() + 8))

    # Photodiode square: state_indicator_x/y are right/bottom margins, as
    # touch.rs (and joystick_intro) draw it.
    margin_x, margin_y = self.indicator_getter()
    corner = self._to_widget(screen_w - STATE_INDICATOR_EDGE_PX - margin_x,
                             screen_h - STATE_INDICATOR_EDGE_PX - margin_y)
    edge = STATE_INDICATOR_EDGE_PX * scale
    painter.setPen(QPen(QColor(200, 200, 60), 1, Qt.PenStyle.DashLine))
    painter.setBrush(Qt.BrushStyle.NoBrush)
    painter.drawRect(QRectF(corner.x(), corner.y(), edge, edge))

    for i, target in enumerate(self.targets_ref):
      self._draw_target(painter, i, target)
    for target in self.generated_getter():
      self._draw_target(painter, -1, target, generated=True)
    painter.restore()

    painter.setPen(QPen(QColor(200, 200, 200), 1))
    if self.drag_enabled:
      painter.drawText(12, 16, 'Drag targets to move them. Box = target, dashed circle = touch window.')
    else:
      painter.drawText(12, 16, 'Drag move is disabled. Click or hover targets to identify them.')
    painter.drawText(12, 32, f'Subject screen {int(screen_w)}x{int(screen_h)} px @ {dpi:.0f} dpi; '
                             'rings every 5° around the task center (+); yellow square = photodiode; '
                             'shaded = random range.')


class LayoutEditorDialog(PersistedGeometryWindow, QDialog):
  """Non-modal editor window that reopens where the operator left it."""


def create_widget(task_config: ObservableCollection) -> QWidget:
  """Settings form + target table + layout editor for simple_touch_rust."""
  result = QWidget()
  layout = QVBoxLayout(result)

  # One-time switch to joystick-style photodiode placement. Before this key
  # existed, state_indicator_x/y held simple_touch_task's absolute top-left
  # position, which is meaningless as bottom-right margins.
  if task_config.get('state_indicator_anchor') != 'bottom_right':
    task_config['state_indicator_x'], task_config['state_indicator_y'] = DEFAULT_STATE_INDICATOR_MARGINS
    task_config['state_indicator_anchor'] = 'bottom_right'

  form = Form.build(task_config, ['Name:', 'Min:', 'Max:'],
    Form.Uniform('Intertrial Interval', 'intertrial_timeout', 1, 1, 's'),
    Form.Uniform('Start Interval', 'start_timeout', 1, 1, 's'),
    Form.Uniform('Hold Interval', 'hold_timeout', 1, 1, 's'),
    Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Bool('Is Choice', 'is_choice', False),
    Form.Bool('No start = fail?', 'no_start_fail', False),
    Form.Constant('Task Center X (fraction of width)', 'center_x', 0.5, precision=3),
    Form.Constant('Task Center Y (fraction of height, 0 = top)', 'center_y', 0.5, precision=3),
    Form.Constant('State Indicator Right Margin', 'state_indicator_x', DEFAULT_STATE_INDICATOR_MARGINS[0], precision=0),
    Form.Constant('State Indicator Bottom Margin', 'state_indicator_y', DEFAULT_STATE_INDICATOR_MARGINS[1], precision=0),
    Form.String('Touch Node', 'touch_node', 'Node 3'),
  )
  apply_row_tooltips(form, SETTINGS_TOOLTIPS)
  layout.addWidget(form)

  if 'targets' not in task_config:
    task_config['targets'] = []
  targets = task_config['targets']
  if 'layout_preview_screen' not in task_config:
    task_config['layout_preview_screen'] = dict(DEFAULT_PREVIEW_SCREEN)

  def indicator_margins() -> typing.Tuple[float, float]:
    return (float(task_config.get('state_indicator_x', DEFAULT_STATE_INDICATOR_MARGINS[0])),
            float(task_config.get('state_indicator_y', DEFAULT_STATE_INDICATOR_MARGINS[1])))

  def task_center() -> typing.Tuple[float, float]:
    def fraction(key: str) -> float:
      try:
        return min(1.0, max(0.0, float(task_config.get(key, 0.5))))
      except (TypeError, ValueError):
        return 0.5
    return fraction('center_x'), fraction('center_y')

  # --- target table ---

  target_table = QTableWidget()
  target_table.setColumnCount(len(TABLE_COLUMNS))
  target_table.setHorizontalHeaderLabels(TABLE_COLUMNS)
  target_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
  target_table.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
  target_table.horizontalHeader().setStretchLastSection(True)
  target_table.verticalHeader().setVisible(False)
  target_table.setMinimumHeight(190)
  target_table.setToolTip(
    'Each row is a target. Radius/Angle accept a fixed value (5) or a random '
    'range (0..360). Multi-select rows to remove several at once.')

  def write_table_row(row: int, target: typing.Dict[str, typing.Any]) -> None:
    enabled_item = QTableWidgetItem('')
    enabled_item.setFlags(Qt.ItemFlag.ItemIsSelectable | Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsUserCheckable)
    enabled_item.setCheckState(Qt.CheckState.Checked if target['enabled'] else Qt.CheckState.Unchecked)
    target_table.setItem(row, COL_ENABLED, enabled_item)
    target_table.setItem(row, COL_NAME, QTableWidgetItem(str(target['name'])))
    target_table.setItem(row, COL_RADIUS, QTableWidgetItem(format_range(target['radius'])))
    target_table.setItem(row, COL_ANGLE, QTableWidgetItem(format_range(target['angle'])))
    target_table.setItem(row, COL_WIDTH, QTableWidgetItem(_fmt(target['width'])))
    target_table.setItem(row, COL_HEIGHT, QTableWidgetItem(_fmt(target['height'])))
    target_table.setItem(row, COL_WINDOW, QTableWidgetItem(_fmt(target['window_size'])))
    target_table.setItem(row, COL_REWARD, QTableWidgetItem(str(int(target['reward_channel']))))

    button = QPushButton()
    button.setStyleSheet(color_style(target['color']))

    def on_pick_color() -> None:
      if not 0 <= row < len(targets):
        return
      normalized = normalize_target(targets[row])
      selected = QColorDialog.getColor(QColor(*normalized['color']), result, 'Select Target Color')
      if selected.isValid():
        normalized['color'] = [selected.red(), selected.green(), selected.blue()]
        targets[row] = normalized
        button.setStyleSheet(color_style(normalized['color']))

    button.clicked.connect(on_pick_color)
    target_table.setCellWidget(row, COL_COLOR, button)

  def sync_table_from_config() -> None:
    target_table.blockSignals(True)
    target_table.setRowCount(0)
    for i, raw_target in enumerate(list(targets)):
      target = normalize_target(raw_target)
      if target != plain(raw_target):
        targets[i] = target
      target_table.insertRow(i)
      write_table_row(i, target)
    target_table.blockSignals(False)
    refresh_group_controls()

  def sync_row_to_config(row: int) -> None:
    if not 0 <= row < len(targets):
      return
    items = [target_table.item(row, col) for col in range(COL_COLOR)]
    if not all(items):
      return
    normalized = normalize_target(targets[row])
    try:
      normalized['radius'] = parse_range(items[COL_RADIUS].text())
      normalized['angle'] = parse_range(items[COL_ANGLE].text())
      normalized['width'] = float(items[COL_WIDTH].text())
      normalized['height'] = float(items[COL_HEIGHT].text())
      normalized['window_size'] = float(items[COL_WINDOW].text())
      normalized['reward_channel'] = int(float(items[COL_REWARD].text()))
    except ValueError:
      # Bad text: put the stored value back.
      target_table.blockSignals(True)
      write_table_row(row, normalize_target(targets[row]))
      target_table.blockSignals(False)
      return
    normalized['name'] = items[COL_NAME].text()
    normalized['enabled'] = items[COL_ENABLED].checkState() == Qt.CheckState.Checked
    normalized = normalize_target(normalized)
    targets[row] = normalized
    target_table.blockSignals(True)
    write_table_row(row, normalized)
    target_table.blockSignals(False)
    refresh_group_controls()

  target_table.itemChanged.connect(lambda item: sync_row_to_config(item.row()))

  def selected_rows() -> typing.List[int]:
    return sorted({index.row() for index in target_table.selectedIndexes()})

  def add_target() -> None:
    rows = selected_rows()
    if rows and rows[0] < len(targets):
      new_target = normalize_target(targets[rows[0]])
      new_target['name'] = f"{new_target['name'].strip() or 'Target'} Copy"
    else:
      new_target = make_default_target(len(targets) + 1)
    targets.append(new_target)
    sync_table_from_config()
    target_table.selectRow(target_table.rowCount() - 1)

  def remove_target() -> None:
    rows = sorted(selected_rows(), reverse=True)
    if not rows and targets:
      rows = [len(targets) - 1]
    for row in rows:
      if 0 <= row < len(targets):
        del targets[row]
    sync_table_from_config()

  def clear_targets() -> None:
    while targets:
      del targets[len(targets) - 1]
    sync_table_from_config()

  def set_enabled_where(predicate: typing.Callable[[typing.Dict[str, typing.Any]], bool], value: bool) -> None:
    for i in range(len(targets)):
      target = normalize_target(targets[i])
      if predicate(target):
        target['enabled'] = value
        targets[i] = target
    sync_table_from_config()

  def apply_selected_field_to_all() -> None:
    rows = selected_rows()
    if not rows or rows[0] >= len(targets):
      return
    source = normalize_target(targets[rows[0]])
    key = {
      'Enabled': 'enabled', 'Name': 'name', 'Radius': 'radius', 'Angle': 'angle',
      'Width': 'width', 'Height': 'height', 'Window': 'window_size',
      'Reward Channel': 'reward_channel', 'Color': 'color',
    }[bulk_field_combo.currentText()]
    for i in range(len(targets)):
      target = normalize_target(targets[i])
      target[key] = copy.deepcopy(source[key])
      targets[i] = target
    sync_table_from_config()
    target_table.selectRow(rows[0])

  controls = QWidget()
  controls_layout = QGridLayout(controls)
  controls_layout.setContentsMargins(0, 0, 0, 0)
  add_target_button = QPushButton('Add Target')
  remove_target_button = QPushButton('Remove Selected')
  clear_targets_button = QPushButton('Clear All')
  enable_all_button = QPushButton('Enable All')
  disable_all_button = QPushButton('Disable All')
  edit_layout_button = QPushButton('Edit Layout...')
  bulk_field_combo = QComboBox()
  bulk_field_combo.addItems(BULK_FIELDS)
  bulk_field_combo.setCurrentText('Window')
  apply_to_all_button = QPushButton('Apply Field to All')
  add_target_button.setToolTip('Add a target. If a row is selected, the new target copies it.')
  remove_target_button.setToolTip('Remove all selected rows.')
  clear_targets_button.setToolTip('Remove every target.')
  enable_all_button.setToolTip('Enable every target (nothing is removed).')
  disable_all_button.setToolTip('Disable every target (nothing is removed). Disabled targets are never shown.')
  edit_layout_button.setToolTip('Open the visual layout editor: drag targets on a picture of the subject screen, generate rings/grids, presets.')
  bulk_field_combo.setToolTip('Which field of the selected row to copy to every target.')
  apply_to_all_button.setToolTip('Copy the chosen field from the selected row to every target.')
  for i, widget in enumerate([
    add_target_button, remove_target_button, clear_targets_button,
    enable_all_button, disable_all_button, edit_layout_button,
    QLabel('Field:'), bulk_field_combo, apply_to_all_button,
  ]):
    controls_layout.addWidget(widget, i // 3, i % 3)
  controls_layout.setColumnStretch(3, 1)

  # Live group toggles: flipping a preset group edits the running config, so
  # it reaches the executor on the next trial.
  target_groups_box = QGroupBox('Target Groups')
  target_groups_box.setToolTip('One checkbox per preset group. Takes effect on the next trial.')
  target_groups_layout = QHBoxLayout(target_groups_box)
  target_groups_layout.setContentsMargins(6, 4, 6, 4)
  target_groups_box.setVisible(False)

  def refresh_group_controls() -> None:
    rebuild_group_toggle_rows(
      target_groups_box, target_groups_layout, [normalize_target(t) for t in targets],
      lambda group, value: set_enabled_where(lambda t: target_group_key(t) == group, value))

  add_target_button.clicked.connect(add_target)
  remove_target_button.clicked.connect(remove_target)
  clear_targets_button.clicked.connect(clear_targets)
  enable_all_button.clicked.connect(lambda: set_enabled_where(lambda t: True, True))
  disable_all_button.clicked.connect(lambda: set_enabled_where(lambda t: True, False))
  apply_to_all_button.clicked.connect(apply_selected_field_to_all)
  edit_layout_button.clicked.connect(lambda: open_layout_editor())

  layout.addWidget(QLabel('Targets (rows = targets; positions are degrees from screen center):'))
  layout.addWidget(target_table)
  layout.addWidget(controls)
  layout.addWidget(target_groups_box)

  # --- layout editor ---

  def open_layout_editor() -> None:
    existing = getattr(result, '_layout_editor_dialog', None)
    if isinstance(existing, QDialog):
      existing.show()
      existing.raise_()
      existing.activateWindow()
      return

    dialog = LayoutEditorDialog(result, Qt.WindowType.Window)
    dialog.setWindowTitle('Simple Touch Target Layout Editor')
    dialog.setModal(False)
    dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose, True)
    dialog.restore_geometry_from_config(task_config, 'touch_layout_editor_geometry', [120, 120, 1100, 640])
    result._layout_editor_dialog = dialog  # type: ignore[attr-defined]

    def forget_dialog(*_args: typing.Any) -> None:
      if getattr(result, '_layout_editor_dialog', None) is dialog:
        result._layout_editor_dialog = None  # type: ignore[attr-defined]

    dialog.destroyed.connect(forget_dialog)

    draft_targets = [normalize_target(target) for target in list(targets)]
    pending_generated: typing.List[typing.Dict[str, typing.Any]] = []
    pending_operation = 'append'
    selected_index = 0 if draft_targets else -1
    draft_screen = dict(plain(task_config.get('layout_preview_screen', DEFAULT_PREVIEW_SCREEN)))

    def draft_preview_screen() -> typing.Tuple[float, float, float]:
      width = max(100.0, float(draft_screen.get('width', 1920)))
      height = max(100.0, float(draft_screen.get('height', 1080)))
      dpi = float(draft_screen.get('dpi', 0) or 0) or float(result.logicalDpiX() or 96)
      return width, height, dpi

    dialog_layout = QHBoxLayout(dialog)
    dialog_layout.setContentsMargins(8, 8, 8, 8)
    side_panel = QWidget(dialog)
    side_layout = QVBoxLayout(side_panel)
    side_layout.setContentsMargins(0, 0, 0, 0)
    side_layout.setSpacing(5)
    side_panel.setMinimumWidth(270)
    side_panel.setMaximumWidth(350)
    side_panel.setStyleSheet(
      'QWidget { font-size: 11px; }'
      'QGroupBox { margin-top: 8px; padding-top: 8px; }'
      'QGroupBox::title { subcontrol-origin: margin; left: 6px; padding: 0 2px; }'
      'QPushButton { min-height: 20px; padding: 1px 5px; }'
    )

    target_list = QListWidget()
    target_list.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    target_list.setMaximumHeight(130)
    side_layout.addWidget(QLabel('Targets'))
    side_layout.addWidget(target_list)

    preset_combo = QComboBox()
    preset_save_button = QPushButton('Save')
    preset_load_button = QPushButton('Load')
    preset_append_button = QPushButton('Append')
    preset_delete_button = QPushButton('Delete')
    preset_combo.setToolTip('Saved target layouts.')
    preset_save_button.setToolTip('Save the draft targets as a named preset.')
    preset_load_button.setToolTip('Replace the draft with the selected preset.')
    preset_append_button.setToolTip("Add the selected preset's targets to the draft (tagged with the preset's name as their group).")
    preset_delete_button.setToolTip('Delete the selected preset.')
    preset_row = QWidget()
    preset_row_layout = QHBoxLayout(preset_row)
    preset_row_layout.setContentsMargins(0, 0, 0, 0)
    preset_row_layout.addWidget(preset_combo, 1)
    for button in (preset_save_button, preset_load_button, preset_append_button, preset_delete_button):
      preset_row_layout.addWidget(button)
    side_layout.addWidget(QLabel('Presets'))
    side_layout.addWidget(preset_row)

    group_panel_box = QGroupBox('Groups')
    group_panel_layout = QVBoxLayout(group_panel_box)
    group_panel_layout.setContentsMargins(6, 4, 6, 4)
    group_panel_layout.setSpacing(2)
    group_panel_box.setVisible(False)
    side_layout.addWidget(group_panel_box)

    def make_spin(lo: float, hi: float, decimals: int, step: float, tip: str) -> QDoubleSpinBox:
      spin = QDoubleSpinBox()
      spin.setRange(lo, hi)
      spin.setDecimals(decimals)
      spin.setSingleStep(step)
      spin.setKeyboardTracking(False)
      spin.setToolTip(tip)
      return spin

    def make_int_spin(lo: int, hi: int, value: int, tip: str) -> QSpinBox:
      spin = QSpinBox()
      spin.setRange(lo, hi)
      spin.setValue(value)
      spin.setToolTip(tip)
      return spin

    def make_tab(*panels: QWidget) -> QScrollArea:
      body = QWidget()
      body_layout = QVBoxLayout(body)
      body_layout.setContentsMargins(0, 0, 0, 0)
      for panel in panels:
        body_layout.addWidget(panel)
      body_layout.addStretch(1)
      scroll = QScrollArea(side_panel)
      scroll.setWidgetResizable(True)
      scroll.setFrameShape(QFrame.Shape.NoFrame)
      scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
      scroll.setWidget(body)
      return scroll

    # Selected-target form.
    form_panel = QGroupBox('Selected Target')
    form_layout = QFormLayout(form_panel)
    form_layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
    name_edit = QLineEdit()
    enabled_box = QCheckBox('Enabled')
    radius_min_spin = make_spin(0.0, 80.0, 2, 0.5, 'Radius (eccentricity) lower bound, degrees.')
    radius_max_spin = make_spin(0.0, 80.0, 2, 0.5, 'Radius upper bound. Equal to min = fixed radius.')
    angle_min_spin = make_spin(-360.0, 720.0, 1, 5.0, 'Angle lower bound, degrees counter-clockwise from +x (right).')
    angle_max_spin = make_spin(-360.0, 720.0, 1, 5.0, 'Angle upper bound. Equal to min = fixed angle.')
    width_spin = make_spin(0.0, 80.0, 2, 0.5, 'Target box width, degrees.')
    height_spin = make_spin(0.0, 80.0, 2, 0.5, 'Target box height, degrees.')
    window_spin = make_spin(0.0, 80.0, 2, 0.5, 'Touch acceptance window radius around the target center, degrees.')
    reward_channel_spin = QSpinBox()
    reward_channel_spin.setRange(0, 255)
    reward_channel_spin.setKeyboardTracking(False)
    reward_channel_spin.setToolTip('Reward schedule channel used when this target succeeds.')
    color_button = QPushButton('Choose Color')
    move_up_button = QPushButton('Move Up')
    move_down_button = QPushButton('Move Down')
    move_up_button.setToolTip('Earlier in the list: its index (presented_target_ids) and its priority when touch windows overlap in choice mode.')
    move_down_button.setToolTip('Later in the list.')
    name_edit.setToolTip('Name shown in the target list and preview.')
    enabled_box.setToolTip('Disabled targets stay in the list but are never presented (Rust executor only).')

    def pair(left: QWidget, right: QWidget) -> QWidget:
      row = QWidget()
      row_layout = QHBoxLayout(row)
      row_layout.setContentsMargins(0, 0, 0, 0)
      row_layout.addWidget(left)
      row_layout.addWidget(right)
      return row

    form_layout.addRow('Name', name_edit)
    form_layout.addRow('', enabled_box)
    form_layout.addRow('Radius min/max (°)', pair(radius_min_spin, radius_max_spin))
    form_layout.addRow('Angle min/max (°)', pair(angle_min_spin, angle_max_spin))
    form_layout.addRow('Width (°)', width_spin)
    form_layout.addRow('Height (°)', height_spin)
    form_layout.addRow('Window (°)', window_spin)
    form_layout.addRow('Reward Channel', reward_channel_spin)
    form_layout.addRow('Color', color_button)
    form_layout.addRow('Order', pair(move_up_button, move_down_button))
    target_fields = (name_edit, enabled_box, radius_min_spin, radius_max_spin, angle_min_spin, angle_max_spin,
                     width_spin, height_spin, window_spin, reward_channel_spin)

    screen_panel = QGroupBox('Preview Screen')
    screen_layout = QFormLayout(screen_panel)
    screen_width_spin = make_int_spin(100, 10000, int(draft_screen.get('width', 1920)), 'Subject display width in pixels (preview only).')
    screen_height_spin = make_int_spin(100, 10000, int(draft_screen.get('height', 1080)), 'Subject display height in pixels (preview only).')
    screen_dpi_spin = make_int_spin(0, 1000, int(draft_screen.get('dpi', 0) or 0),
      "DPI for degree->pixel conversion in the preview. 0 = this machine's logical DPI "
      "(what the task uses when the global config has no 'dpi').")
    screen_dpi_spin.setSpecialValueText('auto')
    enable_drag_box = QCheckBox('Enable drag move')
    enable_drag_box.setChecked(bool(task_config.get('touch_layout_editor_drag_enabled', True)))
    enable_drag_box.setToolTip('Turn off to click targets in the preview without moving them.')
    screen_layout.addRow('Width (px)', screen_width_spin)
    screen_layout.addRow('Height (px)', screen_height_spin)
    screen_layout.addRow('DPI', screen_dpi_spin)
    screen_layout.addRow('', enable_drag_box)

    # Generator.
    generator_panel = QGroupBox('Target Generator')
    generator_layout = QFormLayout(generator_panel)
    generator_layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
    generator_mode_combo = QComboBox()
    generator_mode_combo.addItem('Annulus / Rings', 'annulus')
    generator_mode_combo.addItem('Rectangular Grid', 'grid')
    generator_mode_combo.addItem('Hexagonal Packing', 'hex')
    generator_operation_combo = QComboBox()
    generator_operation_combo.addItem('Append', 'append')
    generator_operation_combo.addItem('Replace', 'replace')
    name_prefix_edit = QLineEdit('Target')
    include_center_box = QCheckBox('Include a center target')
    ring_count_spin = make_int_spin(1, 20, 1, 'Number of concentric rings.')
    points_per_ring_spin = make_int_spin(1, 64, 8, 'Targets on the first ring; ring k gets k times this many.')
    inner_radius_spin = make_spin(0.0, 80.0, 2, 0.5, 'Innermost ring eccentricity, degrees.')
    inner_radius_spin.setValue(10.0)
    outer_radius_spin = make_spin(0.0, 80.0, 2, 0.5, 'Outermost ring eccentricity, degrees.')
    outer_radius_spin.setValue(10.0)
    angle_offset_spin = make_spin(0.0, 360.0, 1, 5.0, 'Rotate the rings, degrees.')
    rows_spin = make_int_spin(1, 50, 3, 'Grid rows.')
    columns_spin = make_int_spin(1, 50, 3, 'Grid columns.')
    spacing_spin = make_spin(0.1, 80.0, 2, 0.5, 'Distance between neighboring grid targets, degrees.')
    spacing_spin.setValue(10.0)
    exclusion_spin = make_spin(0.0, 80.0, 2, 0.5, 'Skip grid points closer than this to the center, degrees.')
    style_source_combo = QComboBox()
    style_source_combo.addItem('Explicit style below', 'explicit')
    style_source_combo.addItem('Inherit from selected target', 'inherit')
    style_source_combo.setToolTip('Explicit: generated targets use the size/window/reward/color below. Inherit: copy them from the selected (or first) target.')
    gen_enabled_box = QCheckBox('Enabled')
    gen_enabled_box.setChecked(True)
    gen_width_spin = make_spin(0.0, 80.0, 2, 0.5, 'Width of every generated target, degrees.')
    gen_width_spin.setValue(3.0)
    gen_height_spin = make_spin(0.0, 80.0, 2, 0.5, 'Height of every generated target, degrees.')
    gen_height_spin.setValue(3.0)
    gen_window_spin = make_spin(0.0, 80.0, 2, 0.5, 'Touch window of every generated target, degrees.')
    gen_window_spin.setValue(8.0)
    gen_reward_channel_spin = make_int_spin(0, 255, 0, 'Reward channel of every generated target.')
    gen_color = list(DEFAULT_TARGET_COLOR)
    gen_color_button = QPushButton('Choose Color')
    generator_hint = QLabel('')
    generator_hint.setWordWrap(True)
    preview_generator_button = QPushButton('Preview')
    apply_preview_button = QPushButton('Apply Preview')
    clear_preview_button = QPushButton('Clear Preview')
    preview_generator_button.setToolTip('Show the generated targets (dashed blue) without changing the draft.')
    apply_preview_button.setToolTip('Add (or replace with) the previewed targets.')
    clear_preview_button.setToolTip('Discard the preview.')

    generator_layout.addRow('Mode', generator_mode_combo)
    generator_layout.addRow('Operation', generator_operation_combo)
    generator_layout.addRow('Name Prefix', name_prefix_edit)
    generator_layout.addRow('', include_center_box)
    generator_layout.addRow('Ring Count', ring_count_spin)
    generator_layout.addRow('Base Points / Ring', points_per_ring_spin)
    generator_layout.addRow('Inner Radius (°)', inner_radius_spin)
    generator_layout.addRow('Outer Radius (°)', outer_radius_spin)
    generator_layout.addRow('Angle Offset (°)', angle_offset_spin)
    generator_layout.addRow('Rows', rows_spin)
    generator_layout.addRow('Columns', columns_spin)
    generator_layout.addRow('Spacing (°)', spacing_spin)
    generator_layout.addRow('Center Exclusion (°)', exclusion_spin)
    generator_layout.addRow('Style Source', style_source_combo)
    generator_layout.addRow(QLabel('Generated Target Style'))
    generator_layout.addRow('', gen_enabled_box)
    generator_layout.addRow('Width (°)', gen_width_spin)
    generator_layout.addRow('Height (°)', gen_height_spin)
    generator_layout.addRow('Window (°)', gen_window_spin)
    generator_layout.addRow('Reward Channel', gen_reward_channel_spin)
    generator_layout.addRow('Color', gen_color_button)
    generator_layout.addRow('', generator_hint)
    generator_buttons = QWidget()
    generator_buttons_layout = QHBoxLayout(generator_buttons)
    generator_buttons_layout.setContentsMargins(0, 0, 0, 0)
    for button in (preview_generator_button, apply_preview_button, clear_preview_button):
      generator_buttons_layout.addWidget(button)
    generator_layout.addRow('', generator_buttons)

    settings_tabs = QTabWidget(side_panel)
    settings_tabs.addTab(make_tab(form_panel, screen_panel), 'Target')
    settings_tabs.addTab(make_tab(generator_panel), 'Generator')
    side_layout.addWidget(settings_tabs, 1)

    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')
    clear_all_button = QPushButton('Clear All')
    enable_all_layout_button = QPushButton('Enable All')
    disable_all_layout_button = QPushButton('Disable All')
    save_button = QPushButton('Save')
    cancel_button = QPushButton('Cancel')
    add_button.setToolTip('Copy the selected target, or add a default target if none is selected.')
    remove_button.setToolTip('Remove every selected target from the draft.')
    save_button.setToolTip('Write the draft targets back to the task config.')
    cancel_button.setToolTip('Close without saving the draft.')
    edit_row = QWidget()
    edit_row_layout = QHBoxLayout(edit_row)
    edit_row_layout.setContentsMargins(0, 0, 0, 0)
    for button in (add_button, remove_button, clear_all_button):
      edit_row_layout.addWidget(button)
    edit_row_layout.addStretch(1)
    action_row = QWidget()
    action_row_layout = QHBoxLayout(action_row)
    action_row_layout.setContentsMargins(0, 0, 0, 0)
    action_row_layout.addWidget(enable_all_layout_button)
    action_row_layout.addWidget(disable_all_layout_button)
    action_row_layout.addStretch(1)
    action_row_layout.addWidget(save_button)
    action_row_layout.addWidget(cancel_button)
    side_layout.addWidget(edit_row)
    side_layout.addWidget(action_row)

    def has_selection() -> bool:
      return 0 <= selected_index < len(draft_targets)

    def refresh_target_list() -> None:
      target_list.blockSignals(True)
      target_list.clear()
      for i, target in enumerate(draft_targets):
        label = f"{i}: {str(target.get('name', '')).strip() or f'Target {i + 1}'}"
        if not target.get('enabled', True):
          label += ' (disabled)'
        target_list.addItem(label)
      if has_selection():
        target_list.setCurrentRow(selected_index)
      target_list.blockSignals(False)

    def populate_controls() -> None:
      enabled = has_selection()
      for widget in target_fields + (color_button, remove_button, move_up_button, move_down_button):
        widget.setEnabled(enabled)
      move_up_button.setEnabled(enabled and selected_index > 0)
      move_down_button.setEnabled(enabled and selected_index < len(draft_targets) - 1)
      for widget in target_fields:
        widget.blockSignals(True)
      if enabled:
        target = draft_targets[selected_index]
        r_lo, r_hi = range_bounds(target['radius'])
        a_lo, a_hi = range_bounds(target['angle'])
        name_edit.setText(str(target['name']))
        enabled_box.setChecked(bool(target['enabled']))
        radius_min_spin.setValue(r_lo)
        radius_max_spin.setValue(r_hi)
        angle_min_spin.setValue(a_lo)
        angle_max_spin.setValue(a_hi)
        width_spin.setValue(float(target['width']))
        height_spin.setValue(float(target['height']))
        window_spin.setValue(float(target['window_size']))
        reward_channel_spin.setValue(int(target['reward_channel']))
        color_button.setStyleSheet(color_style(target['color']))
      else:
        name_edit.setText('')
        color_button.setStyleSheet('')
      for widget in target_fields:
        widget.blockSignals(False)
      preview.set_selected_index(selected_index if enabled else -1)

    def refresh_editor() -> None:
      refresh_target_list()
      populate_controls()
      rebuild_group_toggle_rows(group_panel_box, group_panel_layout, draft_targets, set_draft_group_enabled)
      preview.update()

    def set_draft_group_enabled(group: str, value: bool) -> None:
      for target in draft_targets:
        if target_group_key(target) == group:
          target['enabled'] = value
      refresh_editor()

    def select_index(index: int) -> None:
      nonlocal selected_index
      selected_index = index if 0 <= index < len(draft_targets) else -1
      populate_controls()
      if has_selection():
        target_list.blockSignals(True)
        target_list.setCurrentRow(selected_index)
        target_list.blockSignals(False)

    def apply_field_changes() -> None:
      if not has_selection():
        return
      r_lo, r_hi = sorted((radius_min_spin.value(), radius_max_spin.value()))
      a_lo, a_hi = sorted((angle_min_spin.value(), angle_max_spin.value()))
      target = draft_targets[selected_index]
      target.update({
        'name': name_edit.text(),
        'enabled': enabled_box.isChecked(),
        'radius': make_range(r_lo, r_hi),
        'angle': make_range(a_lo, a_hi),
        'width': width_spin.value(),
        'height': height_spin.value(),
        'window_size': window_spin.value(),
        'reward_channel': reward_channel_spin.value(),
      })
      clear_generated_preview()
      refresh_editor()

    def choose_color() -> None:
      if not has_selection():
        return
      target = draft_targets[selected_index]
      selected = QColorDialog.getColor(QColor(*target['color']), dialog, 'Select Target Color')
      if selected.isValid():
        target['color'] = [selected.red(), selected.green(), selected.blue()]
        refresh_editor()

    def choose_gen_color() -> None:
      selected = QColorDialog.getColor(QColor(*gen_color), dialog, 'Select Generated Target Color')
      if selected.isValid():
        gen_color[:] = [selected.red(), selected.green(), selected.blue()]
        gen_color_button.setStyleSheet(color_style(gen_color))

    def move_selected(delta: int) -> None:
      nonlocal selected_index
      destination = selected_index + delta
      if not (has_selection() and 0 <= destination < len(draft_targets)):
        return
      draft_targets[selected_index], draft_targets[destination] = draft_targets[destination], draft_targets[selected_index]
      selected_index = destination
      refresh_editor()

    def add_layout_target() -> None:
      nonlocal selected_index
      if has_selection():
        new_target = normalize_target(draft_targets[selected_index])
        new_target['name'] = f"{new_target['name'].strip() or 'Target'} Copy"
      else:
        new_target = make_default_target(len(draft_targets) + 1)
      draft_targets.append(new_target)
      selected_index = len(draft_targets) - 1
      clear_generated_preview()
      refresh_editor()

    def remove_layout_targets() -> None:
      nonlocal selected_index
      rows = sorted({index.row() for index in target_list.selectedIndexes()}, reverse=True)
      if not rows and has_selection():
        rows = [selected_index]
      for row in rows:
        if 0 <= row < len(draft_targets):
          del draft_targets[row]
      selected_index = min(selected_index, len(draft_targets) - 1) if draft_targets else -1
      clear_generated_preview()
      refresh_editor()

    def clear_all_layout_targets() -> None:
      nonlocal selected_index
      draft_targets.clear()
      selected_index = -1
      clear_generated_preview()
      refresh_editor()

    def set_all_layout_enabled(value: bool) -> None:
      for target in draft_targets:
        target['enabled'] = value
      refresh_editor()

    def on_screen_changed() -> None:
      draft_screen.update({
        'width': screen_width_spin.value(),
        'height': screen_height_spin.value(),
        'dpi': screen_dpi_spin.value(),
      })
      preview.update()

    def on_drag_enabled_changed(value: bool) -> None:
      task_config['touch_layout_editor_drag_enabled'] = bool(value)
      preview.set_drag_enabled(value)

    # --- generator ---

    def update_generator_controls() -> None:
      mode = str(generator_mode_combo.currentData())
      is_annulus = mode == 'annulus'
      for widget in (ring_count_spin, points_per_ring_spin, inner_radius_spin, outer_radius_spin, angle_offset_spin):
        widget.setEnabled(is_annulus)
      for widget in (rows_spin, columns_spin, spacing_spin, exclusion_spin):
        widget.setEnabled(not is_annulus)
      explicit = str(style_source_combo.currentData()) == 'explicit'
      for widget in (gen_enabled_box, gen_width_spin, gen_height_spin, gen_window_spin, gen_reward_channel_spin, gen_color_button):
        widget.setEnabled(explicit)
      generator_hint.setText({
        'annulus': 'Rings of targets at fixed eccentricities around the screen center.',
        'hex': 'Hexagonally packed grid centered on the screen; odd rows are offset by half the spacing.',
      }.get(mode, 'Rectangular grid centered on the screen.'))

    def generator_template() -> typing.Dict[str, typing.Any]:
      if str(style_source_combo.currentData()) == 'explicit':
        return normalize_target({
          'enabled': gen_enabled_box.isChecked(),
          'width': gen_width_spin.value(),
          'height': gen_height_spin.value(),
          'window_size': gen_window_spin.value(),
          'reward_channel': gen_reward_channel_spin.value(),
          'color': list(gen_color),
        })
      if has_selection():
        return normalize_target(draft_targets[selected_index])
      if draft_targets:
        return normalize_target(draft_targets[0])
      return make_default_target(1)

    def build_generated_targets() -> typing.List[typing.Dict[str, typing.Any]]:
      mode = str(generator_mode_combo.currentData())
      if mode == 'annulus':
        positions = annulus_positions(
          ring_count_spin.value(), points_per_ring_spin.value(), inner_radius_spin.value(),
          outer_radius_spin.value(), angle_offset_spin.value(), include_center_box.isChecked())
      else:
        positions = grid_positions(
          rows_spin.value(), columns_spin.value(), spacing_spin.value(), exclusion_spin.value(),
          mode == 'hex', include_center_box.isChecked())
      template = generator_template()
      prefix = name_prefix_edit.text().strip() or 'Target'
      start = 1 if str(generator_operation_combo.currentData()) == 'replace' else len(draft_targets) + 1
      generated = []
      for i, (radius, angle) in enumerate(positions):
        target = normalize_target(template)
        target['name'] = f'{prefix} {start + i}'
        target['group'] = ''
        target['radius'] = make_range(radius)
        target['angle'] = make_range(angle)
        generated.append(target)
      return generated

    def preview_generated_targets() -> None:
      nonlocal pending_operation
      generated = build_generated_targets()
      if not generated:
        QMessageBox.warning(dialog, 'No Targets Generated', 'These settings produced no targets. Lower the center exclusion.')
        return
      pending_generated[:] = generated
      pending_operation = str(generator_operation_combo.currentData())
      update_preview_buttons()
      preview.update()

    def clear_generated_preview() -> None:
      pending_generated.clear()
      update_preview_buttons()
      preview.update()

    def apply_generated_preview() -> None:
      nonlocal selected_index
      if not pending_generated:
        return
      if pending_operation == 'replace':
        draft_targets[:] = pending_generated
      else:
        draft_targets.extend(pending_generated)
      pending_generated.clear()
      selected_index = len(draft_targets) - 1 if draft_targets else -1
      update_preview_buttons()
      refresh_editor()

    def update_preview_buttons() -> None:
      apply_preview_button.setEnabled(bool(pending_generated))
      clear_preview_button.setEnabled(bool(pending_generated))

    # --- presets (same storage shape as joystick_intro: {"targets": [...]}) ---

    def load_presets() -> typing.Dict[str, typing.Any]:
      raw = plain(task_config.get('target_layout_presets', {}))
      return raw if isinstance(raw, dict) else {}

    def preset_targets(name: str) -> typing.Optional[typing.List[typing.Dict[str, typing.Any]]]:
      stored = load_presets().get(name)
      stored = stored.get('targets') if isinstance(stored, dict) else stored
      if not isinstance(stored, list):
        return None
      tagged = []
      for raw in stored:
        target = normalize_target(raw)
        if not target['group']:
          target['group'] = name
        tagged.append(target)
      return tagged

    def refresh_preset_combo(select_name: typing.Optional[str] = None) -> None:
      preset_combo.blockSignals(True)
      preset_combo.clear()
      for name in sorted(load_presets()):
        preset_combo.addItem(str(name))
      if select_name is not None and preset_combo.findText(select_name) >= 0:
        preset_combo.setCurrentIndex(preset_combo.findText(select_name))
      preset_combo.blockSignals(False)
      has_presets = preset_combo.count() > 0
      for button in (preset_load_button, preset_append_button, preset_delete_button):
        button.setEnabled(has_presets)

    def save_preset() -> None:
      name, ok = QInputDialog.getText(dialog, 'Save Preset', 'Preset name:')
      name = name.strip()
      if not ok or not name:
        return
      presets = load_presets()
      if name in presets and QMessageBox.question(
          dialog, 'Overwrite Preset', f"Preset '{name}' already exists. Overwrite it?") != QMessageBox.StandardButton.Yes:
        return
      presets[name] = {'targets': [normalize_target(target) for target in draft_targets]}
      task_config['target_layout_presets'] = presets
      refresh_preset_combo(name)

    def load_preset() -> None:
      nonlocal selected_index
      name = preset_combo.currentText()
      incoming = preset_targets(name) if name else None
      if incoming is None:
        return
      if draft_targets and QMessageBox.question(
          dialog, 'Load Preset',
          f"Replace the current {len(draft_targets)} draft target(s) with preset '{name}'?") != QMessageBox.StandardButton.Yes:
        return
      draft_targets[:] = incoming
      selected_index = 0 if draft_targets else -1
      clear_generated_preview()
      refresh_editor()

    def append_preset() -> None:
      nonlocal selected_index
      name = preset_combo.currentText()
      incoming = preset_targets(name) if name else None
      if not incoming:
        return
      draft_targets.extend(incoming)
      selected_index = len(draft_targets) - 1
      clear_generated_preview()
      refresh_editor()

    def delete_preset() -> None:
      name = preset_combo.currentText()
      presets = load_presets()
      if not name or name not in presets:
        return
      if QMessageBox.question(dialog, 'Delete Preset', f"Delete preset '{name}'?") != QMessageBox.StandardButton.Yes:
        return
      del presets[name]
      task_config['target_layout_presets'] = presets
      refresh_preset_combo()

    def save_layout() -> None:
      while targets:
        del targets[len(targets) - 1]
      for target in draft_targets:
        targets.append(normalize_target(target))
      task_config['layout_preview_screen'] = dict(draft_screen)
      sync_table_from_config()
      if 0 <= selected_index < target_table.rowCount():
        target_table.selectRow(selected_index)
      dialog.accept()

    preview = TouchLayoutPreview(
      draft_targets, lambda: pending_generated, draft_preview_screen, indicator_margins,
      task_center, select_index, refresh_editor, dialog)
    # Repaint when the center / photodiode settings change in the main form.
    task_config.add_observer(
      lambda _action, key, _value: preview.update()
      if key in ('center_x', 'center_y', 'state_indicator_x', 'state_indicator_y') else None,
      lambda: isdeleted(preview))
    preview.set_drag_enabled(enable_drag_box.isChecked())
    dialog_layout.addWidget(preview, 1)
    dialog_layout.addWidget(side_panel)

    target_list.currentRowChanged.connect(select_index)
    name_edit.editingFinished.connect(apply_field_changes)
    enabled_box.toggled.connect(lambda _v: apply_field_changes())
    for spin in (radius_min_spin, radius_max_spin, angle_min_spin, angle_max_spin,
                 width_spin, height_spin, window_spin, reward_channel_spin):
      spin.valueChanged.connect(lambda _v: apply_field_changes())
    color_button.clicked.connect(choose_color)
    gen_color_button.clicked.connect(choose_gen_color)
    move_up_button.clicked.connect(lambda: move_selected(-1))
    move_down_button.clicked.connect(lambda: move_selected(1))
    for spin in (screen_width_spin, screen_height_spin, screen_dpi_spin):
      spin.valueChanged.connect(lambda _v: on_screen_changed())
    enable_drag_box.toggled.connect(on_drag_enabled_changed)
    generator_mode_combo.currentIndexChanged.connect(lambda _i: update_generator_controls())
    style_source_combo.currentIndexChanged.connect(lambda _i: update_generator_controls())
    preview_generator_button.clicked.connect(preview_generated_targets)
    apply_preview_button.clicked.connect(apply_generated_preview)
    clear_preview_button.clicked.connect(clear_generated_preview)
    preset_save_button.clicked.connect(save_preset)
    preset_load_button.clicked.connect(load_preset)
    preset_append_button.clicked.connect(append_preset)
    preset_delete_button.clicked.connect(delete_preset)
    add_button.clicked.connect(add_layout_target)
    remove_button.clicked.connect(remove_layout_targets)
    clear_all_button.clicked.connect(clear_all_layout_targets)
    enable_all_layout_button.clicked.connect(lambda: set_all_layout_enabled(True))
    disable_all_layout_button.clicked.connect(lambda: set_all_layout_enabled(False))
    save_button.clicked.connect(save_layout)
    cancel_button.clicked.connect(dialog.reject)

    block_wheel_edits(dialog)

    gen_color_button.setStyleSheet(color_style(gen_color))
    refresh_editor()
    refresh_preset_combo()
    update_generator_controls()
    update_preview_buttons()
    dialog.show()
    dialog.raise_()
    dialog.activateWindow()

  sync_table_from_config()
  block_wheel_edits(result)
  return result
