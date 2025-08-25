using Grpc.Core;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System.Collections.ObjectModel;
using Thalamus;
using Thalamus.JsonPath;
using static Thalamus.ObservableCollection;
using Nito.AsyncEx;

namespace Thalamus
{
    public class StateManager : IDisposable
    {
        private Thalamus.ThalamusClient client;
        private ObservableCollection root;
        private AsyncDuplexStreamingCall<ObservableTransaction, ObservableTransaction> stream;
        private Dictionary<ulong, Action> callbacks = [];
        private TaskFactory taskFactory;
        private AsyncQueue<ObservableTransaction> requests = new AsyncQueue<ObservableTransaction>();
        public StateManager(Thalamus.ThalamusClient client, TaskFactory taskFactory, ObservableCollection root, TaskCompletionSource done)
        {
            this.client = client;
            this.root = root;
            this.taskFactory = taskFactory;

            stream = client.observable_bridge_v2();

            taskFactory.Run(async () =>
            {
               await foreach(var transaction in requests)
               {
                    await stream.RequestStream.WriteAsync(transaction);
               }
            });

            taskFactory.Run(async () =>
            {
                await foreach (var response in stream.ResponseStream.ReadAllAsync())
                {
                    if (response.Acknowledged != 0)
                    {
                        var callback = callbacks[response.Acknowledged];
                        callback();
                        callbacks.Remove(response.Acknowledged);
                        continue;
                    }

                    foreach (var change in response.Changes)
                    {
                        var reader = new JsonTextReader(new StringReader(change.Value));
                        var parsed = ObservableCollection.Wrap(JToken.ReadFrom(reader), null);
                        if (change.Address.Trim().Length == 0)
                        {
                            if(parsed == null)
                            {
                                throw new Exception("Attemped to create null root");
                            }
                            root.Merge((ObservableCollection)parsed, true);
                        }
                        else
                        {
                            var path = new JPath(change.Address);
                            object? parent = null;
                            object? child = root;
                            object? key = null;
                            foreach (var filter in path.Filters)
                            {
                                if (filter is ArrayIndexFilter index)
                                {
                                    key = index.Index;
                                }
                                else if (filter is FieldFilter field)
                                {
                                    key = field.Name;
                                }
                                if(key == null)
                                {
                                    throw new Exception("Got null key");
                                }
                                if(child is ObservableCollection current)
                                {
                                    var next = current.ContainsKey(key) ? current[key] : null;
                                    parent = child;
                                    child = next;
                                }
                                else
                                {
                                    throw new Exception("Indexing primitive value");
                                }
                            }

                            if (child is ObservableCollection childColl && parsed is ObservableCollection parsedColl
                                   && childColl.GetCollectionType() == parsedColl.GetCollectionType())
                            {
                                childColl.Merge(parsedColl, true);
                            }
                            else if(parent is ObservableCollection parentColl)
                            {
                                if(key == null)
                                {
                                    throw new Exception("null key");
                                }
                                parentColl.Set(key, parsed, () => { }, true);
                            }
                            else
                            {
                                throw new Exception("Couldn't apply change");
                            }
                        }
                        var text = JsonConvert.SerializeObject(root, Formatting.Indented, new JsonSerializerSettings
                        {
                            Converters = [new ObservableCollection.JsonConverter()]
                        });
                        Console.WriteLine(text);
                    }
                }
                done.SetResult();
                Console.WriteLine("DONE");
            });
        }

        private uint nextId = 0;

        public void RequestChange(ObservableChange.Types.Action action, string address, object? value, Action callback)
        {
            var change = new ObservableChange
            {
                Action = action,
                Address = address,
                Value = JsonConvert.SerializeObject(value),
                Id = nextId
            };
            ++nextId;
            callbacks[change.Id] = callback;
            var transaction = new ObservableTransaction();
            transaction.Changes.Add(change);
            requests.Put(transaction);
        }

        public void Dispose()
        {
            stream.Dispose();
            requests.Dispose();
        }
    }
}
