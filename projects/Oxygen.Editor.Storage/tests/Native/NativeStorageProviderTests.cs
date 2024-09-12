// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Testably.Abstractions.Testing;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Native Storage")]
public class NativeStorageProviderTests
{
    private readonly MockFileSystem fs;
    private readonly NativeStorageProvider nsp;

    public NativeStorageProviderTests()
    {
        this.fs = new MockFileSystem();
        _ = this.fs.Initialize()
            .WithSubdirectory("folder")
            .Initialized(
                d => d.WithFile("file_with_no_extension")
                    .WithFile("project_file.oxy")
                    .WithSubdirectory("sub_folder_1")
                    .WithFile("other_file.xyz")
                    .WithSubdirectory("sub_folder_2"))
            .WithSubdirectory("folder.xyz");

        this.nsp = new NativeStorageProvider(this.fs);
    }

    [TestMethod]
    [DataRow("")]
    [DataRow("|/hello")]
    [DataRow("c:\\folder\afile")]
    public void Normalize_ShouldThrowInvalidPathException_WhenPathIsInvalid(string path)
    {
        // Act
        Action act = () => this.nsp.Normalize(path);

        // Assert
        _ = act.Should()
            .Throw<InvalidPathException>()
            .WithMessage($"path [{path}] could not be normalized*");
    }

    [TestMethod]
    public void Normalize_ShouldThrowInvalidPathException_WhenPathIsEmpty()
    {
        // Act
        Action act = () => this.nsp.Normalize(string.Empty);

        // Assert
        _ = act.Should().Throw<InvalidPathException>();
    }

    [TestMethod]
    public void Normalize_ShouldThrowInvalidPathException_WhenPathIsAllWhitespaces()
    {
        // Act
        Action act = () => this.nsp.Normalize("    ");

        // Assert
        _ = act.Should().Throw<InvalidPathException>();
    }

    [TestMethod]
    [DataRow("c:/", @"\")]
    [DataRow(@"C:\", @"\")]
    [DataRow(@"C:\\", @"\")]
    [DataRow(@"\hello\world\", @"\hello\world")]
    [DataRow(@"\\world\\\\", @"\world")]
    public void Normalize_ShouldStripTrailingDirectorySeparators_ButKeepAtLeastOne(string path, string normalized)
    {
        // Act
        var result = this.nsp.Normalize(path);

        // Assert
        // Note that to avoid the issues with drive letters, we just test for EndWith
        _ = result.Should().EndWith(normalized);
    }

    [TestMethod]
    [DataRow(@"C:\", @"\hello")]
    [DataRow("hello", @"C:\world")]
    [DataRow("hello", "C:world")]
    public void NormalizeRelativeTo_ShouldThrow_WhenRelativePartIsRooted(string basePath, string relativePath)
    {
        // Act
        var act = () => this.nsp.NormalizeRelativeTo(basePath, relativePath);

        // Assert
        // Note that to avoid the issues with drive letters, we just test for EndWith
        _ = act.Should()
            .Throw<InvalidPathException>()
            .WithMessage($"path [{relativePath}] is rooted*");
    }

    [TestMethod]
    [DataRow("hello", "|hello")]
    [DataRow("|hello", "world")]
    [DataRow("|hello", "|world")]
    public void NormalizeRelativeTo_ShouldThrow_WhenOneOfThePartsIsInvalid(string basePath, string relativePath)
    {
        // Act
        var act = () => this.nsp.NormalizeRelativeTo(basePath, relativePath);

        // Assert
        // Note that to avoid the issues with drive letters, we just test for EndWith
        _ = act.Should()
            .Throw<InvalidPathException>()
            .WithMessage("could not combine*");
    }

    [TestMethod]
    [DataRow(@"C:\", "hello", @"C:\hello")]
    [DataRow("hello", "world", @"\hello\world")]
    [DataRow(@"\hello\world\", @"..\good\world\", @"\hello\good\world")]
    public void NormalizeRelativeTo_ShouldCombineAndNormalize(string basePath, string relativePath, string normalized)
    {
        // Act
        var result = this.nsp.NormalizeRelativeTo(basePath, relativePath);

        // Assert
        // Note that to avoid the issues with drive letters, we just test for EndWith
        _ = result.Should().EndWith(normalized);
    }

    [TestMethod]
    public void LogicalDrivesCannotBeEmpty()
    {
        // Arrange
        _ = this.fs.Directory.GetLogicalDrives().Should().NotBeEmpty();

        // Act
        var drives = this.nsp.GetLogicalDrives()
            .ToArray();

        // Assert
        _ = drives.Should().NotBeEmpty();
        foreach (var drive in drives)
        {
            _ = drive.Should().NotBeEmpty();
        }
    }

    [TestMethod]
    public async Task GetFolderFromPathAsync_ShouldThrow_WhenPathRefersToExistingFile()
    {
        // Setup
        const string path = @"C:\folder\project_file.oxy";
        _ = this.fs.File.Exists(path).Should().BeTrue();

        // Act
        var act = async () => await this.nsp.GetFolderFromPathAsync(path).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow("|")]
    [DataRow("")]
    [DataRow("  ")]
    public async Task GetFolderFromPathAsync_ShouldThrow_WhenPathIsInvalid(string path)
    {
        // Act
        var act = async () => await this.nsp.GetFolderFromPathAsync(path).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(@"c:\", "c:")]
    [DataRow(@"c:\", @"c:\")]
    [DataRow("folder", @"c:\folder")]
    [DataRow("folder", @"c:\folder\")]
    [DataRow("folder.xyz", @"c:\folder.xyz")]
    [DataRow("folder.xyz", @"c:\folder.xyz\")]
    [DataRow(@"c:\", @"c:\folder/..")]
    [DataRow("sub_folder_1", @"c:\folder\sub_folder_1")]
    public async Task GetFolderFromPathAsync_ShouldReturnGoodFolderFromGoodPath(string name, string path)
    {
        var folder = await this.nsp.GetFolderFromPathAsync(path);
        _ = folder.Should().NotBeNull();
        _ = folder.Name.Should().BeEquivalentTo(name);
        _ = this.fs.Path.IsPathFullyQualified(folder.Location).Should().BeTrue();
    }

    [TestMethod]
    public async Task GetDocumentFromPathAsync_ShouldThrow_WhenPathRefersToExistingFolder()
    {
        // Setup
        const string path = @"C:\folder";
        _ = this.fs.Directory.Exists(path).Should().BeTrue();

        // Act
        var act = async () => await this.nsp.GetDocumentFromPathAsync(path).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(@"c:\hello\|world")]
    [DataRow(@"c:\")]
    [DataRow(@"c:\folder")]
    [DataRow("")]
    [DataRow("  ")]
    public async Task GetDocumentFromPathAsync_ShouldThrow_WhenPathIsInvalid(string path)
    {
        // Act
        var act = async () => await this.nsp.GetDocumentFromPathAsync(path).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidPathException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow("document", @"c:hello\document")]
    [DataRow("hello", @"c:\hello")]
    [DataRow("file_with_no_extension", @"c:\folder\file_with_no_extension")]
    [DataRow("project_file.oxy", @"c:\folder\project_file.oxy")]
    [DataRow("other_file.xyz", @"c:\folder\sub_folder_1\other_file.xyz")]
    public async Task GetDocumentFromPathAsync_ShouldReturnGoodDocumentFromGoodPath(string name, string path)
    {
        var folder = await this.nsp.GetDocumentFromPathAsync(path);
        _ = folder.Should().NotBeNull();
        _ = folder.Name.Should().BeEquivalentTo(name);
        _ = this.fs.Path.IsPathFullyQualified(folder.Location).Should().BeTrue();
    }

#if FALSE
    [TestMethod]
    public void GetItemsThrowsIfFolderDoesNotExist()
    {
        var act = async () =>
        {
            await foreach (var unused in this.nsp.GetItemsAsync("DOES_NOT_EXIST").ConfigureAwait(false))
            {
            }
        };

        _ = await act.Should().ThrowAsync<ArgumentException>();
    }

    [TestMethod]
    public async Task GetItemsWorksWithEmptyFolder()
    {
        var items = new List<IStorageItem>();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\folder\sub_folder_1"))
        {
            items.Add(item);
        }

        _ = items.Should().BeEmpty();
    }

    [TestMethod]
    public async Task GetItemsReturnsAllItems()
    {
        var items = new List<IStorageItem>();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\folder"))
        {
            _ = item.Should().BeAssignableTo<INestedItem>();
            items.Add(item);
        }

        _ = items.Should().HaveCount(5);
    }

    [TestMethod]
    public async Task GetItemsReturnsFoldersFirst()
    {
        var items = new List<IStorageItem>();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\folder"))
        {
            items.Add(item);
        }

        _ = items[0].Should().BeAssignableTo<IFolder>();
        _ = items[1].Should().BeAssignableTo<IFolder>();
        _ = items[2].Should().BeAssignableTo<IDocument>();
        _ = items[3].Should().BeAssignableTo<IDocument>();
        _ = items[4].Should().BeAssignableTo<IDocument>();
    }

    [TestMethod]
    public async Task GetItemsWorksWhenFolderHasNoFiles()
    {
        var items = new List<IStorageItem>();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\"))
        {
            items.Add(item);
        }

        _ = items.Should().HaveCount(3);
    }

    [TestMethod]
    public async Task GetItemsCanGetOnlyFiles()
    {
        var items = new List<IStorageItem>();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\", ProjectItemKind.File))
        {
            items.Add(item);
        }

        _ = items.Should().BeEmpty();

        items.Clear();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\folder", ProjectItemKind.File))
        {
            items.Add(item);
        }

        _ = items.Should().HaveCount(3);
    }

    [TestMethod]
    public async Task GetItemsCanGetOnlyFolders()
    {
        var items = new List<IStorageItem>();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\", ProjectItemKind.Folder))
        {
            items.Add(item);
        }

        _ = items.Should().HaveCount(3);

        items.Clear();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\folder", ProjectItemKind.Folder))
        {
            items.Add(item);
        }

        _ = items.Should().HaveCount(2);
    }
#endif
}
