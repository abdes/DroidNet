// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using System.Text.Encodings.Web;
using System.Text.Json;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>An import session: the models of one import dialog, their identities and settings, and the run's execution
/// settings, saved as an ImportTool import manifest (<c>oxygen.import-manifest.schema.json</c>).</summary>
/// <remarks>
/// One file serves the editor and the command line (<c>ImportTool batch --manifest … --cooked-root …</c>): one
/// <c>gltf</c> or <c>fbx</c> job per model, in the session's order, with its source (relative to the manifest's folder
/// when it can be), its virtual path and the settings it sets; the run's settings at the top when it sets them; no
/// <c>output</c>, since the editor cooks into the project and the command line names its cooked root.
/// </remarks>
public sealed record ModelImportSession
{
    private const int ManifestVersion = 1;

    private static readonly JsonWriterOptions WriterOptions = new()
    {
        Indented = true,
        NewLine = "\n",
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    // The keys of a job that are not model import settings.
    private static readonly ImmutableHashSet<string> JobKeys = ImmutableHashSet.Create(
        StringComparer.Ordinal, "id", "type", "source", "sources", "virtual_path", "output", "depends_on", "layout", "source_key", "cooked_context_roots", "record");

    /// <summary>Gets the session's models, in order.</summary>
    public ImmutableArray<ModelImportSessionJob> Jobs { get; init; } = [];

    /// <summary>Gets the run's execution settings.</summary>
    public ImportRunSettings Run { get; init; } = ImportRunSettings.None;

    /// <summary>Gets the identifiers, or types, of the manifest's jobs that are not models, which a session leaves out.</summary>
    public ImmutableArray<string> SkippedJobs { get; init; } = [];

    /// <summary>Reads a session from an import manifest, validated against the installed schema.</summary>
    /// <param name="manifestPath">The manifest's path; relative sources resolve beside it.</param>
    /// <param name="bytes">The manifest's bytes.</param>
    /// <returns>The session; jobs of other types are in <see cref="SkippedJobs"/>.</returns>
    /// <exception cref="InvalidDataException">The bytes are not a valid import manifest.</exception>
    public static ModelImportSession Read(string manifestPath, ReadOnlyMemory<byte> bytes)
    {
        ArgumentException.ThrowIfNullOrEmpty(manifestPath);
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(bytes);
        }
        catch (JsonException error)
        {
            throw new InvalidDataException("This file is not an import manifest.", error);
        }

        using (document)
        {
            var root = document.RootElement;
            if (!ImportManifestSchema.IsValid(root))
            {
                throw new InvalidDataException("This file is not a valid import manifest.");
            }

            var folder = Path.GetDirectoryName(Path.GetFullPath(manifestPath))!;
            var jobs = ImmutableArray.CreateBuilder<ModelImportSessionJob>();
            var skipped = ImmutableArray.CreateBuilder<string>();
            foreach (var job in root.GetProperty("jobs").EnumerateArray())
            {
                if (ReadJob(job, folder) is { } model)
                {
                    jobs.Add(model);
                }
                else
                {
                    skipped.Add(job.TryGetProperty("id", out var id) ? id.GetString()! : job.GetProperty("type").GetString()!);
                }
            }

            return new() { Jobs = jobs.ToImmutable(), Run = ImportRunSettings.Read(root), SkippedJobs = skipped.ToImmutable() };
        }
    }

    /// <summary>Writes the session as an import manifest, validated against the installed schema.</summary>
    /// <param name="manifestPath">Where the manifest will live; sources are written relative to its folder when they can be.</param>
    /// <returns>The manifest's complete bytes.</returns>
    /// <exception cref="InvalidDataException">The session does not make a valid import manifest.</exception>
    public byte[] ToBytes(string manifestPath)
    {
        ArgumentException.ThrowIfNullOrEmpty(manifestPath);
        var folder = Path.GetDirectoryName(Path.GetFullPath(manifestPath))!;
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream, WriterOptions))
        {
            writer.WriteStartObject();
            writer.WriteNumber("version", ManifestVersion);
            this.Run.WriteTo(writer);
            writer.WriteStartArray("jobs");
            for (var index = 0; index < this.Jobs.Length; ++index)
            {
                WriteJob(writer, this.Jobs[index], index, folder);
            }

            writer.WriteEndArray();
            writer.WriteEndObject();
        }

        stream.WriteByte((byte)'\n');
        var bytes = stream.ToArray();
        using var document = JsonDocument.Parse(bytes);
        return ImportManifestSchema.IsValid(document.RootElement)
            ? bytes
            : throw new InvalidDataException("The import session does not make a valid import manifest.");
    }

    private static ModelImportSessionJob? ReadJob(JsonElement job, string folder)
    {
        var type = job.GetProperty("type").GetString();
        if (type is not ("gltf" or "fbx")
            || !job.TryGetProperty("source", out var source)
            || !job.TryGetProperty("virtual_path", out var virtualPath)
            || !AssetPath.TryParse(virtualPath.GetString(), out var identity))
        {
            return null;
        }

        var settings = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        foreach (var property in job.EnumerateObject().Where(static property => !JobKeys.Contains(property.Name)))
        {
            settings[property.Name] = property.Value.Clone();
        }

        return new(Path.GetFullPath(source.GetString()!, folder), identity, JsonSerializer.SerializeToElement(settings));
    }

    private static void WriteJob(Utf8JsonWriter writer, ModelImportSessionJob job, int index, string folder)
    {
        writer.WriteStartObject();
        writer.WriteString("id", string.Create(CultureInfo.InvariantCulture, $"model-{index + 1}"));
        writer.WriteString("type", string.Equals(Path.GetExtension(job.Source), ".fbx", StringComparison.OrdinalIgnoreCase) ? "fbx" : "gltf");
        writer.WriteString("source", SourceRelativeTo(folder, job.Source));
        writer.WriteString("virtual_path", job.VirtualPath.VirtualPath);
        if (job.Settings.ValueKind == JsonValueKind.Object)
        {
            foreach (var property in job.Settings.EnumerateObject().Where(static property => !JobKeys.Contains(property.Name)))
            {
                property.WriteTo(writer);
            }
        }

        writer.WriteEndObject();
    }

    // A source on the manifest's volume is written relative to its folder, so the session moves with the project.
    private static string SourceRelativeTo(string folder, string source)
    {
        var full = Path.GetFullPath(source);
        return string.Equals(Path.GetPathRoot(full), Path.GetPathRoot(folder), StringComparison.OrdinalIgnoreCase)
            ? Path.GetRelativePath(folder, full).Replace('\\', '/')
            : full.Replace('\\', '/');
    }
}
