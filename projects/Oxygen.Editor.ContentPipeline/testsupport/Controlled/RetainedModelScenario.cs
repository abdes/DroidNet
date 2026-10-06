// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Import;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class RetainedModelScenario
{
    internal static async Task<Uri> WriteRetainedModelAsync(CookWorkspace workspace, string name, string extension, CancellationToken cancellationToken)
    {
        var relative = "Content/SourceMedia/DCC/" + name;
        var directory = Path.Combine(workspace.Root, relative);
        _ = Directory.CreateDirectory(directory);
        var filename = "model." + extension;
        var bytes = await File.ReadAllBytesAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle." + extension), cancellationToken).ConfigureAwait(false);
        await File.WriteAllBytesAsync(Path.Combine(directory, filename), bytes, cancellationToken).ConfigureAwait(false);
        var retained = new RetainedImportSource(relative, filename, [new(filename, Convert.ToHexString(SHA256.HashData(bytes)))]);

        var settings = NativeSceneImportSettings.Create(retained, "Content", name, "Models/" + name);
        _ = await settings.SaveNewAsync(workspace.Root, new NativeAtomicFileStore(new RealFileSystem()), cancellationToken).ConfigureAwait(false);
        return new Uri("asset:///" + relative + "/" + filename);
    }
}
