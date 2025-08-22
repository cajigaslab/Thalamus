using Grpc.Core;
using Grpc.Net.Client;
using Newtonsoft.Json.Linq;
using System.Diagnostics;
using System.IO;

namespace Thalamus
{
    public class Util
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

        public static TimeSpan FromMillisconds(long ms)
        {
            return new TimeSpan(ms * TimeSpan.TicksPerMillisecond);
        }

        public static long ToMillisconds(TimeSpan span)
        {
            return span.Ticks / TimeSpan.TicksPerMillisecond;
        }

        public static TimeSpan SteadyTime()
        {
            return new TimeSpan(Stopwatch.GetTimestamp());
        }
        public static GrpcChannel FindStateChannel(string rawUrl)
        {
            var url = rawUrl.StartsWith("http") || rawUrl.StartsWith("https") ? rawUrl : string.Format("http://{0}", rawUrl);
            var uri = new Uri(url);
            var channel = GrpcChannel.ForAddress(url);
            var client = new Thalamus.ThalamusClient(channel);


            var redirectResponse = client.get_redirect(new Empty());
            var redirect = redirectResponse.Redirect_.Replace("localhost", uri.Host);
            if (redirect == "")
            {
                return channel;
            }


            channel.Dispose();
            return GrpcChannel.ForAddress(string.Format("http://{0}", redirect));
        }
    }
}
