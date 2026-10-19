// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class RetainedModelScenario
{
    /// <summary>Writes an imported model and its settings at <c>/{mount}/Models/{name}/model.{extension}</c>.</summary>
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
        var retained = new RetainedImportSource(relative, filename, [new(filename, Convert.ToHexString(SHA256.HashData(bytes)))]);

        var settings = NativeSceneImportSettings.Create(retained, name);
        _ = await settings.SaveNewAsync(workspace.Root, new NativeAtomicFileStore(new RealFileSystem()), cancellationToken).ConfigureAwait(false);
        return AssetPath.Parse("/" + relative + "/" + filename);
    }
}
