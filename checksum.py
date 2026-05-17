import sys
import zlib
import time
import pathlib
import argparse

class AdlerHasher:
  def __init__(self):
    self.current = 1

  def update(self, data):
    self.current = zlib.adler32(data, self.current)

  def hexdigest(self):
    digits = hex(self.current)[2:]
    return digits.rjust(8, '0')

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('-i', '--input', type=pathlib.Path)
  parser.add_argument('-a', '--algorithm')
  args = parser.parse_args()

  if args.algorithm == 'adler':
    hasher = AdlerHasher()

  last = time.time()
  filesize = args.input.stat().st_size
  pos = 0
  with open(args.input, 'rb') as input_file:
    data = b'!'
    while data:
      data = input_file.read(1024*1024)
      pos += len(data)
      hasher.update(data)
      now = time.time()
      if now - last > 1:
        print(100*(pos/filesize), pos, filesize, file=sys.stderr)
        last = now

  print(hasher.hexdigest())


if __name__ == '__main__':
  main()