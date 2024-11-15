// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Utils;

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;

[SuppressMessage(
    "Roslynator",
    "RCS1102:Make class static",
    Justification = "class is extended to provide the type T")]
internal abstract class JsonThrowHelper<T>
{
    [DoesNotReturn]
    protected static void MissingRequiredProperty(string propertyName)
        => throw new JsonException(FormatErrorMessage($"was missing required property: {propertyName}"));

    protected static string FormatErrorMessage(string message)
    {
        var stackTrace = new StackTrace();
        var callerFrame = stackTrace.GetFrame(3);
        var callerName = callerFrame?.GetMethod()?.Name;
        Debug.Assert(callerName is not null, "this method must be called with at least two levels deep");
        var operation = callerName.ToUpperInvariant() switch
        {
            "READ" => "deserialization",
            "WRITE" => "serialization",
            _ => "operation",
        };

        return $"JSON {operation} for type '{typeof(T).FullName}' {message}";
    }
}
