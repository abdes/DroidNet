// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed class EnvironmentSchedulingTests
{
    [TestMethod]
    public async Task BackgroundAssetNotificationsUseTheInjectedSchedulerAndRetainRows()
    {
        using var source = new Subject<IReadOnlyList<ContentBrowserAssetItem>>();
        var scheduler = new HistoricalScheduler();
        var provider = CreateProvider(source);
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(assetProvider: provider.Object, observerScheduler: scheduler);
        model.SetScene(fixture.Scene);
        var deliveryThreads = new List<int>();
        model.MeteringMaskRows.CollectionChanged += (_, _) => deliveryThreads.Add(Environment.CurrentManagedThreadId);
        var original = CreateTexture("First");

        await Task.Run(() => source.OnNext([original])).ConfigureAwait(false);
        _ = model.MeteringMaskRows.Should().BeEmpty("notifications must wait for the supplied scheduler");
        var observerThread = Environment.CurrentManagedThreadId;
        scheduler.Start();
        _ = model.MeteringMaskRows.Should().ContainSingle();
        _ = deliveryThreads.Should().OnlyContain(thread => thread == observerThread);
        var row = model.MeteringMaskRows[0];
        _ = row.Item.Name.Should().Be("First");

        await Task.Run(() => source.OnNext([original with { DisplayName = "Renamed" }])).ConfigureAwait(false);
        _ = row.Item.Name.Should().Be("First");
        scheduler.Start();
        _ = model.MeteringMaskRows[0].Should().BeSameAs(row);
        _ = row.Item.Name.Should().Be("Renamed");
        provider.Verify(value => value.RefreshAsync(AssetBrowserFilter.Default, It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public void DisposingTheModelCancelsQueuedAndFutureAssetNotifications()
    {
        using var source = new Subject<IReadOnlyList<ContentBrowserAssetItem>>();
        var scheduler = new HistoricalScheduler();
        var provider = CreateProvider(source);
        using var fixture = new SceneAuthoringFixture();
        var model = new EnvironmentViewModel(assetProvider: provider.Object, observerScheduler: scheduler);
        model.SetScene(fixture.Scene);
        source.OnNext([CreateTexture("Queued")]);

        model.Dispose();
        source.OnNext([CreateTexture("After disposal")]);
        scheduler.Start();

        _ = model.MeteringMaskRows.Should().BeEmpty();
        _ = source.HasObservers.Should().BeFalse();
    }

    [TestMethod]
    public void StandaloneModelReceivesAssetsWithoutAnyUiScheduler()
    {
        using var source = new Subject<IReadOnlyList<ContentBrowserAssetItem>>();
        var provider = CreateProvider(source);
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(assetProvider: provider.Object);
        model.SetScene(fixture.Scene);

        source.OnNext([CreateTexture("Synchronous")]);

        _ = model.MeteringMaskRows.Should().ContainSingle();
        _ = model.MeteringMaskRows[0].Item.Name.Should().Be("Synchronous");
    }

    private static Mock<IContentBrowserAssetProvider> CreateProvider(IObservable<IReadOnlyList<ContentBrowserAssetItem>> items)
    {
        var provider = new Mock<IContentBrowserAssetProvider>(MockBehavior.Strict);
        _ = provider.SetupGet(value => value.Items).Returns(items);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        return provider;
    }

    private static ContentBrowserAssetItem CreateTexture(string name)
        => new(
            new Uri("asset:///Textures/metering-mask.otex"),
            name,
            AssetKind.Texture,
            AssetState.Descriptor,
            null,
            AssetRuntimeAvailability.Mounted,
            "Textures/metering-mask",
            null,
            null,
            null,
            null,
            null,
            [],
            IsSelectable: true);
}
