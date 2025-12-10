using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.DataProtection.KeyManagement;
using Newtonsoft.Json.Linq;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    public interface Node : IDisposable
    {
        public delegate void OnReady(Node node);

        public OnReady Ready { get; set; }

        public Task<JToken> Process(JToken request) { return Task<JToken>.FromResult((JToken)(new JObject())); }

        public string Redirect()
        {
            return "";
        }
    }
}