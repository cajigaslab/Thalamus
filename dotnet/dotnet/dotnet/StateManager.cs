using Grpc.Core;
using Newtonsoft.Json.Linq;
using System.Collections.ObjectModel;
using Thalamus;
using Thalamus.JsonPath;

namespace dotnet
{
    public class StateManager : IDisposable
    {
        private Thalamus.Thalamus.ThalamusClient client;
        private JObject state;
        private AsyncDuplexStreamingCall<ObservableTransaction, ObservableTransaction> stream;
        private Dictionary<ulong, Action> callbacks;
        private MainThread mainThread;
        public StateManager(Thalamus.Thalamus.ThalamusClient client, MainThread mainThread, JObject state)
        {
            this.client = client;
            this.state = state;
            this.mainThread = mainThread;

            stream = client.observable_bridge_v2();

            Task.Run(async () =>
            {
                await foreach (var response in stream.ResponseStream.ReadAllAsync())
                {
                    if(response.Acknowledged != 0)
                    {
                        var callback = callbacks[response.Acknowledged];
                        mainThread.Push(callback);
                        callbacks.Remove(response.Acknowledged);
                        continue;
                    }

                    foreach(var change in response.Changes)
                    {
                        var parsed = JObject.Parse(change.Value);
                        mainThread.Push(() =>
                        {
                            var path = new JPath(change.Address);
                            var token = state.SelectToken(change.Address);
                        });
                    }
                }
            });
        }
        public void Dispose()
        {
            stream.Dispose();
            throw new NotImplementedException();
        }
    }
}
