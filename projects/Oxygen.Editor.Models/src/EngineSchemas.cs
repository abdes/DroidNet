// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using Json.Schema;

namespace Oxygen.Editor.Models;

/// <summary>
/// The engine schemas this assembly embeds from the installed SDK, by file name. They are built with one registry,
/// so a reference from one to another, such as the model import record's to the shared reference definition,
/// resolves to the embedded copy.
/// </summary>
internal static class EngineSchemas
{
    private static readonly ConcurrentDictionary<string, JsonSchema> Built = new(StringComparer.Ordinal);

    private static readonly SchemaRegistry Registry = new()
    {
        Fetch = static (uri, _) => Get(Path.GetFileName(uri.LocalPath)),
    };

    /// <summary>Gets the schema of the file <paramref name="fileName"/>, built once.</summary>
    /// <param name="fileName">The schema's file name, such as <c>oxygen.model-import.schema.json</c>.</param>
    /// <returns>The schema.</returns>
    public static JsonSchema Get(string fileName)
        => Built.GetOrAdd(
            fileName,
            static name => JsonSchema.FromText(ReadText(name), new BuildOptions { SchemaRegistry = Registry }));

    /// <summary>Reads the text of the schema of the file <paramref name="fileName"/>.</summary>
    /// <param name="fileName">The schema's file name.</param>
    /// <returns>The schema's JSON text.</returns>
    /// <exception cref="InvalidOperationException">The assembly does not embed the schema.</exception>
    public static string ReadText(string fileName)
    {
        using var stream = typeof(EngineSchemas).Assembly.GetManifestResourceStream(fileName)
            ?? throw new InvalidOperationException($"The engine schema {fileName} is missing.");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
