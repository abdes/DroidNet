// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>
/// The revision of each importer of the installed ImportTool, by the name it reports. An importer's revision changes
/// when what it cooks from the same inputs changes, so a product cooked at another revision is cooked again.
/// </summary>
/// <param name="Revisions">The revision of each importer, by name.</param>
public sealed record ImporterRevisions(ImmutableDictionary<string, uint> Revisions)
{
    /// <summary>Reads the importers of the ImportTool's <c>versions --json</c> output.</summary>
    /// <param name="json">The tool's output.</param>
    /// <returns>The revisions it reports.</returns>
    /// <exception cref="InvalidDataException">The output names no importers.</exception>
    public static ImporterRevisions Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (!document.RootElement.TryGetProperty("importers", out var importers)
            || importers.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException("The ImportTool's versions report no importers.");
        }

        return new(importers.EnumerateObject().ToImmutableDictionary(
            static importer => importer.Name,
            static importer => importer.Value.GetUInt32(),
            StringComparer.Ordinal));
    }

    /// <summary>Gets the revision of the importer named <paramref name="importer"/>.</summary>
    /// <param name="importer">The importer's name.</param>
    /// <returns>Its revision.</returns>
    /// <exception cref="InvalidDataException">The ImportTool has no such importer.</exception>
    public uint Of(string importer)
        => this.Revisions.TryGetValue(importer, out var revision)
            ? revision
            : throw new InvalidDataException($"The ImportTool reports no '{importer}' importer.");
}
