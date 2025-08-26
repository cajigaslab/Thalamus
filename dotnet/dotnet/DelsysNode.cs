using DelsysAPI.DelsysDevices;
using MathNet.Numerics.Statistics;
using System.Diagnostics;
using static Thalamus.ObservableCollection;
using Nito.AsyncEx;

namespace Thalamus
{
    class DelsysNode : Node, AnalogNode
    {
        private ObservableCollection state;
        private string? key = null;
        private string? license = null;
        private DeviceSourcePortable? deviceSourceCreator = null;
        private IDelsysDevice? deviceSource = null;
        public Node.OnReady Ready { get; set; }
        public AnalogNode.OnChannelsChanged ChannelsChanged { get; set; }

        private TimeSpan now = Util.SteadyTime();
        private bool running = false;
        private Task? task = null;
        private double[] data = [];
        private TaskFactory taskFactory;
        private INodeGraph graph;

        public DelsysNode(ObservableCollection state, TaskFactory taskFactory, INodeGraph graph)
        {
            this.taskFactory = taskFactory;
            Ready = new Node.OnReady(n => { });
            ChannelsChanged = new AnalogNode.OnChannelsChanged(n => { });
            this.state = state;
            this.graph = graph;
            state.Subscriptions += OnChange;
            state.Set("Location", graph.GetAddress());
        }

        public void Dispose()
        {
            running = false;
            state.Subscriptions -= OnChange;
        }

        public void OnChange(object source, ActionType action, object key, object? value)
        {
            if(source == state)
            {
                if(key is string str_key)
                {
                    if(str_key == "Running")
                    {
                        Console.WriteLine(string.Format("Running {0}", value));
                        if(value == null)
                        {
                            throw new ArgumentException();
                        }
                        running = (bool)value;

                        if(running)
                        {
                            graph.Run(async () =>
                            {
                                var start = Util.SteadyTime();
                                var sampleTime = new TimeSpan();
                                while (running)
                                {
                                    await Task.Delay(16);
                                    now = Util.SteadyTime();
                                    var elapsed = now - start;
                                    var sampleMs = sampleTime.Ticks / TimeSpan.TicksPerMillisecond;
                                    var elapsedMs = elapsed.Ticks / TimeSpan.TicksPerMillisecond;

                                    data = Enumerable.Range(0, (int)(elapsedMs - sampleMs)).Select(t =>
                                    {
                                        return Math.Sin((t + sampleMs) / 1000.0);
                                    }).ToArray();
                                    Ready(this);
                                    sampleTime = elapsed;
                                }
                            });
                        }
                    }
                    else if(str_key == "Key")
                    {
                        this.key = (string?)value;
                    }
                    else if (str_key == "License")
                    {
                        license = (string?)value;
                    }

                    if(this.key != null && this.key != "" && license != null && license != "")
                    {
                        deviceSourceCreator = new DeviceSourcePortable(this.key, license);
                        deviceSourceCreator.SetDebugOutputStream((str, args) => Console.WriteLine(string.Format(str, args)));
                        deviceSource = deviceSourceCreator.GetDataSource(SourceType.TRIGNO_RF);
                        deviceSource.Key = this.key;
                        deviceSource.License = license;
                    }
                }
            }
        }

        public static bool Prepare()
        {
            return true;
        }

        public static void Cleanup()
        {

        }

        public int NumChannels()
        {
            return 1;
        }

        public TimeSpan SampleInterval(int channel)
        {
            return Util.FromMillisconds(1);
        }

        public TimeSpan Time()
        {
            return now;
        }

        public string Name(int channel)
        {
            return "Sine";
        }

        public bool HasAnalogData()
        {
            return true;
        }

        public ArraySegment<double> doubles(int channel)
        {
            return new ArraySegment<double>(data);
        }

        public AnalogNode.DataType GetDataType()
        {
            return AnalogNode.DataType.DOUBLE;
        }
    }
}