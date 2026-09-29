#include <thalamus/grpc_tls.hpp>
#include <thalamus/log.hpp>

#include <fstream>
#include <optional>
#include <sstream>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <grpcpp/grpcpp.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {

namespace {
struct GrpcTlsPem {
  std::string cert;
  std::string key;
  std::string ca;
  std::string server_name;
};

// Set once by configure_grpc_tls at startup, before any other thread uses it.
// Never freed, so there's no destructor to run at exit.
std::optional<GrpcTlsPem> &tls_pem_storage() {
  static auto *storage = new std::optional<GrpcTlsPem>();
  return *storage;
}

std::optional<std::string> read_file(const std::string &path, const char *flag) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    THALAMUS_LOG(error) << "Failed to read " << flag << " file " << path;
    return std::nullopt;
  }
  std::stringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}
} // namespace

bool configure_grpc_tls(const GrpcTlsFiles &files) {
  auto &tls_pem = tls_pem_storage();
  tls_pem.reset();
  if (files.cert.empty() && files.key.empty() && files.ca.empty()) {
    return true;
  }
  if (files.cert.empty() || files.key.empty()) {
    THALAMUS_LOG(error) << "gRPC TLS requires both --cert and --key";
    return false;
  }

  GrpcTlsPem pem;
  auto cert = read_file(files.cert, "--cert");
  auto key = read_file(files.key, "--key");
  if (!cert || !key) {
    return false;
  }
  pem.cert = std::move(*cert);
  pem.key = std::move(*key);
  pem.server_name = files.server_name;
  if (!files.ca.empty()) {
    auto ca = read_file(files.ca, "--ca");
    if (!ca) {
      return false;
    }
    pem.ca = std::move(*ca);
  }
  tls_pem = std::move(pem);
  THALAMUS_LOG(info) << "gRPC TLS enabled"
                     << (tls_pem->ca.empty() ? "" : " with client certificate verification")
                     << ", expecting server name " << tls_pem->server_name;
  return true;
}

bool grpc_tls_enabled() { return tls_pem_storage().has_value(); }

std::shared_ptr<grpc::ServerCredentials> grpc_server_credentials() {
  auto &tls_pem = tls_pem_storage();
  if (!tls_pem) {
    return grpc::InsecureServerCredentials();
  }
  grpc::SslServerCredentialsOptions options(
      tls_pem->ca.empty()
          ? GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE
          : GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  options.pem_root_certs = tls_pem->ca;
  options.pem_key_cert_pairs.push_back({tls_pem->key, tls_pem->cert});
  return grpc::SslServerCredentials(options);
}

std::shared_ptr<grpc::Channel> create_grpc_channel(const std::string &target) {
  auto &tls_pem = tls_pem_storage();
  if (!tls_pem) {
    return grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  }
  grpc::SslCredentialsOptions options;
  options.pem_root_certs = tls_pem->ca;
  options.pem_private_key = tls_pem->key;
  options.pem_cert_chain = tls_pem->cert;
  grpc::ChannelArguments args;
  if (!tls_pem->server_name.empty()) {
    args.SetSslTargetNameOverride(tls_pem->server_name);
  }
  return grpc::CreateCustomChannel(target, grpc::SslCredentials(options), args);
}

} // namespace thalamus
