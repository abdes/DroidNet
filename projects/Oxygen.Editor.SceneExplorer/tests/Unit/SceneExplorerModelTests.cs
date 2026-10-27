// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

/// <summary>
/// The Scene Explorer on a model (model editor 5.4): the preview scene's row, holding the model's root and its nodes,
/// without the sun that lights the preview, and the buttons that change the scene disabled with the model's reason.
/// </summary>
[TestClass]
public sealed class SceneExplorerModelTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task ModelDocument_ShowsThePreviewSceneHoldingTheModelWithoutItsSun()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(car)!.Scene;
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), new TestDocumentArea(car), documents.Open, scenes, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(preview).ConfigureAwait(false);

        var root = await explorer.FindAdapterByNodeIdAsync(preview.RootNodes[0].Id).ConfigureAwait(false);
        _ = root.Should().NotBeNull();
        _ = explorer.ShownItems.Should().Contain([explorer.Scene!, root!], "the preview scene's row holds the model's root");
        _ = explorer.Scene!.AttachedObject.Name.Should().NotBe(preview.RootNodes[0].Metadata.Name, "the preview and the model are told apart");
        _ = (await explorer.FindAdapterByNodeIdAsync(preview.RootNodes[1].Id).ConfigureAwait(false)).Should().BeNull("the sun lights the preview and is no part of the model");
    }

    [TestMethod]
    public async Task ModelDocument_DisablesWhatChangesTheSceneWithTheModelsReason()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(car)!;
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), new TestDocumentArea(car), documents.Open, scenes, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(preview.Scene).ConfigureAwait(false);

        _ = explorer.ReadOnlyReason.Should().NotBeNull().And.Be(preview.Authoring.ReadOnlyReason);
        _ = explorer.NewNodeAction.CanExecute(parameter: null).Should().BeFalse();
        _ = explorer.NewFolderAction.CanExecute(parameter: null).Should().BeFalse();
        _ = explorer.PasteAction.CanExecute(parameter: null).Should().BeFalse();
        _ = SceneExplorerViewModel.ToolTipOf(explorer.ReadOnlyReason, "New Node").Should().Be(explorer.ReadOnlyReason);
    }

    [TestMethod]
    public async Task SceneDocument_IsNotReadOnly()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var harbor = await documents.OpenAsync(new Scene { Name = "Harbor" }.Dehydrate()).ConfigureAwait(false);
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), new TestDocumentArea(harbor), documents.Open, scenes, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(SceneContent.Of(harbor).Scene).ConfigureAwait(false);

        _ = explorer.ReadOnlyReason.Should().BeNull();
        _ = explorer.NewNodeAction.CanExecute(parameter: null).Should().BeTrue();
        _ = SceneExplorerViewModel.ToolTipOf(explorer.ReadOnlyReason, "New Node").Should().Be("New Node");
    }

    [TestMethod]
    public async Task ActivatingTheModelThenTheScene_ShowsEachInTurn()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var harbor = await documents.OpenAsync(new Scene { Name = "Harbor" }.Dehydrate()).ConfigureAwait(false);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var area = new TestDocumentArea(harbor, car);
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), area, documents.Open, scenes, new SceneSelectionService());
        await explorer.HandleDocumentOpenedAsync(SceneContent.Of(harbor).Scene).ConfigureAwait(false);

        await this.ActivateAsync(explorer, area, car, scenes.Of(car)!.Scene).ConfigureAwait(false);
        _ = explorer.ReadOnlyReason.Should().NotBeNull();

        await this.ActivateAsync(explorer, area, harbor, SceneContent.Of(harbor).Scene).ConfigureAwait(false);
        _ = explorer.ReadOnlyReason.Should().BeNull();
    }

    // Activates a tab and waits for the explorer to show its scene.
    private async Task ActivateAsync(SceneExplorerViewModel explorer, TestDocumentArea area, Document document, Scene scene)
    {
        var shown = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnChanged(object? sender, PropertyChangedEventArgs args)
        {
            if (ReferenceEquals(explorer.Scene?.AttachedObject, scene))
            {
                _ = shown.TrySetResult();
            }
        }

        explorer.PropertyChanged += OnChanged;
        try
        {
            area.Activate(document);
            await shown.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
        }
        finally
        {
            explorer.PropertyChanged -= OnChanged;
        }
    }
}
