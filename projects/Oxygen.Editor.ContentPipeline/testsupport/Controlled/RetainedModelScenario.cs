// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Models;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class RetainedModelScenario
{
    /// <summary>Writes an imported model and its import record at <c>/{mount}/Models/{name}/model.{extension}</c>.</summary>
    /// <param name="workspace">The workspace; <paramref name="mount"/> is one of its authoring mounts, kept in a folder of the same name.</param>
    /// <param name="name">The model's bundle folder and import name.</param>
    /// <param name="extension">The model format.</param>
    /// <param name="cancellationToken">Cancels the writes.</param>
    /// <param name="mount">The authoring mount the model is imported into.</param>
    /// <returns>The model's identity.</returns>
    internal static async Task<AssetPath> WriteRetainedModelAsync(CookWorkspace workspace, string name, string extension, CancellationToken cancellationToken, string mount = "Content")
    {
        var relative = mount + "/Models/" + name;
        var directory = Path.Combine(workspace.Root, relative);
        _ = Directory.CreateDirectory(directory);
        var filename = "model." + extension;
        var bytes = await File.ReadAllBytesAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle." + extension), cancellationToken).ConfigureAwait(false);
        await File.WriteAllBytesAsync(Path.Combine(directory, filename), bytes, cancellationToken).ConfigureAwait(false);

        // The model is imported when its record names it; the record keeps the source's identity, and the first cook fills
        // in what the import establishes.
        var identity = "/" + relative + "/" + filename;
        var recordPath = ModelImportSettings.PathFor(workspace.Root, Guid.CreateVersion7());
        _ = Directory.CreateDirectory(Path.GetDirectoryName(recordPath)!);
        var record = ModelImportSettings.Create(recordPath, Path.Combine(directory, filename), identity, ModelImportSettings.DefaultSettings(name));
        await File.WriteAllBytesAsync(recordPath, record.ToBytes(), cancellationToken).ConfigureAwait(false);
        return AssetPath.Parse(identity);
    }
}
