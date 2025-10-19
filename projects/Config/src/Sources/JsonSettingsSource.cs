// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Testably.Abstractions;

namespace DroidNet.Config.Sources;

/// <summary>
/// Settings source that persists settings to JSON files with atomic write operations.
/// </summary>
/// <remarks>
/// Initializes a new instance of the <see cref="JsonSettingsSource"/> class.
/// </remarks>
/// <param name="filePath">The path to the JSON file for this settings source.</param>
/// <param name="fileSystem">The file system abstraction to use for file operations.</param>
/// <param name="loggerFactory">Optional logger factory used to create a logger for diagnostic output.</param>
public partial class JsonSettingsSource(
    string filePath,
    IFileSystem fileSystem,
    ILoggerFactory? loggerFactory = null) : ISettingsSource, IDisposable
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    private readonly ILogger<JsonSettingsSource> logger = loggerFactory?.CreateLogger<JsonSettingsSource>() ?? NullLogger<JsonSettingsSource>.Instance;
    private readonly IFileSystem fileSystem = fileSystem ?? throw new ArgumentNullException(nameof(fileSystem));
    private readonly string filePath = filePath ?? throw new ArgumentNullException(nameof(filePath));
    private readonly SemaphoreSlim fileLock = new(1, 1);
    private FileSystemWatcher? fileWatcher;
    private bool isDisposed;

    /// <inheritdoc/>
    public string Id => this.filePath;

    /// <inheritdoc/>
    public bool CanWrite => true;

    /// <inheritdoc/>
    public bool SupportsEncryption => false;

    /// <inheritdoc/>
    public bool IsAvailable
    {
        get
        {
            try
            {
                var directory = this.fileSystem.Path.GetDirectoryName(this.filePath);
                return directory is null || this.fileSystem.Directory.Exists(directory);
            }
            catch (IOException)
            {
                // File system error means source is not available
                return false;
            }
            catch (UnauthorizedAccessException)
            {
                // No access means source is not available
                return false;
            }

            // Other exceptions will be thrown to the caller
        }
    }

    /// <inheritdoc/>
    public async Task<SettingsSourceReadResult> ReadAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        await this.fileLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            return await this.ReadFileAndDeserializeAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (JsonException ex)
        {
            LogJsonDeserializationError(this.logger, ex, this.filePath);
            return SettingsSourceReadResult.CreateFailure(
                $"Failed to deserialize settings from '{this.filePath}': {ex.Message}",
                ex);
        }
        catch (IOException ex)
        {
            LogIoError(this.logger, ex, this.filePath);
            return SettingsSourceReadResult.CreateFailure(
                $"I/O error reading settings from '{this.filePath}': {ex.Message}",
                ex);
        }
        catch (UnauthorizedAccessException ex)
        {
            LogAccessDenied(this.logger, ex, this.filePath);
            return SettingsSourceReadResult.CreateFailure(
                $"Access denied reading settings from '{this.filePath}': {ex.Message}",
                ex);
        }
        finally
        {
            _ = this.fileLock.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<SettingsSourceWriteResult> WriteAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        SettingsMetadata metadata,
        CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        ArgumentNullException.ThrowIfNull(sectionsData);
        ArgumentNullException.ThrowIfNull(metadata);

        await this.fileLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            LogWritingFile(this.logger, this.filePath, sectionsData.Count);

            this.EnsureDirectoryExists();

            // Merge with existing sections to preserve them
            var mergedSections = await this.MergeWithExistingSectionsAsync(sectionsData, cancellationToken).ConfigureAwait(false);

            var jsonContent = SerializeSettings(mergedSections, metadata);
            await this.WriteAtomicallyAsync(jsonContent, cancellationToken).ConfigureAwait(false);

            LogWriteSuccess(this.logger, this.filePath);
            return SettingsSourceWriteResult.CreateSuccess($"Successfully wrote {sectionsData.Count} section(s) to '{this.filePath}'");
        }
        catch (JsonException ex)
        {
            LogJsonSerializationError(this.logger, ex, this.filePath);
            return SettingsSourceWriteResult.CreateFailure(
                $"Failed to serialize settings to '{this.filePath}': {ex.Message}",
                ex);
        }
        catch (IOException ex)
        {
            LogIoError(this.logger, ex, this.filePath);
            return SettingsSourceWriteResult.CreateFailure(
                $"I/O error writing settings to '{this.filePath}': {ex.Message}",
                ex);
        }
        catch (UnauthorizedAccessException ex)
        {
            LogAccessDenied(this.logger, ex, this.filePath);
            return SettingsSourceWriteResult.CreateFailure(
                $"Access denied writing settings to '{this.filePath}': {ex.Message}",
                ex);
        }
        finally
        {
            _ = this.fileLock.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<SettingsSourceResult> ValidateAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        ArgumentNullException.ThrowIfNull(sectionsData);

        await Task.Yield(); // Make method truly async

        try
        {
            LogValidatingContent(this.logger, sectionsData.Count);

            // Create flat structure for validation (same as serialization)
            var flatStructure = new Dictionary<string, object>(StringComparer.Ordinal)
            {
                ["metadata"] = new SettingsMetadata { Version = "1.0", SchemaVersion = "validation" },
            };

            // Add all sections at the root level
            foreach (var kvp in sectionsData)
            {
                flatStructure[kvp.Key] = kvp.Value;
            }

            // Attempt to serialize to validate JSON compatibility
            _ = JsonSerializer.Serialize(flatStructure, SerializerOptions);

            LogValidationSuccess(this.logger, sectionsData.Count);
            return SettingsSourceResult.CreateSuccess($"Successfully validated {sectionsData.Count} section(s)");
        }
        catch (JsonException ex)
        {
            LogValidationFailed(this.logger, ex);
            return SettingsSourceResult.CreateFailure(
                $"Validation failed: Content is not serializable to JSON: {ex.Message}",
                ex);
        }
        catch (NotSupportedException ex)
        {
            // Serialization of certain types may not be supported
            LogValidationFailed(this.logger, ex);
            return SettingsSourceResult.CreateFailure(
                $"Validation failed: Content contains unsupported types: {ex.Message}",
                ex);
        }
    }

    /// <inheritdoc/>
    public async Task<SettingsSourceResult> ReloadAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        try
        {
            LogReloading(this.logger, this.filePath);

            var readResult = await this.ReadAsync(cancellationToken).ConfigureAwait(false);
            if (!readResult.Success)
            {
                return SettingsSourceResult.CreateFailure(
                    $"Reload failed: {readResult.ErrorMessage}",
                    readResult.Exception);
            }

            LogReloadSuccess(this.logger, this.filePath);
            return SettingsSourceResult.CreateSuccess($"Successfully reloaded from '{this.filePath}'");
        }
        catch (OperationCanceledException)
        {
            // Cancellation is not an error - let it bubble up
            throw;
        }
    }

    /// <inheritdoc/>
    public IDisposable? WatchForChanges(Action<string> changeHandler)
    {
        this.ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(changeHandler);

        try
        {
            return this.CreateFileWatcher(changeHandler);
        }
        catch (ArgumentException ex)
        {
            // Invalid path or filter pattern - this is a programming error, log and rethrow
            LogWatchingFailed(this.logger, ex, this.filePath);
            throw;
        }
        catch (IOException ex)
        {
            // Network path unavailable, too many watchers, etc. - watching is optional
            LogWatchingFailed(this.logger, ex, this.filePath);
            return null;
        }
        catch (PlatformNotSupportedException ex)
        {
            // FileSystemWatcher not supported on this platform - watching is optional
            LogWatchingFailed(this.logger, ex, this.filePath);
            return null;
        }
    }

    /// <summary>
    /// Releases resources used by the JsonSettingsSource.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the JsonSettingsSource and optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">True to release both managed and unmanaged resources; false to release only unmanaged resources.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.fileWatcher?.Dispose();
            this.fileLock.Dispose();
        }

        this.isDisposed = true;
    }

    private async Task<Dictionary<string, object>> MergeWithExistingSectionsAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken)
    {
        var mergedSections = new Dictionary<string, object>(StringComparer.Ordinal);

        // Read existing sections to preserve them
        if (this.fileSystem.File.Exists(this.filePath))
        {
            var readResult = await this.ReadFileAndDeserializeAsync(cancellationToken).ConfigureAwait(false);
            if (readResult.Success && readResult.SectionsData != null)
            {
                foreach (var kvp in readResult.SectionsData)
                {
                    mergedSections[kvp.Key] = kvp.Value;
                }
            }
        }

        // Merge new sections (overwriting existing ones with same key)
        foreach (var kvp in sectionsData)
        {
            mergedSections[kvp.Key] = kvp.Value;
        }

        return mergedSections;
    }

    private static string SerializeSettings(IReadOnlyDictionary<string, object> sectionsData, SettingsMetadata metadata)
    {
        // Create a flat dictionary with metadata and all sections at the same level
        var flatStructure = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["metadata"] = metadata,
        };

        // Add all sections at the root level
        foreach (var kvp in sectionsData)
        {
            flatStructure[kvp.Key] = kvp.Value;
        }

        return JsonSerializer.Serialize(flatStructure, SerializerOptions);
    }

    private async Task<SettingsSourceReadResult> ReadFileAndDeserializeAsync(CancellationToken cancellationToken)
    {
        LogReadingFile(this.logger, this.filePath);

        if (!this.fileSystem.File.Exists(this.filePath))
        {
            LogFileNotFound(this.logger, this.filePath);
            return SettingsSourceReadResult.CreateSuccess(
                new Dictionary<string, object>(StringComparer.Ordinal),
                metadata: null);
        }

        var fileContent = await this.fileSystem.File.ReadAllTextAsync(this.filePath, cancellationToken).ConfigureAwait(false);

        if (string.IsNullOrWhiteSpace(fileContent))
        {
            LogEmptyFile(this.logger, this.filePath);
            return SettingsSourceReadResult.CreateFailure(
                $"Settings file '{this.filePath}' is empty",
                new JsonException("Empty settings file"));
        }

        // Deserialize to JsonDocument first to handle flat structure WITHOUT case conversion
        using var document = JsonDocument.Parse(fileContent);
        var root = document.RootElement;

        // Extract metadata (required) - metadata property name is lowercase by convention
        if (!root.TryGetProperty("metadata", out var metadataElement))
        {
            LogMissingMetadata(this.logger, this.filePath);
            return SettingsSourceReadResult.CreateFailure(
                $"Settings file '{this.filePath}' is missing required 'metadata' section",
                new JsonException("Missing required metadata section"));
        }

        // Deserialize metadata WITHOUT case conversion to preserve property names
        var metadata = JsonSerializer.Deserialize<SettingsMetadata>(metadataElement.GetRawText());

        if (metadata is null)
        {
            LogInvalidJsonFormat(this.logger, this.filePath);
            return SettingsSourceReadResult.CreateFailure(
                $"Failed to deserialize metadata from '{this.filePath}'",
                new JsonException("Invalid metadata format"));
        }

        // Extract all sections (everything except "metadata")
        // PRESERVE section name casing - do NOT convert to camelCase!
        var sections = new Dictionary<string, object>(StringComparer.Ordinal);
        foreach (var property in root.EnumerateObject())
        {
            if (property.Name.Equals("metadata", StringComparison.OrdinalIgnoreCase))
            {
                continue; // Skip metadata
            }

            // Deserialize each section as a generic object (will be JsonElement)
            var sectionValue = JsonSerializer.Deserialize<object>(property.Value.GetRawText());
            if (sectionValue is not null)
            {
                sections[property.Name] = sectionValue;
            }
        }

        LogReadSuccess(this.logger, this.filePath, sections.Count);

        Console.WriteLine($"[DEBUG JsonSource] Read {sections.Count} sections from {this.filePath}:");
        foreach (var kvp in sections)
        {
            Console.WriteLine($"[DEBUG JsonSource]   Section: '{kvp.Key}', ValueType: {kvp.Value?.GetType().Name ?? "null"}");
        }

        return SettingsSourceReadResult.CreateSuccess(sections, metadata);
    }

    private DisposableAction? CreateFileWatcher(Action<string> changeHandler)
    {
        var directory = this.fileSystem.Path.GetDirectoryName(this.filePath);
        var fileName = this.fileSystem.Path.GetFileName(this.filePath);

        if (directory is null || !this.fileSystem.Directory.Exists(directory))
        {
            LogCannotWatchInvalidPath(this.logger, this.filePath);
            return null;
        }

        this.fileWatcher = new FileSystemWatcher(directory, fileName)
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size,
            EnableRaisingEvents = true,
        };

        void OnChanged(object sender, FileSystemEventArgs e)
        {
            LogFileChanged(this.logger, e.FullPath, e.ChangeType.ToString());
            changeHandler(this.Id);
        }

        this.fileWatcher.Changed += OnChanged;
        this.fileWatcher.Created += OnChanged;
        this.fileWatcher.Deleted += OnChanged;
        this.fileWatcher.Renamed += (sender, e) =>
        {
            LogFileRenamed(this.logger, e.OldFullPath, e.FullPath);
            changeHandler(this.Id);
        };

        LogWatchingStarted(this.logger, this.filePath);

        return new DisposableAction(() =>
        {
            if (this.fileWatcher is not null)
            {
                this.fileWatcher.EnableRaisingEvents = false;
                this.fileWatcher.Dispose();
                this.fileWatcher = null;
                LogWatchingStopped(this.logger, this.filePath);
            }
        });
    }

    private void EnsureDirectoryExists()
    {
        var directory = this.fileSystem.Path.GetDirectoryName(this.filePath);
        if (directory is not null && !this.fileSystem.Directory.Exists(directory))
        {
            _ = this.fileSystem.Directory.CreateDirectory(directory);
            LogDirectoryCreated(this.logger, directory);
        }
    }

    private async Task WriteAtomicallyAsync(string jsonContent, CancellationToken cancellationToken)
    {
        var tempFilePath = $"{this.filePath}.tmp";
        var backupFilePath = $"{this.filePath}.bak";

        try
        {
            await this.fileSystem.File.WriteAllTextAsync(tempFilePath, jsonContent, cancellationToken).ConfigureAwait(false);

            this.CreateBackupIfExists(backupFilePath);
            this.fileSystem.File.Move(tempFilePath, this.filePath);
            this.DeleteBackupIfExists(backupFilePath);
        }
        catch (IOException ex)
        {
            // IO failure during atomic write - attempt to restore backup
            this.RestoreBackupOrThrow(backupFilePath, ex);
            throw; // Rethrow original exception after successful restore
        }
        catch (UnauthorizedAccessException ex)
        {
            // Permission denied during atomic write - attempt to restore backup
            this.RestoreBackupOrThrow(backupFilePath, ex);
            throw; // Rethrow original exception after successful restore
        }
        catch (OperationCanceledException ex)
        {
            // Cancellation during atomic write - attempt to restore backup
            this.RestoreBackupOrThrow(backupFilePath, ex);
            throw; // Rethrow original exception after successful restore
        }
        finally
        {
            this.CleanupTempFileIfExists(tempFilePath);
        }
    }

    private void CreateBackupIfExists(string backupFilePath)
    {
        if (this.fileSystem.File.Exists(this.filePath))
        {
            if (this.fileSystem.File.Exists(backupFilePath))
            {
                this.fileSystem.File.Delete(backupFilePath);
            }

            this.fileSystem.File.Move(this.filePath, backupFilePath);
            LogBackupCreated(this.logger, backupFilePath);
        }
    }

    private void DeleteBackupIfExists(string backupFilePath)
    {
        if (this.fileSystem.File.Exists(backupFilePath))
        {
            this.fileSystem.File.Delete(backupFilePath);
        }
    }

    private void RestoreBackupOrThrow(string backupFilePath, Exception originalException)
    {
        if (!this.fileSystem.File.Exists(backupFilePath))
        {
            // No backup exists - nothing to restore, original exception will be thrown
            return;
        }

        try
        {
            if (this.fileSystem.File.Exists(this.filePath))
            {
                this.fileSystem.File.Delete(this.filePath);
            }

            this.fileSystem.File.Move(backupFilePath, this.filePath);
            LogBackupRestored(this.logger, this.filePath);
        }
        catch (IOException restoreEx)
        {
            // Restore failed - this is CRITICAL as data may be corrupted
            LogBackupRestoreFailed(this.logger, restoreEx, backupFilePath);
            throw new SettingsPersistenceException(
                $"Write operation failed AND backup restoration failed for '{this.filePath}'. Data may be in an inconsistent state.",
                new AggregateException(originalException, restoreEx));
        }
        catch (UnauthorizedAccessException restoreEx)
        {
            // Restore failed - this is CRITICAL as data may be corrupted
            LogBackupRestoreFailed(this.logger, restoreEx, backupFilePath);
            throw new SettingsPersistenceException(
                $"Write operation failed AND backup restoration failed for '{this.filePath}'. Data may be in an inconsistent state.",
                new AggregateException(originalException, restoreEx));
        }
    }

    private void CleanupTempFileIfExists(string tempFilePath)
    {
        if (!this.fileSystem.File.Exists(tempFilePath))
        {
            return;
        }

        try
        {
            this.fileSystem.File.Delete(tempFilePath);
            LogTempFileCleanedUp(this.logger, tempFilePath);
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            // Best-effort cleanup in finally block - orphaned temp file is acceptable
            // We must not throw here as it would mask the real exception from the operation
            // Temp files are transient and can be manually cleaned up if needed
            LogTempFileCleanupFailed(this.logger, ex, tempFilePath);
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, this);

    /// <summary>
    /// Helper class to create a disposable from an action.
    /// </summary>
    private sealed class DisposableAction(Action action) : IDisposable
    {
        private readonly Action action = action ?? throw new ArgumentNullException(nameof(action));
        private bool isDisposed;

        public void Dispose()
        {
            if (!this.isDisposed)
            {
                this.action();
                this.isDisposed = true;
            }
        }
    }
}
