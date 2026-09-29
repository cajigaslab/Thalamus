#pragma once

#include <memory>
#include <string>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {

/// Paths to the PEM files given by --cert, --key and --ca (empty means not
/// given), and the --server-name clients expect in every server's
/// certificate.
struct GrpcTlsFiles {
  std::string cert;
  std::string key;
  std::string ca;
  std::string server_name = "thalamus.internal";
};

/// Loads the TLS files every gRPC server and channel in this process uses.
/// With no files given gRPC runs without TLS, as before. Otherwise --cert and
/// --key are both required (every Thalamus process runs a server), and:
///   - servers present cert/key, and with --ca require clients to present a
///     certificate signed by it;
///   - clients verify servers against --ca (the system roots without it) and
///     present cert/key as their client certificate;
///   - clients expect every server's certificate to be issued to
///     --server-name, whatever address they connect to, so certificates
///     needn't name localhost or IPs.
/// Returns false, after logging why, if the files are incomplete or
/// unreadable. Call once at startup, before any server or channel is created.
bool configure_grpc_tls(const GrpcTlsFiles &files);

/// Whether configure_grpc_tls enabled TLS.
bool grpc_tls_enabled();

/// Credentials for ServerBuilder::AddListeningPort.
std::shared_ptr<grpc::ServerCredentials> grpc_server_credentials();

/// Opens a channel to target ("host:port"), with TLS and --server-name if
/// configured. Use this rather than grpc::CreateChannel.
std::shared_ptr<grpc::Channel> create_grpc_channel(const std::string &target);

} // namespace thalamus
