using dotnet;
using Grpc.Core;
using Thalamus;

namespace dotnet.Services
{
    public class ThalamusService : Thalamus.Thalamus.ThalamusBase
    {
        private readonly ILogger<ThalamusService> _logger;
        private INodeGraph nodeGraph;
        public ThalamusService(ServiceSettings settings, INodeGraph nodeGraph, ILogger<ThalamusService> logger)
        {
            this.nodeGraph = nodeGraph;
            _logger = logger;
            Console.WriteLine(settings.StateUrl);
        }

        public override Task<AnalogResponse> analog(AnalogRequest request, IServerStreamWriter<global::Thalamus.AnalogResponse> responseStream, ServerCallContext context)
        {
            return Task.FromResult(new AnalogResponse
            {
            });
        }
    }
}
