// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reflection;
using Json.Schema;

namespace Oxygen.Managed.Assets;

/// <summary>
/// The engine schemas an assembly embeds from the installed SDK, each under its file name, such as
/// <c>oxygen.material-descriptor.schema.json</c>. They are built with one registry, so a reference from one schema to
/// another, such as a descriptor's to <c>oxygen.reference.schema.json#/definitions/reference</c>, resolves to the
/// embedded copy whatever base the referring schema has. The assembly embeds every schema its schemas reference.
/// </summary>
public sealed class EngineSchemaSet
{
    private readonly Assembly assembly;
    private readonly ConcurrentDictionary<string, JsonSchema> built = new(StringComparer.Ordinal);
    private readonly SchemaRegistry registry;

    /// <summary>Initializes a new instance of the <see cref="EngineSchemaSet"/> class.</summary>
    /// <param name="assembly">The assembly that embeds the schemas.</param>
    public EngineSchemaSet(Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(assembly);
        this.assembly = assembly;
        this.registry = new SchemaRegistry
        {
            Fetch = (uri, _) => this.Get(Path.GetFileName(uri.LocalPath)),
        };
    }

    /// <summary>Gets the schema of the file <paramref name="fileName"/>, built once.</summary>
    /// <param name="fileName">The schema's file name.</param>
    /// <returns>The schema.</returns>
    /// <exception cref="InvalidOperationException">The assembly does not embed the schema or one it references.</exception>
    public JsonSchema Get(string fileName)
        => this.built.GetOrAdd(
            fileName,
            name => JsonSchema.FromText(this.ReadText(name), new BuildOptions { SchemaRegistry = this.registry }));

    /// <summary>Reads the text of the schema of the file <paramref name="fileName"/>.</summary>
    /// <param name="fileName">The schema's file name.</param>
    /// <returns>The schema's JSON text.</returns>
    /// <exception cref="InvalidOperationException">The assembly does not embed the schema.</exception>
    public string ReadText(string fileName)
    {
        using var stream = this.assembly.GetManifestResourceStream(fileName)
            ?? throw new InvalidOperationException($"The engine schema '{fileName}' is missing.");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
