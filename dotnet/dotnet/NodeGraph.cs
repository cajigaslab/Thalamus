using System.Xml.Linq;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    public class INodeGraph
    {

    }

    public class NodeGraph : INodeGraph, IDisposable
    {
        private Dictionary<string, Func<ObservableCollection, MainThread, INodeGraph, Node>> factories
            = new Dictionary<string, Func<ObservableCollection, MainThread, INodeGraph, Node>>();
        private List<Action> Cleanup = new List<Action>();
        private ObservableCollection nodes;
        private MainThread MainThread;
        private Dictionary<int, Node> nodeImpls = new Dictionary<int, Node>();
        public NodeGraph(ObservableCollection nodes, MainThread mainThread)
        {
            this.MainThread = mainThread;

            if (DelsysNode.Prepare())
            {
                factories["DELSYS"] = (a, b, c) => new DelsysNode(a, b, c);
                Cleanup.Add(() => DelsysNode.Cleanup());
            }
            this.nodes = nodes;
            nodes.Subscriptions += new ObservableCollection.OnChange(OnChange);
        }

        private void UpdateNode(ObservableCollection node)
        {
            var type = (string?)node["type"];
            if (type == null)
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

            if(nodeImpls.ContainsKey(index))
            {
                nodeImpls[index].Dispose();
            }

            var nodeImpl = factories[type](node, MainThread, this);
            nodeImpls[index] = nodeImpl;
            node.Recap();
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
                    UpdateNode((ObservableCollection)value);
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
                UpdateNode(source);
            }
        }

        public void Dispose()
        {
            Cleanup.ForEach(x => x());
        }
    }
}