// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.IO.Abstractions;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Config.Sources;

/// <summary>
///     Settings source that persists settings to JSON files with atomic write operations.
/// </summary>
/// <param name="id">
///     A unique identifier for this settings source. Recommended format: `Domain:FileName` where Domain may be used
///     to distinguish between global, user, and built-in application settings.
/// </param>
/// <param name="filePath">The file system path to the configuration file.</param>
/// <param name="fileSystem">
///     The <see cref="IFileSystem"/> abstraction used for file operations; this parameter cannot be <see
///     langword="null"/> and can be mocked or stubbed in tests.
/// </param>
/// <param name="watch">
///     A value indicating whether the source should watch the file for changes and raise
///     <see cref="ISettingsSource.SourceChanged"/> events accordingly.
/// </param>
/// <param name="crypto">
///     An optional <see cref="IEncryptionProvider"/> used to encrypt and decrypt file contents. If <see
///     langword="null"/>, <see cref="NoEncryptionProvider.Instance"/> is used, meaning no encryption.
/// </param>
/// <param name="loggerFactory">
///     An optional <see cref="ILoggerFactory"/> used to create an <see cref="ILogger{FileSettingsSource}"/>; if
///     <see langword="null"/>, a <see cref="NullLogger{FileSettingsSource}"/> instance is used.
/// </param>
/// <exception cref="ArgumentNullException">
///     Thrown when <paramref name="filePath"/> or <paramref name="fileSystem"/> is <see langword="null"/>.
/// </exception>
public sealed partial class JsonSettingsSource(string id, string filePath, IFileSystem fileSystem, bool watch, IEncryptionProvider? crypto = null, ILoggerFactory? loggerFactory = null)
    : FileSettingsSource(id, filePath, fileSystem, watch, crypto, loggerFactory)
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    private readonly ILogger<JsonSettingsSource> logger = loggerFactory?.CreateLogger<JsonSettingsSource>() ?? NullLogger<JsonSettingsSource>.Instance;
    private readonly SemaphoreSlim operationLock = new(1, 1);
    private bool isDisposed;

    /// <inheritdoc/>
    public override async Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        await this.operationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            LogLoadRequested(this.logger, this.Id, this.SourcePath, reload);

            var payload = await this.ReadFilePayloadAsync(cancellationToken).ConfigureAwait(false);

            LogLoadSucceeded(this.logger, this.Id, payload.SectionCount);
            return Result.Ok(payload);
        }
        catch (JsonException ex)
        {
            return this.FailRead("Failed to deserialize JSON content.", ex);
        }
        catch (SettingsPersistenceException ex)
        {
            return Result.Fail<SettingsReadPayload>(ex);
        }
        finally
        {
            _ = this.operationLock.Release();
        }
    }

    /// <inheritdoc/>
    public override async Task<Result<SettingsWritePayload>> SaveAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
        SettingsSourceMetadata sourceMetadata,
        CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        ArgumentNullException.ThrowIfNull(sectionsData);
        ArgumentNullException.ThrowIfNull(sectionMetadata);
        ArgumentNullException.ThrowIfNull(sourceMetadata);

        await this.operationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            LogSaveRequested(this.logger, this.Id, sectionsData.Count);

            var (sections, metadata) = await this.MergeWithExistingSectionsAsync(sectionsData, sectionMetadata, cancellationToken).ConfigureAwait(false);
            var jsonContent = SerializeSections(sections, metadata, sourceMetadata);

            await this.WriteAllBytesAsync(jsonContent, cancellationToken).ConfigureAwait(false);

            LogSaveSucceeded(this.logger, this.Id, sections.Count);

            var payload = new SettingsWritePayload(sourceMetadata, sections.Count, this.SourcePath);
            return Result.Ok(payload);
        }
        catch (JsonException ex)
        {
            return this.FailWrite("Failed to serialize settings to JSON.", ex);
        }
        catch (SettingsPersistenceException ex)
        {
            return Result.Fail<SettingsWritePayload>(ex);
        }
        finally
        {
            _ = this.operationLock.Release();
        }
    }

    /// <inheritdoc/>
    public override async Task<Result<SettingsValidationPayload>> ValidateAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        ArgumentNullException.ThrowIfNull(sectionsData);

        await Task.Yield();

        try
        {
            LogValidationRequested(this.logger, this.Id, sectionsData.Count);

            var structure = CreateValidationStructure(sectionsData);
            _ = JsonSerializer.Serialize(structure, SerializerOptions);

            LogValidationSucceeded(this.logger, this.Id, sectionsData.Count);

            var payload = new SettingsValidationPayload(sectionsData.Count, $"Validated {sectionsData.Count} section(s).");
            return Result.Ok(payload);
        }
        catch (JsonException ex)
        {
            return this.FailValidation("Content could not be serialized to JSON.", ex);
        }
        catch (NotSupportedException ex)
        {
            return this.FailValidation("Content includes unsupported types for serialization.", ex);
        }
    }

    /// <inheritdoc/>
    protected override void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            base.Dispose(disposing);
            return;
        }

        if (disposing)
        {
            this.operationLock.Dispose();
        }

        this.isDisposed = true;
        base.Dispose(disposing);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1859:Use concrete types when possible for improved performance", Justification = "enforce immutability")]
    private static string SerializeSections(
        IReadOnlyDictionary<string, object> sectionsData,
        IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
        SettingsSourceMetadata sourceMetadata)
    {
        var rootStructure = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["$meta"] = sourceMetadata,
        };

        foreach (var (sectionName, sectionData) in sectionsData)
        {
            var sectionWithMeta = new Dictionary<string, object>(StringComparer.Ordinal);

            // Add section metadata if available
            if (sectionMetadata.TryGetValue(sectionName, out var meta))
            {
                sectionWithMeta["$meta"] = meta;
            }

            // Add section data - need to merge if it's already a dictionary
            if (sectionData is JsonElement jsonElement)
            {
                var deserialized = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonElement.GetRawText());
                if (deserialized != null)
                {
                    foreach (var (key, value) in deserialized)
                    {
                        sectionWithMeta[key] = value;
                    }
                }
            }
            else if (sectionData is Dictionary<string, object> dict)
            {
                foreach (var (key, value) in dict)
                {
                    sectionWithMeta[key] = value;
                }
            }
            else
            {
                // For POCOs, serialize then deserialize to get properties
                var serialized = JsonSerializer.Serialize(sectionData, SerializerOptions);
                var deserialized = JsonSerializer.Deserialize<Dictionary<string, object>>(serialized);
                if (deserialized != null)
                {
                    foreach (var (key, value) in deserialized)
                    {
                        sectionWithMeta[key] = value;
                    }
                }
            }

            rootStructure[sectionName] = sectionWithMeta;
        }

        return JsonSerializer.Serialize(rootStructure, SerializerOptions);
    }

    private static Dictionary<string, object> CreateValidationStructure(IReadOnlyDictionary<string, object> sectionsData)
    {
        var structure = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["$meta"] = new SettingsSourceMetadata
            {
                WrittenAt = DateTimeOffset.UtcNow,
                WrittenBy = "validation",
            },
        };

        foreach (var (key, value) in sectionsData)
        {
            structure[key] = value;
        }

        return structure;
    }

    private Result<SettingsReadPayload> FailRead(string message, Exception exception)
    {
        var wrapped = exception as SettingsPersistenceException
                      ?? new SettingsPersistenceException($"{message} (source: '{this.Id}')", this.Id, exception);

        LogLoadFailed(this.logger, this.Id, wrapped.Message, exception);
        return Result.Fail<SettingsReadPayload>(wrapped);
    }

    private Result<SettingsWritePayload> FailWrite(string message, Exception exception)
    {
        var wrapped = exception as SettingsPersistenceException
                      ?? new SettingsPersistenceException($"{message} (source: '{this.Id}')", this.Id, exception);

        LogSaveFailed(this.logger, this.Id, wrapped.Message, exception);
        return Result.Fail<SettingsWritePayload>(wrapped);
    }

    private Result<SettingsValidationPayload> FailValidation(string message, Exception exception)
    {
        var wrapped = new SettingsPersistenceException($"{message} (source: '{this.Id}')", this.Id, exception);
        LogValidationFailed(this.logger, this.Id, wrapped.Message, exception);
        return Result.Fail<SettingsValidationPayload>(wrapped);
    }

    private async Task<SettingsReadPayload> ReadFilePayloadAsync(CancellationToken cancellationToken)
    {
        if (!this.FileSystem.File.Exists(this.SourcePath))
        {
            LogFileMissing(this.logger, this.Id, this.SourcePath);
            var emptySections = new ReadOnlyDictionary<string, object>(
                new Dictionary<string, object>(StringComparer.Ordinal));
            var emptySectionMetadata = new ReadOnlyDictionary<string, SettingsSectionMetadata>(
                new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal));
            return new SettingsReadPayload(emptySections, emptySectionMetadata, sourceMetadata: null, this.SourcePath);
        }

        var content = await this.ReadAllBytesAsync(cancellationToken).ConfigureAwait(false);

        if (string.IsNullOrWhiteSpace(content))
        {
            throw new SettingsPersistenceException(
                $"Settings file '{this.SourcePath}' is empty.",
                this.Id,
                new JsonException("Empty settings file"));
        }

        using var document = JsonDocument.Parse(content);
        var root = document.RootElement;

        // Read source-level metadata from root $meta
        SettingsSourceMetadata? sourceMetadata = null;
        if (root.TryGetProperty("$meta", out var sourceMetadataElement))
        {
            sourceMetadata = JsonSerializer.Deserialize<SettingsSourceMetadata>(sourceMetadataElement.GetRawText());
        }

        // Read sections and their metadata
        var sections = new Dictionary<string, object>(StringComparer.Ordinal);
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal);

        foreach (var property in root.EnumerateObject())
        {
            // Skip the root $meta
            if (property.Name.Equals("$meta", StringComparison.Ordinal))
            {
                continue;
            }

            var (sectionObj, sectionMeta) = ExtractSection(property.Name, property.Value);
            if (sectionMeta is not null)
            {
                sectionMetadata[property.Name] = sectionMeta;
            }

            sections[property.Name] = sectionObj;
        }

        LogReadSections(this.logger, this.Id, sections.Count);

        return new SettingsReadPayload(
            new ReadOnlyDictionary<string, object>(sections),
            new ReadOnlyDictionary<string, SettingsSectionMetadata>(sectionMetadata),
            sourceMetadata,
            this.SourcePath);

        /*
         * A section is expected to be a JSON object, with an optional $meta property for metadata.
         *
         * Example:
         * "database": {
         *    "$meta": {
         *      "schemaVersion": "1.0",
         *      "service": "UserService"
         *    },
         *    "connectionString": "Server=...",
         *    "poolSize": 5
         * }
         */
        (JsonElement sectionObject, SettingsSectionMetadata? meta) ExtractSection(string sectionName, JsonElement element)
        {
            if (element.ValueKind != JsonValueKind.Object)
            {
                throw new SettingsPersistenceException(
                    $"Section '{sectionName}' in '{this.SourcePath}' must be a JSON object.",
                    this.Id,
                    new JsonException($"Section '{sectionName}' must be a JSON object."));
            }

            SettingsSectionMetadata? meta = null;
            if (element.TryGetProperty("$meta", out var sectionMetaElement))
            {
                meta = JsonSerializer.Deserialize<SettingsSectionMetadata>(sectionMetaElement.GetRawText());
            }

            using var stream = new MemoryStream();
            using (var writer = new Utf8JsonWriter(stream))
            {
                writer.WriteStartObject();
                foreach (var sectionProp in element.EnumerateObject())
                {
                    if (sectionProp.Name.Equals("$meta", StringComparison.Ordinal))
                    {
                        continue;
                    }

                    sectionProp.WriteTo(writer);
                }

                writer.WriteEndObject();
                writer.Flush();
            }

            var bytes = stream.ToArray();
            using var doc = JsonDocument.Parse(bytes);
            return (doc.RootElement.Clone(), meta);
        }
    }

    private async Task<(Dictionary<string, object> sections, Dictionary<string, SettingsSectionMetadata> metadata)> MergeWithExistingSectionsAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
        CancellationToken cancellationToken)
    {
        Dictionary<string, object> mergedSections;
        Dictionary<string, SettingsSectionMetadata> mergedMetadata;

        if (this.FileSystem.File.Exists(this.SourcePath))
        {
            try
            {
                var existing = await this.ReadFilePayloadAsync(cancellationToken).ConfigureAwait(false);
                mergedSections = new Dictionary<string, object>(existing.Sections, StringComparer.Ordinal);
                mergedMetadata = new Dictionary<string, SettingsSectionMetadata>(existing.SectionMetadata, StringComparer.Ordinal);
            }
            catch (SettingsPersistenceException ex)
            {
                LogMergeFailed(this.logger, this.Id, ex.Message, ex);
                throw;
            }
        }
        else
        {
            mergedSections = new Dictionary<string, object>(StringComparer.Ordinal);
            mergedMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal);
        }

        // Merge new sections and metadata
        foreach (var (sectionName, sectionValue) in sectionsData)
        {
            mergedSections[sectionName] = sectionValue;
        }

        foreach (var (sectionName, meta) in sectionMetadata)
        {
            mergedMetadata[sectionName] = meta;
        }

        return (mergedSections, mergedMetadata);
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, this);
}
