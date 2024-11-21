from thalamus import thalamus_pb2
import struct

with open(r'C:\Users\bijanadmin\Documents\Dmitrijs\thalamus_data\test.tha.20241114215953.1', 'rb') as tha_file:
  while True:
    size_bytes = tha_file.read(8)
    print(size_bytes)
    size, = struct.unpack('>Q', size_bytes)
    print(size)
    message_bytes = tha_file.read(size)
    message = thalamus_pb2.StorageRecord()
    message.ParseFromString(message_bytes)
    print(message)
    if message.WhichOneof('body') == 'text':
    #   message.image.data.clear()
      break
    if message.WhichOneof('body') == 'image':
      message.image.data.clear()
      break