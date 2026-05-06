using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.DataProtection.KeyManagement;
using static System.Runtime.InteropServices.JavaScript.JSType;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    public interface AnalogNode : IDisposable
    {
        enum DataType
        {
            DOUBLE,
            SHORT,
            ULONG
        }
        public delegate void OnChannelsChanged(AnalogNode node);

        public OnChannelsChanged ChannelsChanged { get; set; }


        public int NumChannels();

        public TimeSpan SampleInterval(int channel);

        public TimeSpan Time();

        public string Name(int channel);

        public bool HasAnalogData();

        public ArraySegment<double> doubles(int channel)
        {
            throw new NotImplementedException();
        }

        public ArraySegment<short> shorts(int channel)
        {
            throw new NotImplementedException();
        }

        public ArraySegment<ulong> ulongs(int channel)
        {
            throw new NotImplementedException();
        }

        public AnalogNode.DataType GetDataType();
    }
}