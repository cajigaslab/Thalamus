using Grpc.Core;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System.Collections.ObjectModel;
using Thalamus;
using Thalamus.JsonPath;
using static Thalamus.ObservableCollection;
using Nito.AsyncEx;
using System.Collections.Concurrent;

namespace Thalamus
{
    public class StateManager : IDisposable
    {
        private Thalamus.ThalamusClient client;
        private ObservableCollection root;
        private Dictionary<ulong, Action> callbacks = [];
        private TaskFactory taskFactory;
        private AsyncCollection<ObservableTransaction> requests = new AsyncCollection<ObservableTransaction>();
        public StateManager(Thalamus.ThalamusClient client, TaskFactory taskFactory, ObservableCollection root, TaskCompletionSource done)
        {
            this.client = client;
            this.root = root;
            this.taskFactory = taskFactory;
        }

        public async Task Start(CancellationTokenSource cancellationTokenSource) {
            var stream = client.observable_bridge_v2(null, null, cancellationTokenSource.Token);

            var writeTask = taskFactory.Run(async () =>
            {
                try
                {
                    while (true)
                    {
                        var transaction = await requests.TakeAsync(cancellationTokenSource.Token);
                        await stream.RequestStream.WriteAsync(transaction);
                    }
                }
                catch (OperationCanceledException e)
                {

                }
            });

            var readTask = taskFactory.Run(async () =>
            {
                try
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
                                if (parsed == null)
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
                                    if (key == null)
                                    {
                                        throw new Exception("Got null key");
                                    }
                                    if (child is ObservableCollection current)
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

                                if (change.Action == ObservableChange.Types.Action.Delete)
                                {
                                    if (parent is ObservableCollection parentColl)
                                    {
                                        if (key == null)
                                        {
                                            throw new Exception("null key");
                                        }
                                        parentColl.Remove(key, () => { }, true);
                                    }
                                }
                                else if (child is ObservableCollection childColl && parsed is ObservableCollection parsedColl
                                       && childColl.GetCollectionType() == parsedColl.GetCollectionType())
                                {
                                    childColl.Merge(parsedColl, true);
                                }
                                else if (parent is ObservableCollection parentColl)
                                {
                                    if (key == null)
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
                }
                finally
                {
                    cancellationTokenSource.Cancel();
                    Console.WriteLine("DONE");
                }
            });
            var temp = await Task.WhenAny(readTask, writeTask);
            await temp;
            return;
        }

        private uint nextId = 0;

        public void RequestChange(ObservableChange.Types.Action action, string address, object? value, Action callback)
        {
            var change = new ObservableChange
            {
                Action = action,
                Address = address,
                Value = JsonConvert.SerializeObject(value, new JsonSerializerSettings { Converters = [new ObservableCollection.JsonConverter()] }),
                Id = nextId
            };
            ++nextId;
            callbacks[change.Id] = callback;
            var transaction = new ObservableTransaction();
            transaction.Changes.Add(change);
            requests.Add(transaction);
        }

        public void Dispose()
        {
            requests.CompleteAdding();
        }
    }
}
