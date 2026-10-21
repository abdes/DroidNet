// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.SceneInspector.SceneSettings;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

[TestClass]
public sealed class EnvironmentSchedulingTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task BackgroundAssetNotificationsUseTheInjectedSchedulerAndRetainRows()
    {
        using var source = new Subject<IReadOnlyList<AssetEntry>>();
        var scheduler = new HistoricalScheduler();
        var provider = CreateProvider(source);
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(assetProvider: provider.Object, observerScheduler: scheduler);
        model.SetScene(fixture.Scene);
        var deliveryThreads = new List<int>();
        model.Exposure.MeteringMaskChoices.Groups.CollectionChanged += (_, _) => deliveryThreads.Add(System.Environment.CurrentManagedThreadId);
        var original = CreateTexture("First");

        await Task.Run(() => source.OnNext([original]), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = MaskRows(model).Should().BeEmpty("notifications must wait for the supplied scheduler");
        var observerThread = System.Environment.CurrentManagedThreadId;
        scheduler.Start();
        _ = MaskRows(model).Should().ContainSingle();
        _ = deliveryThreads.Should().OnlyContain(thread => thread == observerThread);
        var row = MaskRows(model)[0];
        _ = row.Choice.Name.Should().Be("First");

        await Task.Run(() => source.OnNext([original with { DisplayName = "Renamed" }]), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = row.Choice.Name.Should().Be("First");
        scheduler.Start();
        _ = MaskRows(model)[0].Should().BeSameAs(row);
        _ = row.Choice.Name.Should().Be("Renamed");
        provider.Verify(value => value.RefreshAsync(It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public void DisposingTheModelCancelsQueuedAndFutureAssetNotifications()
    {
        using var source = new Subject<IReadOnlyList<AssetEntry>>();
        var scheduler = new HistoricalScheduler();
        var provider = CreateProvider(source);
        using var fixture = new SceneAuthoringFixture();
        var model = new EnvironmentViewModel(assetProvider: provider.Object, observerScheduler: scheduler);
        model.SetScene(fixture.Scene);
        source.OnNext([CreateTexture("Queued")]);

        model.Dispose();
        source.OnNext([CreateTexture("After disposal")]);
        scheduler.Start();

        _ = MaskRows(model).Should().BeEmpty();
        _ = source.HasObservers.Should().BeFalse();
    }

    [TestMethod]
    public void StandaloneModelReceivesAssetsWithoutAnyUiScheduler()
    {
        using var source = new Subject<IReadOnlyList<AssetEntry>>();
        var provider = CreateProvider(source);
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(assetProvider: provider.Object);
        model.SetScene(fixture.Scene);

        source.OnNext([CreateTexture("Synchronous")]);

        _ = MaskRows(model).Should().ContainSingle();
        _ = MaskRows(model)[0].Choice.Name.Should().Be("Synchronous");
    }

    private static IReadOnlyList<AssetChoiceRow> MaskRows(EnvironmentViewModel model)
        => [.. model.Exposure.MeteringMaskChoices.Groups.SelectMany(static group => group.Rows)];

    private static Mock<IAssetFeed> CreateProvider(IObservable<IReadOnlyList<AssetEntry>> items)
    {
        var provider = new Mock<IAssetFeed>(MockBehavior.Strict);
        _ = provider.SetupGet(value => value.Items).Returns(items);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        return provider;
    }

    private static AssetEntry CreateTexture(string name)
        => new(
            AssetPath.Parse("/Textures/metering-mask.otex"),
            name,
            AssetKind.Texture,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Mounted,
            "Textures/metering-mask",
            SourcePath: null,
            DescriptorPath: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true);
}
