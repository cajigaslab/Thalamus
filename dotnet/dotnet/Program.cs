using CommandLine;
using dotnet;
using dotnet.Services;
using Grpc.Net.Client;
using Nito.AsyncEx;
using Thalamus;

Console.WriteLine("One");
Parser.Default.ParseArguments<Options>(args)
    .WithParsed<Options>(o =>
    {
        using var thread = new AsyncContextThread();

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
        using var stateManager = new StateManager(client, thread.Factory, state, done);
        state.RequestChange = stateManager.RequestChange;

        using var nodeGraph = new NodeGraph(nodes, thread.Factory, $"localhost:{o.Port}");

        // Add services to the container.
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
            return thread.Factory;
        });

        var app = builder.Build();

        // Configure the HTTP request pipeline.
        app.MapGrpcService<ThalamusService>();
        app.MapGet("/", () => "Communication with gRPC endpoints must be made through a gRPC client. To learn how to create a client, visit: https://go.microsoft.com/fwlink/?linkid=2086909");

        var task = Task.Run(async () =>
        {
            await app.StartAsync();
            await done.Task;
            await app.StopAsync();
        });
        task.Wait();
        //app.Run();
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
