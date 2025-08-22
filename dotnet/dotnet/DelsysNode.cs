using DelsysAPI.DelsysDevices;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    class DelsysNode : Node
    {
        private ObservableCollection state;
        private string? key = null;
        private string? license = null;
        private DeviceSourcePortable? deviceSourceCreator = null;
        private IDelsysDevice? deviceSource = null;

        public DelsysNode(ObservableCollection state, MainThread mainThread, INodeGraph graph)
        {
            this.state = state;
            state.Subscriptions += OnChange;
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
                    }
                    else if(str_key == "Key")
                    {
                        this.key = (string?)value;
                    }
                    else if (str_key == "License")
                    {
                        license = (string?)value;
                    }

                    if(this.key != null && license != null)
                    {
                        deviceSourceCreator = new DeviceSourcePortable(this.key, license);
                        deviceSourceCreator.SetDebugOutputStream((str, args) => Console.WriteLine(string.Format(str, args)));
                        deviceSource = deviceSourceCreator.GetDataSource(SourceType.TRIGNO_RF);
                        deviceSource.Key = this.key;
                        deviceSource.License = license;
                    }
                }
            }
        }

        public static bool Prepare()
        {
            return true;
        }

        public static void Cleanup()
        {

        }
    }
}