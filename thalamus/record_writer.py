import re
import io
import sys
import time
import zlib
import json
import shutil
import typing
import struct
import pickle
import pathlib
import argparse
import threading
import traceback
import itertools
import contextlib
import subprocess
import collections
from multiprocessing.pool import ThreadPool

import yaml
import numpy
import scipy.io
import pkg_resources

from thalamus.thalamus_pb2 import StorageRecord, Image, Compressed
import google.protobuf.message

EXECUTABLE_EXTENSION = '.exe' if sys.platform == 'win32' else ''

LONG = ">Q"
LONG_SIZE = struct.calcsize(LONG)
MAX_SIZE = 40e6

def write_record(stream: io.BufferedWriter, message: StorageRecord) -> int:
  serialized = message.SerializeToString()
  serialized_size = struct.pack(LONG, len(serialized))
  
  stream.write(serialized_size)
  stream.write(serialized)

  return len(serialized_size) + len(serialized)
