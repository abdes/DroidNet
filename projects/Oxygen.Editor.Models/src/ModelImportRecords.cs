// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Runtime.CompilerServices;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Models;

/// <summary>The model import records of a project, by source path and by virtual path.</summary>
/// <remarks>
/// A model is imported when a record names it. The index reads <c>&lt;ProjectRoot&gt;/Imports/</c>, recursively, when it
/// is created, so a new index sees the folder as it is; callers create one per operation instead of keeping one. A record
/// file is parsed again only when its size or last write time changed, so creating an index per lookup costs a folder
/// listing. A record that cannot be read, that is not at the place of its virtual path, or whose source's mount is not a
/// bound source mount on this machine, is skipped and listed in <see cref="Problems"/>, so one such record does not hide
/// the others.
/// </remarks>
public sealed class ModelImportRecords
{
    // The parsed record files of each file system, by path. A model's record can be large, and an asset listing looks
    // up the record of every product of the model. A record's source is resolved for each index, through the mount
    // table of its project.
    private static readonly ConditionalWeakTable<IFileSystem, ConcurrentDictionary<string, ParsedRecord>> Parsed = [];

    private readonly ImmutableArray<ModelImportRecord> records;

    /// <summary>Initializes a new instance of the <see cref="ModelImportRecords"/> class from the project's records.</summary>
    /// <param name="project">The project: its file system, its root, the records' content root, and its mount table.</param>
    public ModelImportRecords(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        var fileSystem = project.FileSystem;
        this.Folder = Path.Combine(project.ProjectRoot.FullPath, ModelImportSettings.RecordsFolder);
        var found = ImmutableArray.CreateBuilder<ModelImportRecord>();
        var problems = ImmutableArray.CreateBuilder<ModelImportRecordProblem>();
        if (fileSystem.Directory.Exists(this.Folder))
        {
            foreach (var path in fileSystem.Directory.EnumerateFiles(this.Folder, "*" + ModelImportSettings.RecordSuffix, SearchOption.AllDirectories)
                .Order(StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    var settings = Read(project.FileSystem, path);
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
                catch (Exception failure) when (failure is IOException or InvalidDataException or System.Text.Json.JsonException or UnauthorizedAccessException)
                {
                    problems.Add(new(path, failure.Message));
                }
            }
        }

        this.records = found.ToImmutable();
        this.Problems = problems.ToImmutable();
    }

    /// <summary>Gets the folder that holds the records.</summary>
    public string Folder { get; }

    /// <summary>Gets every readable record.</summary>
    public IReadOnlyList<ModelImportRecord> All => this.records;

    /// <summary>Gets the records that could not be read, each with why.</summary>
    public ImmutableArray<ModelImportRecordProblem> Problems { get; }

    /// <summary>Gets whether a record names <paramref name="sourcePath"/> as its source.</summary>
    /// <param name="sourcePath">A model source file.</param>
    /// <returns><see langword="true"/> when the model is imported.</returns>
    public bool IsImported(string sourcePath) => this.TryFindBySource(sourcePath, out _);

    /// <summary>Finds the record whose source is <paramref name="sourcePath"/>.</summary>
    /// <param name="sourcePath">A model source file.</param>
    /// <param name="record">The record, when one names it.</param>
    /// <returns><see langword="true"/> when one does.</returns>
    /// <remarks>
    /// A file may back several records, one per virtual path; this finds the first. Paths compare with their case: a
    /// path that differs only in case names another asset, as after a rename that changes only case.
    /// </remarks>
    public bool TryFindBySource(string sourcePath, [NotNullWhen(true)] out ModelImportRecord? record)
    {
        var path = Path.GetFullPath(sourcePath);
        record = this.records.FirstOrDefault(entry => string.Equals(entry.SourcePath, path, StringComparison.Ordinal));
        return record is not null;
    }

    // The content of one record file, parsed again only when the file changed since it was last parsed.
    private static ModelImportSettings Read(IFileSystem fileSystem, string path)
    {
        var parsed = Parsed.GetValue(fileSystem, static _ => new(StringComparer.Ordinal));
        var file = fileSystem.FileInfo.New(path);
        var length = file.Length;
        var written = file.LastWriteTimeUtc;
        if (!parsed.TryGetValue(path, out var known) || known.Length != length || known.Written != written)
        {
            known = new(length, written, ModelImportSettings.Parse(path, fileSystem.File.ReadAllBytes(path)));
            parsed[path] = known;
        }

        return known.Settings;
    }

    // A parsed record file, with the size and last write time it had when it was read.
    private sealed record ParsedRecord(long Length, DateTime Written, ModelImportSettings Settings);
}
