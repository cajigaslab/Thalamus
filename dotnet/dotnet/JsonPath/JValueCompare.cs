using Newtonsoft.Json.Linq;
using System.Globalization;
using Thalamus.JsonPath;

namespace Thalamus.JsonPath
{
    public class JValueCompare
    {
        private static int CompareFloat(object objA, object objB)
        {
            double d1 = Convert.ToDouble(objA, CultureInfo.InvariantCulture);
            double d2 = Convert.ToDouble(objB, CultureInfo.InvariantCulture);

            // take into account possible floating point errors
            if (MathUtils.ApproxEquals(d1, d2))
            {
                return 0;
            }

            return d1.CompareTo(d2);
        }

        public static int Compare(JTokenType valueType, object? objA, object? objB)
        {
            if (objA == objB)
            {
                return 0;
            }
            if (objB == null)
            {
                return 1;
            }
            if (objA == null)
            {
                return -1;
            }

            switch (valueType)
            {
                case JTokenType.Integer:
                    {
#if HAVE_BIG_INTEGER
                    if (objA is BigInteger integerA)
                    {
                        return CompareBigInteger(integerA, objB);
                    }
                    if (objB is BigInteger integerB)
                    {
                            return -CompareBigInteger(integerB, objA);
                        }
#endif
                        if (objA is ulong || objB is ulong || objA is decimal || objB is decimal)
                        {
                            return Convert.ToDecimal(objA, CultureInfo.InvariantCulture).CompareTo(Convert.ToDecimal(objB, CultureInfo.InvariantCulture));
                        }
                        else if (objA is float || objB is float || objA is double || objB is double)
                        {
                            return CompareFloat(objA, objB);
                        }
                        else
                        {
                            return Convert.ToInt64(objA, CultureInfo.InvariantCulture).CompareTo(Convert.ToInt64(objB, CultureInfo.InvariantCulture));
                        }
                    }
                case JTokenType.Float:
                    {
#if HAVE_BIG_INTEGER
                    if (objA is BigInteger integerA)
                    {
                        return CompareBigInteger(integerA, objB);
                    }
                    if (objB is BigInteger integerB)
                    {
                        return -CompareBigInteger(integerB, objA);
                    }
#endif
                        if (objA is ulong || objB is ulong || objA is decimal || objB is decimal)
                        {
                            return Convert.ToDecimal(objA, CultureInfo.InvariantCulture).CompareTo(Convert.ToDecimal(objB, CultureInfo.InvariantCulture));
                        }
                        return CompareFloat(objA, objB);
                    }
                case JTokenType.Comment:
                case JTokenType.String:
                case JTokenType.Raw:
                    string? s1 = Convert.ToString(objA, CultureInfo.InvariantCulture);
                    string? s2 = Convert.ToString(objB, CultureInfo.InvariantCulture);

                    return string.CompareOrdinal(s1, s2);
                case JTokenType.Boolean:
                    bool b1 = Convert.ToBoolean(objA, CultureInfo.InvariantCulture);
                    bool b2 = Convert.ToBoolean(objB, CultureInfo.InvariantCulture);

                    return b1.CompareTo(b2);
                case JTokenType.Date:
#if HAVE_DATE_TIME_OFFSET
                    if (objA is DateTime dateA)
                    {
#else
                    DateTime dateA = (DateTime)objA;
#endif
                    DateTime dateB;

#if HAVE_DATE_TIME_OFFSET
                        if (objB is DateTimeOffset offsetB)
                        {
                            dateB = offsetB.DateTime;
                        }
                        else
#endif
                    {
                        dateB = Convert.ToDateTime(objB, CultureInfo.InvariantCulture);
                    }

                    return dateA.CompareTo(dateB);
#if HAVE_DATE_TIME_OFFSET
                    }
                    else
                    {
                        DateTimeOffset offsetA = (DateTimeOffset)objA;
                        if (!(objB is DateTimeOffset offsetB))
                        {
                            offsetB = new DateTimeOffset(Convert.ToDateTime(objB, CultureInfo.InvariantCulture));
                        }

                        return offsetA.CompareTo(offsetB);
                    }
#endif
                case JTokenType.Bytes:
                    if (!(objB is byte[] bytesB))
                    {
                        throw new ArgumentException("Object must be of type byte[].");
                    }

                    byte[]? bytesA = objA as byte[];
                    MiscellaneousUtils.Assert(bytesA != null);

                    return MiscellaneousUtils.ByteArrayCompare(bytesA!, bytesB);
                case JTokenType.Guid:
                    if (!(objB is Guid))
                    {
                        throw new ArgumentException("Object must be of type Guid.");
                    }

                    Guid guid1 = (Guid)objA;
                    Guid guid2 = (Guid)objB;

                    return guid1.CompareTo(guid2);
                case JTokenType.Uri:
                    Uri? uri2 = objB as Uri;
                    if (uri2 == null)
                    {
                        throw new ArgumentException("Object must be of type Uri.");
                    }

                    Uri uri1 = (Uri)objA;

                    return Comparer<string>.Default.Compare(uri1.ToString(), uri2.ToString());
                case JTokenType.TimeSpan:
                    if (!(objB is TimeSpan))
                    {
                        throw new ArgumentException("Object must be of type TimeSpan.");
                    }

                    TimeSpan ts1 = (TimeSpan)objA;
                    TimeSpan ts2 = (TimeSpan)objB;

                    return ts1.CompareTo(ts2);
                default:
                    throw MiscellaneousUtils.CreateArgumentOutOfRangeException(nameof(valueType), valueType, string.Format(CultureInfo.InvariantCulture, "Unexpected value type: {0}", valueType));
            }
        }
    }
}
