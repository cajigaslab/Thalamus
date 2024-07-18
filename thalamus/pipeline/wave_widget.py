from ..qt import *

import dataclasses
import inspect
import typing
from ..config import ObservableCollection

#MISSING: typing.Any = object()
#
#@dataclasses.dataclass
#class DictField:
#  key: typing.Union[int, str]
#  default: typing.Any
#
#T = typing.TypeVar('T')
#def dictfield(key: typing.Union[int, str], default: T = MISSING) -> T:
#  return DictField(key, default) # type: ignore
#
#U = typing.TypeVar('U')
#def dictwrap(cls: type[U]) -> typing.Callable[[typing.Any], U]:
#  annotations = inspect.get_annotations(cls)
#  fields = {}
#  for field_name, field_type in annotations.items():
#    if hasattr(cls, field_name):
#      fields[field_name] = [field_type, getattr(cls, field_name)]
#    else:
#      fields[field_name] = [field_type]
#
#  class DictWrap():
#    def __init__(self, config):
#      self.config = config
#      for k, v in fields.items():
#        if len(v) == 1:
#          continue
#        default = v[1]
#        if isinstance(default, DictField):
#          default = default.default
#        if default is not MISSING and k not in config:
#          config[k] = default
#
#  for field_name, v in fields.items():
#    if len(v) < 2:
#      field_key = field_name
#    else:
#      field_key = v[1].key
#
#    def getter(self, key=field_key):
#      return self.config[key]
#    def setter(self, value, key=field_key):
#      self.config[key] = value
#    def deleter(self, key=field_key):
#      del self.config[key]
#
#    setattr(DictWrap, field_name, property(getter, setter, deleter))
#
#  return DictWrap # type: ignore
#
#@dictwrap
#class WaveConfigWrapper:
#  max_offset: float = 10
#  min_offset: float = -10
#  offset: float = dictfield('Offset')

def to_slider_value(value, name, config):
  value_max, value_min = config[f'Max {name}'], config[f'Min {name}']
  value_offset = value - value_min
  value_range = value_max - value_min

  return int(10000*value_offset/value_range)

class WaveWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    if 'Min Offset' not in config:
      config['Min Offset'] = -10
    if 'Max Offset' not in config:
      config['Max Offset'] = 10

    if 'Min Amplitude' not in config:
      config['Min Amplitude'] = 0
    if 'Max Amplitude' not in config:
      config['Max Amplitude'] = 10

    if 'Min Frequency' not in config:
      config['Min Frequency'] = 0
    if 'Max Frequency' not in config:
      config['Max Frequency'] = 10

    layout = QGridLayout()
    offset_label = QLabel()
    offset_max_spin = QDoubleSpinBox()
    offset_max_spin.setRange(-1e9, 1e9)
    offset_slider = QSlider(Qt.Orientation.Vertical)
    offset_slider.setRange(0, 10000)
    offset_min_spin = QDoubleSpinBox()
    offset_min_spin.setRange(-1e9, 1e9)

    amplitude_label = QLabel()
    amplitude_max_spin = QDoubleSpinBox()
    amplitude_max_spin.setRange(-1e9, 1e9)
    amplitude_slider = QSlider(Qt.Orientation.Vertical)
    amplitude_slider.setRange(0, 10000)
    amplitude_min_spin = QDoubleSpinBox()
    amplitude_min_spin.setRange(-1e9, 1e9)

    frequency_label = QLabel()
    frequency_max_spin = QDoubleSpinBox()
    frequency_max_spin.setRange(-1e9, 1e9)
    frequency_slider = QSlider(Qt.Orientation.Vertical)
    frequency_slider.setRange(0, 10000)
    frequency_min_spin = QDoubleSpinBox()
    frequency_min_spin.setRange(-1e9, 1e9)

    layout.addWidget(offset_label, 0, 0)
    layout.addWidget(offset_max_spin, 1, 0)
    layout.addWidget(offset_slider, 2, 0)
    layout.addWidget(offset_min_spin, 3, 0)

    layout.addWidget(amplitude_label, 0, 1)
    layout.addWidget(amplitude_max_spin, 1, 1)
    layout.addWidget(amplitude_slider, 2, 1)
    layout.addWidget(amplitude_min_spin, 3, 1)

    layout.addWidget(frequency_label, 0, 2)
    layout.addWidget(frequency_max_spin, 1, 2)
    layout.addWidget(frequency_slider, 2, 2)
    layout.addWidget(frequency_min_spin, 3, 2)

    self.setLayout(layout)

    def on_slider_change(slider, name, v):
      slider_value_offset = (v - slider.minimum())
      slider_value_range = (slider.maximum() - slider.minimum())
      slider_scale = 1.0*slider_value_offset/slider_value_range

      value_max, value_min = config[f'Max {name}'], config[f'Min {name}']
      value_range = value_max - value_min

      config[name] = value_range*slider_scale + value_min

    def wire(min_spin: QDoubleSpinBox, max_spin: QDoubleSpinBox, slider, name):
      min_spin.editingFinished.connect(lambda: config.update({f'Min {name}': min_spin.value()}))
      max_spin.editingFinished.connect(lambda: config.update({f'Max {name}': max_spin.value()}))
      slider.valueChanged.connect(lambda v: on_slider_change(slider, name, v))

    wire(offset_min_spin, offset_max_spin, offset_slider, 'Offset')
    wire(amplitude_min_spin, amplitude_max_spin, amplitude_slider, 'Amplitude')
    wire(frequency_min_spin, frequency_max_spin, frequency_slider, 'Frequency')

    def on_change(a, k, v):
      if k == 'Offset':
        slider_value = to_slider_value(v, 'Offset', config)
        offset_label.setText(f'Offset: {v:.6g}')
        offset_slider.setValue(slider_value)
      elif k == 'Max Offset':
        offset_max_spin.setValue(v)
        if 'Offset' in config:
          offset_slider.setValue(to_slider_value(config['Offset'], 'Offset', config))
      elif k == 'Min Offset':
        offset_min_spin.setValue(v)
        if 'Offset' in config:
          offset_slider.setValue(to_slider_value(config['Offset'], 'Offset', config))

      elif k == 'Amplitude':
        slider_value = to_slider_value(v, 'Amplitude', config)
        amplitude_label.setText(f'Amplitude: {v:.6g}')
        amplitude_slider.setValue(slider_value)
      elif k == 'Max Amplitude':
        amplitude_max_spin.setValue(v)
        if 'Amplitude' in config:
          amplitude_slider.setValue(to_slider_value(config['Amplitude'], 'Amplitude', config))
      elif k == 'Min Amplitude':
        amplitude_min_spin.setValue(v)
        if 'Amplitude' in config:
          amplitude_slider.setValue(to_slider_value(config['Amplitude'], 'Amplitude', config))

      elif k == 'Frequency':
        slider_value = to_slider_value(v, 'Frequency', config)
        frequency_label.setText(f'Frequency: {v:.6g}')
        frequency_slider.setValue(slider_value)
      elif k == 'Max Frequency':
        frequency_max_spin.setValue(v)
        if 'Frequency' in config:
          frequency_slider.setValue(to_slider_value(config['Frequency'], 'Frequency', config))
      elif k == 'Min Frequency':
        frequency_min_spin.setValue(v)
        if 'Frequency' in config:
          frequency_slider.setValue(to_slider_value(config['Frequency'], 'Frequency', config))

    config.add_observer(on_change, lambda: isdeleted(self), True)

