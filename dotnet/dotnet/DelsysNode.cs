using Aero.PipeLine;
using DelsysAPI.DelsysDevices;
using DelsysAPI.Pipelines;
using MathNet.Numerics.Statistics;
using Microsoft.AspNetCore.Http.HttpResults;
using Newtonsoft.Json.Linq;
using Nito.AsyncEx;
using System;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;
using Thalamus.JsonPath;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    class DelsysNode : Node, AnalogNode, TextNode
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
        private bool hasTextData = false;
        private bool hasAnalogData = false;
        private string text = "";
        private bool scanned = false;

        private List<string> channelNames = [];
        private Dictionary<Tuple<Guid, Guid>, int> channelToIndex = [];
        private HashSet<int> selectedComponents  = [];
        private List<TimeSpan> sampleIntervals = [];
        private List<int> sampleCounts = [];
        private List<ArraySegment<double>> segments = [];
        private List<double[]> samples = [];
        private ObservableCollection? components = null;
        private bool lastConnected = false;
        private Task currentTask = Task.CompletedTask;
        private Task stopTask = Task.CompletedTask;
        private Task collectionComplete = Task.CompletedTask;
        private TaskCompletionSource collectionCompleteSource = new TaskCompletionSource();

        private Task QueueTask(Action action)
        {
            currentTask = currentTask.ContinueWith((task) => action());
            return currentTask;
        }
        public DelsysNode(ObservableCollection state, TaskFactory taskFactory, INodeGraph graph)
        {
            this.taskFactory = taskFactory;
            Ready = new Node.OnReady(n => { });
            ChannelsChanged = new AnalogNode.OnChannelsChanged(n => { });
            this.state = state;
            this.graph = graph;
            state["Location"] = graph.GetAddress();
            state.Subscriptions += OnChange;
            state.Recap(OnChange);
        }

        public void Dispose()
        {
            running = false;
            state.Subscriptions -= OnChange;
            if (cancellation != null)
            {
                cancellation.Cancel();
            }
        }

        public void CreateDataSource()
        {
            if(deviceSourceCreator != null)
            {
                return;
            }

            var abort = false;
            if(this.key == null || this.key == "")
            {
                EmitText("Missing Key");
                abort = true;
            }

            if (license == null || license == "")
            {
                EmitText("Missing License");
                abort = true;
            }

            if(abort)
            {
                return;
            }

            try
            {
                deviceSourceCreator = new DeviceSourcePortable(this.key, license);
                deviceSourceCreator.SetDebugOutputStream((str, args) =>
                {
                    var text = string.Format(str, args);
                    graph.Run(() =>
                    {
                        EmitText(text);
                        Console.WriteLine(text);
                        return Task.CompletedTask;
                    });
                });
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
                pipeline.CollectionDataReady += OnData;
                pipeline.CollectionComplete += Log<DelsysAPI.Events.CollectionCompleteEvent>;
                EventHandler<DelsysAPI.Events.CollectionCompleteEvent>? collectionCompleteHandler = null;
                collectionCompleteHandler = (object? sender, DelsysAPI.Events.CollectionCompleteEvent e) =>
                {
                    collectionCompleteSource.SetResult();
                    //pipeline.CollectionComplete -= collectionCompleteHandler;
                };
                pipeline.CollectionComplete += collectionCompleteHandler;
                EmitText("Connected");
            }
            catch (DelsysAPI.Exceptions.PipelineException ex)
            {
                deviceSourceCreator = null;
                if(PipelineController.Instance.PipelineIds.Count != 0)
                {
                    PipelineController.Instance.RemovePipeline(0);
                }
                pipeline = null;
                deviceSource = null;
                deviceSourceCreator = null;
                graph.Dialog(new Dialog
                {
                    Message = ex.Message,
                    Title = "Delsys Error",
                    Type = Dialog.Types.Type.Error
                });
            }
        }

        private bool armed = false;
        private ObservableCollection lastComponents = new ObservableCollection();

        public void OnChange(object source, ActionType action, object key, object? value)
        {
            Console.WriteLine($"{key} {value}");
            if (source == state)
            {
                if (key is string str_key)
                {
                    if (str_key == "Running")
                    {
                        ChannelsChanged(this);
                        CreateDataSource();
                        if(pipeline == null)
                        {
                            return;
                        }
                        if (value == null)
                        {
                            throw new ArgumentException();
                        }
                        running = (bool)value;
                        var needConfig = !lastComponents.Equals(components);
                        lastComponents = components.DeepCopy();
                        EmitText($"needConfig == {needConfig}");

                        if (running == true)
                        {
                            var pipeline = this.pipeline;
                            QueueTask(async () =>
                            {
                                if (!scanned)
                                {
                                    var request = new JObject();
                                    request["type"] = "scan";
                                    await Process(request);
                                }
                                if(needConfig)
                                {
                                    if(pipeline.CurrentState == Pipeline.ProcessState.Armed)
                                    {
                                        await pipeline.DisarmPipeline();
                                    }
                                    channelToIndex.Clear();
                                    channelNames.Clear();
                                    sampleIntervals.Clear();
                                    sampleCounts.Clear();
                                    segments.Clear();
                                    Dictionary<long, string> sampleModes = [];
                                    if (components is ObservableCollection compCol)
                                    {
                                        foreach (var row in compCol.Values())
                                        {
                                            if (row is ObservableCollection rowCol)
                                            {
                                                var row_id = (long?)rowCol["ID"];
                                                var selected = (bool?)rowCol["Selected"];
                                                var sampleMode = (string?)rowCol["Sample Mode"];
                                                if (selected == true && row_id != null)
                                                {
                                                    if (row_id is long row_int)
                                                    {
                                                        sampleModes[row_int] = sampleMode ?? "";
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    foreach (var comp in pipeline.TrignoRfManager.Components)
                                    {
                                        if (!sampleModes.ContainsKey(comp.PairNumber))
                                        {
                                            EmitText($"Deselect {comp.PairNumber}");
                                            var deselectStatus = false;
                                            if (comp.State == DelsysAPI.Utils.SelectionState.Allocated)
                                            {
                                                deselectStatus = await pipeline.TrignoRfManager.DeselectComponentAsync(comp);
                                            }
                                            EmitText($"Status {deselectStatus}");
                                            continue;
                                        }

                                        //Console.WriteLine($"Select {comp.PairNumber}");
                                        EmitText($"Select {comp.PairNumber}");
                                        var status = false;

                                        if (comp.State != DelsysAPI.Utils.SelectionState.Allocated)
                                        {
                                            status = await pipeline.TrignoRfManager.SelectComponentAsync(comp);
                                        }
                                        //Console.WriteLine($"Status {status}");
                                        EmitText($"Status {status}");
                                        //Console.WriteLine($"Status {comp.Configuration.SampleModes}");

                                        //Console.WriteLine($"Status {sampleModes.ContainsKey(comp.PairNumber)} {sampleModes[comp.PairNumber]} {comp.Configuration.SampleModes[0]} {comp.Configuration.SampleModes.Length}");

                                        //EmitText($"Status {sampleModes.ContainsKey(comp.PairNumber)} {sampleModes[comp.PairNumber]} {comp.Configuration.SampleModes[0]} {comp.Configuration.SampleModes.Length}");
                                        var sampleModeIndex = Math.Max(0, Array.IndexOf(comp.Configuration.SampleModes, sampleModes[comp.PairNumber]));
                                        var sampleMode = comp.Configuration.SampleModes[sampleModeIndex];
                                        comp.SelectSampleMode(sampleMode);
                                        //comp.Configuration.SampleMode = Math.Max(0, comp.Configuration.SampleModes.IndexOf(s => s == sampleMode));
                                    }

                                    EmitText($"Configure Pipeline");
                                    var dataLine = new DataLine(pipeline);
                                    dataLine.ConfigurePipeline();

                                    foreach (var comp in pipeline.TrignoRfManager.Components)
                                    {
                                        if (!sampleModes.ContainsKey(comp.PairNumber))
                                        {
                                            continue;
                                        }
                                        foreach (var channel in comp.TrignoChannels)
                                        {
                                            if (channel != null)
                                            {
                                                var span = Util.FromSeconds(1 / channel.SampleRate);
                                                sampleIntervals.Add(span);
                                                channelToIndex[Tuple.Create(comp.Id, channel.Id)] = channelNames.Count;
                                                channelNames.Add($"{comp.PairNumber}:{channel.Name}");
                                                sampleCounts.Add(0);
                                                samples.Add(new double[1]);
                                                segments.Add(new ArraySegment<double>());
                                            }
                                        }
                                    }
                                }
                                EmitText($"Channels {channelNames}");

                                EmitText($"Start Pipeline");


                                collectionCompleteSource = new TaskCompletionSource();
                                collectionComplete = collectionCompleteSource.Task;
                                await pipeline.Start();
                                armed = true;

                            });
                        }
                        else
                        {
                            EmitText($"Stop Pipeline");
                            QueueTask(async () =>
                            {
                                EmitText($"Stopping Pipeline");
                                await pipeline.Stop();
                                EmitText($"Stopped Pipeline");
                                await collectionComplete;
                                EmitText($"Collection Complete");
                                //await pipeline.DisarmPipeline();
                                //EmitText($"Disarmed Pipeline");


                                //if (deviceSourceCreator != null)
                                //{
                                //    EmitText($"Deleting Pipeline");
                                //    PipelineController.Instance.RemovePipeline(0);
                                //    pipeline = null;
                                //    deviceSource = null;
                                //    deviceSourceCreator = null;
                                //    EmitText($"Deleted Pipeline");
                                //}

                                //collecting = false;
                                //scanned = false;
                            });
                        }
                    }
                    else if (str_key == "Components")
                    {
                        components = (ObservableCollection?)value;
                        components.Recap(OnChange);
                    }
                    else if (str_key == "Key File")
                    {
                        if (value is string filename)
                        {
                            if (File.Exists(filename))
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


                    //CreateDataSource();
                }
            }
            else if (source == components)
            {
                if (value is ObservableCollection valCol)
                {
                    valCol["Ready"] = false;
                }
            }
        }

        private bool busy = false;

        private void OnData(object? sender, DelsysAPI.Events.ComponentDataReadyEventArgs e)
        {
            if (busy)
            {
                EmitText("Busy, Data dropped");
                return;
            }

            now = Util.SteadyTime();
            for (var i = 0; i < sampleCounts.Count; ++i)
            {
                sampleCounts[i] = 0;
            }
            foreach (var frame in e.Data)
            {
                foreach (var sensor in frame.SensorData)
                {
                    foreach (var channel in sensor.ChannelData)
                    {
                        var i = channelToIndex[Tuple.Create(sensor.Id, channel.Id)];
                        sampleCounts[i] += channel.Data.Count;
                    }
                }
            }

            for (var i = 0; i < sampleCounts.Count; ++i)
            {
                if (samples[i].Length < sampleCounts[i])
                {
                    samples[i] = new double[sampleCounts[i]];
                }
                segments[i] = new ArraySegment<double>(samples[i], 0, sampleCounts[i]);
                sampleCounts[i] = 0;
            }

            //segments.Clear();
            foreach (var frame in e.Data)
            {
                foreach (var sensor in frame.SensorData)
                {
                    foreach (var channel in sensor.ChannelData)
                    {
                        var i = channelToIndex[Tuple.Create(sensor.Id, channel.Id)];
                        //segments.Add(new ArraySegment<double>(samples[i], z, channel.Data.Count));
                        channel.Data.CopyTo(0, samples[i], sampleCounts[i], channel.Data.Count);
                        sampleCounts[i] += channel.Data.Count;
                    }
                }
            }
            busy = true;
            graph.Run(() =>
            {
                hasAnalogData = true;
                hasTextData = false;
                Ready(this);
                busy = false;
            }).Wait();
        }

        private void EmitText(string text)
        {
            now = Util.SteadyTime();
            this.text = text;
            hasAnalogData = false;
            hasTextData = true;
            Ready(this);
        }

        public async Task<JToken> Process(JToken request)
        {
            if (request.Type != JTokenType.Object)
            {
                return new JObject();
            }
            var obj = (JObject)request;

            var type_tok = obj["type"];
            if (type_tok != null && type_tok.Type == JTokenType.String)
            {
                var type_str = (string?)type_tok;
                if (type_str == "get_sample_modes")
                {
                    if (pipeline != null)
                    {
                        var id_tok = obj["id"];
                        if (id_tok != null && id_tok.Type == JTokenType.Integer)
                        {
                            var id_maybe_int = (int?)id_tok;
                            if (id_maybe_int != null)
                            {
                                var id_int = (int)id_maybe_int;
                                var result = new JObject();
                                foreach (var component in pipeline.TrignoRfManager.Components)
                                {
                                    if (component.PairNumber == id_int)
                                    {
                                        result["sample_modes"] = new JArray(component.Configuration.SampleModes);
                                        return result;
                                    }
                                }
                                result["sample_modes"] = new JArray();
                                return result;
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
                    }
                    else
                    {
                        EmitText("Pipeline not ready");
                        var result = new JObject();
                        result["sample_modes"] = new JArray();
                        return result;
                    }
                }
                else if (type_str == "connect")
                {
                    CreateDataSource();
                    return new JObject();
                }
                else if (type_str == "pair")
                {
                    if (pipeline != null)
                    {
                        var id_tok = obj["id"];
                        if (id_tok != null && id_tok.Type == JTokenType.Integer)
                        {
                            var id_maybe_int = (int?)id_tok;
                            if (id_maybe_int != null)
                            {
                                var id_int = (int)id_maybe_int;
                                if (cancellation != null)
                                {
                                    cancellation.Cancel();
                                }
                                cancellation = new CancellationTokenSource();
                                EmitText($"Pairing component {id_int}");

                                graph.Dialog(new Dialog
                                {
                                    Message = $"Pairing component {id_int}, tap to magnet",
                                    Title = "Delsys Pairing",
                                    Type = Dialog.Types.Type.Info
                                });

                                var success = await pipeline.TrignoRfManager.AddTrignoComponent(cancellation.Token, id_int);
                                if (success)
                                {
                                    EmitText($"Component {id_int} paired");
                                }
                                else
                                {
                                    EmitText($"Component {id_int} not paired");
                                }
                                if (components is ObservableCollection compCol)
                                {
                                    foreach (var row in compCol.Values())
                                    {
                                        if (row is ObservableCollection rowCol)
                                        {
                                            var row_id = (long?)rowCol["ID"];
                                            if (row_id == id_int)
                                            {
                                                rowCol["Ready"] = success;
                                            }
                                        }
                                    }
                                }
                                return new JObject();
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
                    }
                    else
                    {
                        EmitText("Pipeline not ready");
                        return new JObject();
                    }
                }
                else if (type_str == "scan")
                {
                    if (pipeline != null)
                    {
                        if (cancellation != null)
                        {
                            cancellation.Cancel();
                        }
                        cancellation = new CancellationTokenSource();
                        EmitText("Scanning");
                        var success = await pipeline.Scan();
                        if (success)
                        {
                            scanned = true;
                            EmitText($"Scan succeeded");
                        }
                        else
                        {
                            EmitText($"Scan failed");
                        }
                        foreach (var component in pipeline.TrignoRfManager.Components)
                        {
                            if (components is ObservableCollection compCol)
                            {
                                foreach (var row in compCol.Values())
                                {
                                    if (row is ObservableCollection rowCol)
                                    {
                                        var row_id = (long?)rowCol["ID"];
                                        if (row_id == component.PairNumber)
                                        {
                                            rowCol["Ready"] = success;
                                        }
                                    }
                                }
                            }
                        }
                        return new JObject();
                    }
                    else
                    {
                        EmitText("Pipeline not ready");
                        return new JObject();
                    }
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
        }

        private void Log<T>(object? sender, T e)
        {
            graph.Run(() =>
            {
                EmitText($"{e}");
                Console.WriteLine(e);
                return Task.CompletedTask;
            });
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
            return channelNames.Count;
        }

        public TimeSpan SampleInterval(int channel)
        {
            return sampleIntervals[channel];
        }

        public TimeSpan Time()
        {
            return now;
        }

        public string Name(int channel)
        {
            return channelNames[channel];
        }

        public bool HasAnalogData()
        {
            return hasAnalogData;
        }

        public ArraySegment<double> doubles(int channel)
        {
            return segments[channel];
        }

        public AnalogNode.DataType GetDataType()
        {
            return AnalogNode.DataType.DOUBLE;
        }

        public string Text()
        {
            return text;
        }

        public bool HasTextData()
        {
            return hasTextData;
        }
    }
}