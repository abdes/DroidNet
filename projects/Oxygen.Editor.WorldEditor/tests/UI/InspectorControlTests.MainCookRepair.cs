// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Hosting.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Moq;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.World.Inspector;
using Oxygen.Managed.Core.Diagnostics;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises the original Main scene repair through controls, saved-input gating and native publication.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A captured -1 remains in failure history while a saved repair to 100 becomes current and survives reopening.</summary>
    /// <returns>The complete Main repair journey.</returns>
    [TestMethod]
    public Task MainCookRepairPreservesFailureAndPublishesSavedOneHundred() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene =>
        {
            scene.Name = "Main";
            AddGeometryNode(scene, "Cube");
            var environment = scene.Environment;
            scene.Hydrate(scene.Dehydrate() with { Environment = environment with { SkyAtmosphere = environment.SkyAtmosphere with { AerialPerspectiveStartDepthMeters = 0 } } });
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(90));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        fixture.Context.Metadata.Title = "Main";
        _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        using var services = new CatalogWorkloadServices(fixture);
        using var catalog = new ProjectAssetCatalog(services.Projects, services.Storage, services.Builtins);
        using var publication = fixture.RegisterWorkspacePublication(services, catalog);
        using var document = fixture.RegisterMainCookDocument(services);
        var uri = new Uri("asset:///Content/Scenes/Main.oscene.json");
        _ = (await services.Pipeline.CookCurrentSceneAsync(uri, timeout.Token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
        var originalOutput = ReadPublishedHashes(fixture.ProjectRoot);
        await fixture.SetSavedLegacyAerialStartAsync().ConfigureAwait(true);
        var actions = CreateMainRepairActions(fixture);
        using var panel = new CookingPanelViewModel(services.Runs, services.Pipeline, services.Projects, actions.Object, CreateStatusHosting());
        var environment = new EnvironmentView { ViewModel = fixture.Model };
        var cooking = new CookingPanelView { ViewModel = panel };
        var root = CreateMainRepairSurface(environment, cooking);
        using var host = new ScaledXamlHost();
        await host.LoadAsync(root, 1, timeout.Token).ConfigureAwait(true);
        var failed = await services.Pipeline.CookCurrentSceneAsync(uri, timeout.Token).ConfigureAwait(true);
        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = ReadPublishedHashes(fixture.ProjectRoot).Should().BeEquivalentTo(originalOutput);
        await CheckMainRepairControlsAsync(fixture, services, panel, cooking, environment, failed.OperationId, timeout.Token).ConfigureAwait(true);
        _ = (await services.Pipeline.ReadAsync(services.Projects.ActiveProject!, [uri], timeout.Token).ConfigureAwait(true)).Single().Freshness.Should().Be(AssetCookFreshness.Current);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        _ = fixture.Source.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(100);
        _ = (await fixture.ReadNativeAsync(timeout.Token).ConfigureAwait(true)).AerialPerspectiveStartDepthMeters.Should().Be(100);
        _ = services.Runs.Runs.Single(run => run.OperationId == failed.OperationId).Messages.Should().Contain(message => message.Text.Contains("= -1", StringComparison.Ordinal));
        actions.Verify(value => value.GoToPropertyAsync(It.IsAny<CookRunSnapshot>(), It.IsAny<DiagnosticRecord>()), Times.Once());
        actions.Verify(value => value.SaveListedAsync(It.IsAny<CookRunSnapshot>()), Times.Once());
        await this.CaptureComponentLayoutAsync(root, "main-cook-repair.png").ConfigureAwait(true);
    });

    private static Grid CreateMainRepairSurface(EnvironmentView environment, CookingPanelView cooking)
    {
        var root = new Grid { Width = 1200, Height = 650, RequestedTheme = ElementTheme.Dark, Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.Colors.Black) };
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(380) });
        root.ColumnDefinitions.Add(new ColumnDefinition());
        root.Children.Add(new ScrollViewer { Content = environment });
        Grid.SetColumn(cooking, 1);
        root.Children.Add(cooking);
        return root;
    }

    private static Mock<ICookingWorkspaceActions> CreateMainRepairActions(NativeSceneFixture fixture)
    {
        var actions = new Mock<ICookingWorkspaceActions>();
        _ = actions.Setup(value => value.GoToPropertyAsync(It.IsAny<CookRunSnapshot>(), It.IsAny<DiagnosticRecord>())).Returns<CookRunSnapshot, DiagnosticRecord>((run, issue) =>
        {
            _ = run.ProjectId.Should().Be(fixture.Source.Project.ProjectInfo.Id);
            fixture.Model.RequestFieldFocus(issue.SuggestedAction!.Payload["PropertyPath"]);
            return Task.FromResult(true);
        });
        _ = actions.Setup(value => value.SaveListedAsync(It.IsAny<CookRunSnapshot>())).Returns<CookRunSnapshot>(async run =>
        {
            _ = run.UnsavedDocuments.Should().ContainSingle().Which.DisplayName.Should().Be("Main");
            return (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(true)).Succeeded;
        });
        return actions;
    }

    private static async Task CheckMainRepairControlsAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, CookingPanelViewModel panel, CookingPanelView cooking, EnvironmentView environment, Guid failedId, CancellationToken cancellationToken)
    {
        while (panel.SelectedRun?.Snapshot is not { State: CookRunState.Failed } displayed || displayed.OperationId != failedId)
        {
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }

        _ = panel.SelectedRun.Snapshot.Messages.Should().Contain(message => message.Text.Contains("= -1", StringComparison.Ordinal));
        _ = panel.SelectedRun.Snapshot.Diagnostics.Should().Contain(issue => issue.Message.Contains("0 m", StringComparison.Ordinal));
        await WaitForRenderAsync().ConfigureAwait(true);
        var link = cooking.FindDescendant<HyperlinkButton>(button => string.Equals(button.Content as string, "Go to property", StringComparison.Ordinal))!;
        ((IInvokeProvider)new HyperlinkButtonAutomationPeer(link).GetPattern(PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);
        var number = environment.FindDescendant<NumberBox>(element => Equals(element.Tag, "AerialPerspectiveStartDepthMeters"))!;
        _ = number.NumberValue.Should().Be(-1);
        _ = FocusManager.GetFocusedElement(environment.XamlRoot).Should().Be(number);
        _ = panel.SelectedRun.Snapshot.OperationId.Should().Be(failedId);
        await EnterTextAsync(number, "-2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await fixture.Model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Source.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(-1);
        await EnterTextAsync(number, "100").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await fixture.Model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.Metadata.IsDirty.Should().BeTrue();
        var retry = panel.RetryCommand.ExecuteAsync(parameter: null);
        await WaitForBlockedMaterialAsync(panel, fixture.Context.DocumentId, cancellationToken).ConfigureAwait(true);
        var resumedId = panel.SelectedRun!.Snapshot.OperationId;
        await InvokeCookingSaveAsync(panel, cooking, cancellationToken).ConfigureAwait(true);
        await retry.WaitAsync(cancellationToken).ConfigureAwait(true);
        _ = services.Runs.Runs.Single(run => run.OperationId == resumedId).State.Should().BeOneOf(CookRunState.Succeeded, CookRunState.SucceededWithWarnings);
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    }

    private sealed partial class NativeSceneFixture
    {
        public ICookDocumentRegistration RegisterMainCookDocument(CatalogWorkloadServices services)
        {
            var source = this.manager.GetSceneSourceVersion(this.Source)!;
            return services.Documents.Register(source.SourcePath, token => this.hosting.Dispatcher.DispatchAsync(() => this.Commands.AcquireCookReadAsync(this.Context, token)));
        }

        public async Task SetSavedLegacyAerialStartAsync()
        {
            this.Model.SetScene(value: null);
            var environment = this.Source.Environment;
            this.Source.Hydrate(this.Source.Dehydrate() with { Environment = environment with { SkyAtmosphere = environment.SkyAtmosphere with { AerialPerspectiveStartDepthMeters = -1 } } });
            _ = (await this.Commands.SaveSceneAsync(this.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
            this.Model.SetScene(this.Source);
        }
    }
}
