// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentExplorer.TestSupport;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;
using Testably.Abstractions.Testing;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.ContentFolders;

/// <summary>
/// The Content mounts dialog shows what a picked library is before it is added, and an Invalid library's finding; the
/// sources tree shows a library's folders as those of what it holds.
/// </summary>
[TestClass]
public sealed class AddLibraryTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private const string ProjectRoot = @"C:\Studio\Game";

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Picking a pak shows its name, form and needs with a warning for what is missing, and Add stages it.</summary>
    /// <returns>The asynchronous rendered-form test.</returns>
    [TestMethod]
    public Task TheFormShowsWhatAPickedLibraryIs() => EnqueueAsync(async () =>
    {
        var project = CreateProject();
        WritePak(project.FileSystem, @"C:\Paks\Props.pak", "Props", "/Props/Crate.omat", "/Vehicles/Paint.omat");
        var model = new ContentMountsViewModel(project, Mock.Of<IDialogService>(), Mock.Of<IEngineContentPak>(pak => pak.Path == FileSystemPath.From(@"C:\SDK\engine.pak")));
        var view = new ContentMountsView { ViewModel = model, Width = 900, Height = 800 };
        await LoadTestContentAsync(view).ConfigureAwait(true);

        model.SelectedKind = ContentMountsViewModel.LibraryKind;
        model.NewMountPath = @"C:\Paks\Props.pak";
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = view.FindDescendant<TextBlock>(static text => text.Text == "Props").Should().NotBeNull("the name comes from the file");
        _ = view.FindDescendant<TextBlock>(static text => text.Text == "Vehicles").Should().NotBeNull();
        _ = view.FindDescendants().OfType<InfoBar>().Should().Contain(static bar => bar.IsOpen && bar.Severity == InfoBarSeverity.Warning && bar.Message.Contains("Vehicles", StringComparison.Ordinal));
        _ = view.FindDescendants().OfType<TextBox>().Should().NotContain(static box => box.Visibility == Visibility.Visible && (string)box.Header == "Mount point name", "a library's name is never typed");

        model.AddMountCommand.Execute(parameter: null);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = model.Mounts.Should().Contain(static row => row.Name == "Props" && row.Kind == ContentMountKind.Library);
        _ = model.Candidate.Should().BeNull();
    });

    /// <summary>A library's row has no rename, and an Invalid one shows its finding in words and by code.</summary>
    /// <returns>The asynchronous rendered-row test.</returns>
    [TestMethod]
    public Task AnInvalidLibrarysRowShowsItsFinding() => EnqueueAsync(async () =>
    {
        var project = CreateProject(new ProjectMount("Kit", MountKind.Cooked, "../../Paks/Props.pak"));
        WritePak(project.FileSystem, @"C:\Paks\Props.pak", "Props", "/Props/Crate.omat");
        var opened = TestProject.Open(TestProject.InfoOf(project), project.FileSystem).Context;
        var model = new ContentMountsViewModel(opened, Mock.Of<IDialogService>(), Mock.Of<IEngineContentPak>(pak => pak.Path == FileSystemPath.From(@"C:\SDK\engine.pak")));
        var view = new ContentMountsView { ViewModel = model, Width = 900, Height = 800 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = view.FindDescendant<TextBlock>(static text => text.Text == "Invalid: holds another root").Should().NotBeNull();
        _ = view.FindDescendant<TextBlock>(static text => text.Text == MountCodes.CookedRootMismatch).Should().NotBeNull();
    });

    /// <summary>A loose library's tree node shows the folders of what it holds, whose paths are its assets'; an Invalid one stays, dimmed.</summary>
    /// <returns>The asynchronous real-browser workflow.</returns>
    [TestMethod]
    public Task LibrariesAreTreeNodesOverWhatTheyHold() => EnqueueAsync(async () =>
    {
        using var fixture = new ExplorerRevealFixture();
        var root = fixture.Session.Context.ProjectRoot.FullPath;
        var props = Path.Combine(root, "Libs", "Props");
        _ = Directory.CreateDirectory(props);
        await File.WriteAllBytesAsync(Path.Combine(props, "container.index.bin"), CookedLibraryFixture.LooseIndex("/Props/Furniture/Chair.omat"), this.TestContext.CancellationToken).ConfigureAwait(true);
        var kit = Path.Combine(root, "Libs", "Kit");
        _ = Directory.CreateDirectory(kit);
        await File.WriteAllBytesAsync(Path.Combine(kit, "container.index.bin"), CookedLibraryFixture.LooseIndex("/Other/Box.omat"), this.TestContext.CancellationToken).ConfigureAwait(true);
        var info = TestProject.InfoOf(fixture.Session.Context);
        info.Mounts.Add(new ProjectMount("Props", MountKind.Cooked, "Libs/Props"));
        info.Mounts.Add(new ProjectMount("Kit", MountKind.Cooked, "Libs/Kit"));
        fixture.Session.ChangeContext(TestProject.Open(info, new RealFileSystem()).Context);

        await fixture.OpenAsync().ConfigureAwait(true);

        var tree = fixture.Folders.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
        var children = await tree.Children.ConfigureAwait(true);
        var library = children.OfType<LibraryMountTreeItemAdapter>().Single(static mount => mount.Label == "Props");
        var folder = (await library.Children.ConfigureAwait(true)).OfType<CookedFolderTreeItemAdapter>().Single();
        _ = folder.VirtualPath.Should().Be(AssetPath.Parse("/Props/Furniture"), "a folder's path is the path of what it holds");
        var invalid = children.OfType<UnavailableMountTreeItemAdapter>().Single(static mount => mount.Label == "Kit");
        _ = invalid.Annotation.Should().Be("Invalid");
        _ = invalid.StateToolTip.Should().Contain("Other");
    });

    private static ProjectContext CreateProject(params ProjectMount[] extra)
    {
        var fileSystem = new MockFileSystem();
        return TestProject.Create(fileSystem, ProjectRoot, [new("Content", MountKind.Source, "Content"), .. extra]).Context;
    }

    private static void WritePak(System.IO.Abstractions.IFileSystem files, string path, string library, string asset, string? needs = null)
    {
        _ = files.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = new MemoryStream();
        PakFixture.Write(stream, new PakDocument(1, Guid.CreateVersion7(), new string('0', 64), [new PakAssetEntry(AssetKey.FromVirtualPath(asset), (byte)AssetType.Material, asset, 0, 0)])
        {
            Library = library,
            Dependencies = needs is null ? [] : [new LibraryDependency(needs.Split('/')[1], [new LibraryRequirement(IsTexture: false, AssetKey.FromVirtualPath(needs), (byte)AssetType.Material)])],
        });
        files.File.WriteAllBytes(path, stream.ToArray());
    }
}
