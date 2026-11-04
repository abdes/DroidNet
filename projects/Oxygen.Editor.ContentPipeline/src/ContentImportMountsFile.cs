// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// The mounts file a cook passes to the ImportTool with <c>--mounts</c>: the project's resolved table, each bound source
/// mount by name and absolute folder, with <c>"root": true</c>. The tool then resolves exactly the table the editor
/// shows, and reads no <c>.mounts.json</c> file a second time.
/// </summary>
/// <param name="Mounts">Each bound source mount's absolute folder, by mount name.</param>
public sealed record ContentImportMountsFile(
    [property: JsonPropertyName("mounts")] ImmutableSortedDictionary<string, string> Mounts)
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    /// <summary>Gets a mounts file that binds nothing: the tool reads only the mounts its manifest declares.</summary>
    public static ContentImportMountsFile Empty { get; } = new(ImmutableSortedDictionary.Create<string, string>(StringComparer.Ordinal));

    /// <summary>Gets the version of the mounts file format.</summary>
    [JsonPropertyName("schema_version")]
    public int Version => ProjectMounts.SchemaVersion;

    /// <summary>Gets a value indicating whether the file stops the walk up the folder hierarchy; it always does.</summary>
    [JsonPropertyName("root")]
    public bool Root => true;

    /// <summary>Creates the mounts file of a project's resolved table.</summary>
    /// <param name="project">The project.</param>
    /// <returns>The file binding every bound source mount.</returns>
    public static ContentImportMountsFile Of(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return new(project.SourceMounts.ToImmutableSortedDictionary(
            static mount => mount.Name,
            static mount => mount.Location!.FullPath.Replace('\\', '/'),
            StringComparer.Ordinal));
    }

    /// <summary>Resolves a job's source as the tool does: a mount path through the bound folders, any other path below
    /// the input root.</summary>
    /// <param name="inputRoot">The project root the tool runs with.</param>
    /// <param name="source">The job's source.</param>
    /// <returns>The source's full path; a mount path whose mount is not bound, unchanged.</returns>
    public string Resolve(string inputRoot, string source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (!CookPaths.IsMountPath(source))
        {
            return Path.GetFullPath(source, inputRoot);
        }

        var end = source.IndexOf('/', 1);
        var name = end < 0 ? source[1..] : source[1..end];
        return this.Mounts.TryGetValue(name, out var folder)
            ? Path.GetFullPath(end < 0 ? folder : Path.Combine(folder, source[(end + 1)..]))
            : source;
    }

    /// <summary>Writes the file beside the manifest of one ImportTool run, in the operation's folder below the project's
    /// <c>.build</c> folder.</summary>
    /// <param name="path">The file's path.</param>
    /// <param name="cancellationToken">Cancels the write.</param>
    /// <returns>A task that completes when the file is written.</returns>
    public async Task WriteAsync(string path, CancellationToken cancellationToken)
    {
        var stream = File.Create(path);
        await using (stream.ConfigureAwait(false))
        {
            await JsonSerializer.SerializeAsync(stream, this, JsonOptions, cancellationToken).ConfigureAwait(false);
        }
    }
}
