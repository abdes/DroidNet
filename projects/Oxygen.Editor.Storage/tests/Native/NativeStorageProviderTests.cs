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
    [DataRow("")]
    [DataRow("does_not_exist")]
    [DataRow(@"c:\folder\file")]
    public void NullFolderFromInvalidPath(string path)
    {
        // Act
        var act = () => this.nsp.GetFolderFromPathAsync(path);

        // Assert
        _ = act.Should().ThrowAsync<ArgumentException>();
    }

    [TestMethod]
    [DataRow(@"c:\", "c:")]
    [DataRow(@"c:\", @"c:\")]
    [DataRow("folder", @"c:\folder")]
    [DataRow("folder", @"c:\folder\")]
    [DataRow("folder.xyz", @"c:\folder.xyz")]
    [DataRow("folder.xyz", @"c:\folder.xyz\")]
    [DataRow(@"c:\", @"c:\folder/..")]
    [DataRow(@"sub_folder_1", @"c:\folder\sub_folder_1")]
    public async Task GoodFolderFromGoodPath(string name, string path)
    {
        var folder = await this.nsp.GetFolderFromPathAsync(path);
        _ = folder.Should().NotBeNull();
        _ = folder.Name.Should().BeEquivalentTo(name);
        _ = folder.Location.Should().BeEquivalentTo(this.fs.Path.GetFullPath(path));
    }

    [TestMethod]
    public void GetItemsThrowsIfFolderDoesNotExist()
    {
        var act = async () =>
        {
            await foreach (var unused in this.nsp.GetItemsAsync("DOES_NOT_EXIST").ConfigureAwait(false))
            {
            }
        };

        _ = act.Should().ThrowAsync<ArgumentException>();
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

    [TestMethod]
    public async Task GetItemsCanFilterFiles()
    {
        var items = new List<IStorageItem>();
        await foreach (var item in this.nsp.GetItemsAsync(@"c:\folder", ProjectItemKind.ProjectManifest))
        {
            items.Add(item);
        }

        _ = items.Should().HaveCount(1);

        var document = items[0] as IDocument;
        _ = document.Should().NotBeNull();
        _ = document!.Name.Should().BeEquivalentTo("project_file.oxy");
    }
}
