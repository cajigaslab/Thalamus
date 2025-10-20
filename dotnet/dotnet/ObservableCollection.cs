using dotnet;
using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.DataProtection.KeyManagement;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using Org.BouncyCastle.Asn1.Esf;
using System.Collections;

namespace Thalamus
{
    public class ObservableCollection
    {
        public class JsonConverter : Newtonsoft.Json.JsonConverter
        {
            public override bool CanConvert(System.Type objectType)
            {
                return objectType == typeof(ObservableCollection);
            }

            public override object? ReadJson(JsonReader reader, System.Type objectType, object? existingValue, JsonSerializer serializer)
            {
                throw new NotImplementedException();
            }

            public override void WriteJson(JsonWriter writer, object value, JsonSerializer serializer)
            {
                var col = (ObservableCollection)value;
                if (col.GetCollectionType() == CollectionType.Dict)
                {
                    writer.WriteStartObject();
                    foreach (var item in col.Items())
                    {
                        writer.WritePropertyName(string.Format("{0}", item.Key));
                        serializer.Serialize(writer, item.Value);
                    }
                    writer.WriteEndObject();
                }
                else
                {
                    writer.WriteStartArray();
                    foreach (var item in col.Items())
                    {
                        serializer.Serialize(writer, item.Value);
                    }
                    writer.WriteEndArray();
                }
            }
        }

        public enum ActionType
        {
            Set,
            Delete
        }
        public delegate void OnChange(ObservableCollection source, ActionType action, object key, object? value);

        public IEnumerable<object> Keys()
        {
            if (dictionaryContent != null)
            {
                return dictionaryContent.Keys;
            }
            else if (arrayContent != null)
            {
                return Enumerable.Range(0, arrayContent.Count).Cast<object>();
            }
            else
            {
                return Enumerable.Empty<object>();
            }
        }

        public IEnumerable<object?> Values()
        {
            if(dictionaryContent != null)
            {
                return dictionaryContent.Values;
            }
            else if(arrayContent != null)
            {
                return arrayContent.AsEnumerable();
            }
            else
            {
                return Enumerable.Empty<object?>();
            }
        }

        public IEnumerable<KeyValuePair<object, object?>> Items()
        {
            if (dictionaryContent != null)
            {
                return dictionaryContent.AsEnumerable();
            }
            else if (arrayContent != null)
            {
                return arrayContent.Select((v, i) => new KeyValuePair<object, object?>(i, v));
            }
            else
            {
                return Enumerable.Empty<KeyValuePair<object, object?>>();
            }
        }

        public int Count
        {
            get
            {
                if (dictionaryContent != null)
                {
                    return dictionaryContent.Count;
                }
                else if (arrayContent != null)
                {
                    return arrayContent.Count;
                }
                return 0;
            }
        }

        public Action<ObservableChange.Types.Action, string, object?, Action>? _requestChange = null;
        public Action<ObservableChange.Types.Action, string, object?, Action>? RequestChange
        {
            get
            {
                return _requestChange;
            }
            set
            {
                _requestChange = value;
                foreach(var v in Values())
                {
                    if(v is ObservableCollection collection)
                    {
                        collection.RequestChange = value;
                    }
                }
            }
        }

        public ObservableCollection? Parent = null;
        private readonly List<object?>? arrayContent = null;
        private readonly Dictionary<object, object?>? dictionaryContent = null;

        public OnChange Subscriptions { get; set; }

        public enum CollectionType
        {
            Dict,
            List
        }

        public CollectionType GetCollectionType()
        {
            return arrayContent != null ? CollectionType.List : CollectionType.Dict;
        }
        public ObservableCollection() : this(new Dictionary<object, object?>(), null) { }
        public ObservableCollection(CollectionType collectionType)
        {
            Subscriptions = new OnChange((source, action, key, value) => { });
            if (collectionType == CollectionType.Dict)
            {
                dictionaryContent = [];
            }
            else
            {
                arrayContent = [];
            }
        }

        public ObservableCollection DeepCopy()
        {
            var collType = GetCollectionType();
            var result = new ObservableCollection(collType);
            //result.Parent = Parent;
            //if (Parent != null)
            //{
            //    result.RequestChange = Parent.RequestChange;
            //}
            foreach (var v in Items())
            {
                if (v.Value is ObservableCollection vColl)
                {
                    result.Set(v.Key, vColl.DeepCopy());
                }
                else
                {
                    result.Set(v.Key, v.Value);
                }
            }
            return result;
        }

        public ObservableCollection(IList original, ObservableCollection? parent)
        {
            Parent = parent;
            if (parent != null)
            {
                RequestChange = parent.RequestChange;
            }
            Subscriptions = new OnChange((source, action, key, value) => { });
            arrayContent = [];
            foreach(var v in original)
            {
                arrayContent.Add(Wrap(v, this));
            }
        }

        public ObservableCollection(IDictionary original, ObservableCollection? parent)
        {
            Parent = parent;
            if(parent != null)
            {
                RequestChange = parent.RequestChange;
            }
            Subscriptions = new OnChange((source, action, key, value) => { });
            dictionaryContent = [];
            foreach (DictionaryEntry v in original)
            {
                dictionaryContent[v.Key] = Wrap(v.Value, this);
            }
        }

        public void Notify(ObservableCollection source, ActionType action, object key, object? value)
        {
            Subscriptions(source, action, key, value);
            if(Parent != null)
            {
                Parent.Notify(source, action, key, value);
            }
        }

        public object? KeyOf(object value)
        {
            foreach(var item in Items())
            {
                if(item.Value == value)
                {
                    return item.Key;
                }
            }
            return null;
        }

        public string GetAddress()
        {
            if(Parent == null)
            {
                return "";
            }

            var key = Parent.KeyOf(this) ?? throw new Exception("Value not found");
            return Parent.GetAddress() + ToIndexString(key);
        }

        private static string ToIndexString(object key)
        {
            return "[" + (key is int ? key : "'" + key + "'") + "]";
        }

        public void Add(object? value, Action callback, bool direct = false)
        {
            if(RequestChange != null && !direct)
            {
                var address = GetAddress() + ToIndexString(Count);
                RequestChange(ObservableChange.Types.Action.Set, address, value, callback);
                return;
            }

            if(arrayContent != null)
            {
                Set(arrayContent.Count, value, callback, direct);
            }
            throw new Exception("Collection is not an array");
        }

        public void Add(object? value)
        {
            Add(value, () => { });
        }
        public object? this[object i]
        {
            get
            {
                if(arrayContent != null)
                {
                    return arrayContent[(int)i];
                }
                else if(dictionaryContent != null)
                {
                    return dictionaryContent[i];
                }
                throw new Exception("Null content");
            }
            set
            {
                Set(i, value);
            }
        }

        public bool ContainsKey(object key)
        {
            if (arrayContent != null)
            {
                return (int)key < arrayContent.Count;
            }
            else if (dictionaryContent != null)
            {
                return dictionaryContent.ContainsKey(key);
            }
            throw new Exception("Null content");
        }

        public static object? Wrap(object? arg, ObservableCollection? parent)
        {
            if( arg == null)
            {
                return null;
            }
            if (arg is JToken tok)
            {
                return tok.Type switch
                {
                    JTokenType.Array => new ObservableCollection(((JArray)tok).Children().ToList(), parent),
                    JTokenType.Object => new ObservableCollection(((JObject)tok).Properties()
                                                .Select(p => new KeyValuePair<object, object?>(p.Name, p.Value)).ToDictionary(), parent),
                    JTokenType.Integer => (long)tok,
                    JTokenType.Float => (double)tok,
                    JTokenType.String => (string?)tok,
                    JTokenType.Null => null,
                    JTokenType.Boolean => (bool)tok,
                    _ => throw new Exception("Unsupported JSON type"),
                };
            }
            else if (arg is ObservableCollection coll)
            {
                coll.Parent = parent;
                coll.RequestChange = parent.RequestChange;
                return arg;
            }
            else if (arg is IList list)
            {
                return new ObservableCollection(list, parent);
            }
            else if (arg is IDictionary dict)
            {
                return new ObservableCollection(dict, parent);
            }
            else
            {
                return arg;
            }
        }

        public void Set(object key, object? value, Action callback, bool direct = false)
        {
            if (this.ContainsKey(key) && this[key] == value)
            {
                callback();
                return;
            }

            if(RequestChange != null && !direct)
            {
                var address = GetAddress() + ToIndexString(key);
                RequestChange(ObservableChange.Types.Action.Set, address, value, callback);
                return;
            }

            var wrapped = Wrap(value, this);
            if (dictionaryContent != null)
            {
                var assigned = false;
                if (dictionaryContent.ContainsKey(key))
                {
                    var current = dictionaryContent[key];
                    if(current is ObservableCollection currentColl)
                    {
                        if(wrapped is ObservableCollection wrappedColl)
                        {
                            currentColl.Merge(wrappedColl, direct);
                            assigned = true;
                        }
                    }
                }
                if(!assigned)
                {
                    dictionaryContent[key] = wrapped;
                    Notify(this, ActionType.Set, key, wrapped);
                }
            }
            else if(arrayContent != null)
            {
                if(arrayContent.Count == (int)key)
                {
                    arrayContent.Add(wrapped);
                    Notify(this, ActionType.Set, key, wrapped);
                }
                else
                {
                    var assigned = false;
                    var current = arrayContent[(int)key];
                    if (current is ObservableCollection currentColl)
                    {
                        if (wrapped is ObservableCollection wrappedColl)
                        {
                            currentColl.Merge(wrappedColl, direct);
                            assigned = true;
                        }
                    }


                    if (!assigned)
                    {
                        arrayContent[(int)key] = wrapped;
                        Notify(this, ActionType.Set, key, wrapped);
                    }
                }
            }
            callback();
        }

        public void Set(object key, object? value)
        {
            Set(key, value, () => { });
        }

        public void Remove(object key, Action callback, bool direct = false)
        {
            if (!ContainsKey(key))
            {
                callback();
                return;
            }

            if (RequestChange != null && !direct)
            {
                var address = GetAddress() + ToIndexString(key);
                RequestChange(ObservableChange.Types.Action.Delete, address, null, callback);
                return;
            }

            if (dictionaryContent != null)
            {
                dictionaryContent.Remove(key);
                Notify(this, ActionType.Delete, key, null);
            }
            else if (arrayContent != null)
            {
                arrayContent.RemoveAt((int)key);
                Notify(this, ActionType.Delete, key, null);
            }
            callback();
        }
        public void Remove(object key)
        {
            Remove(key, () => { });
        }

        public void Merge(ObservableCollection that, bool direct = false)
        {
            var remaining = Keys().ToList();
            foreach(var p in that.Items())
            {
                Set(p.Key, p.Value, () => { }, direct);
                remaining.Remove(p.Key);
            }
            foreach(var k in remaining)
            {
                Remove(k, () => { }, direct);
            }
        }

        public void Recap(Action<ObservableCollection, ActionType, object, object?> callback = null)
        {
            if(callback == null)
            {
                callback = Notify;
            }
            foreach (var item in Items())
            {
                callback(this, ActionType.Set, item.Key, item.Value);
            }
        }

        public override bool Equals(object? otherRaw)
        {
            if(otherRaw is ObservableCollection other)
            {
                var collectionType = GetCollectionType();
                if (collectionType != other.GetCollectionType())
                {
                    return false;
                }

                return Count == other.Count && Keys().All(k =>
                {
                    if (!other.ContainsKey(k))
                    {
                        return false;
                    }
                    var thisValue = this[k];
                    var otherValue = other[k];
                    if (thisValue is ObservableCollection thisValueColl)
                    {
                        if (otherValue is ObservableCollection otherValueColl)
                        {
                            return thisValue.Equals(otherValueColl);
                        }
                        else
                        {
                            return false;
                        }
                    }
                    else
                    {
                        if (otherValue is ObservableCollection otherValueColl)
                        {
                            return false;
                        }
                        else
                        {
                            return thisValue == otherValue;
                        }
                    }
                });
            }
            else
            { 
                return false;
            }
        }
    }
}
