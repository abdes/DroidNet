// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Config.Sources;

/// <summary>
///     Provides access to configuration data stored in a file, with optional encryption and atomic write semantics.
/// </summary>
/// <remarks>
///     This class encapsulates the logic for reading and writing configuration files as UTF-8 encoded text. It
///     delegates encryption and decryption to an <see cref="IEncryptionProvider"/> implementation, allowing the same
///     file source to transparently handle both plain-text and encrypted files.
///     <para>
///     By default, if no encryption provider is supplied, the class uses <see cref="NoEncryptionProvider.Instance"/>,
///     which performs no transformation. This follows the Null Object pattern, ensuring that consumers never need to
///     handle <see langword="null"/> encryption providers explicitly.
///     </para>
///     <para>
///     The decrypted content is always interpreted as UTF-8 text, making this class suitable for human-readable
///     configuration formats such as JSON, YAML, INI, or TOML.
///     </para>
///     <para>
///     When writing updates the implementation attempts to perform an atomic update: the new content is written to a
///     temporary file and then moved or replaced into place. On platforms that support it the code will attempt to
///     use <c>File.Replace</c> for an atomic replacement; if that fails it falls back to a move-with-backup approach
///     and will attempt to restore the original file if the final move fails. This reduces the risk of leaving a
///     partially-written configuration file in case of failures.
///     </para>
/// </remarks>
public abstract partial class FileSettingsSource : SettingsSource, IDisposable
{
    private readonly IFileSystem fs;
    private FileSystemWatcher? fileWatcher;
    private string path;
    private bool isDisposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="FileSettingsSource"/> class.
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
    protected FileSettingsSource(string id, string filePath, IFileSystem fileSystem, bool watch, IEncryptionProvider? crypto = null, ILoggerFactory? loggerFactory = null)
        : base(id, crypto, loggerFactory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        ArgumentNullException.ThrowIfNull(filePath);
        ArgumentNullException.ThrowIfNull(fileSystem);

        this.fs = fileSystem;

        // Validate and normalize the provided path early so the rest of the instance
        // implementation can assume a valid file path and use Debug.Assert for contracts.
        if (!this.PathIsValid(filePath))
        {
            throw new ArgumentException("Path must be an absolute file path and contain a file name.", nameof(filePath));
        }

        this.path = filePath;

        // Start watching if needed
        if (watch)
        {
            this.StartWatching();
        }
    }

    /// <inheritdoc/>
    /// <remarks>
    ///     For a file-based source, checks if the configuration file exists and is readable.
    /// </remarks>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "returning false")]
    public override bool IsAvailable
    {
        get
        {
            try
            {
                var fileInfo = this.fs.FileInfo.New(this.path);
                using var stream = this.fs.File.Open(this.path, FileMode.Open, FileAccess.Read, FileShare.Read);
                return stream.CanRead;
            }
            catch (Exception ex)
            {
                this.LogFileIoError("IsAvailable", ex);
                return false;
            }
        }
    }

    /// <inheritdoc/>
    public override bool WatchForChanges
    {
        // We are watching if the FileSystemWatcher is active
        get => this.fileWatcher is not null;
        set
        {
            if (value)
            {
                this.StartWatching();
            }
            else
            {
                this.StopWatching();
            }
        }
    }

    /// <summary>
    ///     Gets the <see cref="IFileSystem"/> instance used for all filesystem interactions.
    /// </summary>
    protected IFileSystem FileSystem => this.fs;

    /// <summary>
    ///     Gets the absolute path of the underlying source file.
    /// </summary>
    protected string SourcePath => this.path;

    /// <summary>
    ///     Releases resources used by the FileSettingsSource.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    ///     Releases the unmanaged resources used by the FileSettingsSource and optionally releases the managed resources.
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
            try
            {
                this.fileWatcher?.Dispose();
            }
            finally
            {
                this.fileWatcher = null;
            }

            this.isDisposed = true;
        }
    }

    /// <summary>
    ///     Asynchronously reads the configuration file, decrypts it if necessary, and returns the content as UTF-8 text.
    /// </summary>
    /// <param name="cancellationToken">A token that can be used to cancel the asynchronous write operation.</param>
    /// <returns>
    ///     A task that represents the asynchronous operation. The task result contains the decrypted file content as a
    ///     <see cref="string"/>.
    /// </returns>
    /// <exception cref="SettingsPersistenceException">
    ///     Thrown when the file cannot be read (e.g., it does not exist, is locked, or the caller lacks sufficient
    ///     permissions).
    /// </exception>
    /// <exception cref="CryptographicException">
    ///     Thrown when decryption fails (e.g., corrupted input, authentication tag mismatch, or an invalid key/nonce).
    /// </exception>
    /// <exception cref="OperationCanceledException">
    ///     Thrown if the operation is canceled via the provided <paramref name="cancellationToken"/>.
    /// </exception>
    protected async Task<string> ReadAllBytesAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            var raw = await this.fs.File.ReadAllBytesAsync(this.path, cancellationToken).ConfigureAwait(false);
            var decrypted = this.Decrypt(raw);

            // Always interpret as UTF-8 text
            return Encoding.UTF8.GetString(decrypted);
        }
        catch (CryptographicException ex)
        {
            this.LogCryptoError("decrypting", ex);
            throw;
        }
        catch (OperationCanceledException)
        {
            // Preserve the cancellation semantics by rethrowing
            this.LogOperationCancelled("Read");
            throw;
        }
        catch (Exception ex)
        {
            this.LogReadError(ex);
            throw new SettingsPersistenceException($"Error reading configuration data from source '{this.Id}' at '{this.path}'.", ex);
        }
    }

    /// <summary>
    ///     Asynchronously encrypts the supplied text (if an encryption provider is configured) and writes it to the
    ///     configuration file as UTF-8 encoded bytes.
    /// </summary>
    /// <param name="content">The plaintext content to write to the configuration file. Must not be <see langword="null"/>.</param>
    /// <param name="cancellationToken">A token that can be used to cancel the asynchronous read operation.</param>
    /// <returns>A task that represents the asynchronous write operation.</returns>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="content"/> is <see langword="null"/>.
    /// </exception>
    /// <exception cref="IOException">
    ///     Thrown when the file cannot be written (e.g., it is locked, the caller lacks sufficient permissions, or the
    ///     disk is full).
    /// </exception>
    /// <exception cref="CryptographicException">
    ///     Thrown when encryption fails (e.g., invalid key/nonce or internal crypto error).
    /// </exception>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "we handle all failures for an atomic write or nothing")]
    protected async Task WriteAllBytesAsync(string content, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(content);

        Debug.Assert(this.fs is not null, "File system must be provided");
        Debug.Assert(this.PathIsValid(this.path), "`this.path` must be an absolute path with a file name.");

        try
        {
            var plain = Encoding.UTF8.GetBytes(content);
            var encrypted = this.Encrypt(plain);

            var directory = this.fs.Path.GetDirectoryName(this.path);
            Debug.Assert(!string.IsNullOrWhiteSpace(directory), "Validated path must include a directory component.");

            // Ensure directories are created if they do not exist
            _ = this.fs.Directory.CreateDirectory(directory);

            var fileName = this.fs.Path.GetFileName(this.path);
            Debug.Assert(!string.IsNullOrWhiteSpace(fileName), "Validated path must include a file name.");

            var tempFileName = $".{fileName}.tmp.{Guid.NewGuid():N}";
            var tempPath = this.fs.Path.Combine(directory!, tempFileName);

            try
            {
                await this.fs.File.WriteAllBytesAsync(tempPath, encrypted, cancellationToken).ConfigureAwait(false);
                this.ReplaceOrMoveIntoPlace(tempPath, this.path, directory, fileName);
            }
            finally
            {
                this.TryDeleteQuietly(tempPath);
            }
        }
        catch (CryptographicException ex)
        {
            this.LogCryptoError("encrypting", ex);
            throw;
        }
        catch (OperationCanceledException)
        {
            // Preserve the cancellation semantics by rethrowing
            this.LogOperationCancelled("Write");
            throw;
        }
        catch (SettingsPersistenceException ex) when (ex.InnerException is not null)
        {
            // Unwrap to avoid double nested exceptions
            throw new SettingsPersistenceException($"Error writing configuration data to source '{this.Id}' at '{this.path}'.", ex.InnerException);
        }
        catch (Exception ex)
        {
            this.LogWriteError(ex);
            throw new SettingsPersistenceException($"Error writing configuration data to source '{this.Id}' at '{this.path}'.", ex);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Best-effort atomic replacement requires broad exception handling")]
    private void ReplaceOrMoveIntoPlace(string sourcePath, string destinationPath, string destinationDirectory, string destinationFileName)
    {
        if (!this.fs.File.Exists(destinationPath))
        {
            this.MoveFile(sourcePath, destinationPath);
            return;
        }

        if (this.TryReplaceFile(sourcePath, destinationPath))
        {
            return;
        }

        var backupPath = this.fs.Path.Combine(destinationDirectory, $".{destinationFileName}.bak.{Guid.NewGuid():N}");
        this.MoveFile(destinationPath, backupPath);

        try
        {
            this.MoveFile(sourcePath, destinationPath);
        }
        catch (Exception ex)
        {
            this.LogFileIoError($"Move ({sourcePath} -> {destinationPath})", ex);

            this.TryRestoreOriginal(destinationPath, backupPath);

            throw;
        }

        this.TryDeleteQuietly(backupPath);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Wrapping filesystem exceptions for context")]
    private void MoveFile(string sourcePath, string destinationPath)
    {
        try
        {
            this.fs.File.Move(sourcePath, destinationPath);
            this.LogFileMoved(sourcePath, destinationPath);
        }
        catch (Exception ex)
        {
            this.LogFileIoError($"Move ({sourcePath} -> {destinationPath})", ex);
            throw new SettingsPersistenceException($"Failed to move file '{sourcePath}' to '{destinationPath}'.", ex);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Best-effort cleanup should never throw")]
    private void TryDeleteQuietly(string filePath)
    {
        try
        {
            // Avoid logging if the file does not exist
            if (!this.fs.File.Exists(filePath))
            {
                return;
            }

            this.fs.File.Delete(filePath);
            this.LogFileDeleted(filePath);
        }
        catch (Exception ex)
        {
            this.LogFileIoError($"TryDeleteQuietly ({filePath})", ex);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Capture replace failures and continue fallback path")]
    private bool TryReplaceFile(string sourcePath, string destinationPath)
    {
        try
        {
            this.fs.File.Replace(sourcePath, destinationPath, destinationBackupFileName: null);
            this.LogFileReplaced(sourcePath, destinationPath);
            return true;
        }
        catch (Exception ex)
        {
            this.LogFileIoError($"FileReplace ({sourcePath} -> {destinationPath})", ex);
            return false;
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Attempt to restore backup without surfacing additional errors")]
    private void TryRestoreOriginal(string destinationPath, string backupPath)
    {
        try
        {
            this.fs.File.Delete(destinationPath);
            this.fs.File.Move(backupPath, destinationPath);
            this.LogRestoredOriginal(backupPath, destinationPath);
        }
        catch (Exception ex)
        {
            this.LogFileIoError($"RestoreOriginal ({backupPath} -> {destinationPath})", ex);
        }
    }

    // Start/stop watching helpers
    private void StartWatching()
    {
        if (this.fileWatcher is not null)
        {
            return;
        }

        Debug.Assert(this.PathIsValid(this.path), "`this.path` must be an absolute path with a file name.");

        var directory = this.fs.Path.GetDirectoryName(this.path);
        Debug.Assert(!string.IsNullOrWhiteSpace(directory), "Validated path must include a directory component.");
        var fileName = this.fs.Path.GetFileName(this.path);
        Debug.Assert(!string.IsNullOrWhiteSpace(fileName), "Validated path must include a file name.");

        try
        {
            this.fileWatcher = new FileSystemWatcher(directory, fileName)
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size,
                EnableRaisingEvents = true,
            };

            this.fileWatcher.Changed += OnChanged;
            this.fileWatcher.Created += OnChanged;
            this.fileWatcher.Deleted += OnChanged;
            this.fileWatcher.Renamed += OnRenamed;
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            // We already pre-check the directory, so any exception here is
            // unexpected and will be simply reported as not watching.
            this.LogFileIoError("StartWatching", ex);
        }
#pragma warning restore CA1031 // Do not catch general exception types

        void OnChanged(object? sender, FileSystemEventArgs e)
        {
            var changeType = e.ChangeType == WatcherChangeTypes.Deleted ? SourceChangeType.Removed : SourceChangeType.Updated;
            this.OnSourceChanged(new SourceChangedEventArgs(this.Id, changeType));
        }

        void OnRenamed(object? sender, RenamedEventArgs e)
        {
            Debug.Assert(this.PathIsValid(e.FullPath), "Expecting the full path of the renamed file to be valid");

            this.path = e.FullPath; // the file is still the same, just renamed
            this.OnSourceChanged(new SourceChangedEventArgs(this.Id, SourceChangeType.Renamed));
        }
    }

    private void StopWatching()
    {
        if (this.fileWatcher == null)
        {
            return;
        }

        try
        {
            this.fileWatcher.EnableRaisingEvents = false;
            this.fileWatcher.Dispose();
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            // We do not really expect exceptions to be thrown here, but log them if they do.
            this.LogFileIoError("StopWatching", ex);
        }
#pragma warning restore CA1031 // Do not catch general exception types
        finally
        {
            // The ultimate goal is to stop watching, so null out the watcher reference.
            this.fileWatcher = null;
        }
    }

    private bool PathIsValid(string p)
    {
        Debug.Assert(this.fs is not null, "File system must be provided");

        try
        {
            // Path cannot be null or whitespace
            if (string.IsNullOrWhiteSpace(p))
            {
                return false;
            }

            // Must be absolute
            if (!this.fs.Path.IsPathRooted(p))
            {
                return false;
            }

            // Must have a file name component
            var fileName = this.fs.Path.GetFileName(p);
            if (string.IsNullOrWhiteSpace(fileName))
            {
                return false;
            }
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch
        {
            return false;
        }
#pragma warning restore CA1031 // Do not catch general exception types

        return true;
    }
}
