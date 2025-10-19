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
/// Settings source that persists settings to JSON files with atomic write operations.
/// </summary>
public sealed partial class JsonSettingsSource : FileSettingsSource
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    private readonly ILogger<JsonSettingsSource> logger;
    private readonly SemaphoreSlim operationLock = new(1, 1);
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="JsonSettingsSource"/> class.
    /// </summary>
    /// <param name="filePath">The path to the JSON file for this settings source.</param>
    /// <param name="fileSystem">The file system abstraction to use for file operations.</param>
    /// <param name="loggerFactory">Optional logger factory used to create a logger for diagnostic output.</param>
    public JsonSettingsSource(
        string filePath,
        IFileSystem fileSystem,
        ILoggerFactory? loggerFactory = null)
        : base(
            id: filePath ?? throw new ArgumentNullException(nameof(filePath)),
            path: (fileSystem ?? throw new ArgumentNullException(nameof(fileSystem))).Path.GetFullPath(filePath),
            fileSystem: fileSystem,
            crypto: null,
            loggerFactory: loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<JsonSettingsSource>() ?? NullLogger<JsonSettingsSource>.Instance;
    }

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
        SettingsMetadata metadata,
        CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        ArgumentNullException.ThrowIfNull(sectionsData);
        ArgumentNullException.ThrowIfNull(metadata);

        await this.operationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            LogSaveRequested(this.logger, this.Id, sectionsData.Count);

            var mergedSections = await this.MergeSectionsAsync(sectionsData, cancellationToken).ConfigureAwait(false);
            var jsonContent = SerializeSections(mergedSections, metadata);

            await this.WriteAllBytesAsync(jsonContent, cancellationToken).ConfigureAwait(false);

            LogSaveSucceeded(this.logger, this.Id, mergedSections.Count);

            var payload = new SettingsWritePayload(metadata, sectionsData.Count, this.SourcePath);
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

    private static string SerializeSections(
        IReadOnlyDictionary<string, object> sectionsData,
        SettingsMetadata metadata)
    {
        var flatStructure = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["metadata"] = metadata,
        };

        foreach (var (key, value) in sectionsData)
        {
            flatStructure[key] = value;
        }

        return JsonSerializer.Serialize(flatStructure, SerializerOptions);
    }

    private static Dictionary<string, object> CreateValidationStructure(IReadOnlyDictionary<string, object> sectionsData)
    {
        var structure = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["metadata"] = new SettingsMetadata
            {
                Version = "validation",
                SchemaVersion = "validation",
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
            return new SettingsReadPayload(emptySections, metadata: null, this.SourcePath);
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

        if (!root.TryGetProperty("metadata", out var metadataElement))
        {
            throw new SettingsPersistenceException(
                $"Settings file '{this.SourcePath}' is missing required 'metadata' section.",
                this.Id,
                new JsonException("Missing required metadata section"));
        }

        var metadata = JsonSerializer.Deserialize<SettingsMetadata>(metadataElement.GetRawText())
                       ?? throw new SettingsPersistenceException(
                           $"Failed to deserialize metadata from '{this.SourcePath}'.",
                           this.Id,
                           new JsonException("Invalid metadata format"));

        var sections = new Dictionary<string, object>(StringComparer.Ordinal);
        foreach (var property in root.EnumerateObject())
        {
            if (property.Name.Equals("metadata", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var sectionValue = JsonSerializer.Deserialize<object>(property.Value.GetRawText());
            if (sectionValue is not null)
            {
                sections[property.Name] = sectionValue;
            }
        }

        LogReadSections(this.logger, this.Id, sections.Count);

        return new SettingsReadPayload(
            new ReadOnlyDictionary<string, object>(sections),
            metadata,
            this.SourcePath);
    }

    private async Task<Dictionary<string, object>> MergeSectionsAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken)
    {
        var merged = new Dictionary<string, object>(StringComparer.Ordinal);

        if (this.FileSystem.File.Exists(this.SourcePath))
        {
            try
            {
                var existing = await this.ReadFilePayloadAsync(cancellationToken).ConfigureAwait(false);
                foreach (var (sectionName, sectionValue) in existing.Sections)
                {
                    merged[sectionName] = sectionValue;
                }
            }
            catch (SettingsPersistenceException ex)
            {
                LogMergeFailed(this.logger, this.Id, ex.Message, ex);
                throw;
            }
        }

        foreach (var (sectionName, sectionValue) in sectionsData)
        {
            merged[sectionName] = sectionValue;
        }

        return merged;
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, this);
}
