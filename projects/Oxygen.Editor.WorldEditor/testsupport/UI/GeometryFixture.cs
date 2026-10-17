// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>A scene node with a geometry component, and an asset feed that lists a geometry and a material that need cooking.</summary>
internal sealed class GeometryFixture : IDisposable
{
    public GeometryFixture()
    {
        var info = new ProjectInfo("Geometry", Category.Games, "C:/Geometry", "preview.png");
        _ = Mock.Get(this.Authoring.Scene.Project).SetupGet(value => value.ProjectInfo).Returns(info);
        this.Authoring.Projects.Activate(ProjectContext.FromProjectInfo(info));
        this.Geometry = new()
        {
            Name = "Geometry",
            Geometry = new(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        };
        _ = this.Authoring.Node.AddComponent(this.Geometry);
        var geometry = CreateStatusAsset(0) with
        {
            IdentityUri = this.GeometryUri,
            DisplayName = "Custom",
            Kind = AssetKind.Geometry,
            DerivedState = null,
            CookStatus = new(this.GeometryUri, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, OutputAvailability: CookedOutputAvailability.Missing, [], [], []),
        };
        var material = CreateStatusAsset(1) with
        {
            IdentityUri = this.MaterialUri,
            CookStatus = new(this.MaterialUri, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, OutputAvailability: CookedOutputAvailability.Missing, [], [], []),
        };
        _ = this.Assets.SetupGet(value => value.Items).Returns(Observable.Return<IReadOnlyList<AssetEntry>>([geometry, material]));
        _ = this.Assets.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        _ = this.Assets.Setup(value => value.ResolveAsync(this.GeometryUri, It.IsAny<CancellationToken>())).ReturnsAsync(geometry);
        _ = this.Assets.Setup(value => value.ResolveAsync(this.MaterialUri, It.IsAny<CancellationToken>())).ReturnsAsync(material);
    }

    public SceneAuthoringFixture Authoring { get; } = new();

    public Mock<IAssetFeed> Assets { get; } = new();

    public Uri GeometryUri { get; } = new("asset:///Content/Geometry/Custom.ogeo.json");

    public Uri MaterialUri { get; } = new("asset:///Content/Materials/Custom.omat.json");

    public GeometryComponent Geometry { get; }

    public void Dispose() => this.Authoring.Dispose();
}
