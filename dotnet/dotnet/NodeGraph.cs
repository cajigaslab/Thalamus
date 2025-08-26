using Nito.AsyncEx;
using System.Xml.Linq;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    public interface INodeGraph
    {
        string GetAddress();
        public Task<Node> GetNode(NodeSelector selector);
        public Task Run(Func<Task> action);
    }

    public class NodeGraph : INodeGraph, IDisposable
    {
        private Dictionary<string, Func<ObservableCollection, TaskFactory, INodeGraph, Node>> factories
            = new Dictionary<string, Func<ObservableCollection, TaskFactory, INodeGraph, Node>>();
        private List<Action> Cleanup = new List<Action>();
        private ObservableCollection nodes;
        private TaskFactory TaskFactory;
        private Dictionary<int, Node> nodeImpls = new Dictionary<int, Node>();
        private string address = "";

        private List<(NodeSelector selector, TaskCompletionSource<Node> task)> pendingNodeGets = [];
        private TaskCompletionSource done;

        public NodeGraph(ObservableCollection nodes, TaskFactory taskFactory, string address, TaskCompletionSource done)
        {
            this.TaskFactory = taskFactory;
            this.address = address;
            this.done = done;

            if (DelsysNode.Prepare())
            {
                factories["DELSYS"] = (a, b, c) => new DelsysNode(a, b, c);
                Cleanup.Add(() => DelsysNode.Cleanup());
            }
            this.nodes = nodes;
            nodes.Subscriptions += new ObservableCollection.OnChange(OnChange);
        }

        public Task Run(Func<Task> action)
        {
            return TaskFactory.Run(async () =>
            {
                try
                {
                    await action();
                }
                catch (Exception ex)
                {
                    done.SetException(ex);
                }
            });
        }

        public Task<Node> GetNode(NodeSelector selector)
        {
            var field = "";
            var value = "";
            if (selector.Name.Length > 0)
            {
                field = "name";
                value = selector.Name;
            }
            else
            {
                field = "type";
                value = selector.Type;
            }

            var i = 0;
            foreach (var node in nodes.Values())
            {
                if (node is ObservableCollection coll)
                {
                    var name = coll[field];
                    if (value == (string?)name)
                    {
                        return Task.FromResult(nodeImpls[i]);
                    }
                }
                ++i;
            }

            var task = new TaskCompletionSource<Node>();
            pendingNodeGets.Add((selector, task));
            return task.Task;
        }

        public string GetAddress()
        {
            return address;
        }

        private void UpdateNode(ObservableCollection node, object? field)
        {
            var type = (string?)node["type"];
            if (type == null)
            {
                throw new ArgumentNullException("value");
            }
            var name = (string?)node["name"];
            if (name == null)
            {
                throw new ArgumentNullException("value");
            }

            var maybeIndex = node.Parent?.KeyOf(node);
            if (maybeIndex == null)
            {
                throw new ArgumentNullException("index");
            }
            var index = (int)maybeIndex;

            if (!factories.ContainsKey(type))
            {
                return;
            }

            if(field == null || (string?)field == "type")
            {
                if (nodeImpls.ContainsKey(index))
                {
                    nodeImpls[index].Dispose();
                }

                var nodeImpl = factories[type](node, TaskFactory, this);
                nodeImpls[index] = nodeImpl;
            }

            List<int> toRemove = [];
            var i = 0;
            foreach ( var pending in pendingNodeGets )
            {
                if(pending.selector.Name == name || pending.selector.Type == type)
                {
                    pending.task.SetResult(nodeImpls[i]);
                    toRemove.Add(i);
                }
                ++i;
            }

            foreach(var j in toRemove.Reverse<int>())
            {
                pendingNodeGets.RemoveAt(j);
            }
        }

        public void OnChange(ObservableCollection source, ActionType action, object key, object? value)
        {
            if (source == nodes)
            {
                if (action == ActionType.Set)
                {
                    if (value == null)
                    {
                        throw new ArgumentNullException("value");
                    }
                    UpdateNode((ObservableCollection)value, null);
                }
                else
                {
                    if (nodeImpls.ContainsKey((int)key))
                    {
                        nodeImpls[(int)key].Dispose();
                        nodeImpls.Remove((int)key);
                    }
                    throw new ArgumentNullException("value");
                }
            }
            else if(source.Parent == nodes)
            {
                UpdateNode(source, key);
            }
        }

        public void Dispose()
        {
            Cleanup.ForEach(x => x());
        }
    }
}