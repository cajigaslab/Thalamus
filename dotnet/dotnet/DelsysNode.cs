using DelsysAPI.DelsysDevices;
using DelsysAPI.Pipelines;
using MathNet.Numerics.Statistics;
using Microsoft.AspNetCore.Http.HttpResults;
using Newtonsoft.Json.Linq;
using Nito.AsyncEx;
using System.Diagnostics;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    class DelsysNode : Node, AnalogNode
    {
        private ObservableCollection state;
        private string? key = null;
        private string? license = null;
        private DeviceSourcePortable? deviceSourceCreator = null;
        private IDelsysDevice? deviceSource = null;
        private Pipeline? pipeline = null;
        public Node.OnReady Ready { get; set; }
        public AnalogNode.OnChannelsChanged ChannelsChanged { get; set; }

        private TimeSpan now = Util.SteadyTime();
        private bool running = false;
        private Task? task = null;
        private double[] data = [];
        private TaskFactory taskFactory;
        private INodeGraph graph;
        private CancellationTokenSource? cancellation = null;

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
            if(cancellation != null)
            {
                cancellation.Cancel();
            }
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
                    else if(str_key == "Key File")
                    {
                        if (value is string filename)
                        {
                            if(File.Exists(filename))
                            {
                                this.key = File.ReadAllText(filename);
                            }
                        }
                        else
                        {
                            throw new InvalidDataException();
                        }
                    }
                    else if (str_key == "License File")
                    {
                        if (value is string filename)
                        {
                            if (File.Exists(filename))
                            {
                                license = File.ReadAllText(filename);
                            }
                        }
                        else
                        {
                            throw new InvalidDataException();
                        }
                    }

                    if(this.key != null && this.key != "" && license != null && license != "")
                    {
                        deviceSourceCreator = new DeviceSourcePortable(this.key, license);
                        deviceSourceCreator.SetDebugOutputStream((str, args) => Console.WriteLine(string.Format(str, args)));
                        deviceSource = deviceSourceCreator.GetDataSource(SourceType.TRIGNO_RF);
                        deviceSource.Key = this.key;
                        deviceSource.License = license;

                        //try
                        //{
                        PipelineController.Instance.AddPipeline(deviceSource);
                        //}
                        //catch
                        pipeline = PipelineController.Instance.PipelineIds[0];
                        pipeline.TrignoRfManager.ComponentAdded += Log<DelsysAPI.Events.ComponentAddedEventArgs>;
                        pipeline.TrignoRfManager.ComponentLost += Log<DelsysAPI.Events.ComponentLostEventArgs>;
                        pipeline.TrignoRfManager.ComponentRemoved += Log<DelsysAPI.Events.ComponentRemovedEventArgs>;
                        pipeline.TrignoRfManager.ComponentScanComplete += Log<DelsysAPI.Events.ComponentScanCompletedEventArgs>;

                        pipeline.CollectionStarted += Log<DelsysAPI.Events.CollectionStartedEvent>;
                        pipeline.CollectionDataReady += Log<DelsysAPI.Events.ComponentDataReadyEventArgs>;
                        pipeline.CollectionComplete += Log<DelsysAPI.Events.CollectionCompleteEvent>;
                    }
                }
            }
        }
        public async Task Process(JToken request)
        {
            if(request.Type != JTokenType.Object)
            {
                return;
            }
            var obj = (JObject)request;

            var type_tok = obj["type"];
            if(type_tok != null && type_tok.Type == JTokenType.String)
            {
                var type_str = (string?)type_tok;
                if(type_str == "scan")
                {
                    if(pipeline != null)
                    {
                        if(cancellation != null)
                        {
                            cancellation.Cancel();
                        }
                        cancellation = new CancellationTokenSource();
                        if(state.ContainsKey("Components"))
                        {
                            if(state["Components"] is ObservableCollection components)
                            {
                                state["Scan Status"] = "In Progress";
                                var tasks = components.Values().Select(async (rawComponent) =>
                                {
                                    if (rawComponent is ObservableCollection component)
                                    {
                                        component["Paired"] = false;
                                        var sensorNumber = component["ID"];
                                        if (sensorNumber is long sensorLong)
                                        {
                                            var result = pipeline.TrignoRfManager.AddTrignoComponent(cancellation.Token, (int)sensorLong);
                                            component["Paired"] = result;
                                        }
                                        else
                                        {
                                            throw new InvalidDataException();
                                        }
                                    }
                                    else
                                    {
                                        throw new InvalidDataException();
                                    }
                                });
                                await Task.WhenAll(tasks);

                                var scanResult = await pipeline.Scan();
                                state["Scan Result"] = scanResult ? "Success" : "Failed";
                            }
                        }
                    }
                }
            }
        }

        private void Log<T>(object? sender, T e)
        {
            Console.WriteLine(e);
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