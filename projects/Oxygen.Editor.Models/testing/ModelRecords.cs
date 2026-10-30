// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Models.Testing;

/// <summary>
/// The one writer of model import records in the editor's tests.
/// </summary>
/// <remarks>
/// A test names the model's identity and the identity of its source file; where the record lives and how it spells its
/// source are the contract's, decided here, so a change of the record contract changes this class, not the tests.
/// </remarks>
public static class ModelRecords
{
    private static readonly JsonElement EmptySettings = JsonSerializer.SerializeToElement(new Dictionary<string, string>());

    /// <summary>Gets the path of the record of the model <paramref name="identity"/> in <paramref name="project"/>.</summary>
    /// <param name="project">The project that holds the record.</param>
    /// <param name="identity">The model's identity, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <returns>The record's file path.</returns>
    public static string PathOf(ProjectContext project, string identity)
    {
        ArgumentNullException.ThrowIfNull(project);
        return ModelImportSettings.PathFor(project.ProjectRoot.FullPath, identity);
    }

    /// <summary>Gets the text a record of the model <paramref name="identity"/> writes to name its source <paramref name="source"/>.</summary>
    /// <param name="identity">The model's identity.</param>
    /// <param name="source">The source file's mount path, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <returns>Today, the source's mount path.</returns>
    public static string SpellSource(string identity, string source)
    {
        ArgumentNullException.ThrowIfNull(identity);
        ArgumentNullException.ThrowIfNull(source);
        return source;
    }

    /// <summary>Creates the record of a model without writing it.</summary>
    /// <param name="project">The project that holds the record.</param>
    /// <param name="identity">The model's identity.</param>
    /// <param name="source">The source file's mount path.</param>
    /// <param name="settings">The import settings; empty when omitted.</param>
    /// <param name="sourceIdentity">The record's source identity; a new one when omitted.</param>
    /// <returns>The record.</returns>
    public static ModelImportSettings Create(ProjectContext project, string identity, string source, JsonElement? settings = null, Guid? sourceIdentity = null)
    {
        return ModelImportSettings.Create(PathOf(project, identity), SpellSource(identity, source), identity, settings ?? EmptySettings, sourceIdentity);
    }

    /// <summary>Writes the record of a model, creating its folders.</summary>
    /// <param name="project">The project that holds the record.</param>
    /// <param name="identity">The model's identity.</param>
    /// <param name="source">The source file's mount path.</param>
    /// <param name="settings">The import settings; empty when omitted.</param>
    /// <param name="sourceIdentity">The record's source identity; a new one when omitted.</param>
    /// <returns>The record, as written.</returns>
    public static ModelImportSettings Write(ProjectContext project, string identity, string source, JsonElement? settings = null, Guid? sourceIdentity = null)
    {
        var record = Create(project, identity, source, settings, sourceIdentity);
        _ = Write(project, record);
        return record;
    }

    /// <summary>Writes <paramref name="record"/> at the place of its model, creating its folders.</summary>
    /// <param name="project">The project that holds the record.</param>
    /// <param name="record">The record.</param>
    /// <returns>The record's file path.</returns>
    public static string Write(ProjectContext project, ModelImportSettings record)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(record);
        var path = PathOf(project, record.VirtualPath);
        _ = project.FileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        project.FileSystem.File.WriteAllBytes(path, record.ToBytes());
        return path;
    }
}
