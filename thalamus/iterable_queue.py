import asyncio

class IterableQueue:
  def __init__(self):
    print(88)
    self.queue = asyncio.Queue()
    print(80)
    self.sentinel = object()
    print(81)

  def put(self, item):
    return self.queue.put(item)
  
  def close(self):
    return self.queue.put(self.sentinel)

  def join(self):
    return self.queue.join()

  def __aiter__(self):
    return self

  async def __anext__(self):
    try:
      item = await self.queue.get()
    except asyncio.CancelledError:
      raise StopAsyncIteration

    self.queue.task_done()
    if item is self.sentinel:
      raise StopAsyncIteration
    return item
  
  async def __aenter__(self):
    return self

  async def __aexit__(self, *exc):
    await self.close()
    await self.join()
    return False
