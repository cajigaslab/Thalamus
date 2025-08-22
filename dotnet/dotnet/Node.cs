using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.DataProtection.KeyManagement;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    class Node : IDisposable
    {
        public delegate void OnChange(object source, ActionType action, object key, object? value);

        public OnChange Subscriptions { get; set; }

        public Node()
        {
            Subscriptions = new OnChange((source, action, key, value) => { });
        }

        public void Dispose()
        {
        }
    }
}