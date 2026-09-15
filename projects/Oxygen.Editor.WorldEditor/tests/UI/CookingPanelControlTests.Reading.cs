// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls.OutputConsole;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.World.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks the reader's position and selection through incoming messages and run switches.</summary>
public sealed partial class CookingPanelControlTests
{
    /// <summary>Queued messages stay with their source and reloaded consoles resume updates without duplicate controls.</summary>
    /// <returns>The console source/lifetime regression.</returns>
    [TestMethod]
    public Task TranscriptSourceSwitchAndReloadKeepBufferedMessagesScoped() => EnqueueAsync(async () =>
    {
        var first = new System.Collections.ObjectModel.ObservableCollection<OutputLogEntry>();
        var second = new System.Collections.ObjectModel.ObservableCollection<OutputLogEntry>();
        var console = new OutputConsoleView { ItemsSource = first, Width = 600, Height = 300 };
        var clears = 0;
        console.ClearRequested += (_, _) => ++clears;
        await LoadTestContentAsync(console).ConfigureAwait(true);
        var list = console.FindDescendant<ListView>()!;
        first.Add(new() { Message = "Old source queued message", Level = Serilog.Events.LogEventLevel.Information });
        console.ItemsSource = second;
        second.Add(new() { Message = "Current source message", Level = Serilog.Events.LogEventLevel.Information });
        await this.WaitForReadingAsync(() => list.Items.Count == 1).ConfigureAwait(true);
        _ = list.Items.Cast<OutputLogEntry>().Should().Equal(second);
        await LoadTestContentAsync(new Grid()).ConfigureAwait(true);
        second.Add(new() { Message = "While hidden", Level = Serilog.Events.LogEventLevel.Information });
        await LoadTestContentAsync(console).ConfigureAwait(true);
        await this.WaitForReadingAsync(() => list.Items.Count == 2).ConfigureAwait(true);
        second.Add(new() { Message = "After reopening", Level = Serilog.Events.LogEventLevel.Information });
        await this.WaitForReadingAsync(() => list.Items.Count == 3).ConfigureAwait(true);
        _ = list.Items.Cast<OutputLogEntry>().Should().Equal(second);
        var clear = console.FindDescendant<Button>(button => string.Equals(button.Name, "ClearButton", StringComparison.Ordinal))!;
        ((IInvokeProvider)new ButtonAutomationPeer(clear).GetPattern(PatternInterface.Invoke)).Invoke();
        _ = clears.Should().Be(1);
    });

    /// <summary>The shared scroller follows only at the end and restores each run's reading state after switching or hiding.</summary>
    /// <returns>The rendered transcript regression.</returns>
    [TestMethod]
    public Task TranscriptReadingPositionAndSelectionSurviveUpdatesAndRunSwitches() => EnqueueAsync(async () =>
    {
        var runs = new Mock<ICookRunService>();
        using var model = CreateModel(runService: runs);
        var first = model.SelectedRun!;
        AppendReadingMessages(first, 80);
        var secondSnapshot = first.Snapshot with { OperationId = Guid.NewGuid(), DisplayName = "Other cook" };
        runs.Raise(value => value.RunChanged += null, new CookRunChangedEventArgs(secondSnapshot));
        await this.WaitForReadingAsync(() => model.Runs.Count == 2).ConfigureAwait(true);
        var second = model.Runs.Single(run => run.Snapshot.OperationId == secondSnapshot.OperationId);
        var view = new CookingPanelView { ViewModel = model, Width = 700, Height = 340 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var scroll = (ScrollViewer)view.FindName("DetailsScroller");
        var output = (OutputConsoleView)view.FindName("CookOutput");
        await this.WaitForReadingAsync(() => scroll.ScrollableHeight > 600 && first.HasReadingPosition).ConfigureAwait(true);
        _ = scroll.VerticalOffset.Should().BeApproximately(0, 1);
        _ = first.FollowTail.Should().BeFalse();
        await this.CheckReadingFollowAsync(first, scroll, output).ConfigureAwait(true);
        model.SelectedRun = second;
        await this.WaitForReadingAsync(() => second.HasReadingPosition && scroll.VerticalOffset < 1).ConfigureAwait(true);
        _ = scroll.ChangeView(horizontalOffset: null, 220, zoomFactor: null, disableAnimation: true);
        await this.WaitForReadingAsync(() => Math.Abs(second.ReadingOffset - 220) < 1).ConfigureAwait(true);
        output.RestoreSelection([second.Output[5]]);
        model.SelectedRun = first;
        await this.WaitForReadingAsync(() => Math.Abs(scroll.VerticalOffset - 120) < 1).ConfigureAwait(true);
        _ = output.CaptureSelection().Should().Equal(first.Output[2], first.Output[3]);
        model.SelectedRun = second;
        await this.WaitForReadingAsync(() => Math.Abs(scroll.VerticalOffset - 220) < 1).ConfigureAwait(true);
        _ = output.CaptureSelection().Should().Equal([second.Output[5]], "returning to the second run restores its selection");
        await LoadTestContentAsync(new Grid()).ConfigureAwait(true);
        AppendReadingMessages(second, 10);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await this.WaitForReadingAsync(() => Math.Abs(scroll.VerticalOffset - 220) < 1 && output.CaptureSelection().Count == 1).ConfigureAwait(true);
        _ = output.CaptureSelection().Should().Equal([second.Output[5]], "hiding and reopening preserves selected messages");
        var oldExtent = scroll.ScrollableHeight;
        AppendReadingMessages(second, 10);
        await this.WaitForReadingAsync(() => scroll.ScrollableHeight > oldExtent).ConfigureAwait(true);
        _ = scroll.VerticalOffset.Should().BeApproximately(220, 1);
    });

    private static void AppendReadingMessages(CookingRunViewModel run, int count)
    {
        var snapshot = run.Snapshot;
        run.Apply(snapshot with
        {
            Revision = snapshot.Revision + 1,
            Messages = snapshot.Messages.AddRange(Enumerable.Range(snapshot.Messages.Count, count)
                .Select(index => new CookRunMessage(index + 1, DateTimeOffset.UtcNow, DiagnosticSeverity.Info, "Processing asset " + index.ToString(System.Globalization.CultureInfo.InvariantCulture)))),
        });
    }

    private async Task CheckReadingFollowAsync(CookingRunViewModel first, ScrollViewer scroll, OutputConsoleView output)
    {
        output.RestoreSelection([first.Output[2], first.Output[3]]);
        _ = scroll.ChangeView(horizontalOffset: null, scroll.ScrollableHeight, zoomFactor: null, disableAnimation: true);
        await this.WaitForReadingAsync(() => first.FollowTail).ConfigureAwait(true);
        var oldExtent = scroll.ScrollableHeight;
        AppendReadingMessages(first, 20);
        await this.WaitForReadingAsync(() => scroll.ScrollableHeight > oldExtent && Math.Abs(scroll.VerticalOffset - scroll.ScrollableHeight) < 1).ConfigureAwait(true);
        _ = output.CaptureSelection().Should().Equal(first.Output[2], first.Output[3]);
        _ = scroll.ChangeView(horizontalOffset: null, 120, zoomFactor: null, disableAnimation: true);
        await this.WaitForReadingAsync(() => !first.FollowTail && Math.Abs(scroll.VerticalOffset - 120) < 1).ConfigureAwait(true);
        oldExtent = scroll.ScrollableHeight;
        AppendReadingMessages(first, 20);
        await this.WaitForReadingAsync(() => scroll.ScrollableHeight > oldExtent).ConfigureAwait(true);
        _ = scroll.VerticalOffset.Should().BeApproximately(120, 1);
    }

    private async Task WaitForReadingAsync(Func<bool> condition, [System.Runtime.CompilerServices.CallerArgumentExpression(nameof(condition))] string? expression = null)
    {
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
        do
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            if (condition())
            {
                return;
            }

            await Task.Delay(20, this.TestContext.CancellationToken).ConfigureAwait(true);
        }
        while (elapsed.Elapsed < TimeSpan.FromSeconds(5));
        Assert.Fail("Cooking reading state did not settle within five seconds: " + expression);
    }
}
