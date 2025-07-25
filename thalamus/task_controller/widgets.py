"""
Reuseable widgets
"""

import typing
import logging
import functools

from ..qt import *

from ..config import ObservableCollection
from .util import remove_by_is, isdeleted

LOGGER = logging.getLogger(__name__)

class ColorWidget(QWidget):
  """
  Widget for rendering a selected color
  """
  def __init__(self, parent: typing.Optional[QWidget] = None) -> None:
    super().__init__(parent)
    self._color = QColor(0, 0, 0, 0)

  @property
  def color(self) -> QColor:
    '''
    color property
    '''
    return self._color

  @color.setter
  def color(self, value: QColor) -> None:
    self._color = value
    self.update()

  def paintEvent(self, _: QPaintEvent) -> None: # pylint: disable=invalid-name
    """
    Renders a checkerboard pattern and then the selected color on top of it.
    """
    painter = QPainter(self)
    for pix_x, pix_y in ((x, y) for x in range(0, self.width(), 10) for y in range(0, self.height(), 10)):
      color = QColor.fromRgbF(1, 1, 1) if ((pix_x + pix_y) % 20) == 0 else QColor.fromRgbF(0, 0, 0)
      painter.fillRect(pix_x, pix_y, 10, 10, color)
    painter.fillRect(0, 0, self.width(), self.height(), self.color)
    painter.drawRect(0, 0, self.width()-1, self.height()-1)

class Form(QWidget):
  '''
  A row based form for editing an ObservableCollection
  '''
  def __init__(self, config: ObservableCollection) -> None:
    super().__init__()
    self.config = config
    self.grid_layout = QGridLayout()
    self.setLayout(self.grid_layout)
    self.row = 0

  class Uniform(typing.NamedTuple):
    '''
    Configuration for a row that edits a uniform random variable
    '''
    label: str
    field: str
    min: float
    max: float
    suffix: str = ''

  class Constant(typing.NamedTuple):
    '''
    Configuration for a row that edits a constant variable
    '''
    label: str
    field: str
    default: float
    suffix: str = ''
    precision: int = 2

  class String(typing.NamedTuple):
    '''
    Configuration for a row that edits a string
    '''
    label: str
    field: str
    default: str = ''

  Color = typing.NamedTuple('Color', [
    ('label', str),
    ('field', str),
    ('default', QColor)
  ])

  class Bool(typing.NamedTuple):
    '''
    Configuration for a row that edits a bool variable
    '''
    label: str
    field: str
    default: bool = False

  class Choice(typing.NamedTuple):
    '''
    Configuration for a row that edits a selection from several values
    '''
    label: str
    field: str
    options: typing.List[typing.Tuple[str, str]]

  File = typing.NamedTuple('File', [
    ('label', str),
    ('field', str),
    ('default', str),
    ('caption', str),
    ('file_filter', str)
  ])

  Directory = typing.NamedTuple('Directory', [
    ('label', str),
    ('field', str),
    ('default', str),
    ('caption', str)
  ])

  Row = typing.Union['Form.Uniform', 'Form.Constant', 'Form.String', 'Form.Color', 'Form.Bool', 'Form.Choice', 'Form.File', 'Form.Directory']

  def append_labels(self, *headers: str) -> None:
    '''
    Appends a row of labels
    '''
    for i, text in enumerate(headers):
      self.grid_layout.addWidget(QLabel(text), self.row, i)
    self.row += 1

  @staticmethod
  def build(config: ObservableCollection, headers: typing.List[str], *args: Row) -> 'Form':
    '''
    Builds a form from the argument list
    '''
    result = Form(config)
    result.append_labels(*headers)
    for arg in args:
      if isinstance(arg, Form.Uniform):
        result.append_uniform(arg)
      elif isinstance(arg, Form.Constant):
        result.append_constant(arg)
      elif isinstance(arg, Form.String):
        result.append_string(arg)
      elif isinstance(arg, Form.Color):
        result.append_color(arg)
      elif isinstance(arg, Form.Bool):
        result.append_bool(arg)
      elif isinstance(arg, Form.Choice):
        result.append_choice(arg)
      elif isinstance(arg, Form.File):
        result.append_file(arg)
      elif isinstance(arg, Form.Directory):
        result.append_directory(arg)
    result.finish()
    return result

  def append_uniform(self, config: Uniform) -> None:
    '''
    Appends a row for editing a uniform random variable
    '''
    if config.field not in self.config:
      self.config[config.field] = {}
    if 'min' not in self.config[config.field]:
      self.config[config.field]['min'] = config.min
    if 'max' not in self.config[config.field]:
      self.config[config.field]['max'] = config.max

    self.grid_layout.addWidget(QLabel(config.label), self.row, 0)

    min_spin_box = QDoubleSpinBox()
    min_spin_box.setObjectName(f'{config.field}_min')
    min_spin_box.setKeyboardTracking(False)
    min_spin_box.setRange(-1000000, 1000000)
    min_spin_box.setSuffix(config.suffix)
    min_spin_box.setValue(self.config[config.field]['min'])
    self.grid_layout.addWidget(min_spin_box, self.row, 1)

    max_spin_box = QDoubleSpinBox()
    max_spin_box.setObjectName(f'{config.field}_max')
    max_spin_box.setKeyboardTracking(False)
    max_spin_box.setRange(-1000000, 1000000)
    max_spin_box.setSuffix(config.suffix)
    max_spin_box.setValue(self.config[config.field]['max'])
    self.grid_layout.addWidget(max_spin_box, self.row, 2)

    def on_change(field: str, value: float) -> None:
      self.config[config.field][field] = value

    min_spin_box.valueChanged.connect(functools.partial(on_change, 'min'))
    max_spin_box.valueChanged.connect(functools.partial(on_change, 'max'))

    def on_config_change(_: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      print(config.field, 'on_config_change {} = {}'.format(key, value))
      if key == 'min':
        min_spin_box.setValue(value)
      elif key == 'max':
        max_spin_box.setValue(value)

    self.config[config.field].add_observer(on_config_change, functools.partial(isdeleted, self))

    self.row += 1

  def append_constant(self, config: Constant) -> None:
    '''
    Appends a row for editing a constant variable
    '''
    if config.field not in self.config:
      self.config[config.field] = config.default

    label = QLabel(config.label) # 'label' was added to allow to modify GUI labels from the task code
    label.setObjectName(f'{config.field}_label')
    self.grid_layout.addWidget(label, self.row, 0)

    min_spin_box = QDoubleSpinBox()
    min_spin_box.setDecimals(config.precision)
    min_spin_box.setObjectName(f'{config.field}')
    min_spin_box.setKeyboardTracking(False)
    min_spin_box.setRange(-1000000, 1000000)
    min_spin_box.setSuffix(config.suffix)
    min_spin_box.setValue(self.config[config.field])
    self.grid_layout.addWidget(min_spin_box, self.row, 1, 1, 2)

    min_spin_box.valueChanged.connect(lambda v: self.config.update({config.field:v}))

    def on_config_change(_: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if key == config.field:
        min_spin_box.setValue(value)

    self.config.add_observer(on_config_change, functools.partial(isdeleted, self))

    self.row += 1

  def append_string(self, config: String) -> None:
    '''
    Appends a row for editing a constant variable
    '''
    if config.field not in self.config:
      self.config[config.field] = config.default

    self.grid_layout.addWidget(QLabel(config.label), self.row, 0)

    edit = QLineEdit()
    edit.setObjectName(f'{config.field}')
    edit.setText(self.config[config.field])
    self.grid_layout.addWidget(edit, self.row, 1, 1, 2)

    edit.editingFinished.connect(lambda: self.config.update({ config.field:edit.text() }))

    def on_config_change(_: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if key == config.field:
        edit.setText(value)

    self.config.add_observer(on_config_change, functools.partial(isdeleted, self))

    self.row += 1

  def append_color(self, config: Color) -> None:
    '''
    Appends a row for editing a color
    '''
    default_list = [config.default.red(), config.default.green(), config.default.blue()]
    if config.field not in self.config:
      self.config[config.field] = default_list

    color_widget = ColorWidget()
    color_widget.setObjectName(f'{config.field}_display')
    rgb = self.config.get(config.field, default_list)
    color_widget.color = QColor(int(rgb[0]), int(rgb[1]), int(rgb[2]))

    def on_edit_color() -> None:
      '''
      Opens a dialog to select a color
      '''
      color = QColorDialog.getColor(color_widget.color, self, "Select Color",
                                                    QColorDialog.ColorDialogOption.ShowAlphaChannel)
      if color.isValid():
        self.config[config.field] = [color.red(), color.green(), color.blue()]

    color_button = QPushButton("Edit")
    color_button.setObjectName(f'{config.field}_button')
    color_button.clicked.connect(on_edit_color)

    self.grid_layout.addWidget(QLabel(config.label), self.row, 0)
    self.grid_layout.addWidget(color_widget, self.row, 1)
    self.grid_layout.addWidget(color_button, self.row, 2)

    def on_config_change(_: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if key == config.field:
        value = self.config[config.field]
        color_widget.color = QColor(int(value[0]), int(value[1]), int(value[2]))

    self.config.add_observer(on_config_change, functools.partial(isdeleted, self))
    callback = lambda a, k, v: on_config_change(a, config.field, v)
    self.config[config.field].add_observer(callback, functools.partial(isdeleted, self))

    self.row += 1

  def append_bool(self, config: Bool) -> None:
    '''
    Appends a row for editing a boolean variable
    '''
    if config.field not in self.config:
      self.config[config.field] = config.default

    self.grid_layout.addWidget(QLabel(config.label), self.row, 0)

    check_box = QCheckBox()
    check_box.setObjectName(f'{config.field}')
    check_box.setChecked(self.config[config.field])
    self.grid_layout.addWidget(check_box, self.row, 1, 1, 2)

    check_box.toggled.connect(lambda v: self.config.update({config.field:v}))

    def on_config_change(_: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if key == config.field:
        check_box.setChecked(value)

    self.config.add_observer(on_config_change, functools.partial(isdeleted, self))

    self.row += 1

  def append_choice(self, config: Choice) -> None:
    '''
    Appends a row for select from a series of choices
    '''
    if config.field not in self.config:
      self.config[config.field] = config.options[0][1]

    self.grid_layout.addWidget(QLabel(config.label), self.row, 0)

    combobox = QComboBox()
    combobox.setObjectName(f'{config.field}')
    for i, args in enumerate(config.options):
      combobox.addItem(*args)
      if args[1] == self.config[config.field]:
        combobox.setCurrentIndex(i)

    self.grid_layout.addWidget(combobox, self.row, 1, 1, 2)

    combobox.currentIndexChanged.connect(lambda i: self.config.update({config.field: combobox.itemData(i)}))

    def on_config_change(_: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if key == config.field:
        for i, args in enumerate(config.options):
          if args[1] == value:
            combobox.setCurrentIndex(i)

    self.config.add_observer(on_config_change, functools.partial(isdeleted, self))

    self.row += 1

  def append_file(self, config: File) -> None:
    '''
    Appends a row for selecting a file
    '''
    if config.field not in self.config:
      self.config[config.field] = config.default

    edit_widget = QLineEdit()
    edit_widget.setObjectName(f'{config.field}_edit')
    edit_widget.setText(self.config[config.field])
    edit_widget.textChanged.connect(lambda v: self.config.update({config.field: v}))

    def on_select_file() -> None:
      filename, _ = QFileDialog.getOpenFileName(self, config.caption, '', config.file_filter)
      if filename:
        edit_widget.setText(filename)

    button = QPushButton("Select")
    button.setObjectName(f'{config.field}_button')
    button.clicked.connect(on_select_file)

    self.grid_layout.addWidget(QLabel(config.label), self.row, 0)
    self.grid_layout.addWidget(edit_widget, self.row, 1)
    self.grid_layout.addWidget(button, self.row, 2)

    def on_config_change(_: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if key == config.field:
        edit_widget.setText(value)

    self.config.add_observer(on_config_change, functools.partial(isdeleted, self))

    self.row += 1

  def append_directory(self, config: Directory) -> None:
    '''
    Appends a row for selecting a directory
    '''
    if config.field not in self.config:
      self.config[config.field] = config.default

    edit_widget = QLineEdit()
    edit_widget.setObjectName(f'{config.field}_edit')
    edit_widget.setText(self.config[config.field])
    edit_widget.textChanged.connect(lambda v: self.config.update({config.field: v}))

    def on_select_file() -> None:
      filename = QFileDialog.getExistingDirectory(self, config.caption, '')
      if filename:
        edit_widget.setText(filename)

    button = QPushButton("Select")
    button.setObjectName(f'{config.field}_button')
    button.clicked.connect(on_select_file)

    self.grid_layout.addWidget(QLabel(config.label), self.row, 0)
    self.grid_layout.addWidget(edit_widget, self.row, 1)
    self.grid_layout.addWidget(button, self.row, 2)

    def on_config_change(_: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if key == config.field:
        edit_widget.setText(value)

    self.config.add_observer(on_config_change, functools.partial(isdeleted, self))

    self.row += 1

  def finish(self) -> None:
    '''
    Appends a row that shifts the above rows to the top
    '''
    self.grid_layout.addWidget(QLabel(''))
    self.grid_layout.setRowStretch(self.row, 1)

class ListAsTabsWidget(QTabWidget):
  '''
  A QTabWidget for editing an ObervableCollection that wraps a list
  '''
  def __init__(self, config: ObservableCollection,
               tab_factory: typing.Callable[[ObservableCollection], QWidget],
               label_factory: typing.Callable[[ObservableCollection], str]) -> None:
    super().__init__()
    self.setTabsClosable(True)
    self.tabCloseRequested.connect(self.on_delete_item)

    self.config = config
    self.config.add_observer(self.on_items_changed, lambda: isdeleted(self))
    self.tab_factory = tab_factory
    self.label_factory = label_factory
    self.widget_to_config: typing.Dict[QWidget, ObservableCollection] = {}
    for i, item in enumerate(config):
      self.on_items_changed(ObservableCollection.Action.SET, i, item)

  def on_delete_item(self, index: int) -> None:
    '''
    Removes an item from the list being managed by this widget
    '''
    widget = self.widget(index)
    item_config = self.widget_to_config[widget]

    confirm = QMessageBox.question(self, "Delete",
                                   f'Delete {item_config["name"]}?',
                                   QMessageBox.Yes | QMessageBox.No)
    if confirm == QMessageBox.Yes:
      remove_by_is(self.config, item_config)

  def name_updater(self, config: ObservableCollection, widget: QWidget) -> None:
    '''
    Updates the tab text in response to ObservableCollection changes
    '''
    LOGGER.info('name_updater')
    for i in range(self.count()):
      if self.widget(i) is widget:
        self.setTabText(i, self.label_factory(config))
        break

  def on_items_changed(self, action: ObservableCollection.Action, _: typing.Any, value: typing.Any) -> None:
    '''
    Updates the tabs as items are added and removed from the ObservableCollection
    '''
    if action == ObservableCollection.Action.SET:
      page = self.tab_factory(value)
      label = self.label_factory(value)
      self.addTab(page, label)
      self.widget_to_config[page] = value
      value.add_observer(lambda *args: self.name_updater(value, page), lambda: isdeleted(self))
    elif action == ObservableCollection.Action.DELETE:
      for i in range(self.count()):
        widget = self.widget(i)
        if self.widget_to_config[widget] is value:
          self.removeTab(i)
          break