using CommandLine.Text;
using dotnet;
using Grpc.Core;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using Nito.AsyncEx;
using System.Reflection.PortableExecutable;
using Thalamus;

namespace dotnet.Services
{
    public class ThalamusService : Thalamus.Thalamus.ThalamusBase
    {
        private readonly ILogger<ThalamusService> _logger;
        private INodeGraph nodeGraph;
        private TaskFactory taskFactory;
        public ThalamusService(ServiceSettings settings, INodeGraph nodeGraph, TaskFactory taskFactory, ILogger<ThalamusService> logger)
        {
            this.nodeGraph = nodeGraph;
            this.taskFactory = taskFactory;
            _logger = logger;
            Console.WriteLine(settings.StateUrl);
        }

        public override Task<NodeResponse> node_request(NodeRequest request, ServerCallContext context)
        {
            return nodeGraph.Run(async () =>
            {
                var node = await nodeGraph.GetNode(request.Node);
                var reader = new JsonTextReader(new StringReader(request.Json));
                var tok = JToken.ReadFrom(reader);
                var jsonResponse = await node.Process(tok);

                var response = new NodeResponse();
                response.Json = JsonConvert.SerializeObject(jsonResponse);
                response.Status = NodeResponse.Types.Status.Ok;
                return response;
            });
        }
        public override Task text(TextRequest request, IServerStreamWriter<global::Thalamus.Text> responseStream, ServerCallContext context)
        {
            return nodeGraph.Run(async () =>
            {
                while (!context.CancellationToken.IsCancellationRequested)
                {
                    var rawNodeMaybe = await nodeGraph.GetNode(request.Node);
                    if (rawNodeMaybe == null)
                    {
                        await Task.Delay(1000);
                        continue;
                    }

                    var rawNode = (Node)rawNodeMaybe;
                    if (!(rawNode is TextNode))
                    {
                        await Task.Delay(1000);
                        continue;
                    }

                    var node = rawNode as TextNode;
                    if (node == null)
                    {
                        await Task.Delay(1000);
                        continue;
                    }
                    var channelsChanged = true;

                    var redirect = rawNode.Redirect();
                    if (redirect.Length > 0)
                    {
                        var response = new Thalamus.Text();
                        response.Redirect = redirect;
                        await responseStream.WriteAsync(response);
                        return;
                    }

                    var onReady = new Node.OnReady(async (Node _) =>
                    {
                        var redirect = rawNode.Redirect();
                        if (redirect.Length > 0)
                        {
                            var redirectResponse = new Thalamus.Text();
                            redirectResponse.Redirect = redirect;
                            await responseStream.WriteAsync(redirectResponse);
                            return;
                        }

                        if (!node.HasTextData())
                        {
                            return;
                        }

                        var response = new Thalamus.Text();
                        response.Text_ = node.Text();
                        response.Time = (ulong)Util.ToNanoseconds(node.Time());
                        channelsChanged = false;

                        await responseStream.WriteAsync(response);
                    });

                    try
                    {
                        rawNode.Ready += onReady;
                        while (!context.CancellationToken.IsCancellationRequested)
                        {
                            await Task.Delay(1000);
                        }
                    }
                    finally
                    {
                        rawNode.Ready -= onReady;
                    }
                }
            });
        }

        public override Task<Redirect> get_redirect(Empty request, ServerCallContext context)
        {
            return Task.FromResult(new Redirect { Redirect_ = "" });
        }

        public override Task analog(AnalogRequest request, IServerStreamWriter<global::Thalamus.AnalogResponse> responseStream, ServerCallContext context)
        {
            return nodeGraph.Run(async () =>
            {
                while (!context.CancellationToken.IsCancellationRequested)
                {
                    var rawNodeMaybe = await nodeGraph.GetNode(request.Node);
                    if (rawNodeMaybe == null)
                    {
                        await Task.Delay(1000);
                        continue;
                    }

                    var rawNode = (Node)rawNodeMaybe;

                    var node = rawNode as AnalogNode;
                    if (node == null)
                    {
                        await Task.Delay(1000);
                        continue;
                    }
                    var channelsChanged = true;

                    var redirect = rawNode.Redirect();
                    if (redirect.Length > 0)
                    {
                        var response = new AnalogResponse();
                        response.Redirect = redirect;
                        await responseStream.WriteAsync(response);
                        return;
                    }

                    var requestIndexToNodeIndex = new List<int>();

                    var onChannelsChanged = new AnalogNode.OnChannelsChanged((AnalogNode _) =>
                    {
                        requestIndexToNodeIndex.Clear();
                        channelsChanged = true;
                    });
                    var onReady = new Node.OnReady(async (Node _) =>
                    {
                        if (context.CancellationToken.IsCancellationRequested)
                        {
                            return;
                        }

                        var redirect = rawNode.Redirect();
                        if (redirect.Length > 0)
                        {
                            var redirectResponse = new AnalogResponse();
                            redirectResponse.Redirect = redirect;
                            await responseStream.WriteAsync(redirectResponse);
                            return;
                        }

                        if (!node.HasAnalogData())
                        {
                            return;
                        }

                        var numChannels = node.NumChannels();
                        if (request.ChannelNames.Count == 0)
                        {
                            //No Channels specified, get all of them
                            while (requestIndexToNodeIndex.Count < numChannels)
                            {
                                requestIndexToNodeIndex.Add(requestIndexToNodeIndex.Count);
                            }
                        }
                        else
                        {
                            //Only get the requested channels
                            while (requestIndexToNodeIndex.Count < request.ChannelNames.Count)
                            {
                                var foundNewChannel = false;
                                for (var i = 0; i < numChannels; ++i)
                                {
                                    if (node.Name(i) == request.ChannelNames[requestIndexToNodeIndex.Count])
                                    {
                                        requestIndexToNodeIndex.Add(i);
                                        foundNewChannel = true;
                                        break;
                                    }
                                }
                                if (!foundNewChannel)
                                {
                                    return;
                                }
                            }
                        }

                        var response = new AnalogResponse();
                        response.ChannelsChanged = channelsChanged;
                        response.Time = (ulong)Util.ToNanoseconds(node.Time());
                        channelsChanged = false;

                        for (var c = 0; c < requestIndexToNodeIndex.Count; ++c)
                        {
                            var channel = requestIndexToNodeIndex[c];
                            if (channel >= numChannels)
                            {
                                continue;
                            }

                            var span = new Span();
                            span.Begin = (uint)response.Data.Count;
                            span.Name = node.Name(channel);
                            response.SampleIntervals.Add((ulong)Util.ToNanoseconds(node.SampleInterval(channel)));

                            if (node.GetDataType() == AnalogNode.DataType.DOUBLE)
                            {
                                response.Data.Add(node.doubles(channel));
                            }
                            else if (node.GetDataType() == AnalogNode.DataType.SHORT)
                            {
                                response.IntData.Add(node.shorts(channel).Select(a => (int)a));
                                response.IsIntData = true;
                            }
                            else if (node.GetDataType() == AnalogNode.DataType.ULONG)
                            {
                                response.UlongData.Add(node.ulongs(channel));
                                response.IsUlongData = true;
                            }

                            span.End = (uint)response.Data.Count;
                            response.Spans.Add(span);
                        }

                        await responseStream.WriteAsync(response);
                    });

                    try
                    {
                        node.ChannelsChanged += onChannelsChanged;
                        rawNode.Ready += onReady;
                        while (!context.CancellationToken.IsCancellationRequested)
                        {
                            await Task.Delay(1000);
                        }
                    }
                    finally
                    {
                        node.ChannelsChanged -= onChannelsChanged;
                        rawNode.Ready -= onReady;
                    }
                }
            });
        }

        public override Task channel_info(AnalogRequest request, IServerStreamWriter<global::Thalamus.AnalogResponse> responseStream, ServerCallContext context)
        {
            return nodeGraph.Run(async () =>
            {
                while (!context.CancellationToken.IsCancellationRequested)
                {
                    var rawNodeMaybe = await nodeGraph.GetNode(request.Node);
                    if (rawNodeMaybe == null)
                    {
                        await Task.Delay(1000);
                        continue;
                    }

                    var rawNode = (Node)rawNodeMaybe;

                    var node = rawNode as AnalogNode;
                    if (node == null)
                    {
                        throw new InvalidDataException();
                    }
                    var channelsChanged = true;

                    var redirect = rawNode.Redirect();
                    if (redirect.Length > 0)
                    {
                        var response = new AnalogResponse();
                        response.Redirect = redirect;
                        await responseStream.WriteAsync(response);
                        return;
                    }

                    var requestIndexToNodeIndex = new List<int>();

                    var onChannelsChanged = new AnalogNode.OnChannelsChanged((AnalogNode _) =>
                    {
                        requestIndexToNodeIndex.Clear();
                        channelsChanged = true;
                    });
                    var onReady = new Node.OnReady(async (Node _) =>
                    {
                        if (!channelsChanged || context.CancellationToken.IsCancellationRequested)
                        {
                            return;
                        }
                        var redirect = rawNode.Redirect();
                        if (redirect.Length > 0)
                        {
                            var redirectResponse = new AnalogResponse();
                            redirectResponse.Redirect = redirect;
                            await responseStream.WriteAsync(redirectResponse);
                            return;
                        }

                        if (!node.HasAnalogData())
                        {
                            return;
                        }

                        var numChannels = node.NumChannels();
                        if (request.ChannelNames.Count == 0)
                        {
                            //No Channels specified, get all of them
                            while (requestIndexToNodeIndex.Count < numChannels)
                            {
                                requestIndexToNodeIndex.Add(requestIndexToNodeIndex.Count);
                            }
                        }
                        else
                        {
                            //Only get the requested channels
                            while (requestIndexToNodeIndex.Count < request.ChannelNames.Count)
                            {
                                var foundNewChannel = false;
                                for (var i = 0; i < numChannels; ++i)
                                {
                                    if (node.Name(i) == request.ChannelNames[requestIndexToNodeIndex.Count])
                                    {
                                        requestIndexToNodeIndex.Add(i);
                                        foundNewChannel = true;
                                        break;
                                    }
                                }
                                if (!foundNewChannel)
                                {
                                    return;
                                }
                            }
                        }

                        var response = new AnalogResponse();
                        response.ChannelsChanged = channelsChanged;
                        response.Time = (ulong)Util.ToNanoseconds(node.Time());
                        channelsChanged = false;

                        for (var c = 0; c < requestIndexToNodeIndex.Count; ++c)
                        {
                            var channel = requestIndexToNodeIndex[c];
                            if (channel >= numChannels)
                            {
                                continue;
                            }

                            var span = new Span();
                            span.Begin = (uint)response.Data.Count;
                            span.Name = node.Name(channel);
                            response.Spans.Add(span);
                            response.SampleIntervals.Add((ulong)Util.ToNanoseconds(node.SampleInterval(channel)));
                        }

                        await responseStream.WriteAsync(response);
                    });

                    try
                    {
                        node.ChannelsChanged += onChannelsChanged;
                        rawNode.Ready += onReady;
                        while (!context.CancellationToken.IsCancellationRequested)
                        {
                            await Task.Delay(1000);
                        }
                    }
                    finally
                    {
                        node.ChannelsChanged -= onChannelsChanged;
                        rawNode.Ready -= onReady;
                    }
                }
            });
        }

        public override Task graph(GraphRequest request, IServerStreamWriter<global::Thalamus.GraphResponse> responseStream, ServerCallContext context)
        {
            return nodeGraph.Run(async () =>
            {
                while (!context.CancellationToken.IsCancellationRequested)
                {
                    var rawNodeMaybe = await nodeGraph.GetNode(request.Node);
                    if (rawNodeMaybe == null)
                    {
                        await Task.Delay(1000);
                        continue;
                    }

                    var rawNode = (Node)rawNodeMaybe;

                    var node = rawNode as AnalogNode;
                    if (node == null)
                    {
                        throw new InvalidDataException();
                    }
                    var channelsChanged = true;

                    var redirect = rawNode.Redirect();
                    if (redirect.Length > 0)
                    {
                        var response = new GraphResponse();
                        response.Redirect = redirect;
                        await responseStream.WriteAsync(response);
                        return;
                    }

                    var requestIndexToNodeIndex = new List<int>();
                    var mins = new List<double>();
                    var maxes = new List<double>();
                    var previousMins = new List<double>();
                    var previousMaxes = new List<double>();
                    var currentTimes = new List<TimeSpan>();
                    var binEnds = new List<TimeSpan>();
                    TimeSpan? firstTime = null;
                    var binNs = Util.FromNanoseconds((long)request.BinNs);

                    var onChannelsChanged = new AnalogNode.OnChannelsChanged((AnalogNode _) =>
                    {
                        requestIndexToNodeIndex.Clear();
                        channelsChanged = true;
                    });
                    var onReady = new Node.OnReady(async (Node _) =>
                    {
                        if(context.CancellationToken.IsCancellationRequested)
                        {
                            return;
                        }
                        var redirect = rawNode.Redirect();
                        if (redirect.Length > 0)
                        {
                            var redirectResponse = new GraphResponse();
                            redirectResponse.Redirect = redirect;
                            await responseStream.WriteAsync(redirectResponse);
                            return;
                        }

                        if (!node.HasAnalogData())
                        {
                            return;
                        }

                        var numChannels = node.NumChannels();
                        var initialized = false;
                        if (request.ChannelNames.Count == 0)
                        {
                            //No Channels specified, get all of them
                            while (requestIndexToNodeIndex.Count < numChannels)
                            {
                                requestIndexToNodeIndex.Add(requestIndexToNodeIndex.Count);
                                initialized = true;
                            }
                        }
                        else
                        {
                            //Only get the requested channels
                            while (requestIndexToNodeIndex.Count < request.ChannelNames.Count)
                            {
                                var foundNewChannel = false;
                                for (var i = 0; i < numChannels; ++i)
                                {
                                    if (node.Name(i) == request.ChannelNames[requestIndexToNodeIndex.Count])
                                    {
                                        requestIndexToNodeIndex.Add(i);
                                        foundNewChannel = true;
                                        initialized = true;
                                        break;
                                    }
                                }
                                if (!foundNewChannel)
                                {
                                    return;
                                }
                            }
                        }

                        if (initialized)
                        {
                            mins = Enumerable.Range(0, numChannels).Select(i => Double.PositiveInfinity).ToList();
                            maxes = Enumerable.Range(0, numChannels).Select(i => Double.NegativeInfinity).ToList();
                            previousMins = Enumerable.Range(0, numChannels).Select(i => 0.0).ToList();
                            previousMaxes = Enumerable.Range(0, numChannels).Select(i => 0.0).ToList();
                            currentTimes = Enumerable.Range(0, numChannels).Select(i => TimeSpan.Zero).ToList();
                            binEnds = Enumerable.Range(0, numChannels).Select(i => Util.FromNanoseconds((long)request.BinNs)).ToList();
                        }

                        var response = new GraphResponse();
                        response.ChannelsChanged = channelsChanged;
                        channelsChanged = false;
                        var now = node.Time();
                        if (firstTime == null)
                        {
                            firstTime = now;
                        }

                        for (var c = 0; c < requestIndexToNodeIndex.Count; ++c)
                        {
                            var channel = requestIndexToNodeIndex[c];
                            if (channel >= numChannels)
                            {
                                continue;
                            }
                            var min = mins[c];
                            var max = maxes[c];
                            var currentTime = currentTimes[c];
                            var binEnd = binEnds[c];

                            var span = new Span();
                            span.Begin = (uint)response.Bins.Count;
                            span.Name = node.Name(channel);

                            var interval = node.SampleInterval(channel);
                            if (interval == TimeSpan.Zero)
                            {
                                currentTimes[c] = now - (TimeSpan)firstTime;
                            }
                            //response.SampleIntervals.Append((ulong)Util.ToNanoseconds());

                            foreach (var sample in Util.GetData(node, channel))
                            {
                                var wrote = currentTime >= binEnd;
                                while (currentTime >= binEnd)
                                {
                                    response.Bins.Add(min);
                                    response.Bins.Add(max);
                                    binEnd += binNs;
                                }
                                if (wrote)
                                {
                                    min = Double.PositiveInfinity;
                                    max = Double.NegativeInfinity;
                                }
                                min = Math.Min(min, sample);
                                max = Math.Max(max, sample);
                                currentTime += interval;
                            }
                            mins[c] = min;
                            maxes[c] = max;
                            currentTimes[c] = currentTime;
                            binEnds[c] = binEnd;

                            span.End = (uint)response.Bins.Count;
                            response.Spans.Add(span);
                        }

                        await responseStream.WriteAsync(response);
                    });

                    try
                    {
                        node.ChannelsChanged += onChannelsChanged;
                        rawNode.Ready += onReady;
                        while (!context.CancellationToken.IsCancellationRequested)
                        {
                            await Task.Delay(1000);
                        }
                    }
                    finally
                    {
                        node.ChannelsChanged -= onChannelsChanged;
                        rawNode.Ready -= onReady;
                    }
                }
            });
        }
    }
}
