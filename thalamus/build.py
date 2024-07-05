import os
import re
import sys
import toml
import shutil
import base64
import zipfile
import tarfile
import hashlib
import pathlib
import platform
import subprocess
import configparser

def urlsafe_b64encode_nopad(data):
    return base64.urlsafe_b64encode(data).decode().rstrip('=')

def generate():
  services = [
      'ophanim',
      'trace_event/trace_event_grpc',
      'task_controller',
      'util',
      'thalamus'
  ]
  for service in services:
    shutil.copy(f'proto/{service}.proto', f'thalamus/{service}.proto')
    subprocess.check_call([sys.executable, '-m', 'grpc_tools.protoc', '-Iproto', '--python_out=thalamus',
                           '--grpc_python_out=thalamus', f'proto/{service}.proto'])
  dot = "\\."
  regex = re.compile(f'^(from \\w+ )?import ({"|".join(s.split("/")[-1] for s in services).replace("/", dot)})_pb2 as')
  for service in services:
    for suffix in ['_pb2', '_pb2_grpc']:
      old_path, new_path = pathlib.Path(f'thalamus/{service}{suffix}.py'), pathlib.Path(f'thalamus/{service}{suffix}.py.new')
      with open(str(old_path)) as old_file, open(str(new_path), 'w') as new_file:
        for line in old_file:
          new_line = regex.sub('from . import \\2_pb2 as', line)
          new_file.write(new_line)
      old_path.unlink()
      new_path.rename(old_path)

def write_metadata(metadata, filename):
  version = metadata['version']
  description = metadata['description']
  name = metadata['name']
  maintainer = metadata['maintainer']
  maintainer_email = metadata['maintainer_email']
  license = metadata['license']

  with open(filename, 'w') as wheel_file:
    wheel_file.write('Metadata-Version: 2.1\n')
    wheel_file.write(f'Name: {name}\n')
    wheel_file.write(f'Version: {version}\n')
    wheel_file.write(f'Summary: {description}\n')
    wheel_file.write(f'Maintainer: {maintainer}\n')
    wheel_file.write(f'Maintainer-email: {maintainer_email}\n')
    wheel_file.write(f'License: {license}\n')

    with open('requirements.txt') as requirements_file:
      for line in requirements_file:
        wheel_file.write(f'Requires-Dist: {line}')

def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
  config_settings = config_settings if config_settings else {}

  print('build_wheel', pathlib.Path.cwd())
  print(wheel_directory)
  print(config_settings)
  print(metadata_directory)

  generate()
  if 'generate' in config_settings:
    return

  is_release = 'release' in config_settings
  do_config = 'config' in config_settings
  generator = config_settings.get('generator', 'Ninja')
  sanitizer = config_settings.get('sanitizer', None)
  target = config_settings.get('target', None)

  build_path = pathlib.Path.cwd() / 'build' / f'{platform.python_implementation()}-{platform.python_version()}-{"release" if is_release else "debug"}'
  if sanitizer:
    build_path = build_path.with_name(build_path.name + '-' + sanitizer)

  config = toml.load('pyproject.toml')
  metadata = config['metadata']
  version = metadata['version']
  description = metadata['description']
  name = metadata['name']
  maintainer = metadata['maintainer']
  maintainer_email = metadata['maintainer_email']
  license = metadata['license']

  platform_tag = None
  if platform.system() == 'Windows':
    platform_tag = 'win_amd64'
  elif platform.system() == 'Linux':
    ldd_output = subprocess.check_output(['ldd', '--version'], encoding='utf8')
    assert ldd_output is not None
    ldd_line = [l.strip() for l in ldd_output.split('\n') if l[:3] == 'ldd'][0]
    libc_version = ldd_line.split(' ')[-1]
    platform_tag = f'manylunux_{libc_version.replace(".", "_")}'
  assert platform_tag is not None

  whl_name = f'thalamus-{version}-py3-none-{platform_tag}.whl'
  pathlib.Path(f'thalamus-{version}.dist-info').mkdir(exist_ok=True)

  with open(f'thalamus-{version}.dist-info/WHEEL', 'w') as wheel_file:
    wheel_file.write('Wheel-Version: 1.0\n')
    wheel_file.write('Generator: thalamus.build\n')

  write_metadata(metadata, f'thalamus-{version}.dist-info/METADATA')

  cmake_command = [
    'cmake',
    '-S', pathlib.Path.cwd(),
    '-B', build_path,
    f'-DCMAKE_BUILD_TYPE={"Release" if is_release else "Debug"}',
    '-DENABLE_SWIG=OFF',
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
  ]
  if generator:
    cmake_command += ['-G', generator]

  if sys.platform == 'win32':
    cmake_command += [
      '-DCMAKE_C_COMPILER=cl',
      '-DCMAKE_CXX_COMPILER=cl']
  else:
    cmake_command += [
      '-DCMAKE_C_COMPILER=clang',
      '-DCMAKE_CXX_COMPILER=clang++',
      '-DCMAKE_LINKER=clang']

  for key, value in config_settings.items():
    if key[0] == 'D':
      cmake_command += [f'-{key}={value}']

  if sanitizer:
    cmake_command += [f'-DSANITIZER={sanitizer}']

  if not (build_path / 'CMakeCache.txt').exists() or do_config:
    subprocess.check_call(cmake_command)
  shutil.copy(build_path / 'compile_commands.json', 'compile_commands.json')

  command = ['cmake', '--build', build_path, '--config', "Release" if is_release else "Debug", '--parallel', str(os.cpu_count())]
  if target:
    command += ['--target', target]

  print(command)
  subprocess.check_call(command)
  shutil.copy('src/plugin.h', 'thalamus/plugin.h')

  files = []
  with open(f'thalamus-{version}.dist-info/RECORD', 'w') as record_file:
    for path in pathlib.Path('thalamus').rglob('*'):
      if not path.is_file() or path.suffix not in ('.py', '.vert', '.proto', '.comp', '.frag', '.exe', '.dll', '.so', '.h'):
        continue
      files.append(path)
      digest = hashlib.sha256()
      with open(str(path), 'rb') as pack_file:
        for buffer in iter(lambda: pack_file.read(1024), b''):
          digest.update(buffer)
        digest_b64 = urlsafe_b64encode_nopad(digest.digest())
        record_file.write(f'{pack_file},sha256={digest_b64},{path.stat().st_size}\n')

  with zipfile.ZipFile(pathlib.Path(wheel_directory) / whl_name, 'w', zipfile.ZIP_DEFLATED) as whl_file:
    whl_file.write(f'thalamus-{version}.dist-info/WHEEL')
    whl_file.write(f'thalamus-{version}.dist-info/RECORD')
    whl_file.write(f'thalamus-{version}.dist-info/METADATA')
    for f in files:
      whl_file.write(f)

  return whl_name

def build_sdist(sdist_directory, config_settings=None):
  print('build_sdist', pathlib.Path.cwd())
  print(sdist_directory)
  print(config_settings)

  config = toml.load('pyproject.toml')
  metadata = config['metadata']
  version = metadata['version']

  root = pathlib.Path(f'thalamus-{version}')
  sdist_name = f'{root}.tar.gz'

  write_metadata(metadata, f'PKG-INFO')
  files = []
  with tarfile.open(sdist_name, "w:gz") as tar:
    tar.add('PKG-INFO', root/'PKG-INFO')
    for dir in ['thalamus', 'proto', 'thalamus', 'src']:
      for path in pathlib.Path(dir).rglob('*'):
        if not path.is_file():
          continue
        if path.suffix in ('.py', '.vert', '.proto', '.comp', '.frag'):
          tar.add(str(path), root/path)

  return sdist_name
