using System.Diagnostics.CodeAnalysis;

namespace Thalamus.JsonPath
{
    internal static class ValidationUtils
    {
        public static void ArgumentNotNull([NotNull] object? value, string parameterName)
        {
            if (value == null)
            {
                throw new ArgumentNullException(parameterName);
            }
        }
    }
}