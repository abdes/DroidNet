// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Testably.Abstractions.Helpers;
using Testably.Abstractions.Testing;
using Testably.Abstractions.Testing.FileSystem;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Native Storage")]
public class NativeFileTests
{
    private const string File1Content = "file1 content";
    private readonly MockFileSystem fs;
    private readonly NativeStorageProvider nsp;

    public NativeFileTests()
    {
        this.fs = new MockFileSystem();
        _ = this.fs.WithAccessControlStrategy(
                new CustomAccessControlStrategy(path => !path.Contains("FORBIDDEN", StringComparison.Ordinal)))
            .Initialize()
            .WithFile("c:/DELETE")
            .WithFile("c:/FORBIDDEN")
            .WithFile("c:/file1")
            .Which(f => f.HasStringContent(File1Content))
            .WithFile("c:/file2")
            .WithSubdirectory("c:/folder1")
            .Initialized(
                d => d.WithFile("folder1-file1")
                    .WithFile("folder1-file2")
                    .WithSubdirectory("folder1-subfolder1")
                    .WithFile("folder1-file3"))
            .WithSubdirectory("c:/copy-target")
            .Initialized(d => d.WithFile("existing"));

        this.nsp = new NativeStorageProvider(this.fs);
    }

    [TestMethod]
    public async Task LastAccessTime_GetsTheDateTimeFolderWasLastAccessed()
    {
        // Setup
        const string path = "c:/file1";
        var document = await this.nsp.GetDocumentFromPathAsync(path).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var platformLastAccessTime = this.fs.File.GetLastAccessTime(path);

        // Act
        var result = document.LastAccessTime;

        // Assert
        _ = result.Should().Be(platformLastAccessTime);
    }

    [TestMethod]
    public async Task LastAccessTime_ReturnsEarliestFileTime_WhenFileDoesNotExist()
    {
        // Setup
        const string path = "does-not-exist";
        var document = await this.nsp.GetDocumentFromPathAsync(path).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        var result = document.LastAccessTime;

        // Assert
        _ = result.Should().Be(DateTime.FromFileTime(0));
    }

    [TestMethod]
    public Task LastAccessTime_ReturnsEarliestFileTime_WhenItFails()
    {
        // Setup

        // Manually create a NativeFile with invalid path to avoid the checks
        // made by the storage provider.
        var document = new NativeFile(this.nsp, "|invalid|", "|invalid|", @"c:\");

        // Act
        var result = document.LastAccessTime;

        // Assert
        _ = result.Should().Be(DateTime.FromFileTime(0));
        return Task.CompletedTask;
    }

    [TestMethod]
    [DataRow("c:/file1", true)]
    [DataRow("c:/file2", true)]
    [DataRow("c:/folder1/folder1-file1", true)]
    [DataRow("c:/folder1/folder1-file2", true)]
    [DataRow("c:/folder1/folder1-file3", true)]
    [DataRow("does-not-exist", false)]
    [DataRow("c:/folder1/does-not-exist", false)]
    [DataRow("c:/does-not-exist/does-not-exist", false)]
    [DataRow("c:/FORBIDDEN", true)]
    public async Task Exists_ChecksIfFileExists(string path, bool exists)
    {
        // Setup
        var document = await this.nsp.GetDocumentFromPathAsync(path).ConfigureAwait(false);

        // Act
        var result = await document.ExistsAsync().ConfigureAwait(false);

        // Assert
        _ = result.Should().Be(exists);
    }

    [TestMethod]
    public async Task Delete_Works()
    {
        // Setup
        var document = await this.nsp.GetDocumentFromPathAsync("C:/DELETE").ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        await document.DeleteAsync().ConfigureAwait(false);

        // Assert
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();
    }

    [TestMethod]
    public async Task Delete_WhenFileDoesNotExist_Works()
    {
        // Setup
        var document = await this.nsp.GetDocumentFromPathAsync("does-not-exist").ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        var act = async () => await document.DeleteAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().NotThrowAsync().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Delete_ThrowsStorageException_WhenItFails()
    {
        // Setup
        var document = await this.nsp.GetDocumentFromPathAsync("C:/FORBIDDEN").ConfigureAwait(false);

        // Act
        var act = async () => await document.DeleteAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().WithMessage("could not delete*").ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsInvalidPathException_IfNewNameIsNotSingleSegment()
    {
        // Setup
        const string documentPath = @"C:\file1";
        const string newName = @"invalid\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);

        // Act
        var act = async () => await document.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsInvalidPathException_IfNewNameIsInvalid()
    {
        // Setup
        const string documentPath = @"C:\file1";
        const string newName = "|folder1|";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);

        // Act
        var act = async () => await document.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsTargetExistsException_IfNewNameIsExistingFolder()
    {
        // Setup
        const string documentPath = @"C:\file1";
        const string newName = "folder1";
        var target = await this.nsp.GetFolderFromPathAsync($@"C:\{newName}").ConfigureAwait(false);
        _ = (await target.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);

        // Act
        var act = async () => await document.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<TargetExistsException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsTargetExistsException_IfNewNameIsExistingFile()
    {
        // Setup
        const string documentPath = @"C:\file1";
        const string newName = "file2";
        var target = await this.nsp.GetDocumentFromPathAsync($@"C:\{newName}").ConfigureAwait(false);
        _ = (await target.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);

        // Act
        var act = async () => await document.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<TargetExistsException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_UsingSameName_Works()
    {
        // Setup
        const string documentPath = @"C:\file1";
        const string newName = "file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);

        // Act
        await document.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        var referenceDocument = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = document.Name.Should().Be(referenceDocument.Name);
        _ = document.Location.Should().Be(referenceDocument.Location);
        _ = document.ParentPath.Should().Be(referenceDocument.ParentPath);
    }

    [TestMethod]
    public async Task Rename_Works_WhenDocumentDoesNotExist()
    {
        // Setup
        const string documentPath = @"c:\not-existing";
        const string newName = "new-not-existing";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        await document.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        var referenceDocument = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = document.Name.Should().Be(newName);
        _ = document.Location.Should().Be(this.nsp.NormalizeRelativeTo(referenceDocument.ParentPath, newName));
        _ = document.ParentPath.Should().Be(referenceDocument.ParentPath);
    }

    [TestMethod]
    public async Task Rename_Works_WhenDocumentExists()
    {
        // Setup
        const string documentPath = @"c:\file1";
        const string newName = "new-file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        await document.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        var referenceDocument = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = document.Name.Should().Be(newName);
        _ = document.Location.Should().Be(this.nsp.NormalizeRelativeTo(referenceDocument.ParentPath, newName));
        _ = document.ParentPath.Should().Be(referenceDocument.ParentPath);
    }

    [TestMethod]
    public async Task Rename_ThrowsStorageException_OnPlatformError()
    {
        // Setup
        const string documentPath = @"C:\FORBIDDEN";
        const string newName = "NEW-FORBIDDEN";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await document.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("new-name", false)]
    [DataRow("new-name", true)]
    public async Task Copy_WhenDocumentDoesNotExit_ThrowsItemNotFoundException(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = "not-existing";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var act = async () => await CopyAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<ItemNotFoundException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("file1", false)]
    [DataRow("file1", true)]
    public async Task Copy_ToSelf_ShouldReturnSelf(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(document.ParentPath).ConfigureAwait(false);

        // Act
        var moved = await CopyAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = moved.Should().Be(document);
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("new-name", false)]
    [DataRow("new-name", true)]
    public async Task Copy_WhenCancellationTokenIsTriggered_NoDocumentIsCopied(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var cancellationTokenSource = new CancellationTokenSource();
        await cancellationTokenSource.CancelAsync().ConfigureAwait(false);
        var act = async ()
            => await CopyAsyncWrapper(document, targetFolder, newName, overwrite, cancellationTokenSource.Token)
                .ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        var targetPath = this.nsp.NormalizeRelativeTo(targetFolder.Location, newName ?? document.Name);
        _ = this.fs.File.Exists(targetPath).Should().BeFalse();
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("new-name", false)]
    [DataRow("new-name", true)]
    public async Task Copy_WhenCopyFails_ThrowsStorageException(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\FORBIDDEN";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var act = async () => await CopyAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
        var targetPath = this.nsp.NormalizeRelativeTo(targetFolder.Location, newName ?? document.Name);
        _ = this.fs.File.Exists(targetPath).Should().BeFalse();
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("new-name", false)]
    [DataRow("new-name", true)]
    public async Task Copy_WhenTargetFolderDoesNotExist_CreatesIt(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target\auto").ConfigureAwait(false);
        _ = (await targetFolder.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        var moved = await CopyAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = (await moved.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var content = await this.fs.File.ReadAllTextAsync(moved.Location).ConfigureAwait(false);
        _ = content.Should().Be(File1Content);
    }

    [TestMethod]
    [DataRow(@"copy-target\NewNameCanBePath", false)]
    [DataRow(@"copy-target\NewNameCanBePath", true)]
    public async Task Copy_NewNameCanBePath(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\").ConfigureAwait(false);

        // Act
        var moved = await CopyAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = (await moved.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var content = await this.fs.File.ReadAllTextAsync(moved.Location).ConfigureAwait(false);
        _ = content.Should().Be(File1Content);
    }

    [TestMethod]
    [DataRow("|INVALID|", false)]
    [DataRow("|INVALID|", true)]
    public async Task Copy_InvalidNewName_ThrowsInvalidPathException(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\").ConfigureAwait(false);

        // Act
        var act = async () => await CopyAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Copy_TargetExistsNoOverwrite_ThrowsTargetExistsException()
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var act = async ()
            => await CopyAsyncWrapper(document, targetFolder, "existing", overwrite: false).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<TargetExistsException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Copy_TargetExistsOverwrite_Overwrites()
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var moved = await CopyAsyncWrapper(document, targetFolder, "existing", overwrite: true).ConfigureAwait(false);

        // Assert
        _ = (await moved.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var content = await this.fs.File.ReadAllTextAsync(moved.Location).ConfigureAwait(false);
        _ = content.Should().Be(File1Content);
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("new-name", false)]
    [DataRow("new-name", true)]
    public async Task Move_WhenDocumentDoesNotExit_ThrowsItemNotFoundException(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = "not-existing";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var act = async () => await MoveAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<ItemNotFoundException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("file1", false)]
    [DataRow("file1", true)]
    public async Task Move_ToSelf_ShouldReturnSelf(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(document.ParentPath).ConfigureAwait(false);

        // Act
        await MoveAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("new-name", false)]
    [DataRow("new-name", true)]
    public async Task Move_WhenCancellationTokenIsTriggered_NoDocumentIsCopied(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var cancellationTokenSource = new CancellationTokenSource();
        await cancellationTokenSource.CancelAsync().ConfigureAwait(false);
        var act = async ()
            => await MoveAsyncWrapper(document, targetFolder, newName, overwrite, cancellationTokenSource.Token)
                .ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        var targetPath = this.nsp.NormalizeRelativeTo(targetFolder.Location, newName ?? document.Name);
        _ = this.fs.File.Exists(targetPath).Should().BeFalse();
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("new-name", false)]
    [DataRow("new-name", true)]
    public async Task Move_WhenMoveFails_ThrowsStorageException(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\FORBIDDEN";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var act = async () => await MoveAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
        var targetPath = this.nsp.NormalizeRelativeTo(targetFolder.Location, newName ?? document.Name);
        _ = this.fs.File.Exists(targetPath).Should().BeFalse();
    }

    [TestMethod]
    [DataRow(null, false)]
    [DataRow(null, true)]
    [DataRow("new-name", false)]
    [DataRow("new-name", true)]
    public async Task Move_WhenTargetFolderDoesNotExist_CreatesIt(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target\auto").ConfigureAwait(false);
        _ = (await targetFolder.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        await MoveAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = document.ParentPath.Should().Be(targetFolder.Location);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var content = await this.fs.File.ReadAllTextAsync(document.Location).ConfigureAwait(false);
        _ = content.Should().Be(File1Content);
    }

    [TestMethod]
    [DataRow(@"copy-target\NewNameCanBePath", false)]
    [DataRow(@"copy-target\NewNameCanBePath", true)]
    public async Task Move_NewNameCanBePath(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\").ConfigureAwait(false);

        // Act
        await MoveAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = document.Name.Should().Be("NewNameCanBePath");
        _ = document.ParentPath.Should().Be(targetFolder.Location);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var content = await this.fs.File.ReadAllTextAsync(document.Location).ConfigureAwait(false);
        _ = content.Should().Be(File1Content);
    }

    [TestMethod]
    [DataRow("|INVALID|", false)]
    [DataRow("|INVALID|", true)]
    public async Task Move_InvalidNewName_ThrowsInvalidPathException(string? newName, bool overwrite)
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\").ConfigureAwait(false);

        // Act
        var act = async () => await MoveAsyncWrapper(document, targetFolder, newName, overwrite).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Move_TargetExistsNoOverwrite_ThrowsTargetExistsException()
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        var act = async ()
            => await MoveAsyncWrapper(document, targetFolder, "existing", overwrite: false).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<TargetExistsException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Move_TargetExistsOverwrite_Overwrites()
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var targetFolder = await this.nsp.GetFolderFromPathAsync(@"c:\copy-target").ConfigureAwait(false);

        // Act
        await MoveAsyncWrapper(document, targetFolder, "existing", overwrite: true).ConfigureAwait(false);

        // Assert
        _ = document.ParentPath.Should().Be(targetFolder.Location);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var content = await this.fs.File.ReadAllTextAsync(document.Location).ConfigureAwait(false);
        _ = content.Should().Be(File1Content);
    }

    [TestMethod]
    public async Task ReadAllText_ReturnsTextContentOfFile()
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var content = await document.ReadAllTextAsync().ConfigureAwait(false);

        // Assert
        _ = content.Should().Be(File1Content);
    }

    [TestMethod]
    public async Task ReadAllText_WhenErrorOccurs_ThrowsStorageException()
    {
        // Setup
        const string documentPath = @"C:\FORBIDDEN";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await document.ReadAllTextAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task WriteAllText_WhenDocumentExists_OverwritesContent()
    {
        // Setup
        const string documentPath = @"C:\file1";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var originalContent = await document.ReadAllTextAsync().ConfigureAwait(false);
        _ = originalContent.Should().Be(File1Content);
        const string newContent = "new content";

        // Act
        await document.WriteAllTextAsync(newContent).ConfigureAwait(false);

        // Assert
        var writtentContent = await document.ReadAllTextAsync().ConfigureAwait(false);
        _ = writtentContent.Should().Be(newContent);
    }

    [TestMethod]
    public async Task WriteAllText_DocumentNotExisting_CreatesDocumentAndWrites()
    {
        // Setup
        const string documentPath = @"C:\not-existing";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();
        const string content = "content";

        // Act
        await document.WriteAllTextAsync(content).ConfigureAwait(false);

        // Assert
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        var writtentContent = await document.ReadAllTextAsync().ConfigureAwait(false);
        _ = writtentContent.Should().Be(content);
    }

    [TestMethod]
    public async Task ReadAllText_WhenWriteFails_ThrowsStorageException()
    {
        // Setup
        const string documentPath = @"C:\FORBIDDEN";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await document.WriteAllTextAsync("content").ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task ReadAllText_DocumentNotExisting_ThrowsItemNotFoundException()
    {
        // Setup
        const string documentPath = "not-existing";
        var document = await this.nsp.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        var act = async () => await document.ReadAllTextAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<ItemNotFoundException>().ConfigureAwait(false);
    }

    private static Task<IDocument> CopyAsyncWrapper(
        IDocument document,
        IFolder targetFolder,
        string? newName,
        bool overwrite,
        CancellationToken cancellationToken = default)
    {
        if (newName is null)
        {
            return overwrite
                ? document.CopyOverwriteAsync(targetFolder, cancellationToken)
                : document.CopyAsync(targetFolder, cancellationToken);
        }

        return overwrite
            ? document.CopyOverwriteAsync(targetFolder, newName, cancellationToken)
            : document.CopyAsync(targetFolder, newName, cancellationToken);
    }

    private static Task MoveAsyncWrapper(
        IDocument document,
        IFolder targetFolder,
        string? newName,
        bool overwrite,
        CancellationToken cancellationToken = default)
    {
        if (newName is null)
        {
            return overwrite
                ? document.MoveOverwriteAsync(targetFolder, cancellationToken)
                : document.MoveAsync(targetFolder, cancellationToken);
        }

        return overwrite
            ? document.MoveOverwriteAsync(targetFolder, newName, cancellationToken)
            : document.MoveAsync(targetFolder, newName, cancellationToken);
    }

    private sealed class CustomAccessControlStrategy(Func<string, bool> accessController) : IAccessControlStrategy
    {
        /// <inheritdoc cref="IsAccessGranted(string, IFileSystemExtensibility)" />
        public bool IsAccessGranted(string fullPath, IFileSystemExtensibility extensibility)
            => accessController(fullPath);
    }
}
