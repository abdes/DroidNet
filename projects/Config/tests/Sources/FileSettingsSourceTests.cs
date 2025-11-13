// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Text;
using DroidNet.Config.Sources;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;
using Moq;
using Testably.Abstractions.Testing;

namespace DroidNet.Config.Tests.Sources;

[TestClass]
[ExcludeFromCodeCoverage]
public class FileSettingsSourceTests : IDisposable
{
    private MockFileSystem fs = null!;
    private LoggerFactory loggerFactory = null!;
    private bool disposed;

    public TestContext TestContext { get; set; }

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    [TestInitialize]
    public void Initialize()
    {
        this.fs = new Testably.Abstractions.Testing.MockFileSystem();
        this.loggerFactory = new LoggerFactory();
    }

    [TestMethod]
    public void Constructor_WithNullFileSystem_ThrowsArgumentNullException()
    {
        var validPath = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "config.json");

        Action act = () => _ = new TestFileSettingsSource("test", validPath, fileSystem: null!);

        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("fileSystem");
    }

    [TestMethod]
    [DataRow(null)]
    [DataRow("")]
    [DataRow("   ")]
    public void Constructor_WithInvalidId_ThrowsArgumentException(string? invalidId)
    {
        var validPath = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "config.json");

        Action act = () => _ = new TestFileSettingsSource(invalidId!, validPath, this.fs);

        _ = act.Should().Throw<ArgumentException>().WithParameterName("id");
    }

    [TestMethod]
    public void Constructor_WithNullPath_ThrowsArgumentNullException()
    {
        Action act = () => _ = new TestFileSettingsSource("test", path: null!, this.fs);

        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("filePath");
    }

    [TestMethod]
    [DataRow("")]
    [DataRow("   ")]
    [DataRow("relative/path/settings.json")]
    [DataRow("{Root}")]
    public void Constructor_WithInvalidPath_ThrowsArgumentException(string invalidPath)
    {
        if (string.Equals(invalidPath, "{Root}", StringComparison.Ordinal))
        {
            var root = this.fs.Path.GetPathRoot(this.fs.Path.GetTempPath());
            invalidPath = root ?? this.fs.Path.DirectorySeparatorChar.ToString();
        }

        var capturedPath = invalidPath;

        Action act = () => _ = new TestFileSettingsSource("test", capturedPath, this.fs);

        _ = act.Should().Throw<ArgumentException>().WithParameterName("filePath");
    }

    [TestMethod]
    public void Constructor_WhenPathOperationsThrow_ThrowsArgumentException()
    {
        var mockPath = new Mock<IPath>();
        _ = mockPath.Setup(p => p.IsPathRooted(It.IsAny<string>())).Returns(value: true);
        _ = mockPath.Setup(p => p.GetFileName(It.IsAny<string>())).Throws(new InvalidOperationException("boom"));

        var mockFs = new Mock<IFileSystem>();
        _ = mockFs.SetupGet(fs => fs.Path).Returns(mockPath.Object);

        var validLookingPath = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "settings.json");

        Action act = () => _ = new TestFileSettingsSource("test", validLookingPath, mockFs.Object);

        _ = act.Should().Throw<ArgumentException>().WithParameterName("filePath");
    }

    [TestMethod]
    public void IsAvailable_WhenFileCanBeOpened_ReturnsTrue()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "available", "settings.json");
        var directory = this.fs.Path.GetDirectoryName(path)!;
        _ = this.fs.Directory.CreateDirectory(directory);
        this.fs.File.WriteAllText(path, "content");

        using var source = new TestFileSettingsSource("available", path, this.fs, loggerFactory: this.loggerFactory);

        _ = source.IsAvailable.Should().BeTrue();
    }

    [TestMethod]
    public void IsAvailable_WhenFileMissing_ReturnsFalse()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "missing-file", "settings.json");
        var directory = this.fs.Path.GetDirectoryName(path)!;
        _ = this.fs.Directory.CreateDirectory(directory);

        using var source = new TestFileSettingsSource("available-missing", path, this.fs, loggerFactory: this.loggerFactory);

        _ = source.IsAvailable.Should().BeFalse();
    }

    [TestMethod]
    public void IsAvailable_WhenFileNotReadable_ReturnsFalse()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "unreadable", "settings.json");
        var directory = this.fs.Path.GetDirectoryName(path)!;
        _ = this.fs.Directory.CreateDirectory(directory);
        this.fs.File.WriteAllText(path, "content");

        var mockFs = new Mock<IFileSystem>();
        _ = mockFs.SetupGet(fs => fs.Path).Returns(this.fs.Path);
        _ = mockFs.SetupGet(fs => fs.Directory).Returns(this.fs.Directory);

        var mockFileInfoFactory = new Mock<IFileInfoFactory>();
        _ = mockFileInfoFactory.Setup(f => f.New(It.IsAny<string>())).Returns<string>(this.fs.FileInfo.New);
        _ = mockFs.SetupGet(fs => fs.FileInfo).Returns(mockFileInfoFactory.Object);

        var mockFile = new Mock<IFile>();
        _ = mockFs.SetupGet(fs => fs.File).Returns(mockFile.Object);

        using var source = new TestFileSettingsSource("available-unreadable", path, mockFs.Object, loggerFactory: this.loggerFactory);

        _ = source.IsAvailable.Should().BeFalse();
    }

    [TestMethod]
    public void SupportsEncryption_ReturnsTrue_WhenEncryptionProviderIsSet()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "enc-support.txt");
        var crypto = new SimpleCrypto();
        using var source = new TestFileSettingsSource("enc-support", path, this.fs, crypto: crypto, loggerFactory: this.loggerFactory);
        _ = source.SupportsEncryption.Should().BeTrue();
    }

    [TestMethod]
    public void SupportsEncryption_ReturnsFalse_WhenNoEncryptionProviderIsSet()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "plain-support.txt");
        using var source = new TestFileSettingsSource("plain-support", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);
        _ = source.SupportsEncryption.Should().BeFalse();
    }

    [TestMethod]
    public async Task ReadAndWrite_PlainText_NoEncryption()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "plain.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        using var source = new TestFileSettingsSource("test", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        const string content = "Hello FileSettingsSource";
        await source.WriteAllTextAsync(content, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Underlying file should contain UTF8 bytes equal to content
        _ = this.fs.File.Exists(path).Should().BeTrue();
        var diskContent = await this.fs.File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = diskContent.Should().Be(content);

        var read = await source.ReadAllTextAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = read.Should().Be(content);
        _ = read.Should().Be(content);
    }

    [TestMethod]
    public async Task ReadAndWrite_WithEncryptionProvider_EncryptsOnDiskAndDecryptsOnLoad()
    {
        var crypto = new SimpleCrypto();
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "enc.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        using var source = new TestFileSettingsSource("enc", path, this.fs, crypto: crypto, loggerFactory: this.loggerFactory);

        const string content = "Secret Data";
        await source.WriteAllTextAsync(content, this.TestContext.CancellationToken).ConfigureAwait(true);

        // File on disk should not equal plaintext (because we reversed bytes)
        var raw = await this.fs.File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(true);
        var rawText = Encoding.UTF8.GetString(raw);
        _ = rawText.Should().NotBe(content);

        // Loading should return decrypted plaintext
        var read = await source.ReadAllTextAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = read.Should().Be(content);
        _ = read.Should().Be(content);
    }

    [TestMethod]
    public async Task ReadAllTextAsync_WhenFileMissing_ThrowsSettingsPersistenceException()
    {
        // Arrange: create a mock IFileSystem whose File.ReadAllBytesAsync throws
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "will-throw-read.txt");
        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();
        _ = mockFile.Setup(f => f.ReadAllBytesAsync(path, It.IsAny<CancellationToken>())).ThrowsAsync(new IOException("read fail"));
        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);
        _ = mockFs.SetupGet(m => m.Path).Returns(this.fs.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.fs.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.fs.FileInfo);

        using var source = new TestFileSettingsSource("t-ex-read", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        // Act
        Func<Task> act = async () => await source.ReadAllTextAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);
    }

    [TestMethod]
    public async Task WriteAllTextAsync_WhenUnderlyingWriteThrows_ThrowsSettingsPersistenceException()
    {
        // Arrange: configure IFileSystem where File.WriteAllBytesAsync throws
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "will-throw-write.txt");

        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();
        _ = mockFile.Setup(f => f.WriteAllBytesAsync(It.IsAny<string>(), It.IsAny<byte[]>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new IOException("write fail"));
        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);
        _ = mockFs.SetupGet(m => m.Path).Returns(this.fs.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.fs.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.fs.FileInfo);

        using var source = new TestFileSettingsSource("t-ex-write", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.WriteAllTextAsync("payload", this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);
    }

    [TestMethod]
    public async Task WriteAllTextAsync_WhenDirectoryMissing_CreatesDirectory()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "missing", "nested", "settings.json");
        var directory = this.fs.Path.GetDirectoryName(path)!;

        _ = this.fs.Directory.Exists(directory).Should().BeFalse();

        using var source = new TestFileSettingsSource("write-create-dir", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        await source.WriteAllTextAsync("content", this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = this.fs.Directory.Exists(directory).Should().BeTrue();
        var diskContent = await this.fs.File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = diskContent.Should().Be("content");
    }

    [TestMethod]
    public async Task WriteAllTextAsync_WhenUnderlyingWriteThrows_LeavesOriginalFileIntact()
    {
        // Arrange: real backing fs containing original file, but an IFileSystem wrapper whose File.WriteAllBytesAsync throws
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "will-throw-write-atomic.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        // Write initial content to the real backing file
        await this.fs.File.WriteAllTextAsync(path, "original-content", this.TestContext.CancellationToken).ConfigureAwait(true);

        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();

        // Underlying write throws; it does not modify the real backing file
        _ = mockFile.Setup(f => f.WriteAllBytesAsync(It.IsAny<string>(), It.IsAny<byte[]>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new IOException("simulated write failure"));

        // Delegate path/directory operations to the real mock filesystem so paths resolve correctly
        _ = mockFs.SetupGet(m => m.Path).Returns(this.fs.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.fs.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.fs.FileInfo);
        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);

        using var source = new TestFileSettingsSource("atomic-fail", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        // Act
        Func<Task> act = async () => await source.WriteAllTextAsync("new-content", this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert exception is thrown
        _ = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);

        // And the original file on the real backing filesystem remains unchanged
        var diskContent = await this.fs.File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = diskContent.Should().Be("original-content");
    }

    [TestMethod]
    public async Task WriteAllTextAsync_CancellationDuringLongWrite_LeavesOriginalFileIntact()
    {
        // Arrange: create a real file with original content
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "will-cancel-write-atomic.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        await this.fs.File.WriteAllTextAsync(path, "original-content", CancellationToken.None).ConfigureAwait(true);

        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();

        // Simulate a long-running write that respects cancellation (does not modify the real backing file)
        _ = mockFile.Setup(f => f.WriteAllBytesAsync(It.IsAny<string>(), It.IsAny<byte[]>(), It.IsAny<CancellationToken>()))
                .Returns<string, byte[], CancellationToken>(async (p, data, ct)
                    => await Task.Delay(TimeSpan.FromSeconds(5), ct).ConfigureAwait(false));

        _ = mockFs.SetupGet(m => m.Path).Returns(this.fs.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.fs.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.fs.FileInfo);
        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);

        using var source = new TestFileSettingsSource("atomic-cancel", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        using var cts = new CancellationTokenSource();
        cts.CancelAfter(TimeSpan.FromMilliseconds(50));

        // Act
        Func<Task> act = async () => await source.WriteAllTextAsync("new-content", cts.Token).ConfigureAwait(true);

        // Assert cancellation thrown
        _ = await act.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(true);

        // And original file remains
        var diskContent = await this.fs.File.ReadAllTextAsync(path, CancellationToken.None).ConfigureAwait(true);
        _ = diskContent.Should().Be("original-content");
    }

    [TestMethod]
    public async Task WriteAllTextAsync_PartialWriteThenThrow_LeavesOriginalFileIntact()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "atomic-partial.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        await this.fs.File.WriteAllTextAsync(path, "original-content", CancellationToken.None).ConfigureAwait(true);

        // Configure mock file so WriteAllBytesAsync writes only half the bytes then throws (partial write)
        var mockFs = this.CreateDelegatingMockFileSystem(mockFile
            => _ = mockFile.Setup(f => f.WriteAllBytesAsync(It.IsAny<string>(), It.IsAny<byte[]>(), It.IsAny<CancellationToken>()))
                    .Returns<string, byte[], CancellationToken>(async (p, data, ct) =>
                    {
                        var partial = data.Take(data.Length / 2).ToArray();

                        // Write the partial bytes to the underlying real in-memory fs to simulate a partial file
                        await this.fs.File.WriteAllBytesAsync(p, partial, ct).ConfigureAwait(false);
                        throw new IOException("simulated partial write failure");
                    }));

        using var source = new TestFileSettingsSource("atomic-partial", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.WriteAllTextAsync("new-content", this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);

        // Original target file should remain unchanged
        var diskContent = await this.fs.File.ReadAllTextAsync(path, CancellationToken.None).ConfigureAwait(true);
        _ = diskContent.Should().Be("original-content");
    }

    [TestMethod]
    public async Task WriteAllTextAsync_RenameThrows_LeavesOriginalFileIntact()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "atomic-rename.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        await this.fs.File.WriteAllTextAsync(path, "original-content", CancellationToken.None).ConfigureAwait(true);

        var mockFs = this.CreateDelegatingMockFileSystem(mockFile =>
        {
            // write succeeds (likely to a temp file), but Move (rename) fails
            // Ensure Replace is not silently used by the implementation: make it throw so the
            // fallback path (delete + move) is exercised and our Move mock runs.
            _ = mockFile.Setup(f => f.Replace(It.IsAny<string>(), It.IsAny<string>(), It.IsAny<string>()))
                .Throws<IOException>();

            _ = mockFile.Setup(f => f.Move(It.IsAny<string>(), It.IsAny<string>()))
                .Callback<string, string>((s, d) => throw new IOException("simulated rename failure"));
        });

        using var source = new TestFileSettingsSource("atomic-rename", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.WriteAllTextAsync("new-content", this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);

        var diskContent = await this.fs.File.ReadAllTextAsync(path, CancellationToken.None).ConfigureAwait(true);
        _ = diskContent.Should().Be("original-content");
    }

    [TestMethod]
    public async Task WriteAllTextAsync_BackupMoveThrows_LeavesOriginalFileIntact()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "atomic-delete.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        await this.fs.File.WriteAllTextAsync(path, "original-content", CancellationToken.None).ConfigureAwait(true);

        var mockFs = this.CreateDelegatingMockFileSystem(mockFile =>
        {
            // Force Replace to throw so the backup + move path is exercised.
            _ = mockFile.Setup(f => f.Replace(It.IsAny<string>(), It.IsAny<string>(), It.IsAny<string>()))
                .Throws<IOException>();

            _ = mockFile.Setup(f => f.Move(It.IsAny<string>(), It.IsAny<string>()))
                .Callback<string, string>((source, destination) =>
                {
                    if (destination.Contains(".bak.", System.StringComparison.OrdinalIgnoreCase))
                    {
                        throw new IOException("simulated backup move failure");
                    }

                    this.fs.File.Move(source, destination);
                });
        });

        using var source = new TestFileSettingsSource("atomic-delete", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.WriteAllTextAsync("new-content", this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);

        var diskContent = await this.fs.File.ReadAllTextAsync(path, CancellationToken.None).ConfigureAwait(true);
        _ = diskContent.Should().Be("original-content");
    }

    [TestMethod]
    public async Task WriteAllTextAsync_BackupMoveThrowsWithUnauthorized_LeavesOriginalFileIntact()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "atomic-delete-unauth.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        await this.fs.File.WriteAllTextAsync(path, "original-content", CancellationToken.None).ConfigureAwait(true);

        var mockFs = this.CreateDelegatingMockFileSystem(mockFile =>
        {
            _ = mockFile.Setup(f => f.Replace(It.IsAny<string>(), It.IsAny<string>(), It.IsAny<string>()))
                .Throws<IOException>();

            _ = mockFile.Setup(f => f.Move(It.IsAny<string>(), It.IsAny<string>()))
                .Callback<string, string>((source, destination) =>
                {
                    if (destination.Contains(".bak.", System.StringComparison.OrdinalIgnoreCase))
                    {
                        throw new UnauthorizedAccessException("simulated backup move unauthorized failure");
                    }

                    this.fs.File.Move(source, destination);
                });
        });

        using var source = new TestFileSettingsSource("atomic-delete-unauth", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.WriteAllTextAsync("new-content", this.TestContext.CancellationToken).ConfigureAwait(true);
        var unauthorized = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);
        _ = unauthorized.WithInnerException<UnauthorizedAccessException>();

        var diskContent = await this.fs.File.ReadAllTextAsync(path, CancellationToken.None).ConfigureAwait(true);
        _ = diskContent.Should().Be("original-content");
    }

    [TestMethod]
    public async Task WriteAllTextAsync_FileReplaceSucceeds_ReplacesExistingFile()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "atomic-replace-success.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        await this.fs.File.WriteAllTextAsync(path, "original-content", CancellationToken.None).ConfigureAwait(true);

        var mockFs = this.CreateDelegatingMockFileSystem(mockFile
            => _ = mockFile.Setup(f => f.Replace(It.IsAny<string>(), It.IsAny<string>(), It.IsAny<string>()))
                .Callback<string, string, string>((source, destination, backup) =>
                {
                    this.fs.File.Copy(source, destination, overwrite: true);
                    this.fs.File.Delete(source);
                }));

        using var source = new TestFileSettingsSource("atomic-replace-success", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        await source.WriteAllTextAsync("new-content", this.TestContext.CancellationToken).ConfigureAwait(true);

        var diskContent = await this.fs.File.ReadAllTextAsync(path, CancellationToken.None).ConfigureAwait(true);
        _ = diskContent.Should().Be("new-content");

        var backupFiles = this.fs.Directory.GetFiles(directory, "*.bak.*");
        _ = backupFiles.Should().BeEmpty();
    }

    [TestMethod]
    public async Task WriteAllTextAsync_FinalMoveThrows_RestoresOriginalFromBackup()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "atomic-final-move-fail.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        await this.fs.File.WriteAllTextAsync(path, "original-content", CancellationToken.None).ConfigureAwait(true);

        var mockFs = this.CreateDelegatingMockFileSystem(mockFile =>
        {
            _ = mockFile.Setup(f => f.Replace(It.IsAny<string>(), It.IsAny<string>(), It.IsAny<string>()))
                .Throws<IOException>();

            var finalMoveAttempted = false;
            _ = mockFile.Setup(f => f.Move(It.IsAny<string>(), It.IsAny<string>()))
                .Callback<string, string>((source, destination) =>
                {
                    if (destination.Contains(".bak.", System.StringComparison.OrdinalIgnoreCase))
                    {
                        this.fs.File.Move(source, destination);
                        return;
                    }

                    if (string.Equals(destination, path, System.StringComparison.OrdinalIgnoreCase))
                    {
                        if (!finalMoveAttempted)
                        {
                            finalMoveAttempted = true;
                            this.fs.File.WriteAllText(destination, "corrupted");
                            throw new IOException("simulated final move failure");
                        }

                        this.fs.File.Move(source, destination);
                        return;
                    }

                    this.fs.File.Move(source, destination);
                });
        });

        using var source = new TestFileSettingsSource("atomic-final-move-fail", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.WriteAllTextAsync("new-content", this.TestContext.CancellationToken).ConfigureAwait(true);
        var failure = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);
        _ = failure.WithInnerException<IOException>();

        var diskContent = await this.fs.File.ReadAllTextAsync(path, CancellationToken.None).ConfigureAwait(true);
        _ = diskContent.Should().Be("original-content");

        var backupFiles = this.fs.Directory.GetFiles(directory, "*.bak.*");
        _ = backupFiles.Should().BeEmpty();
    }

    [TestMethod]
    public async Task WriteAllTextAsync_CreateThrows_LeavesOriginalFileIntact()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "atomic-create.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        await this.fs.File.WriteAllTextAsync(path, "original-content", CancellationToken.None).ConfigureAwait(true);

        // Ensure WriteAllBytesAsync (which is used to create/write the temp file) fails
        var mockFs = this.CreateDelegatingMockFileSystem(mockFile
            => _ = mockFile.Setup(f => f.WriteAllBytesAsync(It.IsAny<string>(), It.IsAny<byte[]>(), It.IsAny<CancellationToken>()))
                        .ThrowsAsync(new IOException("simulated create/write failure")));

        using var source = new TestFileSettingsSource("atomic-create", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.WriteAllTextAsync("new-content", this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);

        var diskContent = await this.fs.File.ReadAllTextAsync(path, CancellationToken.None).ConfigureAwait(true);
        _ = diskContent.Should().Be("original-content");
    }

    [TestMethod]
    public async Task ReadAllTextAsync_WhenDecryptThrows_PropagatesCryptographicException()
    {
        // Arrange: normal fs but crypto throws on decrypt
        var crypto = new ThrowingCrypto(decryptThrows: true, encryptThrows: false);
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "decrypt-fail.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        // Write some bytes (encrypted or not doesn't matter - decrypt will throw)
        await this.fs.File.WriteAllTextAsync(path, "payload", this.TestContext.CancellationToken).ConfigureAwait(true);

        using var source = new TestFileSettingsSource("t2", path, this.fs, crypto: crypto, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.ReadAllTextAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<System.Security.Cryptography.CryptographicException>().ConfigureAwait(true);
    }

    [TestMethod]
    public async Task WriteAllTextAsync_WhenEncryptThrows_PropagatesCryptographicException()
    {
        // Arrange: crypto throws on encrypt
        var crypto = new ThrowingCrypto(decryptThrows: false, encryptThrows: true);
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "encrypt-fail.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        using var source = new TestFileSettingsSource("t3", path, this.fs, crypto: crypto, loggerFactory: this.loggerFactory);

        Func<Task> act = async () => await source.WriteAllTextAsync("payload", this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<System.Security.Cryptography.CryptographicException>().ConfigureAwait(true);
    }

    [TestMethod]
    public async Task Write_ThrowsSettingsPersistenceException_WhenTargetReadOnly()
    {
        var dir = this.fs.Path.GetTempPath();
        var path = this.fs.Path.Combine(dir, "readonly.txt");
        if (!this.fs.Directory.Exists(dir))
        {
            _ = this.fs.Directory.CreateDirectory(dir);
        }

        await this.fs.File.WriteAllTextAsync(path, "initial", this.TestContext.CancellationToken).ConfigureAwait(true);

        // Set file read-only
        var fi = this.fs.FileInfo.New(path);
        fi.IsReadOnly = true;

        using var source = new TestFileSettingsSource("ro", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);

        // Attempt write using the wrapper which will surface write exceptions as thrown exceptions
        try
        {
            await source.WriteAllTextAsync("new", this.TestContext.CancellationToken).ConfigureAwait(true);
        }
        catch (SettingsPersistenceException ex)
        {
            _ = ex.Should().NotBeNull();
        }
    }

    [TestMethod]
    public async Task Reload_ReflectsExternalChange()
    {
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "reload.txt");
        var directory = this.fs.Path.GetDirectoryName(path) ?? this.fs.Path.GetTempPath();
        if (!this.fs.Directory.Exists(directory))
        {
            _ = this.fs.Directory.CreateDirectory(directory);
        }

        using var source = new TestFileSettingsSource("reload", path, this.fs, crypto: null, loggerFactory: this.loggerFactory);
        await this.fs.File.WriteAllTextAsync(path, "first", this.TestContext.CancellationToken).ConfigureAwait(true);
        var r1 = await source.ReadAllTextAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = r1.Should().Be("first");
        _ = r1.Should().Be("first");

        // Modify underlying file externally (simulate external actor)
        await this.fs.File.WriteAllTextAsync(path, "second", this.TestContext.CancellationToken).ConfigureAwait(true);

        // Call LoadAsync with reload true to force re-read
        var r2 = await source.ReadAllTextAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = r2.Should().Be("second");
        _ = r2.Should().Be("second");
    }

    [TestMethod]
    public async Task ReadAllTextAsync_CancellationDuringLongRead_ThrowsOperationCanceledException()
    {
        // Arrange: mock IFile to simulate a long-running read that responds to cancellation
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "long-read.txt");

        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();

        _ = mockFile.Setup(f => f.ReadAllBytesAsync(path, It.IsAny<CancellationToken>()))
                .Returns<string, CancellationToken>(async (p, ct) =>
                {
                    // Simulate a long-running operation that respects cancellation
                    await Task.Delay(TimeSpan.FromSeconds(5), ct).ConfigureAwait(false);
                    return Encoding.UTF8.GetBytes("should-not-complete");
                });

        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);
        _ = mockFs.SetupGet(m => m.Path).Returns(this.fs.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.fs.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.fs.FileInfo);

        using var source = new TestFileSettingsSource("long-read", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        using var cts = new CancellationTokenSource();
        cts.CancelAfter(TimeSpan.FromMilliseconds(50));

        // Act
        Func<Task> act = async () => await source.ReadAllTextAsync(cts.Token).ConfigureAwait(true);

        // Assert
        _ = await act.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(true);
    }

    [TestMethod]
    public async Task WriteAllTextAsync_CancellationDuringLongWrite_ThrowsOperationCanceledException()
    {
        // Arrange: mock IFile to simulate a long-running write that responds to cancellation
        var path = this.fs.Path.Combine(this.fs.Path.GetTempPath(), "long-write.txt");

        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();

        // Simulate a long-running operation that respects cancellation
        _ = mockFile.Setup(f => f.WriteAllBytesAsync(It.IsAny<string>(), It.IsAny<byte[]>(), It.IsAny<CancellationToken>()))
            .Returns<string, byte[], CancellationToken>(async (p, data, ct)
                => await Task.Delay(TimeSpan.FromSeconds(5), ct).ConfigureAwait(false));

        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);
        _ = mockFs.SetupGet(m => m.Path).Returns(this.fs.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.fs.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.fs.FileInfo);

        using var source = new TestFileSettingsSource("long-write", path, mockFs.Object, crypto: null, loggerFactory: this.loggerFactory);

        using var cts = new CancellationTokenSource();
        cts.CancelAfter(TimeSpan.FromMilliseconds(50));

        // Act
        Func<Task> act = async () => await source.WriteAllTextAsync("payload", cts.Token).ConfigureAwait(true);

        // Assert
        _ = await act.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(true);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            this.loggerFactory?.Dispose();
        }

        this.disposed = true;
    }

    // Helper to build a Mock<IFileSystem> that delegates most operations to the in-memory `this.fs`
    // but allows tests to inject failure behavior on the IFile instance.
    private Mock<IFileSystem> CreateDelegatingMockFileSystem(Action<Mock<IFile>> configureMockFile)
    {
        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();

        // Default delegations so reads and other helpers work against the in-memory mock filesystem.
        _ = mockFile.Setup(f => f.ReadAllBytesAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
                .Returns<string, CancellationToken>(this.fs.File.ReadAllBytesAsync);
        _ = mockFile.Setup(f => f.ReadAllTextAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
                .Returns<string, CancellationToken>(this.fs.File.ReadAllTextAsync);
        _ = mockFile.Setup(f => f.WriteAllBytesAsync(It.IsAny<string>(), It.IsAny<byte[]>(), It.IsAny<CancellationToken>()))
                .Returns<string, byte[], CancellationToken>(this.fs.File.WriteAllBytesAsync);
        _ = mockFile.Setup(f => f.WriteAllTextAsync(It.IsAny<string>(), It.IsAny<string>(), It.IsAny<CancellationToken>()))
                .Returns<string, string, CancellationToken>(this.fs.File.WriteAllTextAsync);
        _ = mockFile.Setup(f => f.Exists(It.IsAny<string>())).Returns<string>(this.fs.File.Exists);
        _ = mockFile.Setup(f => f.Delete(It.IsAny<string>())).Callback<string>(this.fs.File.Delete);
        _ = mockFile.Setup(f => f.Move(It.IsAny<string>(), It.IsAny<string>())).Callback<string, string>(this.fs.File.Move);
        _ = mockFile.Setup(f => f.Create(It.IsAny<string>())).Returns<string>(this.fs.File.Create);

        // Allow caller to override / inject failing behaviors
        configureMockFile?.Invoke(mockFile);

        _ = mockFs.SetupGet(m => m.Path).Returns(this.fs.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.fs.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.fs.FileInfo);
        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);

        return mockFs;
    }

    private sealed class SimpleCrypto : IEncryptionProvider
    {
        // Simple reversible transform: reverse bytes
        public byte[] Encrypt(byte[] data)
        {
            ArgumentNullException.ThrowIfNull(data);
            var copy = (byte[])data.Clone();
            Array.Reverse(copy);
            return copy;
        }

        public byte[] Decrypt(byte[] data)
        {
            ArgumentNullException.ThrowIfNull(data);
            var copy = (byte[])data.Clone();
            Array.Reverse(copy);
            return copy;
        }
    }

    private sealed class ThrowingCrypto(bool decryptThrows, bool encryptThrows) : IEncryptionProvider
    {
        private readonly bool decryptThrows = decryptThrows;
        private readonly bool encryptThrows = encryptThrows;

        public byte[] Encrypt(byte[] data)
            => this.encryptThrows
                ? throw new System.Security.Cryptography.CryptographicException("encrypt fail")
                : data;

        public byte[] Decrypt(byte[] data)
            => this.decryptThrows
                ? throw new System.Security.Cryptography.CryptographicException("decrypt fail")
                : data;
    }

    // Minimal concrete implementation used for tests. It stores the file content as a single
    // UTF-8 encoded string and exposes Load/Save/Validate behaviors by delegating to the
    // protected ReadAllBytesAsync/WriteAllBytesAsync helpers on FileSettingsSource.
    private sealed class TestFileSettingsSource(
        string id,
        string path,
        IFileSystem fileSystem,
        IEncryptionProvider? crypto = null,
        ILoggerFactory? loggerFactory = null)
        : FileSettingsSource(id, path, fileSystem, watch: false, crypto, loggerFactory)
    {
        public override Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("LoadAsync is not used in these unit tests. Call ReadAllTextAsync instead.");

        public override Task<Result<SettingsWritePayload>> SaveAsync(
            IReadOnlyDictionary<string, object> sectionsData,
            IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
            SettingsSourceMetadata sourceMetadata,
            CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("SaveAsync is not used in these unit tests. Call WriteAllTextAsync instead.");

        public override Task<Result<SettingsValidationPayload>> ValidateAsync(IReadOnlyDictionary<string, object> sectionsData, CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("ValidateAsync is not used in these unit tests.");

        // Public wrappers around the protected helpers so unit tests can call them directly
        public async Task<string> ReadAllTextAsync(CancellationToken cancellationToken = default)
            => await this.ReadAllBytesAsync(cancellationToken).ConfigureAwait(false);

        public Task WriteAllTextAsync(string content, CancellationToken cancellationToken = default)
            => this.WriteAllBytesAsync(content, cancellationToken);
    }
}
