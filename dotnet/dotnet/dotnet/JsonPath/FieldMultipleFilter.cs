using System.Collections.Generic;
using System.Globalization;
using Newtonsoft.Json.Linq;
using Newtonsoft.Json;


#if !HAVE_LINQ
using Newtonsoft.Json.Utilities.LinqBridge;
#else
using System.Linq;
#endif
using Newtonsoft.Json.Utilities;

namespace Thalamus.JsonPath
{
    internal class FieldMultipleFilter : PathFilter
    {
        internal List<string> Names;

        public FieldMultipleFilter(List<string> names)
        {
            Names = names;
        }

        public override IEnumerable<JToken> ExecuteFilter(JToken root, IEnumerable<JToken> current, JsonSelectSettings? settings)
        {
            foreach (JToken t in current)
            {
                if (t is JObject o)
                {
                    foreach (string name in Names)
                    {
                        JToken? v = o[name];

                        if (v != null)
                        {
                            yield return v;
                        }

                        if (settings?.ErrorWhenNoMatch ?? false)
                        {
                            throw new JsonException(string.Format(CultureInfo.InvariantCulture, "Property '{0}' does not exist on JObject.", name));
                        }
                    }
                }
                else
                {
                    if (settings?.ErrorWhenNoMatch ?? false)
                    {
                        throw new JsonException(string.Format(CultureInfo.InvariantCulture, "Properties {0} not valid on {1}.", string.Join(", ", Names.Select(n => "'" + n + "'")
#if !HAVE_STRING_JOIN_WITH_ENUMERABLE
                            .ToArray()
#endif
                            ), t.GetType().Name));
                    }
                }
            }
        }
    }
}