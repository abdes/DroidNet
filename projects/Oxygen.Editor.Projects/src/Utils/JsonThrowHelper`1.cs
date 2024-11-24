// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;

namespace Oxygen.Editor.Projects.Utils;

/// <summary>
/// Provides helper methods for throwing JSON-related exceptions during serialization and deserialization.
/// </summary>
/// <typeparam name="T">The type for which the JSON operations are being performed.</typeparam>
/// <remarks>
/// This class is designed to assist with throwing consistent and informative exceptions related to JSON operations.
/// It ensures that error messages include the context of the operation (serialization or deserialization) and the type involved.
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <![CDATA[
/// internal sealed class ExampleUsage : JsonThrowHelper<ExampleUsage>
/// {
///     public string Name { get; set; } = string.Empty;
///
///     public static ExampleUsage FromJson(string json)
///     {
///         var example = JsonSerializer.Deserialize<ExampleUsage>(json);
///         if (example == null || string.IsNullOrEmpty(example.Name))
///         {
///             MissingRequiredProperty(nameof(Name));
///         }
///         return example;
///     }
/// }
/// ]]>
/// </example>
internal abstract class JsonThrowHelper<T>
{
    /// <summary>
    /// Throws a <see cref="JsonException"/> indicating that a required property is missing.
    /// </summary>
    /// <param name="propertyName">The name of the missing property.</param>
    /// <exception cref="JsonException">Always thrown to indicate the missing property.</exception>
    /// <remarks>
    /// This method is used to enforce the presence of required properties during JSON deserialization.
    /// </remarks>
    [DoesNotReturn]
    protected static void MissingRequiredProperty(string propertyName)
        => throw new JsonException(FormatErrorMessage($"was missing required property: {propertyName}"));

    /// <summary>
    /// Formats an error message for JSON operations.
    /// </summary>
    /// <param name="message">The error message to format.</param>
    /// <returns>A formatted error message string.</returns>
    /// <remarks>
    /// This method uses the call stack to determine the name of the calling method and includes this information in the error message.
    /// It ensures that the error message specifies whether the issue occurred during serialization or deserialization.
    /// </remarks>
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
