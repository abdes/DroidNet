// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The model import records the scene's edits read, with the replacements they request, each run at once as the content
/// pipeline runs it: a record that changed since is left as it is.
/// </summary>
public sealed class FakeModelRecords : IModelImportRecordFiles
{
    private readonly Dictionary<AssetPath, byte[]> records = [];

    /// <summary>Gets the models whose replacement was requested, each reimported, in order.</summary>
    public List<AssetPath> Reimported { get; } = [];

    /// <summary>Sets a model's record.</summary>
    /// <param name="model">The model's source.</param>
    /// <param name="content">The record's bytes.</param>
    public void Set(AssetPath model, byte[] content) => this.records[model] = [.. content];

    /// <inheritdoc/>
    public byte[]? Read(AssetPath model) => this.records.TryGetValue(model, out var content) ? [.. content] : null;

    /// <inheritdoc/>
    public void Replace(AssetPath model, ReadOnlyMemory<byte> expected, ReadOnlyMemory<byte> replacement)
    {
        if (this.records.TryGetValue(model, out var current) && current.AsSpan().SequenceEqual(expected.Span))
        {
            this.records[model] = replacement.ToArray();
            this.Reimported.Add(model);
        }
    }
}
