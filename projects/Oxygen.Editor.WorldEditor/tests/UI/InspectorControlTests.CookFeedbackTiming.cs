// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.World.Cooking;

namespace Oxygen.Editor.World.Tests;

/// <summary>Measures visible Cooking feedback through the production coordinator and native pipeline.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Each explicit scope reports a changed cook promptly in the qualification project, then reuses unchanged output.</summary>
    /// <param name="scope">The public cook entry point.</param>
    /// <returns>The asynchronous timing and no-op regression.</returns>
    [TestMethod]
    [TestCategory("Performance")]
    [DataRow(CookTargetKind.Asset)]
    [DataRow(CookTargetKind.Folder)]
    [DataRow(CookTargetKind.CurrentScene)]
    [DataRow(CookTargetKind.Project)]
    public Task ExplicitCookScopesRenderFeedbackWithinBudget(CookTargetKind scope) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromMinutes(3));
        using var services = new CatalogWorkloadServices(fixture);
        var builtins = (await services.Builtins.GetAsync(timeout.Token).ConfigureAwait(true)).Catalog!;
        await SeedCatalogWorkloadAsync(fixture, services.Projects.ActiveProject!, services.Pipeline, builtins, timeout.Token).ConfigureAwait(true);
        var material = new Uri("asset:///" + WorkloadMaterialPath(0));
        var source = Path.Combine(fixture.ProjectRoot, WorkloadMaterialPath(0));
        var changed = JsonNode.Parse(await File.ReadAllTextAsync(source, timeout.Token).ConfigureAwait(true))!;
        changed["PbrMetallicRoughness"]!["RoughnessFactor"] = 0.8;
        await File.WriteAllTextAsync(source, changed.ToJsonString(), timeout.Token).ConfigureAwait(true);
        var baselineRuns = services.Runs.Runs.Count;
        using var model = fixture.CreateTimingCookingPanel(services);
        var view = new CookingPanelView { ViewModel = model, Width = 960, Height = 420 };
        var window = VisualUserInterfaceTestsApp.MainWindow.AppWindow;
        var originalSize = window.Size;
        await LoadTestContentAsync(view).ConfigureAwait(true);
        window.Resize(new((int)(960 * view.XamlRoot.RasterizationScale) + 60, (int)(420 * view.XamlRoot.RasterizationScale) + 100));
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var scene = new Uri("asset:///Content/Scenes/" + Uri.EscapeDataString(fixture.Source.Name) + ".oscene.json");
            var first = await this.MeasureCookFeedbackAsync(view, model, () => SubmitTimedCook(services.Pipeline, scope, material, scene, timeout.Token), timeout.Token).ConfigureAwait(true);
            _ = first.IsPublished.Should().BeTrue(DescribeWorkloadCook(first));
            var repeated = await this.MeasureCookFeedbackAsync(view, model, () => SubmitTimedCook(services.Pipeline, scope, material, scene, timeout.Token), timeout.Token).ConfigureAwait(true);
            _ = repeated.IsUpToDate.Should().BeTrue();
            _ = repeated.IsPublished.Should().BeFalse();
            _ = services.Runs.Runs.Should().HaveCount(baselineRuns + 2);
            await this.RecordCatalogWorkloadAsync(fixture, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            await UnloadTestContentAsync(view).ConfigureAwait(true);
            window.Resize(originalSize);
        }
    });

    private static Task<ContentCookResult> SubmitTimedCook(ContentPipelineService pipeline, CookTargetKind scope, Uri material, Uri scene, CancellationToken cancellationToken)
        => scope switch
        {
            CookTargetKind.Asset => pipeline.CookAssetAsync(material, cancellationToken),
            CookTargetKind.Folder => pipeline.CookFolderAsync(new("asset:///Content/Materials"), cancellationToken),
            CookTargetKind.CurrentScene => pipeline.CookCurrentSceneAsync(scene, cancellationToken),
            CookTargetKind.Project => pipeline.CookProjectAsync(cancellationToken),
            _ => throw new ArgumentOutOfRangeException(nameof(scope)),
        };

    private async Task<ContentCookResult> MeasureCookFeedbackAsync(CookingPanelView view, CookingPanelViewModel model, Func<Task<ContentCookResult>> submit, CancellationToken cancellationToken)
    {
        var previous = model.SelectedRun?.Snapshot.OperationId;
        var clock = Stopwatch.StartNew();
        var work = submit();
        var submission = clock.Elapsed.TotalMilliseconds;
        double visible = 0;
        do
        {
            cancellationToken.ThrowIfCancellationRequested();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
            {
                var title = (TextBlock)view.FindName("RunTitle");
                var status = (CookingStatusIndicator)view.FindName("RunStatus");
                var progress = view.FindDescendant<ProgressBar>()!;
                if (model.SelectedRun is { } run && run.Snapshot.OperationId != previous
                    && title.IsLoaded && title.ActualWidth > 0 && string.Equals(title.Text, run.Title, StringComparison.Ordinal)
                    && status.IsLoaded && status.State == run.Snapshot.State
                    && (!run.IsBusy || (progress.Visibility == Visibility.Visible && progress.IsIndeterminate)))
                {
                    visible = clock.Elapsed.TotalMilliseconds;
                }
            }).ConfigureAwait(true);
        }
        while (visible == 0);

        var result = await work.WaitAsync(cancellationToken).ConfigureAwait(true);
        this.TestContext.WriteLine(string.Create(CultureInfo.InvariantCulture, $"Cook feedback: scope={result.TargetKind}; reused={result.IsUpToDate}; submit_ms={submission:F2}; visible_ms={visible:F2}"));
        _ = visible.Should().BeLessThanOrEqualTo(100, "cook progress or completion must become visible promptly");
        return result;
    }

    private sealed partial class NativeSceneFixture
    {
        public CookingPanelViewModel CreateTimingCookingPanel(CatalogWorkloadServices services)
            => new(services.Runs, services.Pipeline, services.Projects, Mock.Of<ICookingWorkspaceActions>(), this.hosting);
    }
}
