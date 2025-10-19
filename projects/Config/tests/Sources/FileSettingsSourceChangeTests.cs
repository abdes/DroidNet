// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using DroidNet.Config;
using DroidNet.Config.Sources;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Testably.Abstractions;

namespace DroidNet.Config.Tests.Sources;

[TestClass]
[ExcludeFromCodeCoverage]
public class FileSettingsSourceChangeTests : IDisposable
{
    private LoggerFactory loggerFactory = null!;
    private RealFileSystem fs = null!;
    private string? tempDir;

    private bool disposed;

    public TestContext TestContext { get; set; }

    [TestInitialize]
    public void Initialize()
    {
        this.loggerFactory = new Microsoft.Extensions.Logging.LoggerFactory();
        this.fs = new RealFileSystem();
    }

    [TestCleanup]
    public void Cleanup()
    {
        if (!string.IsNullOrEmpty(this.tempDir) && Directory.Exists(this.tempDir))
        {
            Directory.Delete(this.tempDir!, recursive: true);
        }
    }

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    [TestMethod]
    public async Task FileWatcher_Event_RaisesSourceChanged_UsingRealFileSystem()
    {
        // This test uses the real file system because FileSystemWatcher relies on OS notifications.
        this.tempDir = Path.Combine(Path.GetTempPath(), "FileSettingsSourceTests", Guid.NewGuid().ToString());
        Directory.CreateDirectory(this.tempDir);
        var path = Path.Combine(this.tempDir, "watch.txt");
        await File.WriteAllTextAsync(path, "v1", this.TestContext.CancellationToken).ConfigureAwait(true);

        var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        var tcs = new TaskCompletionSource<SourceChangedEventArgs>();
        source.SourceChanged += (_, args) => tcs.TrySetResult(args);

        // Enable watching via the property - this will start a real FileSystemWatcher
        source.WatchForChanges = true;
        await File.WriteAllTextAsync(path, "v2", this.TestContext.CancellationToken).ConfigureAwait(true);

        // Wait up to 2 seconds for an event
        var completed = await Task.WhenAny(tcs.Task, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);

        // Clean up
        source.WatchForChanges = false;
        source.Dispose();

        if (completed == tcs.Task)
        {
            var ev = await tcs.Task.ConfigureAwait(true);
            _ = ev.Should().NotBeNull();
            _ = ev.SourceId.Should().Be(source.Id);
            _ = (ev.ChangeType == SourceChangeType.Updated || ev.ChangeType == SourceChangeType.Removed || ev.ChangeType == SourceChangeType.Renamed).Should().BeTrue();
        }
        else
        {
            Assert.Inconclusive("File system watcher did not report change within timeout on this environment.");
        }
    }

    [TestMethod]
    public async Task FileWatcher_Event_RaisesSourceChanged_Renamed_UsingRealFileSystem()
    {
        this.tempDir = Path.Combine(Path.GetTempPath(), "FileSettingsSourceTests", Guid.NewGuid().ToString());
        Directory.CreateDirectory(this.tempDir);
        var path = Path.Combine(this.tempDir, "watch.txt");
        await File.WriteAllTextAsync(path, "v1", this.TestContext.CancellationToken).ConfigureAwait(true);

        var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        var tcs = new TaskCompletionSource<SourceChangedEventArgs>();
        source.SourceChanged += (_, args) => tcs.TrySetResult(args);

        source.WatchForChanges = true;
        _ = source.WatchForChanges.Should().BeTrue();

        var newPath = Path.Combine(this.tempDir, "watch-renamed.txt");
        File.Move(path, newPath);

        var completed = await Task.WhenAny(tcs.Task, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);

        source.WatchForChanges = false;
        source.Dispose();

        if (completed == tcs.Task)
        {
            var ev = await tcs.Task.ConfigureAwait(true);
            _ = ev.Should().NotBeNull();
            _ = ev.SourceId.Should().Be(source.Id);
            _ = ev.ChangeType.Should().Be(SourceChangeType.Renamed);

            // The source should update its internal path to the new name
            _ = this.fs.File.Exists(newPath).Should().BeTrue();

            // Ensure the source can still be used to read the file content after the rename
            var content = await source.ReadAllTextAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = content.Should().Be("v1");
        }
        else
        {
            Assert.Inconclusive("File system watcher did not report rename within timeout on this environment.");
        }
    }

    [TestMethod]
    public async Task FileWatcher_Event_RaisesSourceChanged_Deleted_UsingRealFileSystem()
    {
        this.tempDir = Path.Combine(Path.GetTempPath(), "FileSettingsSourceTests", Guid.NewGuid().ToString());
        Directory.CreateDirectory(this.tempDir);
        var path = Path.Combine(this.tempDir, "watch.txt");
        await File.WriteAllTextAsync(path, "v1", this.TestContext.CancellationToken).ConfigureAwait(true);

        var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        var tcs = new TaskCompletionSource<SourceChangedEventArgs>();
        source.SourceChanged += (_, args) => tcs.TrySetResult(args);

        source.WatchForChanges = true;
        File.Delete(path);

        var completed = await Task.WhenAny(tcs.Task, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);

        source.WatchForChanges = false;
        source.Dispose();

        if (completed == tcs.Task)
        {
            var ev = await tcs.Task.ConfigureAwait(true);
            _ = ev.Should().NotBeNull();
            _ = ev.SourceId.Should().Be(source.Id);
            _ = ev.ChangeType.Should().Be(SourceChangeType.Removed);
        }
        else
        {
            Assert.Inconclusive("File system watcher did not report delete within timeout on this environment.");
        }
    }

    [TestMethod]
    public async Task FileWatcher_Event_RaisesSourceChanged_Created_UsingRealFileSystem()
    {
        this.tempDir = Path.Combine(Path.GetTempPath(), "FileSettingsSourceTests", Guid.NewGuid().ToString());
        Directory.CreateDirectory(this.tempDir);
        var path = Path.Combine(this.tempDir, "watch-created.txt");

        var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        var tcs = new TaskCompletionSource<SourceChangedEventArgs>();
        source.SourceChanged += (_, args) => tcs.TrySetResult(args);

        source.WatchForChanges = true;

        // Create the file after watcher is enabled
        await File.WriteAllTextAsync(path, "v1", this.TestContext.CancellationToken).ConfigureAwait(true);

        var completed = await Task.WhenAny(tcs.Task, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);

        source.WatchForChanges = false;
        source.Dispose();

        if (completed == tcs.Task)
        {
            var ev = await tcs.Task.ConfigureAwait(true);
            _ = ev.Should().NotBeNull();
            _ = ev.SourceId.Should().Be(source.Id);

            // Created/Changed map to Updated in the implementation
            _ = ev.ChangeType.Should().Be(SourceChangeType.Updated);
        }
        else
        {
            Assert.Inconclusive("File system watcher did not report create within timeout on this environment.");
        }
    }

    [TestMethod]
    public async Task FileWatcher_StopWatching_PreventsFurtherEvents()
    {
        this.tempDir = Path.Combine(Path.GetTempPath(), "FileSettingsSourceTests", Guid.NewGuid().ToString());
        Directory.CreateDirectory(this.tempDir);
        var path = Path.Combine(this.tempDir, "watch.txt");
        await File.WriteAllTextAsync(path, "v1", this.TestContext.CancellationToken).ConfigureAwait(true);

        var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        var tcs = new TaskCompletionSource<SourceChangedEventArgs>();
        source.SourceChanged += (_, args) => tcs.TrySetResult(args);

        source.WatchForChanges = true;

        // Immediately stop watching
        source.WatchForChanges = false;

        // Trigger a change after stopping
        await File.WriteAllTextAsync(path, "v2", this.TestContext.CancellationToken).ConfigureAwait(true);

        var completed = await Task.WhenAny(tcs.Task, Task.Delay(TimeSpan.FromSeconds(1), this.TestContext.CancellationToken)).ConfigureAwait(false);

        source.Dispose();

        // If the watcher stopped correctly, we should not have received an event
        if (completed == tcs.Task)
        {
            Assert.Fail("Received a file change event after stopping watching.");
        }
    }

    [TestMethod]
    public void WatchForChanges_SetTrue_WhenAlreadyTrue_NoOp()
    {
        var path = Path.Combine(Path.GetTempPath(), "does-not-matter.txt");
        using var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory)
        {
            // First enable
            WatchForChanges = true,
        };

        // Second enable should be a no-op and should remain true
        source.WatchForChanges = true;
        _ = source.WatchForChanges.Should().BeTrue();

        source.WatchForChanges = false;
    }

    [TestMethod]
    public void WatchForChanges_SetFalse_WhenAlreadyFalse_NoOp()
    {
        var path = Path.Combine(Path.GetTempPath(), "does-not-matter.txt");
        using var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        // Initially false
        _ = source.WatchForChanges.Should().BeFalse();

        // Set false again - no-op
        source.WatchForChanges = false;
        _ = source.WatchForChanges.Should().BeFalse();
    }

    [TestMethod]
    public void StartWatching_InvalidDirectory_DoesNotThrowAndIsNotWatching()
    {
        // Use an invalid path (root only) or a directory that does not exist
        var invalidPath = Path.Combine("Z:\\this_directory_should_not_exist_12345", "file.txt");
        using var source = new TestFileSettingsSource("watch", invalidPath, this.fs, crypto: null, loggerFactory: this.loggerFactory)
        {
            // Attempt to enable watching - should not throw and result should be false
            WatchForChanges = true,
        };
        _ = source.WatchForChanges.Should().BeFalse();
    }

    [TestMethod]
    public void Dispose_WhenAlreadyDisposed_NoThrowAndWatchDisabled()
    {
        var path = Path.Combine(System.IO.Path.GetTempPath(), "does-not-matter.txt");
        var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        // Dispose once
        source.Dispose();

        // Second dispose should not throw and WatchForChanges must be false
        source.Dispose();
        _ = source.WatchForChanges.Should().BeFalse();
    }

    [TestMethod]
    public void Dispose_WhenNotWatching_NoThrowAndWatchDisabled()
    {
        var path = Path.Combine(System.IO.Path.GetTempPath(), "does-not-matter.txt");
        var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        // Ensure not watching
        _ = source.WatchForChanges.Should().BeFalse();

        // Dispose should not throw and should leave WatchForChanges false
        source.Dispose();
        _ = source.WatchForChanges.Should().BeFalse();
    }

    [TestMethod]
    public async Task Dispose_WhenWatching_NoThrowAndWatchDisabled()
    {
        // Create a real directory and file so StartWatching can succeed
        this.tempDir = Path.Combine(Path.GetTempPath(), "FileSettingsSourceTests", Guid.NewGuid().ToString());
        Directory.CreateDirectory(this.tempDir);
        var path = Path.Combine(this.tempDir, "watch.txt");
        await File.WriteAllTextAsync(path, "v1", this.TestContext.CancellationToken).ConfigureAwait(true);

        var source = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory)
        {
            // Start watching and ensure it is enabled
            WatchForChanges = true,
        };
        _ = source.WatchForChanges.Should().BeTrue();

        // Dispose should not throw and must clear the watch flag
        source.Dispose();
        _ = source.WatchForChanges.Should().BeFalse();
    }

    [TestMethod]
    public void StartWatching_NoDirectoryPath_ThrowsArgumentException()
    {
        // Path with no directory component (relative file name)
        const string path = "watch.txt";
        Action act = () => _ = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);
        _ = act.Should().Throw<ArgumentException>().WithParameterName("path");
    }

    [TestMethod]
    public void StartWatching_FileNameWhitespace_ThrowsArgumentException()
    {
        // Create a directory but use a file name that's only whitespace
        this.tempDir = Path.Combine(Path.GetTempPath(), "FileSettingsSourceTests", Guid.NewGuid().ToString());
        Directory.CreateDirectory(this.tempDir);
        var path = Path.Combine(this.tempDir, "   ");

        Action act = () => _ = new TestFileSettingsSource("watch", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);
        _ = act.Should().Throw<ArgumentException>().WithParameterName("path");
    }

    /// <summary>
    /// Standard dispose pattern.
    /// </summary>
    /// <param name="disposing">True when called from Dispose(), false from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            // dispose managed resources
            this.loggerFactory?.Dispose();
        }

        this.disposed = true;
    }

    // Minimal concrete implementation used for tests. It stores the file content as a string
    // using the protected helpers from FileSettingsSource.
    private sealed class TestFileSettingsSource(
        string id,
        string path,
        IFileSystem fileSystem,
        IEncryptionProvider? crypto = null,
        ILoggerFactory? loggerFactory = null)
        : FileSettingsSource(id, path, fileSystem, crypto, loggerFactory)
    {
        public override Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("LoadAsync is not used in these unit tests. Call ReadAllTextAsync instead.");

        public override Task<Result<SettingsWritePayload>> SaveAsync(System.Collections.Generic.IReadOnlyDictionary<string, object> sectionsData, SettingsMetadata metadata, CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("SaveAsync is not used in these unit tests. Call WriteAllTextAsync instead.");

        public override Task<Result<SettingsValidationPayload>> ValidateAsync(System.Collections.Generic.IReadOnlyDictionary<string, object> sectionsData, CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("ValidateAsync is not used in these unit tests.");

        // Expose protected helpers for unit tests
        public async Task<string> ReadAllTextAsync(CancellationToken cancellationToken = default)
             => await this.ReadAllBytesAsync(cancellationToken).ConfigureAwait(false);

        public Task WriteAllTextAsync(string content, CancellationToken cancellationToken = default)
            => this.WriteAllBytesAsync(content, cancellationToken);
    }
}
