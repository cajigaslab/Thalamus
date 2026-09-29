using Grpc.Core;
using Grpc.Net.Client;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using Newtonsoft.Json.Linq;
using System.Diagnostics;
using System.IO;
using Nito.AsyncEx;

namespace Thalamus
{
    /// <summary>
    /// PEM files from --cert, --key and --ca, matching the rest of Thalamus. The server
    /// presents Cert/Key and, with Ca, requires clients to present a certificate signed
    /// by it. Clients present Cert/Key as their client certificate and verify servers
    /// against Ca (the system roots without it), expecting every server's certificate
    /// to be issued to ServerName whatever address they connect to.
    /// </summary>
    public class TlsFiles
    {
        public required string Cert { get; init; }
        public required string Key { get; init; }
        public string? Ca { get; init; }
        public required string ServerName { get; init; }

        /// <summary>Cert and Key, usable by both TLS clients and servers.</summary>
        public X509Certificate2 LoadCertificate()
        {
            var pemCert = X509Certificate2.CreateFromPemFile(Cert, Key);
            // The PEM loader leaves the key ephemeral, which SChannel can't use for
            // TLS; a PKCS#12 round trip persists it.
            return new X509Certificate2(pemCert.Export(X509ContentType.Pkcs12));
        }

        /// <summary>The certificates in Ca, or null without it.</summary>
        public X509Certificate2Collection? LoadCa()
        {
            if (Ca == null)
            {
                return null;
            }
            var caCerts = new X509Certificate2Collection();
            caCerts.ImportFromPemFile(Ca);
            return caCerts;
        }

        /// <summary>Whether certificate chains to one of caCerts.</summary>
        public static bool IsSignedBy(X509Certificate certificate, X509Certificate2Collection caCerts)
        {
            using var chain = new X509Chain();
            chain.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
            chain.ChainPolicy.CustomTrustStore.AddRange(caCerts);
            chain.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
            return chain.Build(new X509Certificate2(certificate));
        }
    }

    public static class Util
    {
        private static long frequency = Stopwatch.Frequency;

        public static TimeSpan FromNanoseconds(long ns)
        {
            return new TimeSpan(ns / TimeSpan.NanosecondsPerTick);
        }

        public static long ToNanoseconds(TimeSpan span)
        {
            return span.Ticks * TimeSpan.NanosecondsPerTick;
        }

        public static TimeSpan FromMilliseconds(long ms)
        {
            return new TimeSpan(ms * TimeSpan.TicksPerMillisecond);
        }

        public static long ToMilliseconds(TimeSpan span)
        {
            return span.Ticks / TimeSpan.TicksPerMillisecond;
        }

        public static TimeSpan FromSeconds(double s)
        {
            return new TimeSpan((long)(s * TimeSpan.TicksPerSecond));
        }

        public static double ToSeconds(TimeSpan span)
        {
            return span.Ticks / TimeSpan.TicksPerSecond;
        }

        public static TimeSpan SteadyTime()
        {
            return new TimeSpan(Stopwatch.GetTimestamp());
        }
        /// <summary>
        /// Opens a channel to hostPort ("host:port"), over TLS if tls is given.
        /// </summary>
        public static GrpcChannel CreateChannel(string hostPort, TlsFiles? tls)
        {
            if (tls == null)
            {
                return GrpcChannel.ForAddress(string.Format("http://{0}", hostPort));
            }

            var handler = new SocketsHttpHandler();
            handler.SslOptions.ClientCertificates = new X509CertificateCollection { tls.LoadCertificate() };
            var caCerts = tls.LoadCa();
            handler.SslOptions.RemoteCertificateValidationCallback = (sender, certificate, chain, errors) =>
            {
                if (certificate == null)
                {
                    return false;
                }
                // The certificate is checked against ServerName instead of the
                // address connected to.
                var serverCert = new X509Certificate2(certificate);
                if (!serverCert.MatchesHostname(tls.ServerName))
                {
                    return false;
                }
                var remaining = errors & ~SslPolicyErrors.RemoteCertificateNameMismatch;
                if (caCerts == null)
                {
                    return remaining == SslPolicyErrors.None;
                }
                // Chain errors are re-evaluated against the CA.
                if ((remaining & ~SslPolicyErrors.RemoteCertificateChainErrors) != 0)
                {
                    return false;
                }
                return TlsFiles.IsSignedBy(serverCert, caCerts);
            };
            return GrpcChannel.ForAddress(string.Format("https://{0}", hostPort), new GrpcChannelOptions { HttpHandler = handler });
        }

        public static GrpcChannel FindStateChannel(string rawUrl, TlsFiles? tls)
        {
            var url = rawUrl.StartsWith("http") || rawUrl.StartsWith("https") ? rawUrl : string.Format("http://{0}", rawUrl);
            var uri = new Uri(url);
            var channel = CreateChannel(uri.Authority, tls);
            var client = new Thalamus.ThalamusClient(channel);


            var redirectResponse = client.get_redirect(new Empty());
            var redirect = redirectResponse.Redirect_.Replace("localhost", uri.Host);
            if (redirect == "")
            {
                return channel;
            }


            channel.Dispose();
            return CreateChannel(redirect, tls);
        }

        public static IEnumerable<double> GetData(AnalogNode node, int channel)
        {
            if (node.GetDataType() == AnalogNode.DataType.DOUBLE)
            {
                return node.doubles(channel);
            }
            else if (node.GetDataType() == AnalogNode.DataType.SHORT)
            {
                return node.shorts(channel).Select(s => (double)s);
            }
            else if (node.GetDataType() == AnalogNode.DataType.ULONG)
            {
                return node.ulongs(channel).Select(s => (double)s);
            }
            else
            {
                throw new InvalidOperationException();
            }
        }
    }
}
