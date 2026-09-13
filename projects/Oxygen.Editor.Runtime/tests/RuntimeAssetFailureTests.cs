// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;

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
        var target = new RuntimeSceneTarget(sut.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Scene", this.TestContext.CancellationToken).ConfigureAwait(false);
        var first = new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetGeometry(Guid.NewGuid(), "/Content/Missing.ogeo"));
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
    public async Task MaterialFailures_AreScopedToSlotAndInvalidatedByDetach()
    {
        var native = CreateTransport();
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
        var target = new RuntimeSceneTarget(sut.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Scene", this.TestContext.CancellationToken).ConfigureAwait(false);
        var node = Guid.NewGuid();
        var first = new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetMaterialOverride(node, 0, "/Content/First.omat"));
        var otherSlot = new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetMaterialOverride(node, 1, "/Content/Other.omat"));
        _ = sut.Execute(first, this.TestContext.CancellationToken);
        _ = sut.Execute(otherSlot, this.TestContext.CancellationToken);
        _ = sut.Execute(first with { OperationId = Guid.NewGuid() }, this.TestContext.CancellationToken);

        _ = sut.IsCurrentAssetRequest(first).Should().BeFalse();
        _ = sut.IsCurrentAssetRequest(otherSlot).Should().BeTrue();
        _ = sut.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeDetachGeometry(node)), this.TestContext.CancellationToken);
        _ = sut.IsCurrentAssetRequest(otherSlot).Should().BeFalse();
        _ = sut.AssetRequests.Should().BeEmpty();
    }

    private static Mock<IRuntimeCommandTransport> CreateTransport()
    {
        var transport = new Mock<IRuntimeCommandTransport>(MockBehavior.Strict);
        _ = transport.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        _ = transport.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>()));
        return transport;
    }
}
