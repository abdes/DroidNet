// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed class RuntimeAssetFailureTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task NativeFailure_RetainsGenerationAndDiscardsSupersededDelivery()
    {
        var native = CreateTransport();
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
        var target = new RuntimeSceneTarget(sut.RunId, SceneId.New(), Guid.NewGuid(), Guid.NewGuid());
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Scene", this.TestContext.CancellationToken).ConfigureAwait(false);
        var first = new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetGeometry(NodeId.New(), "/Content/Missing.ogeo"));
        var second = first with { OperationId = Guid.NewGuid() };
        var failures = new List<RuntimeAssetLoadFailedEventArgs>();
        sut.AssetLoadFailed += (_, args) => failures.Add(args);

        _ = sut.Execute(first, this.TestContext.CancellationToken);
        native.Raise(value => value.AssetLoadFailed += null, new RuntimeAssetLoadFailedEventArgs(first, 42, "First failure"));
        _ = sut.Execute(second, this.TestContext.CancellationToken);
        native.Raise(value => value.AssetLoadFailed += null, new RuntimeAssetLoadFailedEventArgs(first, 42, "Late first failure"));
        native.Raise(value => value.AssetLoadFailed += null, new RuntimeAssetLoadFailedEventArgs(second, 43, "Current failure"));

        _ = failures.Should().HaveCount(2);
        _ = failures[1].Generation.Should().Be(43);
        _ = failures[1].Request.Should().BeSameAs(second);
        _ = sut.AssetRequests.Should().ContainSingle().Which.Succeeded.Should().BeFalse();
        _ = sut.IsCurrentAssetFailure(failures[1]).Should().BeTrue();
        native.Raise(value => value.AssetLoadSucceeded += null, new RuntimeAssetLoadSucceededEventArgs(second, 44));
        _ = sut.AssetRequests.Should().ContainSingle().Which.Succeeded.Should().BeTrue();
        _ = sut.IsCurrentAssetFailure(failures[1]).Should().BeFalse("a successful native retry supersedes queued failure feedback");
        native.Raise(value => value.AssetLoadFailed += null, new RuntimeAssetLoadFailedEventArgs(second, 43, "Old generation"));
        _ = sut.AssetRequests.Should().ContainSingle().Which.Succeeded.Should().BeTrue();
        _ = sut.IsCurrentAssetRequest(first).Should().BeFalse("a queued UI diagnostic must recheck after replacement");
        _ = sut.IsCurrentAssetRequest(second).Should().BeTrue();
        sut.EndRun();
        _ = sut.AssetRequests.Should().BeEmpty();
        native.Raise(value => value.AssetLoadFailed += null, new RuntimeAssetLoadFailedEventArgs(second, 43, "After shutdown"));
        _ = failures.Should().HaveCount(2);
    }

    [TestMethod]
    public async Task MaterialFailures_AreScopedToSurfaceAndInvalidatedByDetach()
    {
        var native = CreateTransport();
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
        var target = new RuntimeSceneTarget(sut.RunId, SceneId.New(), Guid.NewGuid(), Guid.NewGuid());
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Scene", this.TestContext.CancellationToken).ConfigureAwait(false);
        var node = NodeId.New();
        var first = new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetOverrides(node, "/Content/Mesh.ogeo", new RuntimeOverridePart(Guid.NewGuid(), new string('0', 64)), [new RuntimeOverride(RuntimeOverrideId.GeometrySurfaceMaterial, new RuntimeOverrideValue.Asset("/Content/First.omat"))], OverrideIntent.ObservedEdit));
        var otherSurface = new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetOverrides(node, "/Content/Mesh.ogeo", new RuntimeOverridePart(Guid.NewGuid(), new string('0', 64)), [new RuntimeOverride(RuntimeOverrideId.GeometrySurfaceMaterial, new RuntimeOverrideValue.Asset("/Content/Other.omat"))], OverrideIntent.ObservedEdit));
        _ = sut.Execute(first, this.TestContext.CancellationToken);
        _ = sut.Execute(otherSurface, this.TestContext.CancellationToken);
        _ = sut.Execute(first with { OperationId = Guid.NewGuid() }, this.TestContext.CancellationToken);

        _ = sut.IsCurrentAssetRequest(first).Should().BeFalse();
        _ = sut.IsCurrentAssetRequest(otherSurface).Should().BeTrue();
        _ = sut.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeDetachGeometry(node)), this.TestContext.CancellationToken);
        _ = sut.IsCurrentAssetRequest(otherSurface).Should().BeFalse();
        _ = sut.AssetRequests.Should().BeEmpty();
    }

    [TestMethod]
    public async Task MaterialRequestsKeepGeometryContextWhenSurfaceIdsMatch()
    {
        var native = CreateTransport();
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
        var target = new RuntimeSceneTarget(sut.RunId, SceneId.New(), Guid.NewGuid(), Guid.NewGuid());
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Scene", this.TestContext.CancellationToken).ConfigureAwait(false);
        var node = NodeId.New();
        var surface = Guid.NewGuid();
        var current = new RuntimeWorldRequest(
            Guid.NewGuid(),
            target,
            new RuntimeSetOverrides(node, "/Content/Sphere.ogeo", new RuntimeOverridePart(surface, new string('a', 64)), [new RuntimeOverride(RuntimeOverrideId.GeometrySurfaceMaterial, new RuntimeOverrideValue.Asset("/Content/Red.omat"))], OverrideIntent.RetainedOverride));
        var foreign = new RuntimeWorldRequest(
            Guid.NewGuid(),
            target,
            new RuntimeSetOverrides(node, "/Content/Cube.ogeo", new RuntimeOverridePart(surface, new string('a', 64)), [new RuntimeOverride(RuntimeOverrideId.GeometrySurfaceMaterial, new RuntimeOverrideValue.Asset("/Content/Blue.omat"))], OverrideIntent.RetainedOverride));
        _ = sut.Execute(current, this.TestContext.CancellationToken);
        _ = sut.Execute(foreign, this.TestContext.CancellationToken);
        native.Raise(value => value.AssetLoadFailed += null, new RuntimeAssetLoadFailedEventArgs(foreign, 2, "Different geometry"));
        native.Raise(value => value.AssetLoadSucceeded += null, new RuntimeAssetLoadSucceededEventArgs(current, 1));

        _ = sut.IsCurrentAssetRequest(current).Should().BeTrue();
        _ = sut.AssetRequests.Single(status => status.Request == current).Succeeded.Should().BeTrue();
        _ = sut.AssetRequests.Single(status => status.Request == foreign).Succeeded.Should().BeFalse();
        sut.EndRun();
    }

    private static Mock<IRuntimeCommandTransport> CreateTransport()
    {
        var transport = new Mock<IRuntimeCommandTransport>(MockBehavior.Strict);
        _ = transport.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        _ = transport.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>()));
        return transport;
    }
}
