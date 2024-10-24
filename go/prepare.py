import subprocess

subprocess.check_call(['go', 'install', 'google.golang.org/protobuf/cmd/protoc-gen-go@latest'])
subprocess.check_call(['go', 'install', 'google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest'])
