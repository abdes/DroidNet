// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>A scene node with a geometry component, and an asset feed that lists a geometry and a material that need cooking.</summary>
internal sealed class GeometryFixture : IDisposable
{
    public GeometryFixture()
    {
        this.Geometry = new()
        {
            Name = "Geometry",
            Geometry = EngineAssets.BasicShape("Cube"),
        };
        _ = this.Authoring.Node.AddComponent(this.Geometry);
        var geometry = CreateStatusAsset(0) with
        {
            Identity = this.GeometryAsset,
            DisplayName = "Custom",
            Kind = AssetKind.Geometry,
            DerivedState = null,
            CookStatus = new(this.GeometryAsset, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, OutputAvailability: CookedOutputAvailability.Missing, [], []),
        };
        var material = CreateStatusAsset(1) with
        {
            Identity = this.MaterialAsset,
            CookStatus = new(this.MaterialAsset, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, OutputAvailability: CookedOutputAvailability.Missing, [], []),
        };
        _ = this.Assets.SetupGet(value => value.Items).Returns(Observable.Return<IReadOnlyList<AssetEntry>>([geometry, material]));
        _ = this.Assets.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        _ = this.Assets.Setup(value => value.ResolveAsync(this.GeometryAsset, It.IsAny<CancellationToken>())).ReturnsAsync(geometry);
        _ = this.Assets.Setup(value => value.ResolveAsync(this.MaterialAsset, It.IsAny<CancellationToken>())).ReturnsAsync(material);
    }

    public SceneAuthoringFixture Authoring { get; } = new();

    public Mock<IAssetFeed> Assets { get; } = new();

    public AssetPath GeometryAsset { get; } = AssetPath.Parse("/Content/Geometry/Custom.ogeo");

    public AssetPath MaterialAsset { get; } = AssetPath.Parse("/Content/Materials/Custom.omat");

    public GeometryComponent Geometry { get; }

    public void Dispose() => this.Authoring.Dispose();
}
