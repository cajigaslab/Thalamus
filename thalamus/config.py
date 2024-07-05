"""
Module for reading a config file and wrapping it in a ObservableCollection
"""

import os.path
import abc
import json
import enum
import typing
import pathlib
import itertools

class ObservableCollection(abc.ABC):
  """
  Wrapper for lists and dicts that allows observers to be notified when the contents change
  """
  class Action(enum.Enum):
    """
    Enumerations of ways that an ObervableCollection can change
    """
    SET = 1
    DELETE = 2

  @abc.abstractmethod
  def get_content(self) -> typing.Any:
    '''
    Assert type and return self
    '''

  def __init__(self, initial: typing.Union[typing.List[typing.Any], typing.Dict[typing.Any, typing.Any]],
               parent: typing.Optional['ObservableCollection'] = None):
    if isinstance(initial, ObservableCollection):
      initial_content = initial.content
    else:
      initial_content = initial
    self.content: typing.Collection[typing.Any] = type(initial_content)()
    self.next_observer_id = 1
    self.observers: typing.Dict[int, typing.Callable[[ObservableCollection.Action, typing.Any, typing.Any], None]] = {}
    self.parent = parent

    if isinstance(initial_content, dict):
      self.initialize_dict(initial_content)
    elif isinstance(initial_content, list):
      self.initialize_list(initial_content)

  def initialize_dict(self, initial: typing.Dict[typing.Any, typing.Any]) -> None:
    '''
    Initialize from dict
    '''
    assert isinstance(self.content, dict), 'Content is not a dict'
    for key, value in initial.items():
      if isinstance(value, ObservableDict):
        self.content[key] = ObservableDict(value.get_content(), self)
      elif isinstance(value, ObservableList):
        self.content[key] = ObservableList(value.get_content(), self)
      elif isinstance(value, dict):
        self.content[key] = ObservableDict(value, self)
      elif isinstance(value, list):
        self.content[key] = ObservableList(value, self)
      else:
        self.content[key] = value

  def initialize_list(self, initial: typing.List[typing.Any]) -> None:
    '''
    Initialize from list
    '''
    assert isinstance(self.content, list), 'Content is not a list'
    for value in initial:
      if isinstance(value, ObservableDict):
        self.content.append(ObservableDict(value.get_content(), self))
      elif isinstance(value, ObservableList):
        self.content.append(ObservableList(value.get_content(), self))
      elif isinstance(value, dict):
        self.content.append(ObservableDict(value, self))
      elif isinstance(value, list):
        self.content.append(ObservableList(value, self))
      else:
        self.content.append(value)

  def copy(self: typing.Any) -> typing.Any:
    """
    Returns a deep copy of this ObervableCollections
    """
    if isinstance(self, ObservableDict):
      return ObservableDict(self.get_content(), self.parent)
    return ObservableList(self.get_content(), self.parent)

  def unwrap(self) -> typing.Union[typing.List[typing.Any], typing.Dict[typing.Any, typing.Any]]:
    """
    Returns a deep copy of this ObservableCollection where all ObservableCollections inside it are also unwrapped.
    """
    if isinstance(self.content, dict):
      return dict((key, (value.unwrap() if isinstance(value, ObservableCollection) else value))
                  for key, value in self.content.items())
    return [value.unwrap() if isinstance(value, ObservableCollection) else value for value in self.content]

  def __getitem__(self, key: typing.Any) -> typing.Any:
    assert isinstance(self.content, (list, dict)), 'content is neither list or dict'
    return self.content[key]

  def get(self, key: typing.Any, default: typing.Any = None) -> typing.Any:
    """
    Reads from context using the dict.get function
    """
    if not isinstance(self.content, dict):
      raise RuntimeError("Attempted to append to ObservableCollection that doesn't wrap a list")

    return self.content.get(key, default)

  def __contains__(self, key: typing.Any) -> typing.Any:
    return key in self.content

  def __setitem__(self, key: typing.Any, value: typing.Any) -> None:
    assert isinstance(self.content, (list, dict)), 'content is neither list or dict'

    if isinstance(key, slice):
      start = 0 if key.start is None else key.start
      step = 1 if key.step is None else key.step
      stop = len(self) if key.stop is None else key.stop
      for key2, value2 in zip(range(start, stop, step), value):
        self[key2] = value2
      return

    if not isinstance(value, ObservableCollection):
      if isinstance(value, dict):
        value = ObservableDict(value, self)
      elif isinstance(value, list):
        value = ObservableList(value, self)

    if isinstance(value, ObservableCollection):
      value.parent = self
    self.content[key] = value

    for observer in list(self.observers.values()):
      observer(ObservableCollection.Action.SET, key, value)

  def __delitem__(self, key: typing.Any) -> None:
    assert isinstance(self.content, (list, dict)), 'content is neither list or dict'

    value = self.content[key]
    del self.content[key]
    if isinstance(value, ObservableCollection):
      value.parent = None

    for observer in list(self.observers.values()):
      observer(ObservableCollection.Action.DELETE, key, value)

  def append(self, value: typing.Any) -> None:
    """
    Appends value to the underlying collection using the append function.  Also converts the value
    to an ObservableCollection if it is a dict or list
    """
    self.insert(len(self), value)

  def extend(self, value: typing.List[typing.Any]) -> None:
    """
    Appends value to the underlying collection using the append function.  Also converts the value
    to an ObservableCollection if it is a dict or list
    """
    for v in value:
      self.insert(len(self), v)

  def insert(self, i: int, value: typing.Any) -> None:
    """
    Appends value to the underlying collection using the append function.  Also converts the value
    to an ObservableCollection if it is a dict or list
    """
    if not isinstance(self.content, list):
      raise RuntimeError("Attempted to append to ObservableCollection that doesn't wrap a list")

    if not isinstance(value, ObservableCollection):
      if isinstance(value, dict):
        value = ObservableDict(value, self)
      elif isinstance(value, list):
        value = ObservableList(value, self)

    if isinstance(value, ObservableCollection):
      value.parent = self
    i = min(i, len(self.content))
    self.content.insert(i, value)

    for observer in list(self.observers.values()):
      observer(ObservableCollection.Action.SET, i, value)

  def remove(self, value: typing.Any) -> None:
    """
    Removes value from the underlying collection using the remove function.
    """
    if not isinstance(self.content, list):
      raise RuntimeError("Attempted to append to ObservableCollection that doesn't wrap a list")

    index = self.content.index(value)
    self.content.remove(value)
    for observer in list(self.observers.values()):
      observer(ObservableCollection.Action.DELETE, index, value)

  def __repr__(self) -> str:
    return repr(self.content)

  def __str__(self) -> str:
    return str(self.content)

  def __len__(self) -> int:
    return len(self.content)

  def __eq__(self, other: typing.Any) -> bool:
    if isinstance(other, ObservableCollection):
      return self.content == other.content
    return bool(self.content == other)

  def __ne__(self, other: typing.Any) -> bool:
    return not self.__eq__(other)

  def items(self) -> typing.ItemsView[typing.Any, typing.Any]:
    """
    Returns the result of calling items() on the underlying collection
    """
    if not isinstance(self.content, dict):
      raise RuntimeError("Attempted to call items() on ObservableCollection that doesn't wrap a dict")

    return self.content.items()

  def keys(self) -> typing.KeysView[typing.Any]:
    """
    Returns the result of calling keys() on the underlying collection
    """
    if not isinstance(self.content, dict):
      raise RuntimeError("Attempted to call keys() on ObservableCollection that doesn't wrap a dict")

    return self.content.keys()

  def values(self) -> typing.ValuesView[typing.Any]:
    """
    Returns the result of calling values() on the underlying collection
    """
    if not isinstance(self.content, dict):
      raise RuntimeError("Attempted to call values() on ObservableCollection that doesn't wrap a dict")

    return self.content.values()

  def update(self, *args: typing.Union[typing.Mapping[typing.Any, typing.Any],
                                       typing.Iterable[typing.Tuple[typing.Any, typing.Any]]],
                   **kwargs: typing.Any) -> None:
    """
    Returns the result of calling items() on the underlying collection
    """
    other = args[0] if args else kwargs
    if not isinstance(self.content, dict):
      raise RuntimeError("Attempted to call update() on ObservableCollection that doesn't wrap a dict")

    for key, value in dict(other).items():
      self[key] = value

  def __iter__(self) -> typing.Iterator[typing.Any]:
    """
    Returns an iterator for the underlying content
    """
    return iter(self.content)

  def assign(self, other: typing.Union[typing.Dict[typing.Any, typing.Any], typing.List[typing.Any]]) -> None:
    """
    Recursively merges a dictionary into this ObservableCollection and removes keys not in the merged dictionary.  It
    starts with the leaves and moves inward, triggering observers along the way.
    """
    items = list(other.items() if isinstance(other, dict) else enumerate(other))
    for key, value in items:
      current_value = self[key] if key in self else None
      if isinstance(current_value, ObservableCollection):
        current_value.assign(value)
      else:
        self[key] = value

    current_keys = set(self.content.keys() if isinstance(self.content, dict) else range(len(self.content)))
    new_keys = set(i[0] for i in items)
    for key in current_keys - new_keys:
      del self[key]

  def merge(self, other: typing.Union[typing.Dict[typing.Any, typing.Any], typing.List[typing.Any]]) -> None:
    """
    Recursively merges a dictionary into this ObservableCollection.  It starts with the leaves and moves inward,
    triggering observers along the way.
    """
    items = list(other.items() if isinstance(other, dict) else enumerate(other))
    for key, value in items:
      current_value = self[key] if key in self else None
      if isinstance(current_value, ObservableCollection):
        current_value.merge(value)
      else:
        if isinstance(self.content, list) and key == len(self.content):
          self.append(value)
        else:
          self[key] = value

  def add_observer(self,
                   observer: typing.Callable[['ObservableCollection.Action', typing.Any, typing.Any], None],
                   remove_when: typing.Optional[typing.Callable[[], bool]] = None) -> None:
    '''
    Add an observer.  If remove_when is defined then the observer is removed if it returns fals.
    '''
    observer_id = self.next_observer_id
    self.next_observer_id += 1

    if not remove_when:
      self.observers[observer_id] = observer
      return
    valid_remove_when = remove_when

    def callback(action: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if valid_remove_when():
        if observer_id in self.observers:
          del self.observers[observer_id]
      else:
        observer(action, key, value)

    self.observers[observer_id] = callback

  def pop(self, i = -1) -> typing.Any:
    if not isinstance(self.content, list):
      raise RuntimeError("Attempted to append to ObservableCollection that doesn't wrap a list")
    if i < 0:
      i = len(self) + i
    value = self.content.pop(i)

    for observer in list(self.observers.values()):
      observer(ObservableCollection.Action.DELETE, i, value)

    return value

class ObservableDict(ObservableCollection, typing.Dict[typing.Any, typing.Any]):
  """
  ObservableCollection that also inherits dict
  """

  def get_content(self) -> typing.Dict[typing.Any, typing.Any]:
    '''
    Assert that content is a dict and return it
    '''
    assert isinstance(self.content, dict), 'content is not a dict'
    return self.content

class ObservableList(ObservableCollection, typing.List[typing.Any]):
  """
  ObservableCollection that also inherits list
  """

  def get_content(self) -> typing.List[typing.Any]:
    '''
    Assert that content is a list and return it
    '''
    assert isinstance(self.content, list), 'content is not a list'
    return self.content

def fill_types(collection: typing.Dict[typing.Any, typing.Any], name: typing.Optional[str] = None) -> None:
  """
  Fills in missing type information for task_clusters and tasks
  """
  if not isinstance(collection, (dict, list)):
    return

  if name == 'task_clusters':
    for item in collection:
      item['type'] = 'task_cluster'
  elif name == 'tasks':
    for item in collection:
      item['type'] = 'task'

  items = collection.items() if isinstance(collection, dict) else zip(itertools.cycle(['']), collection)
  for key, value in items:
    fill_types(value, key)

def load(filename: typing.Union[str, pathlib.Path]) -> ObservableCollection:
  """
  Loads a configuration from a .json file and returns an ObervableCollection
  """
  with open(str(filename), encoding='utf-8') as json_file:
    result = json.load(json_file)

  fill_types(result)
  result['queue'] = []
  if 'reward_schedule' not in result:
    result['reward_schedule'] = {'schedules': [[0]], 'index': 0}
  #print(json.dumps(result, indent=2))

  result2 = ObservableDict(result)
  return result2

def save(filename: typing.Union[str, pathlib.Path], config: typing.Any) -> None:
  """
  Saves a configuration to a .json file
  """
  filename_str = str(filename)
  if isinstance(config, ObservableCollection):
    config = config.unwrap()

  with open(filename_str, "w", encoding='utf-8') as json_output:
    json.dump(config, json_output, indent=2)