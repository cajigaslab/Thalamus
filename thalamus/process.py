import asyncio
import logging

LOGGER = logging.getLogger(__name__)


PROCESSES = set()
def cleanup():
  if not PROCESSES:
    return
  LOGGER.warn('%d subprocesses still running', len(PROCESSES))
  for process in set(PROCESSES):
    process.kill()

class ProcessWrapper:
  def __init__(self, underlying: asyncio.subprocess.Process):
    self.underlying = underlying
    PROCESSES.add(self)

  async def wait(self):
    result = await self.underlying.wait()
    PROCESSES.remove(self)
    return result

  async def communicate(self, input=None):
    result = await self.underlying.communicate(input)
    PROCESSES.remove(self)
    return result

  def send_signal(self, signal):
    return self.underlying.send_signal(signal)

  def terminate(self):
    PROCESSES.remove(self)
    return self.underlying.terminate()

  def kill(self):
    PROCESSES.remove(self)
    return self.underlying.kill()

  @property
  def stdin(self):
    return self.underlying.stdin

  @property
  def stdout(self):
    return self.underlying.stdout

  @property
  def stderr(self):
    return self.underlying.stderr

  @property
  def pid(self):
    return self.underlying.pid

  @property
  def returncode(self):
    return self.underlying.returncode

async def create_subprocess_exec(*args, **kwargs):
  proc = await asyncio.create_subprocess_exec(*args, **kwargs)
  return ProcessWrapper(proc)

async def create_subprocess_shell(*args, **kwargs):
  proc = await asyncio.create_subprocess_shell(*args, **kwargs)
  return ProcessWrapper(proc)