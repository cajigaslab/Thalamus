using System.Collections.Generic;
using System.Globalization;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using Newtonsoft.Json.Utilities;

namespace Thalamus.JsonPath
{
    internal class FieldFilter : PathFilter
    {
        internal string? Name;

        public FieldFilter(string? name)
        {
            Name = name;
        }

        public override IEnumerable<JToken> ExecuteFilter(JToken root, IEnumerable<JToken> current, JsonSelectSettings? settings)
        {
            foreach (JToken t in current)
            {
                if (t is JObject o)
                {
                    if (Name != null)
                    {
                        JToken? v = o[Name];

                        if (v != null)
                        {
                            yield return v;
                        }
                        else if (settings?.ErrorWhenNoMatch ?? false)
                        {
                            throw new JsonException(string.Format(CultureInfo.InvariantCulture, "Property '{0}' does not exist on JObject.", Name));
                        }
                    }
                    else
                    {
                        foreach (KeyValuePair<string, JToken?> p in o)
                        {
                            yield return p.Value!;
                        }
                    }
                }
                else
                {
                    if (settings?.ErrorWhenNoMatch ?? false)
                    {
                        throw new JsonException(string.Format(CultureInfo.InvariantCulture, "Property '{0}' not valid on {1}.", Name ?? "*", t.GetType().Name));
                    }
                }
            }
        }
    }
}