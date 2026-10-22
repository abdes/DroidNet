// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TestHelpers;
using DroidNet.Tests;
using Microsoft.UI;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.TestSupport;

internal sealed class DocumentTransitionScenario : DroidNet.Tests.VisualUserInterfaceTests
{
    internal DocumentTransitionScenario(TestContext testContext) => this.TestContext = testContext;

    private TestContext TestContext { get; }

    /// <summary>Document activation drains each old viewport and recreates the scene view without stale native resources.</summary>
    /// <param name="targetFps">The native cadence, including slow debugging-like frames.</param>
    /// <param name="cycles">The number of complete scene/material/scene transitions.</param>
    /// <returns>The asynchronous document-transition regression.</returns>
    public Task RunAsync(uint targetFps, int cycles) => EnqueueAsync(async () =>
    {
        // The shared engine: the scenario is the document transition at a slow cadence, not GPU validation.
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedShadowTransitionScene(scene, 4));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(150));
        _ = (await fixture.Document.SaveAsync().ConfigureAwait(true)).Succeeded.Should().BeTrue();
        using var services = new CatalogWorkloadServices(fixture);
        var sceneAsset = AssetPath.Create("Content", $"Scenes/{fixture.Source.Name}.oscene");
        _ = (await services.Pipeline.CookCurrentSceneAsync(sceneAsset, timeout.Token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var (material, _) = await fixture.CookTestMaterialAsync("Transition", timeout.Token).ConfigureAwait(true);
        await fixture.RefreshCookedRootsAsync().ConfigureAwait(true);
        fixture.Runtime.TargetFps = targetFps;
        var container = HostContainer.Create();
        await using var containerLifetime = container.ConfigureAwait(true);
        var window = VisualUserInterfaceTestsApp.MainWindow.AppWindow;
        var originalSize = window.Size;
        using var host = fixture.CreateDocumentTransitionHost(container, services, window.Id);
        var documents = fixture.TransitionTabs!;
        using var service = fixture.TransitionDocuments!;
        using var area = fixture.TransitionArea!;
        using var attachments = fixture.TransitionAttachments!;
        var root = new DocumentAreaView
        {
            ViewModel = host,
            Width = 960,
            Height = 600,
        };
        await LoadTestContentAsync(root).ConfigureAwait(true);
        window.Resize(new((int)(root.Width * root.XamlRoot.RasterizationScale) + 60, (int)(root.Height * root.XamlRoot.RasterizationScale) + 100));
        try
        {
            _ = await area.ShowAsync(fixture.Document).ConfigureAwait(true);
            await fixture.NotifyTransitionSceneReadyAsync(timeout.Token).ConfigureAwait(true);
            await WaitForDocumentViewportAsync(host, fixture, timeout.Token).ConfigureAwait(true);
            await fixture.SuspendCookedContentAsync().ConfigureAwait(true);
            await fixture.RefreshCookedRootsAsync().ConfigureAwait(true);
            await RunMaterialTransitionsAsync(fixture, host, area, documents, material, window.Id, cycles, timeout.Token).ConfigureAwait(true);
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

    private static async Task WaitForDocumentViewportAsync(DocumentAreaViewModel host, NativeSceneFixture fixture, CancellationToken cancellationToken)
    {
        while (host.ActiveEditorView is not SceneEditorView { IsLoaded: true } || host.ActiveEditor is not SceneEditorViewModel model || model.Viewports.Count != 1 || !model.Viewports[0].AssignedViewId.IsValid || fixture.Runtime.ActiveSurfaceCount != 1)
        {
            _ = fixture.Runtime.State.Should().Be(EngineServiceState.Running);
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }

        await ObserveRenderedFramesAsync(fixture, cancellationToken).ConfigureAwait(true);
    }

    private static async Task RunMaterialTransitionsAsync(NativeSceneFixture fixture, DocumentAreaViewModel host, DocumentArea area, DocumentTabs documents, AssetPath material, WindowId windowId, int cycles, CancellationToken cancellationToken)
    {
        var mainId = fixture.Document.Id;
        for (var cycle = 0; cycle < cycles; ++cycle)
        {
            var viewport = ((SceneEditorViewModel)host.ActiveEditor!).Viewports.Single();
            var oldView = viewport.AssignedViewId;
            _ = (await area.OpenAsync(material, cancellationToken).ConfigureAwait(true)).Succeeded.Should().BeTrue();
            while (host.ActiveEditorView is not MaterialEditorView { IsLoaded: true })
            {
                await Task.Delay(20, cancellationToken).ConfigureAwait(true);
            }

            _ = fixture.Runtime.ActiveSurfaceCount.Should().Be(0);
            _ = viewport.AssignedViewId.IsValid.Should().BeFalse();
            _ = documents.GetOpenDocuments(windowId).Should().HaveCount(2);
            _ = (await documents.SelectDocumentAsync(windowId, mainId).ConfigureAwait(true)).Should().BeTrue();
            await fixture.NotifyTransitionSceneReadyAsync(cancellationToken).ConfigureAwait(true);
            await WaitForDocumentViewportAsync(host, fixture, cancellationToken).ConfigureAwait(true);
            _ = ((SceneEditorViewModel)host.ActiveEditor!).Viewports.Single().AssignedViewId.Should().NotBe(oldView);
            _ = fixture.Document.IsDirty.Should().BeFalse();
        }
    }
}
