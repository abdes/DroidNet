// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.Input;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Turns a relocation request into physical moves, an identity mapping and rewritten referrers, rejecting
/// any request that would break an import, a scene's identity or derived content.
/// </summary>
internal static class AssetRelocationPlanner
{
    /// <summary>Plans a relocation against the saved project files.</summary>
    /// <param name="project">The project.</param>
    /// <param name="request">The requested moves.</param>
    /// <param name="index">The project's current references.</param>
    /// <param name="files">Reads referrer baselines.</param>
    /// <param name="cancellationToken">Cancels planning.</param>
    /// <returns>The plan.</returns>
    /// <exception cref="AssetRelocationException">The request is not allowed; nothing was changed.</exception>
    public static async Task<AssetRelocationPlan> PlanAsync(
        ProjectContext project,
        AssetRelocationRequest request,
        AuthoredReferenceIndex index,
        IAtomicFileStore files,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(index);
        var map = new RelocationMap();
        AddRemovedSourceCopies(project, index, map, request.RemovedSourceCopies);
        foreach (var move in request.Moves.Where(static move => move.Source != move.Target))
        {
            AddRequestedMove(project, index, map, move);
        }

        var closure = AddSharedClosure(project, index, map);
        var referenced = FindReferencedSources(project, index, map);
        var copies = request.CopiesSourcesIn ? AddSourceCopies(project, map, referenced) : [];

        // A file's references name the asset it stands for; a script source's name the script it cooks to. A
        // container's products are named under it, so they move with it as a folder's files do.
        foreach (var move in map.Moves)
        {
            var source = RelocationPaths.ToVirtual(project, move.Source)!;
            var target = RelocationPaths.ToVirtual(project, move.Target)!;
            map.AddPath(source, target, move.IsDirectory);
            var isContainer = !move.IsDirectory && (HasProducts(source) || HasProducts(AssetFileTypes.ToIdentity(source)));
            map.AddIdentity(move.IsDirectory ? source : AssetFileTypes.ToIdentity(source), move.IsDirectory ? target : AssetFileTypes.ToIdentity(target), move.IsDirectory || isContainer);
        }

        var renames = new Dictionary<FileSystemPath, List<InputProductRename>>();
        foreach (var rename in request.ProductRenames.Where(static rename => rename.Product != rename.Target))
        {
            AddProductRename(project, index, map, renames, rename);
        }

        if (request.Retargets.FirstOrDefault(retarget => !index.Descriptors.Any(descriptor => descriptor.Location == retarget.File)) is { } gone)
        {
            throw new AssetRelocationException($"'{gone.File.Name}' changed since its references were checked. Check them again.");
        }

        RefuseReferencesIntoOwnMounts(project, index, map);
        var edits = await PlanEditsAsync(project, index, map, renames, request.Retargets.ToLookup(static retarget => retarget.File), files, cancellationToken).ConfigureAwait(false);
        var referrers = edits.Where(edit => !map.IsMoved(edit.OriginalPath) && !renames.ContainsKey(edit.OriginalPath))
            .Select(edit => RelocationPaths.ToVirtual(project, edit.OriginalPath)!)
            .Order().ToArray();
        var touched = map.Moves.SelectMany(move => new[] { RelocationPaths.ToVirtual(project, move.Source), RelocationPaths.ToVirtual(project, move.Target) }).OfType<AssetPath>();
        return new(request, map, edits, referrers)
        {
            ClosureMoves = closure,
            SharedMounts = RelocationPaths.SharedMountsOf(project, [.. touched, .. referrers]),
            ReferencedSources = request.CopiesSourcesIn ? [] : [.. referenced.Select(static import => import.PrimaryVirtualPath).Distinct().Order()],
            SourceCopies = copies,
        };
    }

    /// <summary>Finds the imported model a product is named under.</summary>
    /// <param name="product">The identity.</param>
    /// <returns>The model, or null when the identity is not a model's product.</returns>
    internal static AssetPath? ModelOf(AssetPath product)
        => AssetFileTypes.ContainerOf(product) is { } container && AssetFileTypes.Is(container.Name, AssetFileCategory.Model) ? container : null;

    private static bool HasProducts(AssetPath path) => AssetFileTypes.Find(path.Name) is { ProductExtensions.Count: > 0 };

    private static void AddRequestedMove(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map, AssetRelocationMove move)
    {
        if (RelocationPaths.ToPhysical(project, move.Source) is { } file
            && map.Moves.Any(existing => file.IsSameOrInside(existing.Source) || existing.Source.IsSameOrInside(file)))
        {
            throw new AssetRelocationException("Select either a folder or the items inside it, not both.");
        }

        AddMove(project, index, map, move, createsFolders: false);
    }

    // A model moves as its record and companions; a source file as its own files; anything else on its own.
    private static void AddMove(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map, AssetRelocationMove move, bool createsFolders)
    {
        RequireMovable(project, move);
        if (index.FindModel(move.Source) is { } model)
        {
            AddModelMove(project, map, model, move, createsFolders);
            return;
        }

        var (source, target, isDirectory) = Resolve(project, move, createsFolders);
        if (!isDirectory && index.Imports.FirstOrDefault(import => import.Files.Contains(source)) is { } read)
        {
            AddSourceMove(project, map, read, source, target);
            AddPreviewOfFollowingRecord(project, map, read);
            return;
        }

        map.AddMove(new(source, target, isDirectory));
        if (!isDirectory)
        {
            AddTextureCompanions(project, index, map, source, target);
            AddModelPreview(project, map, source, target);
        }
    }

    private static void AddModelMove(ProjectContext project, RelocationMap map, AuthoredReferenceIndex.ImportSource model, AssetRelocationMove move, bool createsFolders)
    {
        var identity = AssetFileTypes.ToIdentity(move.Target);
        if (!string.Equals(AssetFileTypes.GetSuffix(model.Identity.Name), AssetFileTypes.GetSuffix(identity.Name), StringComparison.OrdinalIgnoreCase))
        {
            throw new AssetRelocationException("A rename keeps the asset's type; change only its name.");
        }

        var recordMove = new AssetRelocationMove(RelocationPaths.ToVirtual(project, model.RecordPath)!, identity.WithName(identity.Name + AssetFileTypes.ImportSettings));
        if (RelocationPaths.ToPhysical(project, recordMove.Target) is not { } target)
        {
            throw new AssetRelocationException($"'{model.Identity}' cannot be moved to '{identity}'.");
        }

        RequireValidTarget(project, recordMove, model.RecordPath, target, isDirectory: false, createsFolders);
        if (model.HasCompanionSource && !map.IsRemoved(model.PrimaryPath))
        {
            AddSourceMove(project, map, model, model.PrimaryPath, CompanionTarget(model, target.Parent!, identity.Name));
        }

        if (project.FileSystem.File.Exists(model.Preview.FullPath) && !map.IsMoved(model.Preview))
        {
            AddFile(project, map, model.Preview, target.Parent!.Combine(AssetFileTypes.PreviewOf(identity.Name)));
        }

        map.AddMove(new(model.RecordPath, target, IsDirectory: false));
    }

    // A companion keeps its place below the record: beside it with the model's name, or in the folder of that name.
    private static FileSystemPath CompanionTarget(AuthoredReferenceIndex.ImportSource model, FileSystemPath folder, string name)
    {
        if (model.PrimaryPath.Parent == model.RecordPath.Parent)
        {
            return folder.Combine(name);
        }

        var below = model.PrimaryPath.GetPathBelow(model.RecordPath.Parent!).Split('/');
        below[0] = RelocationPaths.GetDisplayName(name);
        return folder.Combine(string.Join('/', below));
    }

    // The files a source reads keep their places beside it, since it names them: they follow it, and it follows them.
    private static void AddSourceMove(ProjectContext project, RelocationMap map, AuthoredReferenceIndex.ImportSource import, FileSystemPath file, FileSystemPath target)
    {
        var folder = FolderOfMovedSource(import, file, target);
        var primary = file == import.PrimaryPath ? target : folder.Combine(import.PrimaryPath.Name);
        foreach (var (source, destination) in import.SourceFiles.Select(member => (member, member == import.PrimaryPath ? primary : folder.Combine(member.GetPathBelow(import.Folder)))))
        {
            if (source != destination && project.FileSystem.File.Exists(source.FullPath) && !map.IsMoved(source))
            {
                AddFile(project, map, source, destination);
            }
        }
    }

    // A record beside its source follows it when the record is edited, and the preview beside the record goes with them.
    private static void AddPreviewOfFollowingRecord(ProjectContext project, RelocationMap map, AuthoredReferenceIndex.ImportSource import)
    {
        if (import.Settings.Identity == import.PrimaryVirtualPath && project.FileSystem.File.Exists(import.Preview.FullPath) && !map.IsMoved(import.Preview))
        {
            var target = map.MapPath(import.PrimaryPath);
            AddFile(project, map, import.Preview, target.Parent!.Combine(AssetFileTypes.PreviewOf(target.Name)));
        }
    }

    private static FileSystemPath FolderOfMovedSource(AuthoredReferenceIndex.ImportSource import, FileSystemPath file, FileSystemPath target)
    {
        if (file == import.PrimaryPath)
        {
            return target.Parent!;
        }

        // A file the source reads keeps its name, and its place below the folder that holds the source.
        var depth = file.Parent == import.Folder ? 0 : file.Parent!.GetPathBelow(import.Folder).Split('/').Length;
        var folder = target.Parent;
        for (var level = 0; level < depth && folder is not null; level++)
        {
            folder = folder.Parent;
        }

        return folder is not null && string.Equals(file.Name, target.Name, FileSystemPath.NameComparison)
            && target.GetPathBelow(folder) == file.GetPathBelow(import.Folder)
                ? folder
                : throw new AssetRelocationException($"'{import.PrimaryVirtualPath.Name}' reads '{file.Name}' by its name and place. Move the model to move it.");
    }

    // Shared content never references an own mount: what a move into a shared mount needs from own mounts goes with it, at
    // its path below its mount, and so does what that needs.
    private static List<AssetRelocationMove> AddSharedClosure(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map)
    {
        var closure = new List<AssetRelocationMove>();
        for (var added = true; added;)
        {
            added = false;
            foreach (var (needed, mount) in NeededByShared(project, index, map).ToArray())
            {
                if (FindSource(project, needed) is { } source && !map.IsMoved(source.File))
                {
                    var move = new AssetRelocationMove(source.Path, AssetPath.Parse("/" + mount).Combine(source.Path.GetPathBelow(AssetPath.Parse("/" + source.Path.Mount))));
                    AddMove(project, index, map, move, createsFolders: true);
                    closure.Add(move);
                    added = true;
                }
            }
        }

        return closure;
    }

    // The models moved into a shared mount that reference a source in another mount, which does not move with them.
    private static List<AuthoredReferenceIndex.ImportSource> FindReferencedSources(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map)
        => [.. index.Imports.Where(import => map.IsMoved(import.RecordPath) && !map.IsMoved(import.PrimaryPath)
            && SharedMountAfter(project, map, import.RecordPath) is { } mount
            && !string.Equals(import.PrimaryVirtualPath.Mount, mount, StringComparison.Ordinal)
            && project.FileSystem.File.Exists(import.PrimaryPath.FullPath))];

    // A model's own copy of its source is a companion: beside its record with its name, or in a folder of that name when the
    // source reads other files, which keep their places beside it.
    private static List<AssetRelocationMove> AddSourceCopies(ProjectContext project, RelocationMap map, List<AuthoredReferenceIndex.ImportSource> models)
    {
        var copies = new List<AssetRelocationMove>();
        foreach (var model in models)
        {
            var record = map.MapPath(model.RecordPath);
            var name = model.Identity.Name;
            var members = model.SourceFiles.ToArray();
            var folder = members.Length == 1 ? record.Parent! : record.Parent!.Combine(RelocationPaths.GetDisplayName(name));
            foreach (var member in members)
            {
                var copy = member == model.PrimaryPath ? folder.Combine(name) : folder.Combine(member.GetPathBelow(model.Folder));
                map.AddSourceReplacement(model.RecordPath, member, copy);
                if (project.FileSystem.File.Exists(member.FullPath))
                {
                    if (project.FileSystem.File.Exists(copy.FullPath) || map.Copies.Any(existing => existing.Target == copy))
                    {
                        throw new AssetRelocationException($"'{copy.Name}' already exists next to the destination.");
                    }

                    map.AddCopy(member, copy);
                    copies.Add(new(RelocationPaths.ToVirtual(project, copy)!, RelocationPaths.ToVirtual(project, member)!));
                }
            }
        }

        return copies;
    }

    // An undo gives a model back the source it referenced, and removes the copy only when nothing would be lost.
    private static void AddRemovedSourceCopies(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map, IReadOnlyList<AssetRelocationMove> copies)
    {
        var fileSystem = project.FileSystem;
        foreach (var removal in copies)
        {
            if (RelocationPaths.ToPhysical(project, removal.Source) is not { } copy || RelocationPaths.ToPhysical(project, removal.Target) is not { } original
                || !fileSystem.File.Exists(copy.FullPath) || !fileSystem.File.Exists(original.FullPath))
            {
                throw new AssetRelocationException($"'{removal.Source}' or the source it was copied from no longer exists.");
            }

            if (!fileSystem.File.ReadAllBytes(copy.FullPath).AsSpan().SequenceEqual(fileSystem.File.ReadAllBytes(original.FullPath)))
            {
                throw new AssetRelocationException($"'{removal.Source}' changed after it was copied, and removing it would lose that.");
            }

            var readers = index.Imports.Where(import => import.SourceFiles.Contains(copy)).ToArray();
            if (readers.Length != 1)
            {
                throw new AssetRelocationException($"'{removal.Source}' is not read by the one model it was copied for.");
            }

            map.AddSourceReplacement(readers[0].RecordPath, copy, original);
            map.AddRemoved(copy);
        }
    }

    // What the files moved into a shared mount reference in an own mount, with the shared mount that needs it.
    private static IEnumerable<(AssetPath Needed, string Mount)> NeededByShared(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map)
    {
        foreach (var descriptor in index.Descriptors.Where(descriptor => map.IsMoved(descriptor.Location)))
        {
            if (SharedMountAfter(project, map, descriptor.Location) is { } mount)
            {
                foreach (var reference in descriptor.References.Where(reference => RelocationPaths.RoleOf(project, reference) == MountRole.Own))
                {
                    yield return (reference, mount);
                }
            }
        }

        foreach (var import in index.Imports.Where(import => map.IsMoved(import.RecordPath)))
        {
            if (SharedMountAfter(project, map, import.RecordPath) is { } mount && RelocationPaths.RoleOf(project, import.PrimaryVirtualPath) == MountRole.Own)
            {
                yield return (import.PrimaryVirtualPath, mount);
            }
        }
    }

    private static string? SharedMountAfter(ProjectContext project, RelocationMap map, FileSystemPath file)
        => RelocationPaths.ToVirtual(project, map.MapPath(file)) is { } target && RelocationPaths.RoleOf(project, target) == MountRole.Shared ? target.Mount : null;

    // The file that stands for an identity, a product's being its container's.
    private static (AssetPath Path, FileSystemPath File)? FindSource(ProjectContext project, AssetPath identity)
    {
        foreach (var candidate in AssetFileTypes.SourcesOf(AssetFileTypes.ContainerOf(identity) ?? identity))
        {
            if (RelocationPaths.ToPhysical(project, candidate) is { } file && project.FileSystem.File.Exists(file.FullPath))
            {
                return (candidate, file);
            }
        }

        return null;
    }

    // A move can leave shared content referencing an own mount when the reference is not the closure's to follow: an asset
    // moved out of a shared mount into an own one. Files a move does not touch are the reference check's to report.
    private static void RefuseReferencesIntoOwnMounts(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map)
    {
        foreach (var descriptor in index.Descriptors)
        {
            var moved = map.IsMoved(descriptor.Location);
            if (RelocationPaths.ToVirtual(project, map.MapPath(descriptor.Location)) is not { } place || RelocationPaths.RoleOf(project, place) != MountRole.Shared)
            {
                continue;
            }

            foreach (var reference in descriptor.References)
            {
                var mapped = map.MapIdentity(reference);
                if ((moved || mapped is not null) && RelocationPaths.RoleOf(project, mapped ?? reference) == MountRole.Own)
                {
                    throw new AssetRelocationException(
                        ReferenceCheck.ReferenceIntoProjectMount,
                        $"'{place}' is in the shared mount '{place.Mount}' and would reference '{mapped ?? reference}' in this project's own mount. Move that asset out of the way first, or change the reference.");
                }
            }
        }
    }

    private static (FileSystemPath Source, FileSystemPath Target, bool IsDirectory) Resolve(ProjectContext project, AssetRelocationMove move, bool createsFolders)
    {
        if (RelocationPaths.ToPhysical(project, move.Source) is not { } source || RelocationPaths.ToPhysical(project, move.Target) is not { } target)
        {
            throw new AssetRelocationException($"'{move.Source}' cannot be moved to '{move.Target}'.");
        }

        var fileSystem = project.FileSystem;
        var isDirectory = fileSystem.Directory.Exists(source.FullPath);
        if (!isDirectory && !fileSystem.File.Exists(source.FullPath))
        {
            throw new AssetRelocationException($"'{move.Source}' no longer exists; it was deleted or moved outside the Content Explorer.");
        }

        RequireValidTarget(project, move, source, target, isDirectory, createsFolders);
        return (source, target, isDirectory);
    }

    private static void RequireMovable(ProjectContext project, AssetRelocationMove move)
    {
        if (ModelOf(move.Source) is { } model)
        {
            throw AssetRelocationException.ModelProduct(move.Source, model);
        }

        if (RelocationPaths.FindWritableMount(project, move.Source) is null)
        {
            throw new AssetRelocationException($"'{move.Source}' is read-only. Only project content can be renamed or moved.");
        }

        if (RelocationPaths.FindWritableMount(project, move.Target) is null)
        {
            throw new AssetRelocationException("Choose a destination under Content or another authoring folder.");
        }

        if (move.Source.IsMount)
        {
            throw new AssetRelocationException("A content mount is renamed in Mounts, not here.");
        }
    }

    private static void RequireValidTarget(ProjectContext project, AssetRelocationMove move, FileSystemPath source, FileSystemPath target, bool isDirectory, bool createsFolders)
    {
        var fileSystem = project.FileSystem;
        var name = move.Target.Name;
        if (VirtualPath.ValidateSegment(name, VirtualPathRuleSet.Authored) is { } invalid)
        {
            throw new AssetRelocationException(invalid.ToMessage());
        }

        if (isDirectory && move.Target.IsSameOrInside(move.Source))
        {
            throw new AssetRelocationException("A folder cannot be moved into itself.");
        }

        if (!isDirectory && !string.Equals(AssetFileTypes.GetSuffix(move.Source.Name), AssetFileTypes.GetSuffix(name), StringComparison.OrdinalIgnoreCase))
        {
            throw new AssetRelocationException("A rename keeps the asset's type; change only its name.");
        }

        // A change of case only is a rename of the same file.
        if (source != target && (fileSystem.File.Exists(target.FullPath) || fileSystem.Directory.Exists(target.FullPath)))
        {
            throw new AssetRelocationException($"'{name}' already exists in '{move.Target.Parent}'.");
        }

        if (!createsFolders && !fileSystem.Directory.Exists(target.Parent!.FullPath))
        {
            throw new AssetRelocationException($"The destination folder '{move.Target.Parent}' does not exist.");
        }

        if (!isDirectory && SourceCollisions.Find(project, target, ignored: source) is [var collision, ..])
        {
            throw new AssetRelocationException(AssetRelocationException.SourceCollisionCode, $"'{collision.Name}' is already in '{move.Target.Parent}'; both would cook to the same asset. Choose another name.");
        }
    }

    private static void AddFile(ProjectContext project, RelocationMap map, FileSystemPath source, FileSystemPath target)
    {
        if (project.FileSystem.File.Exists(target.FullPath) && source != target)
        {
            throw new AssetRelocationException($"'{target.Name}' already exists next to the destination.");
        }

        map.AddMove(new(source, target, IsDirectory: false));
    }

    // A model's preview is named after the model's whole file name and stays beside it: it moves and renames with it.
    private static void AddModelPreview(ProjectContext project, RelocationMap map, FileSystemPath source, FileSystemPath target)
    {
        if (!AssetFileTypes.Is(source.Name, AssetFileCategory.Model))
        {
            return;
        }

        var preview = FileSystemPath.From(AssetFileTypes.PreviewOf(source.FullPath));
        if (project.FileSystem.File.Exists(preview.FullPath) && !map.IsMoved(preview))
        {
            AddFile(project, map, preview, FileSystemPath.From(AssetFileTypes.PreviewOf(target.FullPath)));
        }
    }

    // A texture descriptor and the image beside it are one asset: each moves with the other, and a rename
    // renames an image that shares the descriptor's name.
    private static void AddTextureCompanions(ProjectContext project, AuthoredReferenceIndex index, RelocationMap map, FileSystemPath source, FileSystemPath target)
    {
        var fileSystem = project.FileSystem;
        var folder = source.Parent!;
        var targetFolder = target.Parent!;
        var stem = RelocationPaths.GetDisplayName(source.Name);
        var newStem = RelocationPaths.GetDisplayName(target.Name);
        FileSystemPath Companion(FileSystemPath file)
            => targetFolder.Combine(string.Equals(RelocationPaths.GetDisplayName(file.Name), stem, FileSystemPath.NameComparison)
                ? newStem + AssetFileTypes.GetSuffix(file.Name)
                : file.Name);

        if (source.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal))
        {
            var content = fileSystem.File.ReadAllBytes(source.FullPath);
            foreach (var image in AuthoredReferenceFormat.ReadTextureSources(project, source, content)
                .Where(image => image.Parent == folder && fileSystem.File.Exists(image.FullPath) && !map.IsMoved(image)))
            {
                AddFile(project, map, image, Companion(image));
            }

            return;
        }

        foreach (var descriptor in index.Descriptors.Where(descriptor => descriptor.Location.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal)
            && descriptor.Location.Parent == folder && !map.IsMoved(descriptor.Location)))
        {
            if (AuthoredReferenceFormat.ReadTextureSources(project, descriptor.Location, fileSystem.File.ReadAllBytes(descriptor.Location.FullPath)).Contains(source))
            {
                AddFile(project, map, descriptor.Location, Companion(descriptor.Location));
            }
        }
    }

    // An action or a context is renamed inside its source: the source is rewritten, and the product's new identity
    // retargets every reference to it.
    private static void AddProductRename(
        ProjectContext project,
        AuthoredReferenceIndex index,
        RelocationMap map,
        Dictionary<FileSystemPath, List<InputProductRename>> renames,
        InputProductRename rename)
    {
        if (ModelOf(rename.Product) is { } model)
        {
            throw AssetRelocationException.ModelProduct(rename.Product, model);
        }

        if (AssetFileTypes.ContainerOf(rename.Product) is not { } container || RelocationPaths.FindWritableMount(project, rename.Product) is null)
        {
            throw new AssetRelocationException($"'{rename.Product}' is not an action or a mapping context of the project's input sources.");
        }

        var file = InputSourceFiles.FileOf(project, container) is { } source && project.FileSystem.File.Exists(source.FullPath)
            ? source
            : throw new AssetRelocationException($"The input source of '{rename.Product.Name}' no longer exists.");
        if (map.IsMoved(file) || !index.Descriptors.Any(descriptor => descriptor.Location == file))
        {
            throw new AssetRelocationException($"Rename '{rename.CurrentName}' on its own, not while its input source moves.");
        }

        var pending = renames.TryGetValue(file, out var existing) ? existing : renames[file] = [];
        try
        {
            var content = project.FileSystem.File.ReadAllBytes(file.FullPath);
            foreach (var earlier in pending)
            {
                content = AuthoredReferenceFormat.RenameInputProduct(content, earlier);
            }

            _ = AuthoredReferenceFormat.RenameInputProduct(content, rename);
        }
        catch (AssetFormatException error)
        {
            throw new AssetRelocationException(error.Problem.Message, error);
        }

        pending.Add(rename);
        map.AddIdentity(rename.Product, rename.Target, isFolder: false);
    }

    private static async Task<IReadOnlyList<RelocationEdit>> PlanEditsAsync(
        ProjectContext project,
        AuthoredReferenceIndex index,
        RelocationMap map,
        Dictionary<FileSystemPath, List<InputProductRename>> renames,
        ILookup<FileSystemPath, ReferenceRetarget> retargets,
        IAtomicFileStore files,
        CancellationToken cancellationToken)
    {
        var edits = new List<RelocationEdit>();
        foreach (var descriptor in index.Descriptors)
        {
            var snapshot = await files.ReadAsync(descriptor.Location.FullPath, cancellationToken).ConfigureAwait(false);
            var content = snapshot.Content.ToArray();
            var renamed = content;
            foreach (var rename in renames.GetValueOrDefault(descriptor.Location) ?? [])
            {
                renamed = AuthoredReferenceFormat.RenameInputProduct(renamed, rename);
            }

            foreach (var retarget in retargets[descriptor.Location])
            {
                renamed = AuthoredReferenceFormat.RetargetReference(descriptor.Location, renamed, retarget);
            }

            if ((AuthoredReferenceFormat.RewriteDescriptor(project, descriptor.Location, renamed, map)
                ?? (ReferenceEquals(renamed, content) ? null : renamed)) is { } rewritten)
            {
                edits.Add(new(descriptor.Location, map.MapPath(descriptor.Location), snapshot.Version, content, rewritten));
            }
        }

        foreach (var import in index.Imports)
        {
            var edit = await PlanRecordEditAsync(project, import, map, files, cancellationToken).ConfigureAwait(false);
            if (edit is not null)
            {
                edits.Add(edit);
            }
        }

        return edits;
    }

    // A record beside the model it names moves and renames with it, so its products are named under the model's new path;
    // any other stays where it is, or moves with its folder. Either way its source is spelled from where it now is.
    private static async Task<RelocationEdit?> PlanRecordEditAsync(ProjectContext project, AuthoredReferenceIndex.ImportSource import, RelocationMap map, IAtomicFileStore files, CancellationToken cancellationToken)
    {
        var target = map.MapSource(import.RecordPath, import.PrimaryPath);
        var folder = target.Parent!;

        // The primary may be renamed in its folder; the files it reads must keep their place beside it.
        if (import.Files.Any(file => file != import.PrimaryPath && !string.Equals(
            Path.GetRelativePath(import.Folder.FullPath, file.FullPath),
            Path.GetRelativePath(folder.FullPath, map.MapSource(import.RecordPath, file).FullPath),
            StringComparison.OrdinalIgnoreCase)))
        {
            throw new AssetRelocationException($"The move would separate '{import.PrimaryVirtualPath.Name}' from its files. Move the model's folder instead.");
        }

        // Paths compare without case on Windows; a rename that changes only case still renames the model.
        var followsModel = import.Settings.Identity == import.PrimaryVirtualPath;
        var mapped = map.MapPath(import.RecordPath);
        var finalPath = followsModel && string.Equals(mapped.FullPath, import.RecordPath.FullPath, StringComparison.Ordinal)
            ? target.Parent!.Combine(target.Name + ModelImportSettings.RecordSuffix)
            : mapped;
        if (string.Equals(target.FullPath, import.PrimaryPath.FullPath, StringComparison.Ordinal)
            && string.Equals(finalPath.FullPath, import.RecordPath.FullPath, StringComparison.Ordinal))
        {
            return null;
        }

        var snapshot = await files.ReadAsync(import.RecordPath.FullPath, cancellationToken).ConfigureAwait(false);
        var content = snapshot.Content.ToArray();
        var identity = RelocationPaths.ToVirtual(project, finalPath) is { } place
            ? AssetFileTypes.ToIdentity(place)
            : throw new AssetRelocationException($"'{import.PrimaryVirtualPath.Name}' would leave the project's authoring mounts.");
        var source = project.ToFileReference(identity, target)
            ?? throw new AssetRelocationException($"'{import.PrimaryVirtualPath.Name}' would leave the project's source mounts.");
        var rewritten = (ModelImportSettings.Parse(content, import.Settings.Identity) with { Identity = identity }).WithSource(source).ToBytes();
        if (!string.Equals(finalPath.FullPath, mapped.FullPath, StringComparison.Ordinal))
        {
            map.AddMove(new(import.RecordPath, finalPath, IsDirectory: false));
        }

        return rewritten.AsSpan().SequenceEqual(content) ? null : new(import.RecordPath, finalPath, snapshot.Version, content, rewritten);
    }
}
