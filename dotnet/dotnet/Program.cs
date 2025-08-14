using CommandLine;
using dotnet;
using dotnet.Services;
using Grpc.Net.Client;
using Thalamus;

Console.WriteLine("One");
Parser.Default.ParseArguments<Options>(args)
    .WithParsed<Options>(o =>
    {
        var stateUrl = o.StateUrl;
        Console.WriteLine("Two " + stateUrl);
        using var channel = Util.FindStateChannel(stateUrl);
        //using var channel = GrpcChannel.ForAddress(string.Format("http://{0}", stateUrl));
        var client = new Thalamus.Thalamus.ThalamusClient(channel);
        using var mainThread = new MainThread();
        var builder = WebApplication.CreateBuilder(args);
        var stateManager = new StateManager(client, mainThread, new Newtonsoft.Json.Linq.JObject());

        // Add services to the container.
        builder.Services.AddGrpc();
        builder.Services.AddScoped<ServiceSettings>(arg =>
        {
            return new ServiceSettings { StateUrl = stateUrl };
        });
        builder.Services.AddScoped<MainThread>(arg =>
        {
            return mainThread;
        });

        var app = builder.Build();

        // Configure the HTTP request pipeline.
        app.MapGrpcService<ThalamusService>();
        app.MapGet("/", () => "Communication with gRPC endpoints must be made through a gRPC client. To learn how to create a client, visit: https://go.microsoft.com/fwlink/?linkid=2086909");

        app.Run();
    });

public class ServiceSettings
{
    public string StateUrl { get; set; }
}

public class Options
{
    [Option('s', "state-url", Required = true, HelpText = "Set output to verbose messages.")]
    public string StateUrl { get; set; }
}
