// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Reads the object a native import diagnostic is about: its path inside the source file and the name the file
/// gives it, such as a glTF material.</summary>
internal static class NativeSourceObject
{
    /// <summary>Gets the affected source object of a native diagnostic.</summary>
    /// <param name="issue">The diagnostic, as an analysis or import report writes it.</param>
    /// <returns>The affected object, or null when the diagnostic is about the whole source.</returns>
    public static AffectedScope? Read(JsonElement issue)
    {
        var path = Text(issue, "object_path");
        var name = Text(issue, "object_name");
        return path is null && name is null ? null : new AffectedScope { SourceObjectPath = path, SourceObjectName = name };
    }

    private static string? Text(JsonElement issue, string property)
        => issue.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.String && value.GetString() is { Length: > 0 } text
            ? text
            : null;
}
