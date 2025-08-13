using System.Collections.Generic;
using System.Globalization;
using Newtonsoft.Json.Utilities;
using Newtonsoft.Json.Linq;
using Newtonsoft.Json;

namespace Thalamus.JsonPath
{
    internal abstract class PathFilter
    {
        public abstract IEnumerable<JToken> ExecuteFilter(JToken root, IEnumerable<JToken> current, JsonSelectSettings? settings);

        protected static JToken? GetTokenIndex(JToken t, JsonSelectSettings? settings, int index)
        {
            if (t is JArray a)
            {
                if (a.Count <= index)
                {
                    if (settings?.ErrorWhenNoMatch ?? false)
                    {
                        throw new JsonException(string.Format(CultureInfo.InvariantCulture, "Index {0} outside the bounds of JArray.", index));
                    }

                    return null;
                }

                return a[index];
            }
            else if (t is JConstructor c)
            {
                if (c.Count <= index)
                {
                    if (settings?.ErrorWhenNoMatch ?? false)
                    {
                        throw new JsonException(string.Format(CultureInfo.InvariantCulture, "Index {0} outside the bounds of JConstructor.", index));
                    }

                    return null;
                }

                return c[index];
            }
            else
            {
                if (settings?.ErrorWhenNoMatch ?? false)
                {
                    throw new JsonException(string.Format(CultureInfo.InvariantCulture, "Index {0} not valid on {1}.", index, t.GetType().Name));
                }

                return null;
            }
        }

        protected static JToken? GetNextScanValue(JToken originalParent, JToken? container, JToken? value)
        {
            // step into container's values
            if (container != null && container.HasValues)
            {
                value = container.First;
            }
            else
            {
                // finished container, move to parent
                while (value != null && value != originalParent && value == value.Parent!.Last)
                {
                    value = value.Parent;
                }

                // finished
                if (value == null || value == originalParent)
                {
                    return null;
                }

                // move to next value in container
                value = value.Next;
            }

            return value;
        }
    }
}