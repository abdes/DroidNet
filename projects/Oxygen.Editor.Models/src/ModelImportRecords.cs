// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;

namespace Oxygen.Editor.Models;

/// <summary>The model import records of a project, by source path and by virtual path.</summary>
/// <remarks>
/// A model is imported when a record names it. The index reads <c>&lt;ProjectRoot&gt;/imports/</c> when it is created,
/// so a new index sees the folder as it is; callers create one per operation instead of keeping one. A record that
/// cannot be read is skipped and listed in <see cref="Problems"/>, so one damaged record does not hide the others.
/// </remarks>
public sealed class ModelImportRecords
{
    private readonly ImmutableArray<ModelImportRecord> records;

    /// <summary>Initializes a new instance of the <see cref="ModelImportRecords"/> class from the project's records.</summary>
    /// <param name="fileSystem">The project's file system.</param>
    /// <param name="projectRoot">The project root, the records' content root.</param>
    public ModelImportRecords(IFileSystem fileSystem, string projectRoot)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        this.Folder = Path.Combine(Path.GetFullPath(projectRoot), ModelImportSettings.RecordsFolder);
        var records = ImmutableArray.CreateBuilder<ModelImportRecord>();
        var problems = ImmutableArray.CreateBuilder<ModelImportRecordProblem>();
        if (fileSystem.Directory.Exists(this.Folder))
        {
            foreach (var path in fileSystem.Directory.EnumerateFiles(this.Folder, "*" + ModelImportSettings.RecordSuffix)
                .Order(StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    var settings = ModelImportSettings.Parse(path, fileSystem.File.ReadAllBytes(path));
                    records.Add(new(path, settings, settings.ResolveSource(path)));
                }
                catch (Exception failure) when (failure is IOException or InvalidDataException or System.Text.Json.JsonException or UnauthorizedAccessException)
                {
                    problems.Add(new(path, failure.Message));
                }
            }
        }

        this.records = records.ToImmutable();
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
}
