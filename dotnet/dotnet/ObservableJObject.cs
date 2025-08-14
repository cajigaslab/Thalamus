using dotnet;
using Newtonsoft.Json.Linq;

namespace Thalamus
{
    public class ObservableCollection : JObject
    {
    }
        public class ObservableJObject : JObject
    {
        public enum Action
        {
            Set,
            Delete
        }
        public delegate void OnChange(object source, Action action, JToken key, JToken value);
        private bool direct = false;
        private StateManager stateManager;


        public OnChange Subscriptions { get; set; }
        public ObservableJObject(StateManager stateManager, JObject original) : base(original) {
            this.stateManager = stateManager;
        }

        public void Add(JToken)
        {
            if(!direct)
            {
                //stateManager.requestChange()
            }
        }
    }
}
