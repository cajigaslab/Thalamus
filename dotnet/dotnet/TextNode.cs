using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.DataProtection.KeyManagement;
using static System.Runtime.InteropServices.JavaScript.JSType;
using static Thalamus.ObservableCollection;

namespace Thalamus
{
    public interface TextNode
    {
        public string Text();
        public TimeSpan Time();
        public bool HasTextData();
    }
}