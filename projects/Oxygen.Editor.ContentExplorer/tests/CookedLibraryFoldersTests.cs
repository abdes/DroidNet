// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Checks which rows a browsed cooked-library folder shows.</summary>
[TestClass]
public sealed partial class CookedLibraryFoldersTests
{
    /// <summary>A folder outside every library shows the resolved rows unchanged.</summary>
    [TestMethod]
    public void OrdinaryFolderKeepsTheResolvedRows()
    {
        using var fixture = new Fixture();
        IReadOnlyList<AssetEntry> items = [fixture.Shared];
        _ = CookedLibraryFolders.ForFolders(items, fixture.Project, [AssetPath.Parse("/Content")]).Should().BeSameAs(items);
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-LibraryFolders-");

        public Fixture()
        {
            var output = this.WriteLibrary($".cooked/generations/{Guid.NewGuid():N}");
            this.First = this.WriteLibrary("Libraries/First");
            var second = this.WriteLibrary("Libraries/Second");
            this.Project = TestProject.Create(
                new RealFileSystem(),
                this.directory.FullName,
                [new ProjectMount("Content", MountKind.Source, "Content"), new("First", MountKind.Cooked, "Libraries/First"), new("Second", MountKind.Cooked, "Libraries/Second")]).Context with
            {
                Name = "Libraries",
            };
            var identity = AssetPath.Parse("/Content/Shared.omat");
            this.Shared = new(identity, "Shared", AssetKind.Material, AssetState.Cooked, DerivedState: null, AssetRuntimeAvailability.Unknown, identity.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true)
            {
                CookedMetadata = Metadata(output),
                OverriddenCookedSources = [Metadata(this.First), Metadata(second)],
            };
        }

        public FileSystemPath First { get; }

        public ProjectContext Project { get; }

        public AssetEntry Shared { get; }

        public void Dispose() => this.directory.Delete(recursive: true);

        private static CookedAssetMetadata Metadata(FileSystemPath root)
            => new(root, "payloads/shared.bin", Guid.NewGuid(), new(1, 2), 1, 6, new string('0', 64)) { VirtualPath = AssetPath.Parse("/Content/Shared.omat") };

        private FileSystemPath WriteLibrary(string relative)
        {
            var root = Path.GetFullPath(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(Path.Combine(root, "payloads"));
            File.WriteAllBytes(Path.Combine(root, "payloads", "shared.bin"), [1]);
            File.WriteAllBytes(Path.Combine(root, "container.index.bin"), []);
            return FileSystemPath.From(root);
        }
    }
}
