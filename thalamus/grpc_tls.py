"""
gRPC TLS configuration shared by every server and channel in a process.

The --cert, --key and --ca flags name PEM files. With none of them given gRPC
runs without TLS, as before. Otherwise --cert and --key are both required
(every Thalamus program runs a server), and:
  - servers present cert/key, and with --ca require clients to present a
    certificate signed by it;
  - clients verify servers against --ca (the system roots without it) and
    present cert/key as their client certificate;
  - clients expect every server's certificate to be issued to --server-name
    (default thalamus.internal), whatever address they connect to, so certificates
    needn't name localhost or IPs.

The native thalamus process implements the same flags (src/thalamus/grpc_tls.cpp).
"""

import argparse
import dataclasses
import pathlib
import typing

import grpc
import grpc.aio

@dataclasses.dataclass
class _TlsConfig:
  cert_path: str
  key_path: str
  ca_path: typing.Optional[str]
  cert: bytes
  key: bytes
  ca: typing.Optional[bytes]
  server_name: str

_CONFIG: typing.Optional[_TlsConfig] = None

def add_arguments(parser: argparse.ArgumentParser) -> None:
  '''
  Adds --cert, --key, --ca and --server-name to parser
  '''
  parser.add_argument('--cert', help='PEM certificate for gRPC TLS, used by servers and as the client certificate')
  parser.add_argument('--key', help='PEM private key for --cert')
  parser.add_argument('--ca', help='PEM certificate authority that servers verify clients against and clients verify servers against')
  parser.add_argument('--server-name', default='thalamus.internal',
                      help='Name clients expect in every gRPC server\'s TLS certificate, regardless of the address they connect to')

def configure(arguments: argparse.Namespace) -> None:
  '''
  Loads the TLS files named by the parsed --cert, --key and --ca flags. Call once at startup, before creating any
  server or channel.
  '''
  global _CONFIG
  _CONFIG = None
  cert, key, ca = arguments.cert, arguments.key, arguments.ca
  if cert is None and key is None and ca is None:
    return
  if cert is None or key is None:
    raise ValueError('gRPC TLS requires both --cert and --key')
  _CONFIG = _TlsConfig(
    cert_path=str(pathlib.Path(cert).absolute()),
    key_path=str(pathlib.Path(key).absolute()),
    ca_path=str(pathlib.Path(ca).absolute()) if ca is not None else None,
    cert=pathlib.Path(cert).read_bytes(),
    key=pathlib.Path(key).read_bytes(),
    ca=pathlib.Path(ca).read_bytes() if ca is not None else None,
    server_name=arguments.server_name)

def enabled() -> bool:
  '''
  Whether configure enabled TLS
  '''
  return _CONFIG is not None

def command_line_args() -> typing.Tuple[str, ...]:
  '''
  The flags that give a child process (native thalamus, the image viewer) the same TLS configuration
  '''
  if _CONFIG is None:
    return ()
  args = ('--cert', _CONFIG.cert_path, '--key', _CONFIG.key_path, '--server-name', _CONFIG.server_name)
  if _CONFIG.ca_path is not None:
    args += ('--ca', _CONFIG.ca_path)
  return args

def server_credentials() -> typing.Optional[grpc.ServerCredentials]:
  '''
  Server credentials, or None without TLS
  '''
  if _CONFIG is None:
    return None
  return grpc.ssl_server_credentials(
    [(_CONFIG.key, _CONFIG.cert)],
    root_certificates=_CONFIG.ca,
    require_client_auth=_CONFIG.ca is not None)

def channel_credentials() -> typing.Optional[grpc.ChannelCredentials]:
  '''
  Channel credentials, or None without TLS
  '''
  if _CONFIG is None:
    return None
  return grpc.ssl_channel_credentials(
    root_certificates=_CONFIG.ca,
    private_key=_CONFIG.key,
    certificate_chain=_CONFIG.cert)

def add_port(server: typing.Union[grpc.Server, grpc.aio.Server], address: str) -> int:
  '''
  Adds a listening port to server, secured with TLS if configured
  '''
  credentials = server_credentials()
  if credentials is None:
    return server.add_insecure_port(address)
  return server.add_secure_port(address, credentials)

def aio_channel(target: str) -> grpc.aio.Channel:
  '''
  Opens an asyncio channel to target, secured with TLS if configured
  '''
  credentials = channel_credentials()
  if credentials is None:
    return grpc.aio.insecure_channel(target)
  assert _CONFIG is not None
  return grpc.aio.secure_channel(target, credentials, options=[('grpc.ssl_target_name_override', _CONFIG.server_name)])
