// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Runtime.CompilerServices;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>The model import records of a project, by model identity and by the source they name.</summary>
/// <remarks>
/// A record is found at its identity, which <see cref="TryRead"/> does without listing anything. The few operations that
/// need every record create an index, which lists the project's bound source mounts, own and shared, when it is created,
/// so a new index sees them as they are; callers create one per operation instead of keeping one. A record file is parsed
/// again only when its size or last write time changed. A record that cannot be read, whose name names no model, or whose
/// source's mount is not a bound source mount on this machine, is skipped and listed in <see cref="Problems"/>, so one
/// such record does not hide the others.
/// </remarks>
public sealed class ModelImportRecords
{
    // The parsed record files of each file system, by path. A model's record can be large, and an asset listing looks
    // up the record of every model. A record's source is resolved for each read, through the mount table of its project.
    private static readonly ConditionalWeakTable<IFileSystem, ConcurrentDictionary<string, ParsedRecord>> Parsed = [];

    private readonly ImmutableDictionary<AssetPath, ModelImportRecord> byIdentity;
    private readonly ILookup<string, ModelImportRecord> bySource;

    /// <summary>Initializes a new instance of the <see cref="ModelImportRecords"/> class from the project's records.</summary>
    /// <param name="project">The project: its file system and its mount table.</param>
    public ModelImportRecords(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        var found = ImmutableArray.CreateBuilder<ModelImportRecord>();
        var problems = ImmutableArray.CreateBuilder<ModelImportRecordProblem>();
        foreach (var path in project.SourceMounts.SelectMany(mount => RecordFiles(project.FileSystem, mount.Location!)).Order(StringComparer.OrdinalIgnoreCase))
        {
            try
            {
                var identity = IdentityOf(project, path)
                    ?? throw new InvalidDataException($"The file '{path}' is named as a model import record, but names no model.");
                var settings = Read(project.FileSystem, path, identity);
                if (settings.ResolveSource(project) is { } source)
                {
                    found.Add(new(path, settings, source.FullPath));
                }
                else
                {
                    problems.Add(new(path, $"The record names its source '{settings.Source}' in a mount that is not bound on this machine.")
                    {
                        UnboundSource = settings.Source,
                    });
                }
            }
            catch (Exception failure) when (IsUnreadable(failure))
            {
                problems.Add(new(path, failure.Message));
            }
        }

        this.All = found.ToImmutable();
        this.Problems = problems.ToImmutable();
        this.byIdentity = this.All.ToImmutableDictionary(static record => record.Identity);
        this.bySource = this.All.ToLookup(static record => record.SourcePath, StringComparer.Ordinal);
    }

    /// <summary>Gets every readable record.</summary>
    public IReadOnlyList<ModelImportRecord> All { get; }

    /// <summary>Gets the records that could not be read, each with why.</summary>
    public ImmutableArray<ModelImportRecordProblem> Problems { get; }

    /// <summary>Reads the record of a model at its identity, without listing the project's mounts.</summary>
    /// <param name="project">The model's project.</param>
    /// <param name="identity">The model's identity, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <returns>The record, or <see langword="null"/> when the model has none, it cannot be read, or its source's mount is
    /// not a bound source mount on this machine.</returns>
    public static ModelImportRecord? TryRead(ProjectContext project, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(identity);
        if (identity.IsMount || !AssetFileTypes.Is(identity.Name, AssetFileCategory.Model)
            || project.Find(identity.WithName(identity.Name + ModelImportSettings.RecordSuffix)) is not { MountKind: ProjectMountKind.Authoring } location
            || !project.FileSystem.File.Exists(location.File.FullPath))
        {
            return null;
        }

        try
        {
            var settings = Read(project.FileSystem, location.File.FullPath, identity);
            return settings.ResolveSource(project) is { } source ? new(location.File.FullPath, settings, source.FullPath) : null;
        }
        catch (Exception failure) when (IsUnreadable(failure))
        {
            return null;
        }
    }

    /// <summary>Finds the record of a model.</summary>
    /// <param name="identity">The model's identity.</param>
    /// <param name="record">The record, when the model has one.</param>
    /// <returns><see langword="true"/> when it does.</returns>
    public bool TryFind(AssetPath identity, [NotNullWhen(true)] out ModelImportRecord? record)
    {
        ArgumentNullException.ThrowIfNull(identity);
        return this.byIdentity.TryGetValue(identity, out record);
    }

    /// <summary>Gets every record that names a file as its source: a source has any number of records.</summary>
    /// <param name="source">A source file.</param>
    /// <returns>The records. Paths compare with their case: a path that differs only in case names another file, as after
    /// a rename that changes only case.</returns>
    public IReadOnlyList<ModelImportRecord> RecordsNaming(FileSystemPath source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return [.. this.bySource[source.FullPath]];
    }

    // The files named as records below a mount's folder; a dot-named folder is an output folder by rule.
    private static IEnumerable<string> RecordFiles(IFileSystem fileSystem, FileSystemPath root)
    {
        if (!fileSystem.Directory.Exists(root.FullPath))
        {
            return [];
        }

        var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint, IgnoreInaccessible = true };
        return fileSystem.Directory.EnumerateFiles(root.FullPath, "*" + ModelImportSettings.RecordSuffix, options)
            .Where(path => !FileSystemPath.From(path).GetPathBelow(root).Split('/').Any(static segment => segment.StartsWith('.')));
    }

    private static AssetPath? IdentityOf(ProjectContext project, string path)
        => project.FindByFilePath(FileSystemPath.From(path)) is { Identity: var identity } && AssetFileTypes.Is(identity.Name, AssetFileCategory.Model) ? identity : null;

    private static bool IsUnreadable(Exception failure)
        => failure is IOException or InvalidDataException or System.Text.Json.JsonException or UnauthorizedAccessException;

    // The content of one record file, parsed again only when the file changed since it was last parsed, or a project binds
    // its folder under another mount.
    private static ModelImportSettings Read(IFileSystem fileSystem, string path, AssetPath identity)
    {
        var parsed = Parsed.GetValue(fileSystem, static _ => new(StringComparer.Ordinal));
        var file = fileSystem.FileInfo.New(path);
        var length = file.Length;
        var written = file.LastWriteTimeUtc;
        if (!parsed.TryGetValue(path, out var known) || known.Length != length || known.Written != written || known.Settings.Identity != identity)
        {
            known = new(length, written, ModelImportSettings.Parse(fileSystem.File.ReadAllBytes(path), identity));
            parsed[path] = known;
        }

        return known.Settings;
    }

    // A parsed record file, with the size and last write time it had when it was read.
    private sealed record ParsedRecord(long Length, DateTime Written, ModelImportSettings Settings);
}
