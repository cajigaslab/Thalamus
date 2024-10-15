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

RemoteStorage = typing.Callable[['ObservableCollection.Action', str, typing.Any, typing.Callable[[], None]], bool]

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
    self.recursive_observers: typing.Dict[int, typing.Callable[[ObservableCollection, ObservableCollection.Action, typing.Any, typing.Any], None]] = {}
    self.parent = parent
    self.remote_storage = None

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

  def set_remote_storage(self, value: typing.Optional[RemoteStorage]):
    self.remote_storage = value
    items = self.values() if isinstance(self, ObservableDict) else self
    for v in items:
      if isinstance(v, ObservableCollection):
        v.set_remote_storage(value)

  def address(self):
    reverse_path = []
    current = self
    while current is not None and current.parent is not None:
      items = current.parent.items() if isinstance(current.parent, ObservableDict) else enumerate(current.parent)
      for k, v in items:
        if v is current:
          reverse_path.append(k)
          break
      current = current.parent

    path = reverse_path[::-1]
    return ''.join(f'[{repr(p)}]' for p in path)

  def __setitem__(self, key: typing.Any, value: typing.Any) -> None:
    self.setitem(key, value)

  def setitem(self, key: typing.Any, value: typing.Any, callback: typing.Callable[[], None] = lambda: None, from_remote = False):
    assert isinstance(self.content, (list, dict)), 'content is neither list or dict'

    if isinstance(key, slice):
      start = 0 if key.start is None else key.start
      step = 1 if key.step is None else key.step
      stop = len(self) if key.stop is None else key.stop
      for key2, value2 in zip(range(start, stop, step), value):
        self.setitem(key2, value2, callback, from_remote)
      return

    if isinstance(value, (int, float, str)) and key in self.content and self.content[key] == value:
      callback()
      return

    if not from_remote and self.remote_storage is not None:
      address = self.address() + f'[{repr(key)}]'
      self.remote_storage(ObservableCollection.Action.SET, address, value, callback)
      return

    if not isinstance(value, ObservableCollection):
      if isinstance(value, dict):
        value = ObservableDict(value, self)
      elif isinstance(value, list):
        value = ObservableList(value, self)

    if isinstance(value, ObservableCollection):
      value.parent = self
      value.set_remote_storage(self.remote_storage)
    self.content[key] = value

    self.__notify(self, ObservableCollection.Action.SET, key, value)
    callback()

  def __add_impl(self, other, reverse=False):
    assert isinstance(self.content, list), 'content is not a list'

    other_content = other.content if isinstance(other, ObservableCollection) else other
    assert isinstance(other_content, list), 'argument is not a list'

    new_content = (other_content + self.content) if reverse else (self.content + other_content)
    return ObservableList(new_content)

  def __add__(self, other):
    return self.__add_impl(other)

  def __radd__(self, other):
    return self.__add_impl(other, True)

  def __delitem__(self, key: typing.Any) -> None:
    self.delitem(key)

  def delitem(self, key: typing.Any, callback: typing.Callable[[], None] = lambda: None, from_remote = False):
    assert isinstance(self.content, (list, dict)), 'content is neither list or dict'

    if not from_remote and self.remote_storage is not None:
      address = self.address() + f'[{repr(key)}]'
      value = self[key]
      self.remote_storage(ObservableCollection.Action.DELETE, address, value, callback)
      return

    value = self.content[key]
    del self.content[key]
    if isinstance(value, ObservableCollection):
      value.parent = None
      value.set_remote_storage(None)

    self.__notify(self, ObservableCollection.Action.DELETE, key, value)
    callback()

  def append(self, value: typing.Any, callback = lambda: None, from_remote = False) -> None:
    """
    Appends value to the underlying collection using the append function.  Also converts the value
    to an ObservableCollection if it is a dict or list
    """
    self.insert(len(self), value, callback, from_remote)

  def extend(self, value: typing.List[typing.Any], callback = lambda: None, from_remote = False) -> None:
    """
    Appends value to the underlying collection using the append function.  Also converts the value
    to an ObservableCollection if it is a dict or list
    """
    for v in value:
      self.insert(len(self), v, callback, from_remote)

  def insert(self, i: int, value: typing.Any, callback: typing.Callable[[], None] = lambda: None, from_remote = False) -> None:
    """
    Appends value to the underlying collection using the append function.  Also converts the value
    to an ObservableCollection if it is a dict or list
    """
    if not isinstance(self.content, list):
      raise RuntimeError("Attempted to append to ObservableCollection that doesn't wrap a list")

    i = min(i, len(self.content))
    if not from_remote and self.remote_storage is not None:
      address = self.address() + f'[{i}]'
      self.remote_storage(ObservableCollection.Action.SET, address, value, callback)
      return

    if not isinstance(value, ObservableCollection):
      if isinstance(value, dict):
        value = ObservableDict(value, self)
      elif isinstance(value, list):
        value = ObservableList(value, self)

    if isinstance(value, ObservableCollection):
      value.parent = self
      value.set_remote_storage(self.remote_storage)
    self.content.insert(i, value)

    self.__notify(self, ObservableCollection.Action.SET, i, value)
    callback()

  def remove(self, value: typing.Any, callback: typing.Callable[[], None] = lambda: None, from_remote = False) -> None:
    """
    Removes value from the underlying collection using the remove function.
    """
    if not isinstance(self.content, list):
      raise RuntimeError("Attempted to append to ObservableCollection that doesn't wrap a list")

    index = self.content.index(value)

    if not from_remote and self.remote_storage is not None:
      address = self.address() + f'[{index}]'
      self.remote_storage(ObservableCollection.Action.SET, address, value, callback)
      return

    self.content.remove(value)
    if isinstance(value, ObservableCollection):
      value.parent = None
      value.set_remote_storage(None)
    self.__notify(self, ObservableCollection.Action.DELETE, index, value)
    callback()

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

  def assign(self, other: typing.Union[typing.Dict[typing.Any, typing.Any], typing.List[typing.Any]], callback = lambda: None, from_remote = False) -> None:
    """
    Recursively merges a dictionary into this ObservableCollection and removes keys not in the merged dictionary.  It
    starts with the leaves and moves inward, triggering observers along the way.
    """
    items = list(other.items() if isinstance(other, dict) else enumerate(other))
    for key, value in items:
      current_value = self[key] if key in self else None
      if isinstance(current_value, ObservableCollection):
        current_value.assign(value, callback, from_remote)
      else:
        self.setitem(key, value, callback, from_remote)

    current_keys = set(self.content.keys() if isinstance(self.content, dict) else range(len(self.content)))
    new_keys = set(i[0] for i in items)
    for key in current_keys - new_keys:
      self.delitem(key, callback, from_remote)

  def merge(self, other: typing.Union[typing.Dict[typing.Any, typing.Any], typing.List[typing.Any]], callback = lambda: None, from_remote = False) -> None:
    """
    Recursively merges a dictionary into this ObservableCollection.  It starts with the leaves and moves inward,
    triggering observers along the way.
    """
    items = list(other.items() if isinstance(other, dict) else enumerate(other))
    for key, value in items:
      current_value = self[key] if key in self else None
      if isinstance(current_value, ObservableCollection):
        current_value.merge(value, callback, from_remote)
      else:
        if isinstance(self.content, list) and key == len(self.content):
          self.append(value, callback, from_remote)
        else:
          self.setitem(key, value, callback, from_remote)

  def add_observer(self,
                   observer: typing.Callable[['ObservableCollection.Action', typing.Any, typing.Any], None],
                   remove_when: typing.Optional[typing.Callable[[], bool]] = None,
                   recap: bool = False) -> None:
    '''
    Add an observer.  If remove_when is defined then the observer is removed if it returns fals.
    '''
    observer_id = self.next_observer_id
    self.next_observer_id += 1

    if not remove_when:
      self.observers[observer_id] = observer
      if recap:
        self.recap(observer)
      return
    valid_remove_when = remove_when

    def callback(action: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if valid_remove_when():
        if observer_id in self.observers:
          del self.observers[observer_id]
      else:
        observer(action, key, value)

    self.observers[observer_id] = callback

    if recap:
      self.recap(callback)

  def add_recursive_observer(self,
                   observer: typing.Callable[['ObservableCollection', 'ObservableCollection.Action', typing.Any, typing.Any], None],
                   remove_when: typing.Optional[typing.Callable[[], bool]] = None,
                   recap: bool = False) -> None:
    '''
    Add an observer.  If remove_when is defined then the observer is removed if it returns fals.
    '''
    observer_id = self.next_observer_id
    self.next_observer_id += 1

    if not remove_when:
      self.recursive_observers[observer_id] = observer
      if recap:
        self.recap(lambda *args: observer(self, *args))
      return
    valid_remove_when = remove_when

    def callback(source: ObservableCollection, action: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      if valid_remove_when():
        if observer_id in self.recursive_observers:
          del self.recursive_observers[observer_id]
      else:
        observer(source, action, key, value)

    self.recursive_observers[observer_id] = callback

    if recap:
      self.recap(lambda *args: callback(self, *args))

  def pop(self, i = -1, callback = lambda: None, from_remote = False) -> typing.Any:
    if not isinstance(self.content, list):
      raise RuntimeError("Attempted to append to ObservableCollection that doesn't wrap a list")
    if i < 0:
      i = len(self) + i

    value = self.content[i]
    if not from_remote and self.remote_storage is not None:
      address = self.address() + f'[{i}]'
      self.remote_storage(ObservableCollection.Action.SET, address, value, callback)
      return

    self.content.pop(i)
    if isinstance(value, ObservableCollection):
      value.parent = None
      value.set_remote_storage(None)

    self.__notify(self, ObservableCollection.Action.DELETE, i, value)

    return value

  def recap(self, observer: typing.Optional[typing.Callable[['ObservableCollection.Action', typing.Any, typing.Any], None]] = None) -> None:
    items = (self.content.items() if isinstance(self.content, dict) else enumerate(self.content))

    default_observer = lambda a, k, v: self.__notify(self, a, k, v)

    observer = observer if observer is not None else default_observer
    for k, v in items:
      observer(ObservableCollection.Action.SET, k, v)

  def __notify(self, source: 'ObservableCollection', action: 'ObservableCollection.Action', key: typing.Any, value: typing.Any):
    if source == self:
      for observer in list(self.observers.values()):
        observer(action, key, value)

    for observer in list(self.recursive_observers.values()):
      observer(source, action, key, value)

    if self.parent is not None:
      self.parent.__notify(source, action, key, value)

  def is_descendent(self, root):
    current = self
    while True:
      if current is None:
        return False
      if current is root:
        return True
      current = current.parent

  def key_in_parent(self):
    assert self.parent is not None
    items = self.parent.items() if isinstance(self.parent, ObservableDict) else enumerate(self.parent)
    for k, v in items:
      if v is self:
        return k

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
