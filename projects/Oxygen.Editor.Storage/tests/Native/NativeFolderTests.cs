// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Oxygen.Editor.Storage.Native;
using Testably.Abstractions.Helpers;
using Testably.Abstractions.Testing;
using Testably.Abstractions.Testing.FileSystem;

namespace Oxygen.Editor.Storage.Tests.Native;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Native Storage")]
public class NativeFolderTests
{
    private readonly MockFileSystem fs;
    private readonly NativeStorageProvider nsp;

    public NativeFolderTests()
    {
        this.fs = new MockFileSystem();
        _ = this.fs.WithAccessControlStrategy(
                new CustomAccessControlStrategy(path => !path.Contains("FORBIDDEN", StringComparison.Ordinal)))
            .Initialize()
            .WithSubdirectory("c:/DELETE")
            .WithSubdirectory("c:/FORBIDDEN")
            .WithFile("c:/file1")
            .WithSubdirectory("c:/folder1")
            .Initialized(
                d => d.WithFile("folder1-file1")
                    .WithFile("folder1-file2")
                    .WithSubdirectory("folder1-subfolder1")
                    .WithFile("folder1-file3")
                    .WithSubdirectory("folder1-subfolder2"))
            .WithSubdirectory("c:/folder2")
            .Initialized(
                d => d.WithFile("FORBIDDEN-FILE")
                    .WithSubdirectory("FORBIDDEN-FOLDER"))
            .WithSubdirectory("c:/DELETE-RECURSIVE")
            .Initialized(
                d => d.WithFile("f1")
                    .WithFile("f2")
                    .WithSubdirectory("d1")
                    .Initialized(
                        sd => sd.WithFile("f")
                            .WithSubdirectory("d")));

        this.nsp = new NativeStorageProvider(this.fs);
    }

    [TestMethod]
    public async Task LastAccessTime_GetsTheDateTimeFolderWasLastAccessed()
    {
        // Setup
        const string path = "c:/folder1";
        var folder = await this.nsp.GetFolderFromPathAsync(path).ConfigureAwait(false);
        var exists = await folder.ExistsAsync().ConfigureAwait(false);
        _ = exists.Should().BeTrue();
        var platformLastAccessTime = this.fs.Directory.GetLastAccessTime(path);

        // Act
        var result = folder.LastAccessTime;

        // Assert
        _ = result.Should().Be(platformLastAccessTime);
    }

    [TestMethod]
    public async Task LastAccessTime_ReturnsEarliestFileTime_WhenFolderDoesNotExist()
    {
        // Setup
        const string path = "does-not-exist";
        var folder = await this.nsp.GetFolderFromPathAsync(path).ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        var result = folder.LastAccessTime;

        // Assert
        _ = result.Should().Be(DateTime.FromFileTime(0));
    }

    [TestMethod]
    public Task LastAccessTime_ReturnsEarliestFileTime_WhenItFails()
    {
        // Setup

        // Manually create a NativeFolder with invalid path to avoid the checks
        // made by the storage provider.
        var folder = new NativeFolder(this.nsp, "|invalid|", "|invalid|");

        // Act
        var result = folder.LastAccessTime;

        // Assert
        _ = result.Should().Be(DateTime.FromFileTime(0));
        return Task.CompletedTask;
    }

    [TestMethod]
    [DataRow("c:/folder1", true)]
    [DataRow("c:/folder2", true)]
    [DataRow("c:/folder1/folder1-subfolder1", true)]
    [DataRow("c:/folder1/folder1-subfolder2", true)]
    [DataRow("does-not-exist", false)]
    [DataRow("c:/folder1/does-not-exist", false)]
    [DataRow("c:/does-not-exist/does-not-exist", false)]
    [DataRow("c:/folder2/FORBIDDEN-FOLDER", true)]
    public async Task Exists_ChecksIfFolderExists(string path, bool exists)
    {
        // Setup
        var folder = await this.nsp.GetFolderFromPathAsync(path).ConfigureAwait(false);

        // Act
        var result = await folder.ExistsAsync().ConfigureAwait(false);

        // Assert
        _ = result.Should().Be(exists);
    }

    [TestMethod]
    public async Task Delete_Works()
    {
        // Setup
        var folder = await this.nsp.GetFolderFromPathAsync("C:/DELETE").ConfigureAwait(false);
        var exists = await folder.ExistsAsync().ConfigureAwait(false);
        _ = exists.Should().BeTrue();

        // Act
        await folder.DeleteAsync().ConfigureAwait(false);

        // Assert
        exists = await folder.ExistsAsync().ConfigureAwait(false);
        _ = exists.Should().BeFalse();
    }

    [TestMethod]
    public async Task Delete_WhenFolderDoesNotExist_Works()
    {
        // Setup
        var folder = await this.nsp.GetFolderFromPathAsync("does-not-exist").ConfigureAwait(false);

        // Act
        var act = async () => await folder.DeleteAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().NotThrowAsync().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Delete_WhenFolderNotEmpty_Fails()
    {
        // Setup
        var folder = await this.nsp.GetFolderFromPathAsync("C:/DELETE-RECURSIVE").ConfigureAwait(false);
        var exists = await folder.ExistsAsync().ConfigureAwait(false);
        _ = exists.Should().BeTrue();

        // Act
        var act = async () => await folder.DeleteAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Delete_ThrowsStorageException_WhenItFails()
    {
        // Setup
        var folder = await this.nsp.GetFolderFromPathAsync("C:/FORBIDDEN").ConfigureAwait(false);

        // Act
        var act = async () => await folder.DeleteAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().WithMessage("could not delete*").ConfigureAwait(false);
    }

    [TestMethod]
    public async Task DeleteRecursive_Works()
    {
        // Setup
        var folder = await this.nsp.GetFolderFromPathAsync("C:/DELETE-RECURSIVE").ConfigureAwait(false);
        var exists = await folder.ExistsAsync().ConfigureAwait(false);
        _ = exists.Should().BeTrue();

        // Act
        await folder.DeleteRecursiveAsync().ConfigureAwait(false);

        // Assert
        exists = await folder.ExistsAsync().ConfigureAwait(false);
        _ = exists.Should().BeFalse();
    }

    [TestMethod]
    public async Task DeleteRecursive_WhenFolderDoesNotExist_Works()
    {
        // Setup
        var folder = await this.nsp.GetFolderFromPathAsync("does-not-exist").ConfigureAwait(false);
        var exists = await folder.ExistsAsync().ConfigureAwait(false);
        _ = exists.Should().BeFalse();

        // Act
        var act = async () => await folder.DeleteRecursiveAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().NotThrowAsync().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task DeleteRecursive_ThrowsStorageException_WhenItFails()
    {
        // Setup
        var folder = await this.nsp.GetFolderFromPathAsync("C:/FORBIDDEN").ConfigureAwait(false);

        // Act
        var act = async () => await folder.DeleteRecursiveAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should()
            .ThrowAsync<StorageException>()
            .WithMessage("could not recursively delete*")
            .ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GetDocument_ThrowsInvalidPathException_WhenDocumentNameIsInvalid()
    {
        // Setup
        const string documentName = "|INVALID|";
        var folder = await this.nsp.GetFolderFromPathAsync("c:/folder1").ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await folder.GetDocumentAsync(documentName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GetDocument_ThrowsInvalidPathException_WhenDocumentNameRefersToExistingFolder()
    {
        // Setup
        const string documentName = "folder1-subfolder1";
        var folder = await this.nsp.GetFolderFromPathAsync("c:/folder1").ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await folder.GetDocumentAsync(documentName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GetDocument_Works_WhenDocumentNameRefersToExistingFile()
    {
        // Setup
        const string documentName = "folder1-file1";
        var folder = await this.nsp.GetFolderFromPathAsync("c:/folder1").ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var document = await folder.GetDocumentAsync(documentName).ConfigureAwait(false);

        // Assert
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        _ = document.Name.Should().Be(documentName);
        _ = document.Location.Should().Be(@"c:\folder1\folder1-file1");
        _ = document.ParentPath.Should().Be(@"c:\folder1");
    }

    [TestMethod]
    public async Task GetDocument_Works_WhenDocumentNameRefersToNonExistingFile()
    {
        // Setup
        const string documentName = "new-file";
        var folder = await this.nsp.GetFolderFromPathAsync("c:/folder1").ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var document = await folder.GetDocumentAsync(documentName).ConfigureAwait(false);

        // Assert
        _ = (await document.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = document.Name.Should().Be(documentName);
        _ = document.Location.Should().Be(@"c:\folder1\new-file");
        _ = document.ParentPath.Should().Be(@"c:\folder1");
    }

    [TestMethod]
    public async Task GetFolder_ThrowsInvalidPathException_WhenFolderNameIsInvalid()
    {
        // Setup
        const string folderName = "|INVALID|";
        var folder = await this.nsp.GetFolderFromPathAsync("c:/folder1").ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await folder.GetFolderAsync(folderName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GetFolder_ThrowsInvalidPathException_WhenFolderNameRefersToExistingFolder()
    {
        // Setup
        const string folderName = "folder1-file1";
        var folder = await this.nsp.GetFolderFromPathAsync("c:/folder1").ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await folder.GetFolderAsync(folderName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GetFolder_Works_WhenFolderNameRefersToExistingFile()
    {
        // Setup
        const string folderName = "folder1-subfolder1";
        var folder = await this.nsp.GetFolderFromPathAsync("c:/folder1").ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var subfolder = await folder.GetFolderAsync(folderName).ConfigureAwait(false);

        // Assert
        _ = (await subfolder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();
        _ = subfolder.Name.Should().Be(folderName);
        _ = subfolder.Location.Should().Be(@"c:\folder1\folder1-subfolder1");
        _ = ((INestedFolder)subfolder).ParentPath.Should().Be(@"c:\folder1");
    }

    [TestMethod]
    public async Task GetFolder_Works_WhenFolderNameRefersToNonExistingFile()
    {
        // Setup
        const string folderName = "new-folder";
        var folder = await this.nsp.GetFolderFromPathAsync("c:/folder1").ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var subfolder = await folder.GetFolderAsync(folderName).ConfigureAwait(false);

        // Assert
        _ = (await subfolder.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = subfolder.Name.Should().Be(folderName);
        _ = subfolder.Location.Should().Be(@"c:\folder1\new-folder");
        _ = ((INestedFolder)subfolder).ParentPath.Should().Be(@"c:\folder1");
    }

    [TestMethod]
    public async Task Create_Works_IfFolderDoesNotExist()
    {
        // Setup
        const string folderPath = @"C:\NEW-FOLDER";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        await folder.CreateAsync().ConfigureAwait(false);

        // Assert
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Cleanup
        await folder.DeleteAsync().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Create_Works_IfFolderExists()
    {
        // Setup
        const string folderPath = @"c:\folder1";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await folder.CreateAsync().ConfigureAwait(false);

        // Assert
        _ = act.Should().NotThrowAsync();
    }

    [TestMethod]
    public async Task Create_ThrowsStorageException_WhenItFails()
    {
        // Setup
        const string folderPath = @"C:\FORBIDDEN\new-folder";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        var act = async () => await folder.CreateAsync().ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsStorageException_IfFolderIsNotNested()
    {
        // Setup
        const string folderPath = @"C:\";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);

        // Act
        var act = async () => await folder.RenameAsync(@"C:\").ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsInvalidPathException_IfNewNameIsNotSingleSegment()
    {
        // Setup
        const string folderPath = @"C:\folder1";
        const string newName = @"invalid\folder1";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);

        // Act
        var act = async () => await folder.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsInvalidPathException_IfNewNameIsInvalid()
    {
        // Setup
        const string folderPath = @"C:\folder1";
        const string newName = "|folder1|";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);

        // Act
        var act = async () => await folder.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsTargetExistsException_IfNewNameIsExistingFolder()
    {
        // Setup
        const string folderPath = @"C:\folder1";
        const string newName = "folder2";
        var target = await this.nsp.GetFolderFromPathAsync($@"C:\{newName}").ConfigureAwait(false);
        _ = (await target.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);

        // Act
        var act = async () => await folder.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<TargetExistsException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsTargetExistsException_IfNewNameIsExistingFile()
    {
        // Setup
        const string folderPath = @"C:\folder1";
        const string newName = "file1";
        var target = await this.nsp.GetDocumentFromPathAsync($@"C:\{newName}").ConfigureAwait(false);
        _ = (await target.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);

        // Act
        var act = async () => await folder.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<TargetExistsException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_UsingSameName_Works()
    {
        // Setup
        const string folderPath = @"c:\folder1";
        const string newName = "folder1";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);

        // Act
        await folder.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        var referenceFolder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = folder.Name.Should().Be(referenceFolder.Name);
        _ = folder.Location.Should().Be(referenceFolder.Location);
        _ = ((INestedFolder)folder).ParentPath.Should().Be(((INestedFolder)referenceFolder).ParentPath);
    }

    [TestMethod]
    public async Task Rename_Works_WhenFolderDoesNotExist()
    {
        // Setup
        const string folderPath = @"c:\not-existing";
        const string newName = "new-not-existing";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeFalse();

        // Act
        await folder.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        var referenceFolder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = folder.Name.Should().Be(newName);
        _ = folder.Location.Should()
            .Be(this.nsp.NormalizeRelativeTo(((INestedFolder)referenceFolder).ParentPath, newName));
        _ = ((INestedFolder)folder).ParentPath.Should().Be(((INestedFolder)referenceFolder).ParentPath);
    }

    [TestMethod]
    public async Task Rename_Works_WhenFolderExists()
    {
        // Setup
        const string folderPath = @"c:\existing";
        const string newName = "new-existing";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = folder.CreateAsync().ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        await folder.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        var referenceFolder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = folder.Name.Should().Be(newName);
        _ = folder.Location.Should()
            .Be(this.nsp.NormalizeRelativeTo(((INestedFolder)referenceFolder).ParentPath, newName));
        _ = ((INestedFolder)folder).ParentPath.Should().Be(((INestedFolder)referenceFolder).ParentPath);

        // Cleanup
        _ = folder.DeleteAsync().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Rename_ThrowsStorageException_OnPlatformError()
    {
        // Setup
        const string folderPath = @"C:\FORBIDDEN";
        const string newName = "NEW-FORBIDDEN";
        var folder = await this.nsp.GetFolderFromPathAsync(folderPath).ConfigureAwait(false);
        _ = (await folder.ExistsAsync().ConfigureAwait(false)).Should().BeTrue();

        // Act
        var act = async () => await folder.RenameAsync(newName).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GetDocuments_ShouldReturnAllDocumentsInFolder()
    {
        // Arrange
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\folder1").ConfigureAwait(false);

        // Act
        var documents = new List<IDocument>();
        await foreach (var document in folder.GetDocumentsAsync().ConfigureAwait(false))
        {
            documents.Add(document);
        }

        // Assert
        _ = documents.Should()
            .HaveCount(3)
            .And.Contain(d => d.Name == "folder1-file1")
            .And.Contain(d => d.Name == "folder1-file2")
            .And.Contain(d => d.Name == "folder1-file3");
    }

    [TestMethod]
    public async Task GetDocuments_ShouldThrowStorageException_WhenEnumerationFails()
    {
        // Arrange
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\non-existing").ConfigureAwait(false);

        // Act
        var act = async () =>
        {
            await foreach (var document in folder.GetDocumentsAsync().ConfigureAwait(false))
            {
                _ = document;
            }
        };

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().WithMessage("cannot enumerate*").ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GetDocuments_ShouldStopEnumeration_WhenCancellationIsRequested()
    {
        // Arrange
        using var cancellationTokenSource = new CancellationTokenSource();
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\folder1", cancellationTokenSource.Token)
            .ConfigureAwait(false);

        // Act
        var documents = new List<IDocument>();
        await foreach (var document in folder.GetDocumentsAsync(cancellationTokenSource.Token).ConfigureAwait(false))
        {
            documents.Add(document);
            await cancellationTokenSource.CancelAsync().ConfigureAwait(false);
        }

        // Assert
        _ = documents.Should().HaveCount(1);
    }

    [TestMethod]
    public async Task GetDocuments_ShouldSkipInaccessibleDocuments()
    {
        // Arrange
        using var cancellationTokenSource = new CancellationTokenSource();
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\folder2", cancellationTokenSource.Token)
            .ConfigureAwait(false);

        // Act
        var documents = new List<IDocument>();
        await foreach (var document in folder.GetDocumentsAsync(cancellationTokenSource.Token).ConfigureAwait(false))
        {
            documents.Add(document);
        }

        // Assert
        _ = documents.Should().BeEmpty();
    }

    [TestMethod]
    public async Task GetFolders_ShouldReturnAllSubfoldersInFolder()
    {
        // Arrange
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\folder1").ConfigureAwait(false);

        // Act
        var subfolders = new List<IFolder>();
        await foreach (var subfolder in folder.GetFoldersAsync().ConfigureAwait(false))
        {
            subfolders.Add(subfolder);
        }

        // Assert
        _ = subfolders.Should()
            .HaveCount(2)
            .And.Contain(d => d.Name == "folder1-subfolder1")
            .And.Contain(d => d.Name == "folder1-subfolder2");
    }

    [TestMethod]
    public async Task GetFolders_ShouldThrowStorageException_WhenEnumerationFails()
    {
        // Arrange
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\non-existing").ConfigureAwait(false);

        // Act
        var act = async () =>
        {
            await foreach (var subfolder in folder.GetFoldersAsync().ConfigureAwait(false))
            {
                _ = subfolder;
            }
        };

        // Assert
        _ = await act.Should().ThrowAsync<StorageException>().WithMessage("cannot enumerate*").ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GetFolders_ShouldStopEnumeration_WhenCancellationIsRequested()
    {
        // Arrange
        using var cancellationTokenSource = new CancellationTokenSource();
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\folder1", cancellationTokenSource.Token)
            .ConfigureAwait(false);

        // Act
        var subfolders = new List<IFolder>();
        await foreach (var subfolder in folder.GetFoldersAsync(cancellationTokenSource.Token).ConfigureAwait(false))
        {
            subfolders.Add(subfolder);
            await cancellationTokenSource.CancelAsync().ConfigureAwait(false);
        }

        // Assert
        _ = subfolders.Should().HaveCount(1);
    }

    [TestMethod]
    public async Task GetFolders_ShouldSkipInaccessibleFolders()
    {
        // Arrange
        using var cancellationTokenSource = new CancellationTokenSource();
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\folder2", cancellationTokenSource.Token)
            .ConfigureAwait(false);

        // Act
        var subfolders = new List<IDocument>();
        await foreach (var subfolder in folder.GetDocumentsAsync(cancellationTokenSource.Token).ConfigureAwait(false))
        {
            subfolders.Add(subfolder);
        }

        // Assert
        _ = subfolders.Should().BeEmpty();
    }

    [TestMethod]
    public async Task GetItems_ShouldReturnAllItemsInFolderExceptInaccessible_FoldersFirst()
    {
        // Arrange
        var folder = await this.nsp.GetFolderFromPathAsync(@"C:\folder1").ConfigureAwait(false);

        // Act
        var items = new List<IStorageItem>();
        await foreach (var item in folder.GetItemsAsync().ConfigureAwait(false))
        {
            items.Add(item);
        }

        // Assert
        _ = items.Should()
            .HaveCount(5)
            .And.Contain(d => d.Name == "folder1-subfolder1")
            .And.Contain(d => d.Name == "folder1-subfolder2")
            .And.Contain(d => d.Name == "folder1-file1")
            .And.Contain(d => d.Name == "folder1-file2")
            .And.Contain(d => d.Name == "folder1-file3");

        _ = items[0].Should().BeAssignableTo<IFolder>();
        _ = items[1].Should().BeAssignableTo<IFolder>();
        _ = items[2].Should().BeAssignableTo<IDocument>();
        _ = items[3].Should().BeAssignableTo<IDocument>();
        _ = items[4].Should().BeAssignableTo<IDocument>();
    }

    private sealed class CustomAccessControlStrategy(Func<string, bool> accessController) : IAccessControlStrategy
    {
        /// <inheritdoc cref="IsAccessGranted(string, IFileSystemExtensibility)" />
        public bool IsAccessGranted(string fullPath, IFileSystemExtensibility extensibility)
            => accessController(fullPath);
    }
}
