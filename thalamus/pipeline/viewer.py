import typing
import asyncio
import logging
import argparse

from .motion_capture import XsensWidget
from ..config import *

from ..qt import *

UNHANDLED_EXCEPTION: typing.List[Exception] = []

def exception_handler(loop: asyncio.AbstractEventLoop, context: typing.Mapping[str, typing.Any]) -> None:
  """
  Logs unhandled exceptions and terminates program
  """
  logging.error(context['message'])

  if 'exception' not in context:
    return

  logging.exception('', exc_info=context['exception'])
  UNHANDLED_EXCEPTION.append(context['exception'])
  loop.stop()

async def async_main():
  parser = argparse.ArgumentParser(
                    prog='Thalamus data view',
                    description='Transforms thalamus files into HDF5')
  parser.add_argument('input_file', metavar='input-file')

  args = parser.parse_args()

  done_future = asyncio.get_event_loop().create_future()

  config = ObservableDict({})
  with open(args.input_file, 'rb') as data_file:
    window = XsensWidget(config, data_file, done_future)

    while not done_future.done() and not UNHANDLED_EXCEPTION:
      QApplication.processEvents()
      await asyncio.sleep(.016)

def main() -> None:
  '''
  Setup before running async_main
  '''
  loop = asyncio.get_event_loop()
  try:
    loop.run_until_complete(async_main())
  except RuntimeError:
    if not UNHANDLED_EXCEPTION:
      raise
  finally:
    if UNHANDLED_EXCEPTION:
      raise UNHANDLED_EXCEPTION[0] from None

if __name__ == '__main__':
  main()
