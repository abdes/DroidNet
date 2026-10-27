// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>
/// The preview file beside each model, <c>Car.glb.preview.json</c>: how the model is looked at in the Model Editor, its
/// preview's environment and sun, in the editor's scene format. A missing or unreadable file shows the default preview.
/// </summary>
/// <param name="session">The project session, whose file system holds the models.</param>
/// <param name="store">Writes each file whole, so an interrupted write never leaves half a file.</param>
/// <param name="logger">Reports the files that could not be read or written.</param>
internal sealed partial class ModelPreviewFiles(IProjectSession session, IAtomicFileStore store, ILogger logger)
{
    /// <summary>Gets the preview file of a model document, beside the model.</summary>
    /// <param name="document">The model document.</param>
    /// <returns>The file's path.</returns>
    public static FileSystemPath PathOf(Document document) => FileSystemPath.From(AssetFileTypes.PreviewOf(document.File.Path.FullPath));

    /// <summary>Encodes what a preview scene keeps: its environment and its sun, without the model it places.</summary>
    /// <param name="scene">The preview scene, whose last root node is the sun.</param>
    /// <returns>The file's content.</returns>
    public static byte[] Encode(Scene scene)
    {
        var data = scene.Dehydrate();
        var kept = new SceneData
        {
            Id = data.Id,
            Name = data.Name,
            RootNodes = [data.RootNodes[^1]],
            Environment = data.Environment,
        };
        return JsonSerializer.SerializeToUtf8Bytes(new SceneFileData(kept, explorerLayout: []), SceneFileJsonContext.Default.SceneFileData);
    }

    /// <summary>Reads a model's preview file.</summary>
    /// <param name="path">The file.</param>
    /// <returns>The kept scene, or <see langword="null"/> when there is none or it cannot be read, which is reported.</returns>
    public SceneData? Read(FileSystemPath path)
    {
        var fileSystem = session.Context.FileSystem;
        if (!fileSystem.File.Exists(path.FullPath))
        {
            return null;
        }

        try
        {
            return JsonSerializer.Deserialize(fileSystem.File.ReadAllBytes(path.FullPath), SceneFileJsonContext.Default.SceneFileData);
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException or NotSupportedException)
        {
            this.LogUnreadable(path, ex);
            return null;
        }
    }

    /// <summary>Writes a model's preview file whole, over whatever it holds.</summary>
    /// <param name="path">The file.</param>
    /// <param name="content">The file's content.</param>
    /// <returns>A task that completes when the file is written, or its failure is reported.</returns>
    public async Task WriteAsync(FileSystemPath path, byte[] content)
    {
        try
        {
            var current = await store.ReadAsync(path.FullPath).ConfigureAwait(false);
            _ = await store.WriteAsync(path.FullPath, content, current.Version).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // A conflict, StorageWriteConflictException, is one: another writer replaced the file at that moment.
            this.LogUnwritable(path, ex);
        }
    }

    /// <summary>Deletes a model's preview file, so the model shows the default preview.</summary>
    /// <param name="path">The file.</param>
    /// <returns><see langword="true"/> when no file is left.</returns>
    public bool Delete(FileSystemPath path)
    {
        try
        {
            session.Context.FileSystem.File.Delete(path.FullPath);
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            this.LogUnwritable(path, ex);
            return false;
        }
    }
}
