// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Tests;
using DryIoc;
using Microsoft.UI;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspection;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed class DocumentTransitionScenario : DroidNet.Tests.VisualUserInterfaceTests
{
    internal DocumentTransitionScenario(TestContext testContext) => this.TestContext = testContext;

    private TestContext TestContext { get; }

    /// <summary>Document activation drains each old viewport and recreates the scene view without stale native resources.</summary>
    /// <param name="targetFps">The native cadence, including slow debugging-like frames.</param>
    /// <param name="cycles">The number of complete Main/Inspect/Main transitions.</param>
    /// <returns>The asynchronous document-transition regression.</returns>
    public Task RunAsync(uint targetFps, int cycles) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedShadowTransitionScene(scene, 4), new EngineSettings { Graphics = new() { EnableDebugLayer = true, EnableValidation = true }, Renderer = new() { DirectionalShadowPolicy = DirectionalShadowPolicy.ConventionalOnly, EnableImGui = false }, });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(150));
        _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        using var services = new CatalogWorkloadServices(fixture);
        var sceneUri = new Uri("asset:///Content/Scenes/" + Uri.EscapeDataString(fixture.Source.Name) + ".oscene.json");
        _ = (await services.Pipeline.CookCurrentSceneAsync(sceneUri, timeout.Token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        await fixture.RefreshCookedRootsAsync().ConfigureAwait(true);
        fixture.Runtime.TargetFps = targetFps;
        var container = new Container();
        await using var containerLifetime = container.ConfigureAwait(true);
        var documents = new EditorDocumentService();
        var window = VisualUserInterfaceTestsApp.MainWindow.AppWindow;
        var originalSize = window.Size;
        using var host = fixture.CreateDocumentTransitionHost(container, documents, services, window.Id);
        using var manager = fixture.CreateTransitionDocumentManager(documents, services.Projects, window.Id);
        var root = new DocumentHostView
        {
            ViewModel = host,
            Width = 960,
            Height = 600
        };
        await LoadTestContentAsync(root).ConfigureAwait(true);
        window.Resize(new((int)(root.Width * root.XamlRoot.RasterizationScale) + 60, (int)(root.Height * root.XamlRoot.RasterizationScale) + 100));
        try
        {
            _ = await documents.OpenDocumentAsync(window.Id, fixture.Context.Metadata).ConfigureAwait(true);
            await fixture.NotifyTransitionSceneReadyAsync(timeout.Token).ConfigureAwait(true);
            await WaitForDocumentViewportAsync(host, fixture, timeout.Token).ConfigureAwait(true);
            await fixture.SuspendCookedContentAsync().ConfigureAwait(true);
            await fixture.RefreshCookedRootsAsync().ConfigureAwait(true);
            await this.RunInspectionTransitionsAsync(fixture, host, documents, services, window.Id, cycles, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            if (host.ActiveEditorView is SceneEditorView sceneView)
            {
                await sceneView.DeactivateAsync().ConfigureAwait(true);
            }

            await UnloadTestContentAsync(root).ConfigureAwait(true);
            window.Resize(originalSize);
        }

        _ = fixture.Runtime.ActiveSurfaceCount.Should().Be(0);
        _ = fixture.Results.Should().NotContain(result => result.OperationKind == RuntimeOperationKinds.Loop);
    });

    private static async Task WaitForDocumentViewportAsync(DocumentHostViewModel host, NativeSceneFixture fixture, CancellationToken cancellationToken)
    {
        while (host.ActiveEditorView is not SceneEditorView { IsLoaded: true } || host.ActiveEditor is not SceneEditorViewModel model || model.Viewports.Count != 1 || !model.Viewports[0].AssignedViewId.IsValid || fixture.Runtime.ActiveSurfaceCount != 1)
        {
            _ = fixture.Runtime.State.Should().Be(EngineServiceState.Running);
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }

        await ObserveRenderedFramesAsync(fixture, cancellationToken).ConfigureAwait(true);
    }

    private async Task RunInspectionTransitionsAsync(NativeSceneFixture fixture, DocumentHostViewModel host, EditorDocumentService documents, CatalogWorkloadServices services, WindowId windowId, int cycles, CancellationToken cancellationToken)
    {
        var mainId = fixture.Context.DocumentId;
        for (var cycle = 0; cycle < cycles; ++cycle)
        {
            var viewport = ((SceneEditorViewModel)host.ActiveEditor!).Viewports.Single();
            var oldView = viewport.AssignedViewId;
            _ = (await fixture.OpenTransitionInspectionAsync(services.Projects.ActiveProject!).ConfigureAwait(true)).Should().BeTrue();
            while (host.ActiveEditorView is not CookedInspectionView { IsLoaded: true })
            {
                await Task.Delay(20, cancellationToken).ConfigureAwait(true);
            }

            await ((CookedInspectionViewModel)host.ActiveEditor!).CurrentWork.ConfigureAwait(true);
            _ = fixture.Runtime.ActiveSurfaceCount.Should().Be(0);
            _ = viewport.AssignedViewId.IsValid.Should().BeFalse();
            _ = documents.GetOpenDocuments(windowId).Should().HaveCount(2);
            _ = (await documents.SelectDocumentAsync(windowId, mainId).ConfigureAwait(true)).Should().BeTrue();
            await fixture.NotifyTransitionSceneReadyAsync(cancellationToken).ConfigureAwait(true);
            await WaitForDocumentViewportAsync(host, fixture, cancellationToken).ConfigureAwait(true);
            _ = ((SceneEditorViewModel)host.ActiveEditor!).Viewports.Single().AssignedViewId.Should().NotBe(oldView);
            _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        }
    }
}
