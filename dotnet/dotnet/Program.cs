using CommandLine;
using dotnet;
using dotnet.Services;
using Grpc.Net.Client;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Nito.AsyncEx;
using Thalamus;

Console.WriteLine("One");
Parser.Default.ParseArguments<Options>(args)
    .WithParsed<Options>(o =>
    {
        AsyncContext.Run(async () =>
        {
            var cancellationTokenSource = new CancellationTokenSource();
            var scheduler = TaskScheduler.FromCurrentSynchronizationContext();
            var taskFactory = new TaskFactory(scheduler);

            var stateUrl = o.StateUrl;
            Console.WriteLine("Two " + stateUrl);
            using var channel = Util.FindStateChannel(stateUrl);
            //using var channel = GrpcChannel.ForAddress(string.Format("http://{0}", stateUrl));
            var client = new Thalamus.Thalamus.ThalamusClient(channel);
            var builder = WebApplication.CreateBuilder(args);
            var state = new ObservableCollection();
            var nodes = new ObservableCollection(new List<object>(), null);
            state["nodes"] = nodes;
            var done = new TaskCompletionSource();
            //var exception = new TaskCompletionSource<System.Exception>();
            //Util.SetupExceptions(done, cancellationTokenSource);

            using var stateManager = new StateManager(client, taskFactory, state, done);
            var stateTask = stateManager.Start(cancellationTokenSource);
            state.RequestChange = stateManager.RequestChange;

            var url = $"localhost:{o.Port}";
            using var nodeGraph = new NodeGraph(nodes, taskFactory, url, done);

            builder.Services.AddGrpc();
            builder.Services.AddScoped<ServiceSettings>(arg =>
            {
                return new ServiceSettings { StateUrl = stateUrl };
            });
            builder.Services.AddScoped<INodeGraph>(arg =>
            {
                return nodeGraph;
            });
            builder.Services.AddScoped<TaskFactory>(arg =>
            {
                return taskFactory;
            });
            //builder.WebHost.UseUrls($"http://{url}");
            builder.WebHost.ConfigureKestrel(options =>
            {
                options.ListenAnyIP(o.Port, listenOptions =>
                {
                    listenOptions.Protocols = HttpProtocols.Http2;
                });
            });

            var app = builder.Build();
            
            app.MapGrpcService<ThalamusService>();
            app.MapGet("/", () => "Communication with gRPC endpoints must be made through a gRPC client. To learn how to create a client, visit: https://go.microsoft.com/fwlink/?linkid=2086909");
            
            var tt = await Task.WhenAny(
                stateTask,
                done.Task,
                app.RunAsync(cancellationTokenSource.Token));
            await tt;
            Console.WriteLine("DONE12");
        });
    });

public class ServiceSettings
{
    public string StateUrl { get; set; }
}

public class Options
{
    [Option('s', "state-url", Required = true, HelpText = "Set output to verbose messages.")]
    public string StateUrl { get; set; }
    [Option('p', "port", Default = 50052, HelpText = "GRPC port.")]
    public int Port { get; set; }
    [Option('t', "trace", Default = false, HelpText = "Enable Perfetto tracing")]
    public bool Trace { get; set; }
}
