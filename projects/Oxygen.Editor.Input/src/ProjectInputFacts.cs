// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// The input sources of one project: read when the project opens, read again when a file changes, and published as a
/// new <see cref="InputSnapshot"/> after each change.
/// </summary>
/// <remarks>
/// Reads and publications are serialized, so a source is never read twice at once and snapshots are published in the
/// order they were made. Nothing is published once the project is closed.
/// </remarks>
/// <param name="project">The project.</param>
/// <param name="publish">Publishes a snapshot, with the source that changed or <see langword="null"/> for all.</param>
/// <param name="loggerFactory">The logger factory.</param>
internal sealed partial class ProjectInputFacts(ProjectContext project, Action<InputSnapshot, AssetPath?> publish, ILoggerFactory? loggerFactory = null) : IDisposable
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectInputFacts>() ?? NullLogger<ProjectInputFacts>.Instance;
    private readonly Lock gate = new();
    private ImmutableDictionary<AssetPath, InputSourceEntry> entries = [];
    private bool disposed;

    /// <summary>Gets the project.</summary>
    public ProjectContext Project => project;

    /// <summary>Reads every input source of the project's authoring mounts and publishes them.</summary>
    /// <exception cref="IOException">A folder of the project cannot be listed.</exception>
    public void Scan()
    {
        var sources = this.EnumerateSources().ToList();
        this.LogProjectScan(sources.Count);
        lock (this.gate)
        {
            if (this.disposed)
            {
                return;
            }

            var read = ImmutableDictionary.CreateBuilder<AssetPath, InputSourceEntry>();
            foreach (var (identity, file) in sources)
            {
                if (this.Read(identity, file) is { } entry)
                {
                    read[identity] = entry;
                }
            }

            this.entries = read.ToImmutable();
            publish(InputSnapshot.Create(project, this.entries), null);
        }
    }

    /// <summary>Reads again the input source a catalog change names, and publishes the result when it changed.</summary>
    /// <param name="change">The change.</param>
    public void Update(AssetChange change)
    {
        ArgumentNullException.ThrowIfNull(change);
        lock (this.gate)
        {
            if (this.disposed)
            {
                return;
            }

            var before = this.entries;
            var sources = new[] { change.PreviousIdentity, change.Identity }.Select(this.SourceOf).OfType<(AssetPath Identity, FileSystemPath File)>().ToList();
            foreach (var (identity, file) in sources)
            {
                // The file is read whatever the change says: changes can arrive out of order, and the file is the truth.
                this.entries = this.Read(identity, file) is { } entry ? this.entries.SetItem(identity, entry) : this.entries.Remove(identity);
            }

            if (!ReferenceEquals(before, this.entries))
            {
                // A move changes two sources: the one it left and the one it made.
                publish(InputSnapshot.Create(project, this.entries), sources is [var only] ? only.Identity : null);
            }
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        lock (this.gate)
        {
            this.disposed = true;
        }
    }

    private IEnumerable<(AssetPath Identity, FileSystemPath File)> EnumerateSources()
    {
        foreach (var mount in project.AuthoringMounts.Where(static mount => !ProjectOutputFolders.Contains(mount)))
        {
            var root = project.ProjectRoot.Combine(mount.RelativePath);
            if (!project.FileSystem.Directory.Exists(root.FullPath))
            {
                continue;
            }

            foreach (var path in project.FileSystem.Directory.EnumerateFiles(root.FullPath, "*" + AssetFileTypes.InputFile, SearchOption.AllDirectories))
            {
                var file = FileSystemPath.From(path);
                if (!file.GetPathBelow(root).Split('/').Any(static segment => segment.StartsWith('.'))
                    && this.SourceOf(project.FindByFilePath(file)?.Path) is { } source)
                {
                    yield return source;
                }
            }
        }
    }

    // The source a file of the project is, when it is an input source in an authoring mount.
    private (AssetPath Identity, FileSystemPath File)? SourceOf(AssetPath? path)
        => path is not null && AssetFileTypes.Is(path.Name, AssetFileCategory.Input) && InputSourceFiles.IsAuthored(project, path)
            && project.Find(path) is { File: var file }
                ? (AssetFileTypes.ToIdentity(path), file)
                : null;

    // The file as it is now: null when it is gone; the entry read before when it cannot be read now, as while the
    // editor that saved it still holds it, since its next change reads it again.
    private InputSourceEntry? Read(AssetPath identity, FileSystemPath file)
    {
        try
        {
            var report = InputSourceReader.Check(project.FileSystem.File.ReadAllBytes(file.FullPath));
            return new InputSourceEntry(file, report.Source is { } source ? new InputContainer(identity, source) : null, report.Problems);
        }
        catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException)
        {
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            this.LogSourceUnreadable(file, exception);
            return this.entries.GetValueOrDefault(identity);
        }
    }
}
