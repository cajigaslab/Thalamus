import json
import enum
import typing
import logging
import sqlite3

import jsonpath_ng
import jsonpath_ng.ext

from .config import ObservableCollection

LOGGER = logging.getLogger(__name__)

class MatchContext(typing.NamedTuple):
  value: ObservableCollection

class PatchMatch(typing.NamedTuple):
  value: typing.Optional[ObservableCollection]
  context: MatchContext
  path: jsonpath_ng.Child

class SaveConfig(enum.Enum):
  ROOT = enum.auto()
  VALUE = enum.auto()

class CacheManager:
  def __init__(self, config):
    connection = sqlite3.connect('thalamus.db')
    exists = connection.execute('SELECT * from sqlite_master WHERE name =\'cache\'').fetchone()
    if not exists:
      connection.execute('CREATE TABLE cache (address, value)').fetchone()
    for address, value in connection.execute('SELECT address, value FROM cache'):
      try:
        jsonpath_expr = jsonpath_ng.ext.parse(address)
      except Exception as _exc: # pylint: disable=broad-except
        LOGGER.exception('Failed to parse JSONPATH %s', address)
        continue
      
      matches = jsonpath_expr.find(config)

      try:
        value = json.loads(value)
      except json.JSONDecodeError:
        LOGGER.exception('Failed to decode JSON: %s', value)
        continue

      if not matches:
        if isinstance(jsonpath_expr, jsonpath_ng.Child):
          for m in jsonpath_expr.left.find(config):
            matches.append(PatchMatch(None, MatchContext(m.value), jsonpath_expr.right))
        else:
          matches.append(PatchMatch(None, MatchContext(config), jsonpath_expr))

      for match in matches:
        if isinstance(match.value, ObservableCollection):
          match.value.assign(value)
        elif isinstance(match.path, jsonpath_ng.Index):
          match.context.value[match.path.index] = value
        elif isinstance(match.path, jsonpath_ng.Fields):
          match.context.value[match.path.fields[0]] = value

    if 'Persistence' not in config:
      config['Persistence'] = {}
    persistence_config = config['Persistence']

    if 'Cached' not in persistence_config:
      persistence_config['Cached'] = []
    cached_config = persistence_config['Cached']

    def on_property_change(address: str, root, save_config: SaveConfig, key_filter, source, action, key, value):
      print('on_property_change', address, root, save_config, key_filter, key, key_filter == key, value)
      if action == ObservableCollection.Action.DELETE:
        return

      exists = connection.execute('SELECT * from cache WHERE address = ?', (address,)).fetchone()
      print('exists', exists)
      if exists:
        if save_config == SaveConfig.ROOT:
          print('UPDATE ROOT')
          connection.execute('UPDATE cache SET value = ? WHERE address = ?', (json.dumps(root.unwrap()), address))
          connection.commit()
        elif key == key_filter:
          print('UPDATE VALUE')
          dumped = json.dumps(value.unwrap() if isinstance(value, ObservableCollection) else value)
          connection.execute('UPDATE cache SET value = ? WHERE address = ?', (dumped, address))
          connection.commit()
      else:
        if save_config == SaveConfig.ROOT:
          print('INSERT ROOT')
          connection.execute('INSERT INTO cache VALUES (?, ?)', (address, json.dumps(root.unwrap())))
          connection.commit()
        elif key == key_filter:
          print('INSERT VALUE')
          dumped = json.dumps(value.unwrap() if isinstance(value, ObservableCollection) else value)
          connection.execute('INSERT INTO cache VALUES (?, ?)', (address, dumped))
          connection.commit()

    next_tag = 0
    current_tag = -1
    def on_cached_change(source, action, key, value):
      nonlocal current_tag
      current_tag += 1
      generation_tag = current_tag
      gen_end = lambda: current_tag != generation_tag
      for c in cached_config:
        address = c['Address']
        print('ADDRESS', address)
        try:
          jsonpath_expr = jsonpath_ng.ext.parse(address)
        except Exception as _exc: # pylint: disable=broad-except
          LOGGER.exception('Failed to parse JSONPATH %s', address)
          continue
        
        matches = jsonpath_expr.find(config)

        if not matches:
          if isinstance(jsonpath_expr, jsonpath_ng.Child):
            for m in jsonpath_expr.left.find(config):
              matches.append(PatchMatch(None, MatchContext(m.value), jsonpath_expr.right))
          else:
            matches.append(PatchMatch(None, MatchContext(config), jsonpath_expr))

        for match in matches:
          if isinstance(match.value, ObservableCollection):
            print('Observing1', match.value)
            observer = lambda *args, address=address: on_property_change(address, match.value, SaveConfig.ROOT, None, *args)
            match.value.add_recursive_observer(observer, gen_end)
            match.value.recap(lambda *args: observer(match.value, *args))
          elif isinstance(match.path, jsonpath_ng.Index):
            print('Observing2', match.context.value, match.path.index)
            observer = lambda *args, address=address, key_filter=match.path.index: on_property_change(address, match.context.value, SaveConfig.VALUE, key_filter, *args)
            match.context.value.add_recursive_observer(observer, gen_end)
            match.context.value.recap(lambda *args: observer(match.context.value, *args))
          elif isinstance(match.path, jsonpath_ng.Fields):
            print('Observing3', match.context.value, match.path.fields[0])
            observer = lambda *args, address=address, key_filter=match.path.fields[0]: on_property_change(address, match.context.value, SaveConfig.VALUE, key_filter, *args)
            match.context.value.add_recursive_observer(observer, gen_end)
            match.context.value.recap(lambda *args: observer(match.context.value, *args))

    cached_config.add_recursive_observer(on_cached_change)
    cached_config.recap(lambda *args: on_cached_change(None, *args))

