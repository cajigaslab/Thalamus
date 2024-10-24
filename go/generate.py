import os
import sys
import pathlib
import subprocess

proto_gen = pathlib.Path('proto-gen')
proto_gen.mkdir(exist_ok=True)

gopath = pathlib.Path(subprocess.check_output(['go', 'env', 'GOPATH'], encoding='utf8').strip())
print(gopath)
env = dict(os.environ)
env['PATH'] = env.get('PATH', '') + os.pathsep + str(gopath / 'bin')
print(env['PATH'])
subprocess.check_call([sys.executable, '-m', 'grpc_tools.protoc', '--go_out=proto-gen', '--go_opt=paths=source_relative',
                       '--go-grpc_out=proto-gen', '--go-grpc_opt=paths=source_relative',
                       '-I../proto', '../proto/thalamus.proto'], env=env)
subprocess.check_call([sys.executable, '-m', 'grpc_tools.protoc', '--go_out=proto-gen',
                       '--go_opt=paths=source_relative', '--go-grpc_out=proto-gen', '--go-grpc_opt=paths=source_relative',
                       '-I../proto', '../proto/util.proto'], env=env)
