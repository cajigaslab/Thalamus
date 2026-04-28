using Newtonsoft.Json.Linq;
using System.Collections.Generic;

namespace Thalamus.JsonPath
{
    internal class RootFilter : PathFilter
    {
        public static readonly RootFilter Instance = new RootFilter();

        private RootFilter()
        {
        }

        public override IEnumerable<JToken> ExecuteFilter(JToken root, IEnumerable<JToken> current, JsonSelectSettings? settings)
        {
            return new[] { root };
        }
    }
}