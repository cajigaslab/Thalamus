using dotnet;
using Grpc.Core;
using Nito.AsyncEx;
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

        public override Task analog(AnalogRequest request, IServerStreamWriter<global::Thalamus.AnalogResponse> responseStream, ServerCallContext context)
        {
            return taskFactory.Run(async () =>
            {
                while (!context.CancellationToken.IsCancellationRequested)
                {
                    var rawNode = await nodeGraph.GetNode(request.Node);
                    if (!(rawNode is AnalogNode))
                    {
                        await Task.Delay(1000);
                        continue;
                    }

                    var node = rawNode as AnalogNode;
                    if (node == null)
                    {
                        throw new InvalidDataException();
                    }
                    var channelsChanged = true;

                    var redirect = node.Redirect();
                    if(redirect.Length > 0)
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
                        var redirect = node.Redirect();
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
                        if(request.ChannelNames.Count == 0)
                        {
                            //No Channels specified, get all of them
                            while (requestIndexToNodeIndex.Count < request.ChannelNames.Count)
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

                        for(var c = 0;c < requestIndexToNodeIndex.Count;++c)
                        {
                            var channel = requestIndexToNodeIndex[c];
                            if(channel >= numChannels)
                            {
                                continue;
                            }

                            var span = new Span();
                            span.Begin = (uint)response.Data.Count;
                            span.Name = node.Name(channel);
                            response.SampleIntervals.Append((ulong)Util.ToNanoseconds(node.SampleInterval(channel)));

                            if(node.GetDataType() == AnalogNode.DataType.DOUBLE)
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
                        }

                        await responseStream.WriteAsync(response);
                    });

                    try
                    {
                        node.ChannelsChanged += onChannelsChanged;
                        rawNode.Ready += onReady;
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
