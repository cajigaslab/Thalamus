using DelsysAPI.Components.TrignoRf;
using DelsysAPI.DelsysDevices;
using DelsysAPI.Events;
using DelsysAPI.Exceptions;
using DelsysAPI.Pipelines;
using DelsysAPI.Utils;
using Google.Protobuf;
using System.ComponentModel;
using System.Security.AccessControl;
//using ThalamusGrpc;

namespace thalamus
{
    public class Program
    {
        class Component
        {

        }

        //static void writeMessage(StorageRecord record, Stream stream)
        //{
        //
        //    var sizeBytes = BitConverter.GetBytes((long)record.CalculateSize());
        //    if (BitConverter.IsLittleEndian)
        //    {
        //        Array.Reverse(sizeBytes);
        //    }
        //    stream.Write(sizeBytes);
        //    record.WriteTo(stream);
        //}
        public static void Main(string[] args)
        {
            var components = new List<Component>();
            var keyFile = "";
            var licenseFile = "";
            DeviceSourcePortable? deviceSourceCreator = null;
            IDelsysDevice? deviceSource = null;
            Pipeline? pipeline = null;

            using (Stream stdin = Console.OpenStandardInput())
            {
                using (Stream stdout = Console.OpenStandardOutput())
                {
                    var buffer = new byte[1024];
                    while(true)
                    {
                        var count = 0;
                        while (count < 8)
                        {
                            var newCount = stdin.Read(buffer, count, buffer.Length - count);
                            if (newCount == 0)
                            {
                                return;
                            }
                            count += newCount;
                        }

                        var messageLength = buffer.Take(8).Aggregate(0L, (a, b) => a << 8 + b);
                        if (buffer.Length < messageLength)
                        {
                            buffer = new byte[messageLength];
                        }

                        count = 0;
                        while (count < messageLength)
                        {
                            var newCount = stdin.Read(buffer, count, buffer.Length - count);
                            if (newCount == 0)
                            {
                                return;
                            }
                            count += newCount;
                        }

                        var record = DelsysGrpc.Message.Parser.ParseFrom(buffer, 0, (int)messageLength);
                        Console.WriteLine(record.ToString());
                        /*
                        switch(record.BodyCase)
                        {
                            case ThalamusGrpc.StorageRecord.BodyOneofCase.Transaction:
                                var transaction = record.Transaction;
                                foreach (var change in transaction.Changes)
                                {
                                    var tokens = change.Address.Split(new char[] { '[', ']', '.' }, StringSplitOptions.RemoveEmptyEntries);
                                    var licenseChange = false;
                                    if (tokens[0] == "Running")
                                    {
                                        //start
                                    }
                                    else if (tokens[0] == "Key File")
                                    {
                                        keyFile = change.Value;
                                        licenseChange = true;
                                    }
                                    else if (tokens[0] == "License File")
                                    {
                                        licenseFile = change.Value;
                                        licenseChange = true;
                                    }
                                    else if (tokens[0] == "Components")
                                    {
                                        var index = int.Parse(tokens[1]);
                                        while (components.Count <= index)
                                        {
                                            components.Add(new Component());
                                        }
                                    }

                                    if(licenseChange && keyFile.Length > 0 && licenseFile.Length > 0)
                                    {
                                        try
                                        {
                                            var key = File.ReadAllText(keyFile);
                                            var license = File.ReadAllText(licenseFile);
                                            deviceSourceCreator = new DeviceSourcePortable(key, license);
                                            deviceSource = deviceSourceCreator.GetDataSource(SourceType.TRIGNO_RF);
                                            deviceSource.Key = key;
                                            deviceSource.License = license;

                                            PipelineController.Instance.AddPipeline(deviceSource);

                                            pipeline = PipelineController.Instance.PipelineIds[0];
                                            pipeline.TrignoRfManager.InformationScanTime = 5;

                                            /*pipeline.TrignoRfManager.ComponentAdded += ComponentAdded;
                                            pipeline.TrignoRfManager.ComponentLost += ComponentLost;
                                            pipeline.TrignoRfManager.ComponentRemoved += ComponentRemoved;
                                            pipeline.TrignoRfManager.ComponentScanComplete += ComponentScanComplete;
                                            pipeline.CollectionStarted += CollectionStarted;
                                            pipeline.CollectionDataReady += CollectionDataReady;
                                            pipeline.CollectionComplete += CollectionComplete;
                                        }
                                        catch (Exception e)
                                        {
                                            var notification = new StorageRecord
                                            {
                                                Notification = new Notification
                                                {
                                                    Type = Notification.Types.Type.Error,
                                                    Title = "Delsys Error",
                                                    Message = e.Message
                                                }
                                            };
                                            writeMessage(notification, stdout);
                                        }
                                    }

                                    /*}
                                        }
                                    } else if (tokens[0] == "Components")
                                    {
                                        if(tokens.Length > 1)
                                        {
                                            var index = int.Parse(tokens[1]);
                                            while(components.Count <= index)
                                            {
                                                components.Add(new Component());
                                            }
                                        }
                                    }
                                }
                                break;
                        }*/
                    }



                }
            }
        }
    }
}