using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.DataProtection.KeyManagement;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    public interface Node : IDisposable
    {
        public delegate void OnReady(Node node);

        public OnReady Ready { get; set; }
    }
}