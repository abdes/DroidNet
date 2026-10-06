// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;
using DroidNet.Storage;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>Plans and commits a scene-file rename and project-local scene dependency repairs.</summary>
internal sealed class SceneAssetRenameOperation(IStorageProvider storage, IAtomicFileStore files)
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    internal static string ValidateName(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        var trimmed = name.Trim();
        if (trimmed.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || trimmed.EndsWith('.')
            || trimmed is "." or "..")
        {
            throw new ArgumentException("Choose a scene name without path separators, invalid filename characters or a trailing dot.", nameof(name));
        }

        var stem = trimmed.Split('.')[0];
        if (stem.Equals("CON", StringComparison.OrdinalIgnoreCase)
            || stem.Equals("PRN", StringComparison.OrdinalIgnoreCase)
            || stem.Equals("AUX", StringComparison.OrdinalIgnoreCase)
            || stem.Equals("NUL", StringComparison.OrdinalIgnoreCase)
            || (stem.Length == 4 && stem[3] is >= '1' and <= '9'
                && (stem.StartsWith("COM", StringComparison.OrdinalIgnoreCase) || stem.StartsWith("LPT", StringComparison.OrdinalIgnoreCase))))
        {
            throw new ArgumentException("The scene name is reserved by the filesystem.", nameof(name));
        }

        return trimmed;
    }

    internal async Task<SceneAssetRenameResult> ExecuteAsync(
        Scene scene,
        string name,
        SceneSourceVersion source,
        IReadOnlyDictionary<string, FileVersion> baselines,
        CancellationToken cancellationToken)
    {
        var previousName = scene.Name;
        var folder = await storage.GetFolderFromPathAsync(Path.GetDirectoryName(source.SourcePath)!, cancellationToken).ConfigureAwait(true);
        var document = await folder.GetDocumentAsync(Path.GetFileName(source.SourcePath), cancellationToken).ConfigureAwait(true);
        var destination = await folder.GetDocumentAsync(name + Constants.SceneFileExtension, cancellationToken).ConfigureAwait(true);
        var previousAssetPath = GetAssetPath(scene.Project.ProjectInfo, source.SourcePath);
        var assetPath = GetAssetPath(scene.Project.ProjectInfo, destination.Location);
        if (string.Equals(previousName, name, StringComparison.Ordinal))
        {
            return new(previousName, name, previousAssetPath, assetPath, []);
        }

        var caseOnly = string.Equals(source.SourcePath, destination.Location, StringComparison.OrdinalIgnoreCase);
        if (!caseOnly && await destination.ExistsAsync().ConfigureAwait(true))
        {
            throw new TargetExistsException($"A scene or other file already exists at '{destination.Location}'.");
        }

        var edits = await this.PlanAsync(scene, name, source, folder, previousAssetPath, assetPath, baselines, cancellationToken).ConfigureAwait(true);
        var committed = new List<SourceEdit>();
        try
        {
            foreach (var edit in edits)
            {
                edit.Written = await files.WriteAsync(edit.Document.Location, edit.Content, edit.Before.Version, cancellationToken).ConfigureAwait(true);
                committed.Add(edit);
            }

            // Once the files are written, finish the move or restore the transaction without cancellation.
            if (caseOnly)
            {
                await document.RenameAsync($".oxygen-scene-rename-{Guid.NewGuid():N}.tmp", CancellationToken.None).ConfigureAwait(true);
            }

            await document.RenameAsync(destination.Name, CancellationToken.None).ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is StorageException or IOException or UnauthorizedAccessException or OperationCanceledException)
        {
            await this.RollbackAsync(document, source.SourcePath, committed, exception).ConfigureAwait(true);
            throw;
        }

        var changes = edits.Select(edit => new SceneAssetSourceChange(
            edit.SceneId,
            new(edit.Document.Location, edit.Before.Version),
            new(edit.SceneId == scene.Id ? destination.Location : edit.Document.Location, edit.Written!))).ToArray();
        return new(previousName, name, previousAssetPath, assetPath, Array.AsReadOnly(changes));
    }

    private static string GetAssetPath(IProjectInfo project, string sourcePath)
    {
        foreach (var mount in project.AuthoringMounts)
        {
            var root = Path.GetFullPath(Path.Combine(project.Location!, mount.RelativePath));
            var relative = Path.GetRelativePath(root, sourcePath);
            if (!Path.IsPathRooted(relative) && !string.Equals(relative, "..", StringComparison.Ordinal)
                && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal))
            {
                return "/" + mount.Name + "/" + relative[..^".json".Length].Replace('\\', '/');
            }
        }

        throw new InvalidOperationException("The scene source is not inside a project authoring mount.");
    }

    private static bool RepairReferences(JsonObject root, string previousAssetPath, string assetPath)
    {
        if (root[nameof(Scene.References)]?[nameof(Oxygen.Editor.World.Serialization.SceneReferencesData.ExtraAssets)] is not JsonArray references)
        {
            return false;
        }

        var changed = false;
        for (var index = 0; index < references.Count; ++index)
        {
            if (string.Equals(references[index]?.GetValue<string>(), previousAssetPath, StringComparison.Ordinal))
            {
                references[index] = assetPath;
                changed = true;
            }
        }

        return changed;
    }

    private async Task<List<SourceEdit>> PlanAsync(
        Scene scene,
        string name,
        SceneSourceVersion source,
        IFolder folder,
        string previousAssetPath,
        string assetPath,
        IReadOnlyDictionary<string, FileVersion> baselines,
        CancellationToken cancellationToken)
    {
        var edits = new List<SourceEdit>();
        var identities = new HashSet<Guid>();
        await foreach (var document in folder.GetDocumentsAsync(cancellationToken).ConfigureAwait(true))
        {
            if (!document.Name.EndsWith(Constants.SceneFileExtension, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var snapshot = await files.ReadAsync(document.Location, cancellationToken).ConfigureAwait(true);
            var root = JsonNode.Parse(snapshot.Content.AsSpan()) as JsonObject
                ?? throw new InvalidDataException($"The scene source '{document.Location}' is not a JSON object.");
            if (root[nameof(Scene.Id)] is not JsonValue identity || !identity.TryGetValue<Guid>(out var id)
                || id == Guid.Empty || !identities.Add(id))
            {
                throw new InvalidDataException($"The scene source '{document.Location}' has a missing or duplicate identity.");
            }

            var renamed = string.Equals(document.Location, source.SourcePath, StringComparison.OrdinalIgnoreCase);
            if (renamed && (snapshot.Version != source.Version || id != scene.Id
                || !string.Equals(root[nameof(Scene.Name)]?.GetValue<string>(), scene.Name, StringComparison.Ordinal)))
            {
                throw new StorageWriteConflictException("The scene source changed outside this document. Reload it before renaming.");
            }

            var changed = RepairReferences(root, previousAssetPath, assetPath);
            if (!renamed && !changed)
            {
                continue;
            }

            if (baselines.TryGetValue(document.Location, out var expected) && expected != snapshot.Version)
            {
                throw new StorageWriteConflictException($"The referencing scene '{document.Name}' changed outside the editor. Reload it before renaming.");
            }

            if (renamed)
            {
                root[nameof(Scene.Name)] = name;
            }

            edits.Add(new(id, document, snapshot, JsonSerializer.SerializeToUtf8Bytes(root, JsonOptions)));
        }

        if (!edits.Any(edit => edit.SceneId == scene.Id
            && string.Equals(edit.Document.Location, source.SourcePath, StringComparison.OrdinalIgnoreCase)))
        {
            throw new FileNotFoundException("The scene source is no longer present.", source.SourcePath);
        }

        return edits.OrderBy(edit => edit.SceneId == scene.Id ? 1 : 0).ToList();
    }

    private async Task RollbackAsync(IDocument document, string originalPath, List<SourceEdit> committed, Exception failure)
    {
        var errors = new List<Exception> { failure };
        if (!string.Equals(document.Location, originalPath, StringComparison.Ordinal))
        {
            try
            {
                await document.RenameAsync(Path.GetFileName(originalPath)).ConfigureAwait(true);
            }
            catch (StorageException exception)
            {
                errors.Add(exception);
            }
        }

        foreach (var edit in committed.AsEnumerable().Reverse())
        {
            try
            {
                _ = await files.WriteAsync(edit.Document.Location, edit.Before.Content.AsMemory(), edit.Written!, CancellationToken.None).ConfigureAwait(true);
            }
            catch (Exception exception) when (exception is StorageException or IOException or UnauthorizedAccessException)
            {
                errors.Add(exception);
            }
        }

        if (errors.Count > 1)
        {
            throw new StorageException(
                $"Scene rename failed and could not be completely restored. Inspect '{originalPath}' and '{document.Location}' before retrying.",
                new AggregateException(errors));
        }
    }

    private sealed class SourceEdit(Guid sceneId, IDocument document, FileSnapshot before, byte[] content)
    {
        internal Guid SceneId { get; } = sceneId;

        internal IDocument Document { get; } = document;

        internal FileSnapshot Before { get; } = before;

        internal byte[] Content { get; } = content;

        internal FileVersion? Written { get; set; }
    }
}
