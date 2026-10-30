// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Text.Json;
using System.Text.RegularExpressions;
using Json.Schema;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>
/// Resolves a project's mount table as the engine does: the committed <c>Mounts</c> of <c>Project.oxy</c>, the
/// project root's <c>.mounts.json</c>, which binds and overrides, and the parent <c>.mounts.json</c> files up to one
/// marked <c>"root": true</c>, which only bind placeholders and add local mounts. The engine and the editor run the
/// same case table, <c>MountTableCases.json</c>, so the two readers cannot drift.
/// </summary>
public static class ProjectMounts
{
    /// <summary>The name of a mounts file.</summary>
    public const string FileName = ".mounts.json";

    private const string SchemaResourceName = "oxygen.mounts.schema.json";

    private static readonly Lazy<JsonDocument> SchemaDocument = new(LoadSchemaDocument);

    private static readonly Lazy<JsonSchema> Schema = new(() => JsonSchema.FromText(SchemaDocument.Value.RootElement.GetRawText()));

    private static readonly Lazy<Regex> NamePattern = new(() => new Regex(
        SchemaDocument.Value.RootElement.GetProperty("definitions").GetProperty("mount_name").GetProperty("pattern").GetString()!,
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1)));

    /// <summary>Checks a mount name against the engine's name rule, from its schema.</summary>
    /// <param name="name">The name.</param>
    /// <returns><see langword="true"/> when the name follows the rule.</returns>
    public static bool IsValidName(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        return NamePattern.Value.IsMatch(name);
    }

    /// <summary>
    /// Checks committed mount declarations without a machine: names follow the rule and are unique ignoring case, no
    /// path is absolute, and at least one source mount exists. The checks of folders need <see cref="Resolve"/>.
    /// </summary>
    /// <param name="declared">The committed mounts.</param>
    /// <returns>The first error, or <see langword="null"/> when the declarations are valid.</returns>
    public static MountFinding? ValidateDeclarations(IReadOnlyCollection<ProjectMount> declared)
    {
        ArgumentNullException.ThrowIfNull(declared);
        if (declared.FirstOrDefault(static mount => ProjectAssetFiles.ValidateDeclaredMountName(mount.Name) is not null) is { } invalid)
        {
            return new(MountCodes.NameInvalid, $"'{invalid.Name}' is not a mount name: {ProjectAssetFiles.ValidateDeclaredMountName(invalid.Name)}", invalid.Name);
        }

        if (declared.GroupBy(static mount => mount.Name, StringComparer.OrdinalIgnoreCase).FirstOrDefault(static group => group.Count() > 1) is { } duplicate)
        {
            return new(MountCodes.NameDuplicate, $"Mount '{duplicate.Key}' is declared twice, ignoring case.", duplicate.Key);
        }

        if (declared.FirstOrDefault(static mount => mount.Path is { } path && Path.IsPathRooted(path.Replace('\\', '/'))) is { } absolute)
        {
            return new(MountCodes.AbsoluteCommitted, $"Mount '{absolute.Name}' commits the absolute path '{absolute.Path}'; only .mounts.json may hold one.", absolute.Name);
        }

        return declared.Any(static mount => mount.Kind == MountKind.Source)
            ? null
            : new(MountCodes.SourceRequired, "The project declares no source mount.");
    }

    /// <summary>Resolves a project's mount table on this machine.</summary>
    /// <param name="fileSystem">The file system.</param>
    /// <param name="projectRoot">The folder of <c>Project.oxy</c>.</param>
    /// <param name="declared">The committed mounts.</param>
    /// <returns>The table and what resolving it found.</returns>
    public static ProjectMountTable Resolve(IFileSystem fileSystem, FileSystemPath projectRoot, IEnumerable<ProjectMount> declared)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        ArgumentNullException.ThrowIfNull(projectRoot);
        ArgumentNullException.ThrowIfNull(declared);
        var root = MountFolders.Canonical(fileSystem, projectRoot.FullPath);
        var builder = new MountTableBuilder(fileSystem, root);
        builder.Declare(declared);
        var files = ReadFiles(fileSystem, root, builder);
        foreach (var file in files.Where(static file => !file.IsParent))
        {
            builder.Apply(file);
        }

        builder.ApplyCommitted();
        foreach (var file in files.Where(static file => file.IsParent))
        {
            builder.Apply(file);
        }

        return builder.Finish();
    }

    /// <summary>Reads one mounts file; a file with an error binds nothing, and <c>"root": true</c> still stops the walk.</summary>
    /// <param name="fileSystem">The file system.</param>
    /// <param name="path">The file.</param>
    /// <param name="isParent">Whether the file is above the project root.</param>
    /// <param name="builder">Where findings go.</param>
    /// <returns>The file's bindings, or <see langword="null"/> when it is refused, and whether it stops the walk.</returns>
    internal static (MountsFile? File, bool StopsWalk) ReadFile(IFileSystem fileSystem, FileSystemPath path, bool isParent, MountTableBuilder builder)
    {
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(fileSystem.File.ReadAllText(path.FullPath));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            builder.Report(MountCodes.FileInvalid, $"'{path}' cannot be read as JSON: {exception.Message}", file: path);
            return (null, false);
        }

        using (document)
        {
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                builder.Report(MountCodes.FileInvalid, $"'{path}' is not a JSON object.", file: path);
                return (null, false);
            }

            var stopsWalk = root.TryGetProperty("root", out var stop) && stop.ValueKind == JsonValueKind.True;
            return IsAccepted(root, path, builder)
                ? (new MountsFile(path, isParent, ReadEntries(root)), stopsWalk)
                : (null, stopsWalk);
        }
    }

    private static List<MountsFile> ReadFiles(IFileSystem fileSystem, FileSystemPath projectRoot, MountTableBuilder builder)
    {
        var files = new List<MountsFile>();
        for (var folder = projectRoot; folder is not null; folder = folder.Parent)
        {
            var path = folder.Combine(FileName);
            if (!fileSystem.File.Exists(path.FullPath))
            {
                continue;
            }

            var (file, stopsWalk) = ReadFile(fileSystem, path, folder != projectRoot, builder);
            if (file is not null)
            {
                files.Add(file);
            }

            if (stopsWalk)
            {
                break;
            }
        }

        return files;
    }

    private static bool IsAccepted(JsonElement root, FileSystemPath path, MountTableBuilder builder)
    {
        if (root.TryGetProperty("version", out var version) && version.ValueKind == JsonValueKind.Number
            && !(version.TryGetInt32(out var number) && number == 1))
        {
            builder.Report(MountCodes.FileVersion, $"'{path}' has version {version.GetRawText()}; only version 1 is read.", file: path);
            return false;
        }

        var namesValid = !root.TryGetProperty("mounts", out var mounts) || mounts.ValueKind != JsonValueKind.Object
            || builder.CheckFileNames(mounts.EnumerateObject().Select(static entry => entry.Name), path);
        if (!namesValid)
        {
            return false;
        }

        var result = Schema.Value.Evaluate(root, new EvaluationOptions { OutputFormat = OutputFormat.List });
        if (result.IsValid)
        {
            return true;
        }

        var reason = (result.Details ?? []).Where(static detail => detail.Errors is { Count: > 0 })
            .SelectMany(static detail => detail.Errors!.Select(error => $"{detail.InstanceLocation}: {error.Value}"))
            .FirstOrDefault() ?? "it breaks oxygen.mounts.schema.json";
        builder.Report(MountCodes.FileInvalid, $"'{path}' is not a valid mounts file: {reason}.", file: path);
        return false;
    }

    private static List<(string Name, string Path)> ReadEntries(JsonElement root)
        => root.TryGetProperty("mounts", out var mounts)
            ? [.. mounts.EnumerateObject().Select(static entry => (entry.Name, entry.Value.GetString()!))]
            : [];

    private static JsonDocument LoadSchemaDocument()
    {
        using var stream = typeof(ProjectMounts).Assembly.GetManifestResourceStream(SchemaResourceName)
            ?? throw new InvalidOperationException("The engine's mounts schema is missing.");
        return JsonDocument.Parse(stream);
    }
}
