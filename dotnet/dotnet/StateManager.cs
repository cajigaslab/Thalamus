using Grpc.Core;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System.Collections.ObjectModel;
using Thalamus;
using Thalamus.JsonPath;

namespace dotnet
{
    public class StateManager : IDisposable
    {
        private Thalamus.Thalamus.ThalamusClient client;
        private JObject root;
        private AsyncDuplexStreamingCall<ObservableTransaction, ObservableTransaction> stream;
        private Dictionary<ulong, Action> callbacks;
        private MainThread mainThread;
        public StateManager(Thalamus.Thalamus.ThalamusClient client, MainThread mainThread, JObject root)
        {
            this.client = client;
            this.root = root;
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
                        var reader = new JsonTextReader(new StringReader(change.Value));
                        var parsed = JToken.ReadFrom(reader);
                        mainThread.Push(() =>
                        {
                            if(change.Address.Trim().Length == 0)
                            {
                                root.Merge(parsed);
                            }
                            else
                            {
                                var selected = root.SelectToken(change.Address);
                                if (selected == null)
                                {
                                    var path = new JPath(change.Address);
                                    var last = path.Filters.Last();
                                    path.Filters.RemoveAt(path.Filters.Count - 1);

                                    var parent = path.Filters.Count > 0 ? path.Evaluate(root, root, null).First() : root;
                                    if (last is ArrayIndexFilter)
                                    {
                                        var index = (ArrayIndexFilter)last;
                                        if (parent is JObject)
                                        {
                                            var obj = (JObject)parent;
                                            obj[index.Index] = parsed;
                                        }
                                        else if (parent is JArray)
                                        {
                                            var arr = (JArray)parent;
                                            if (index.Index < arr.Count)
                                            {
                                                arr[index.Index] = parsed;
                                            }
                                            else
                                            {
                                                while (arr.Count < index.Index)
                                                {
                                                    arr.Add(null);
                                                }
                                                arr.Add(parsed);
                                            }
                                        }
                                    }
                                    else if (last is FieldFilter)
                                    {
                                        var field = (FieldFilter)last;
                                        var obj = (JObject)parent;
                                        obj[field.Name] = parsed;
                                    }
                                    else
                                    {
                                        throw new Exception("Unexpected JSONPath filter type " + last);
                                    }
                                }
                                else
                                {
                                    selected.Replace(parsed);
                                }
                            }
                            //Console.WriteLine(selected);
                            //var path = new JPath(change.Address);
                            //if(path.Filters.Count == 0)
                            //{
                            //    root.Merge(parsed);
                            //}
                            //var current = root;
                            //foreach(var p in path.Filters.Take(path.Filters.Count-1))
                            //{
                            //    Console.WriteLine(p);
                            //}

                            //Console.WriteLine(path);
                            //Console.WriteLine(parsed);
                            //var token = state.SelectToken(change.Address);
                            Console.WriteLine(root);
                        });
                    }
                }
                Console.WriteLine("DONE");
            });
        }
        public void Dispose()
        {
            stream.Dispose();
            throw new NotImplementedException();
        }
    }
}
