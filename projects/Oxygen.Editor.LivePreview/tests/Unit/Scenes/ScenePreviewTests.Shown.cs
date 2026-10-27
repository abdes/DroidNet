// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>
/// Checks that the preview shows the document the user activates (model editor 6): any open document that shows a
/// scene, a model's preview scene included, and nothing for a document it does not follow.
/// </summary>
public sealed partial class ScenePreviewTests
{
    [TestMethod]
    public async Task Show_AnOpenDocumentNotShown_ShowsIt()
    {
        using var fixture = new PreviewFixture();
        var first = await fixture.Documents.OpenAsync(new Scene { Name = "First" }.Dehydrate()).ConfigureAwait(false);
        var second = await fixture.Documents.OpenAsync(new Scene { Name = "Second" }.Dehydrate()).ConfigureAwait(false);

        var shown = fixture.Preview.Show(first.Id);

        _ = shown.Should().BeTrue();
        _ = fixture.Preview.IsReady(first.Id).Should().BeTrue();
        _ = fixture.Preview.IsReady(second.Id).Should().BeFalse("the engine shows one scene: the active one");
        _ = fixture.World.Activations.Select(static target => target.SceneId).Last().Should().Be(PreviewFixture.SceneOf(first).Id);
    }

    [TestMethod]
    public async Task Show_TheShownDocument_RebuildsNothing()
    {
        using var fixture = new PreviewFixture();
        var document = await fixture.Documents.OpenAsync(new Scene { Name = "Shown" }.Dehydrate()).ConfigureAwait(false);
        var activations = fixture.World.Activations.Count;

        _ = fixture.Preview.Show(document.Id).Should().BeTrue();

        _ = fixture.World.Activations.Should().HaveCount(activations);
    }

    [TestMethod]
    public void Show_ADocumentNotOpen_IsRefused()
    {
        using var fixture = new PreviewFixture();

        _ = fixture.Preview.Show(DocumentId.New()).Should().BeFalse();
        _ = fixture.World.Activations.Should().BeEmpty();
    }

    [TestMethod]
    public async Task Show_AModelDocument_ShowsItsPreviewScene()
    {
        using var fixture = new PreviewFixture();
        var harbor = await fixture.Documents.OpenAsync(new Scene { Name = "Harbor" }.Dehydrate()).ConfigureAwait(false);
        var car = await fixture.OpenModelAsync(AssetPath.Parse("/Content/Car.glb")).ConfigureAwait(false);
        var preview = fixture.Scenes.Of(car)!.Scene;

        _ = fixture.Preview.Show(harbor.Id).Should().BeTrue();
        _ = fixture.Preview.Show(car.Id).Should().BeTrue();

        _ = fixture.Preview.IsReady(car.Id).Should().BeTrue();
        _ = fixture.Preview.IsReady(harbor.Id).Should().BeFalse();
        _ = fixture.World.Activations.Select(static target => target.SceneId).Last().Should().Be(preview.Id);
    }
}
