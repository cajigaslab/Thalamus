using CommandLine;
using dotnet;
using dotnet.Services;
using Grpc.Net.Client;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Microsoft.AspNetCore.Server.Kestrel.Https;
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
            TlsFiles? tls = null;
            if (o.Cert != null || o.Key != null || o.Ca != null)
            {
                if (o.Cert == null || o.Key == null)
                {
                    throw new ArgumentException("gRPC TLS requires both --cert and --key");
                }
                tls = new TlsFiles { Cert = o.Cert, Key = o.Key, Ca = o.Ca, ServerName = o.ServerName };
            }
            using var channel = Util.FindStateChannel(stateUrl, tls);
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
            using var nodeGraph = new NodeGraph(client, nodes, taskFactory, url, done);

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
            var serverCert = tls?.LoadCertificate();
            var caCerts = tls?.LoadCa();
            Action<ListenOptions> configureListen = listenOptions =>
            {
                listenOptions.Protocols = HttpProtocols.Http2;
                if (serverCert != null)
                {
                    listenOptions.UseHttps(httpsOptions =>
                    {
                        httpsOptions.ServerCertificate = serverCert;
                        if (caCerts != null)
                        {
                            httpsOptions.ClientCertificateMode = ClientCertificateMode.RequireCertificate;
                            httpsOptions.ClientCertificateValidation = (certificate, chain, errors) =>
                                TlsFiles.IsSignedBy(certificate, caCerts);
                        }
                    });
                }
            };
            builder.WebHost.ConfigureKestrel(options =>
            {
                if (o.Open)
                {
                    options.ListenAnyIP(o.Port, configureListen);
                }
                else
                {
                    options.ListenLocalhost(o.Port, configureListen);
                }
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
    [Option("open", Default = false, HelpText = "Bind to 0.0.0.0 instead of localhost only")]
    public bool Open { get; set; }
    [Option("cert", HelpText = "PEM certificate for gRPC TLS, used by the server and as the client certificate")]
    public string? Cert { get; set; }
    [Option("key", HelpText = "PEM private key for --cert")]
    public string? Key { get; set; }
    [Option("ca", HelpText = "PEM certificate authority that the server verifies clients against and the client verifies servers against")]
    public string? Ca { get; set; }
    [Option("server-name", Default = "thalamus.internal", HelpText = "Name the client expects in every gRPC server's TLS certificate, regardless of the address it connects to")]
    public string ServerName { get; set; } = "thalamus.internal";
}
