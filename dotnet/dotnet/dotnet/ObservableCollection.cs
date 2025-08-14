using System.Text.Json.Nodes;

namespace dotnet
{
    internal class ObservableCollection
    {
        public enum Action
        {
            Set,
            Delete
        }
        public delegate void OnChange(ObservableCollection source, Action action, object key, object value);
        private JsonNode Content;

        public ObservableCollection(JsonNode content)
        {
            this.Content = content.DeepClone();
        }

        void setJsonPath(string address, object newValue, bool fromRemote = false)
        {

        }
    }
}