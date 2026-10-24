// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls.OutputConsole.Model;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Tests;

[ExcludeFromCodeCoverage]
[TestClass]
[TestCategory("UITest")]
public class OutputConsoleViewTests : VisualUserInterfaceTests
{
    private static readonly SubsystemMap Subsystems = new([new("Test.Engine", "Engine")]);

    [TestMethod]
    public Task AppendingPastCapacity_KeepsSelectionAndRowCollection() => EnqueueAsync(async () =>
    {
        var buffer = new OutputLogBuffer(Subsystems, capacity: 20);
        var (view, list) = await LoadConsoleAsync(buffer).ConfigureAwait(true);
        AppendLines(buffer, 20);
        await WaitUntilAsync(() => Rows(list).Count == 20).ConfigureAwait(true);
        var kept = buffer.Snapshot()[14];
        view.SelectedItem = kept;
        var rows = list.ItemsSource;

        AppendLines(buffer, 10);
        await WaitUntilAsync(() => Rows(list).Count == 20 && RowAt(list, 19).First.Sequence == 30).ConfigureAwait(true);

        _ = list.ItemsSource.Should().BeSameAs(rows);
        _ = view.CaptureSelection().Should().Equal(kept);
        _ = RowAt(list, 0).First.Sequence.Should().Be(11);
    });

    [TestMethod]
    public Task ChangingAFilter_ReplacesTheRowCollection() => EnqueueAsync(async () =>
    {
        var buffer = new OutputLogBuffer(Subsystems, capacity: 50);
        var (view, list) = await LoadConsoleAsync(buffer).ConfigureAwait(true);
        AppendLines(buffer, 5);
        buffer.Append(new OutputLogEntry { Level = LogEventLevel.Warning, Message = "warning", Source = "Test.Engine" });
        await WaitUntilAsync(() => Rows(list).Count == 6).ConfigureAwait(true);
        var before = list.ItemsSource;

        view.LevelFilter = LevelMask.Warning;

        _ = list.ItemsSource.Should().NotBeSameAs(before);
        _ = Rows(list).Should().ContainSingle();
    });

    [TestMethod]
    public Task Pausing_StopsNewRowsUntilResumed() => EnqueueAsync(async () =>
    {
        var buffer = new OutputLogBuffer(Subsystems, capacity: 50);
        var (view, list) = await LoadConsoleAsync(buffer).ConfigureAwait(true);
        AppendLines(buffer, 3);
        await WaitUntilAsync(() => Rows(list).Count == 3).ConfigureAwait(true);

        view.IsPaused = true;
        AppendLines(buffer, 4);
        await WaitForRenderAsync().ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var whilePaused = Rows(list).Count;

        view.IsPaused = false;
        await WaitUntilAsync(() => Rows(list).Count == 7).ConfigureAwait(true);

        _ = whilePaused.Should().Be(3);
    });

    [TestMethod]
    public Task SelectingAFoldedRow_CapturesEveryFoldedEntry() => EnqueueAsync(async () =>
    {
        var buffer = new OutputLogBuffer(Subsystems, capacity: 50);
        var (view, list) = await LoadConsoleAsync(buffer, fold: true).ConfigureAwait(true);
        for (var i = 0; i < 4; i++)
        {
            buffer.Append(new OutputLogEntry { Message = "repeated", Source = "Test.Engine" });
        }

        await WaitUntilAsync(() => Rows(list).Count == 1 && RowAt(list, 0).Count == 4).ConfigureAwait(true);

        list.SelectedIndex = 0;

        _ = view.CaptureSelection().Should().HaveCount(4).And.Equal(buffer.Snapshot());
    });

    [TestMethod]
    public Task ShowTimestamps_AddsTheTimeToTheRealizedRowsPrefix() => EnqueueAsync(async () =>
    {
        var buffer = new OutputLogBuffer(Subsystems, capacity: 50);
        var (view, list) = await LoadConsoleAsync(buffer).ConfigureAwait(true);
        AppendLines(buffer, 3);
        await WaitUntilAsync(() => Rows(list).Count == 3).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var row = list.FindDescendant<OutputLogRowView>();
        _ = row.Should().NotBeNull();
        var withoutTime = PrefixOf(row!);

        view.ShowTimestamps = true;

        _ = withoutTime.Should().Be("[INF (Engine)]");
        _ = PrefixOf(row).Should().MatchRegex(@"^\[\d{2}:\d{2}:\d{2}\.\d{3} INF \(Engine\)\]$");
    });

    [TestMethod]
    public Task SubsystemsFlyout_OffersEveryMappedSubsystemAndDimsThoseWithoutLines() => EnqueueAsync(async () =>
    {
        var (_, boxes) = await OpenSubsystemsFlyoutAsync().ConfigureAwait(true);

        // Only Engine has logged; Cooker is offered all the same, its label dimmed and its check box usable.
        _ = boxes.Keys.Should().Equal("All subsystems", "Cooker", "Engine");
        _ = boxes["All subsystems"].IsThreeState.Should().BeTrue();
        _ = LabelOf(boxes["Cooker"]).ReadLocalValue(TextBlock.ForegroundProperty).Should().NotBe(DependencyProperty.UnsetValue);
        _ = LabelOf(boxes["Engine"]).ReadLocalValue(TextBlock.ForegroundProperty).Should().Be(DependencyProperty.UnsetValue);
        _ = boxes["Cooker"].IsEnabled.Should().BeTrue();
    });

    [TestMethod]
    public Task SubsystemsFlyout_AllCheckBoxHidesAndShowsAndShowsMixedState() => EnqueueAsync(async () =>
    {
        var (view, boxes) = await OpenSubsystemsFlyoutAsync().ConfigureAwait(true);
        var all = boxes["All subsystems"];

        all.IsChecked = false;
        var hiddenByAll = view.HiddenSubsystems.ToList();
        boxes["Engine"].IsChecked = true;

        _ = hiddenByAll.Should().BeEquivalentTo("Cooker", "Engine");
        _ = view.HiddenSubsystems.Should().Equal("Cooker");
        _ = all.IsChecked.Should().BeNull();
    });

    [TestMethod]
    public Task RowText_UsesTheFixedWidthConsoleFont() => EnqueueAsync(async () =>
    {
        var buffer = new OutputLogBuffer(Subsystems, capacity: 50);
        var (_, list) = await LoadConsoleAsync(buffer).ConfigureAwait(true);
        AppendLines(buffer, 1);
        await WaitUntilAsync(() => Rows(list).Count == 1).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        // WinUI's ListViewItem style sets the proportional UI font on each row unless the console overrides it.
        var cells = list.FindDescendant<OutputLogRowView>()!.FindDescendants().OfType<TextBlock>().ToList();

        _ = cells.Should().NotBeEmpty();
        _ = cells.Should().AllSatisfy(cell => cell.FontFamily.Source.Should().Be("Cascadia Mono, Consolas"));
    });

    [TestMethod]
    [SuppressMessage("Performance", "CA1861:Avoid constant arrays as arguments", Justification = "constant values for this test case only")]
    public Task FindNext_MovesOnlyBetweenMatchingRowsAndWraps() => EnqueueAsync(async () =>
    {
        var buffer = new OutputLogBuffer(Subsystems, capacity: 50);
        var (view, list) = await LoadConsoleAsync(buffer).ConfigureAwait(true);
        var messages = new[] { "alpha", "needle one", "beta", "needle two", "gamma" };
        foreach (var message in messages)
        {
            buffer.Append(new OutputLogEntry { Message = message, Source = "Test.Engine" });
        }

        await WaitUntilAsync(() => Rows(list).Count == 5).ConfigureAwait(true);
        view.SearchMode = OutputSearchMode.Find;
        view.TextFilter = "needle";
        var next = view.FindDescendant<Button>(b => string.Equals(b.Name, "NextMatchButton", StringComparison.Ordinal));
        _ = next.Should().NotBeNull();
        var invoke = new ButtonAutomationPeer(next!);

        invoke.Invoke();
        var first = list.SelectedIndex;
        invoke.Invoke();
        var second = list.SelectedIndex;
        invoke.Invoke();
        var wrapped = list.SelectedIndex;

        _ = Rows(list).Should().HaveCount(5, "Find mode keeps every row");
        _ = (first, second, wrapped).Should().Be((1, 3, 1));
    });

    private static async Task<(OutputConsoleView View, ListView List)> LoadConsoleAsync(OutputLogBuffer buffer, bool fold = false)
    {
        var view = new OutputConsoleView
        {
            ItemsSource = buffer,
            FollowTail = false,
            FoldDuplicates = fold,
            LevelFilter = LevelMask.All,
            Width = 800,
            Height = 400,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var list = view.FindDescendant<ListView>();
        _ = list.Should().NotBeNull();
        return (view, list!);
    }

    // Loads a console whose map defines Engine and Cooker, logs one Engine line, opens the Subsystems flyout and
    // returns its check boxes by label.
    private static async Task<(OutputConsoleView View, Dictionary<string, CheckBox> Boxes)> OpenSubsystemsFlyoutAsync()
    {
        var buffer = new OutputLogBuffer(new SubsystemMap([new("Test.Engine", "Engine"), new("Test.Cooker", "Cooker")]), capacity: 50);
        var (view, _) = await LoadConsoleAsync(buffer).ConfigureAwait(true);
        buffer.Append(new OutputLogEntry { Message = "line", Source = "Test.Engine" });
        var dropDown = view.FindDescendant<DropDownButton>(d => string.Equals(d.Name, "SubsystemsDropDown", StringComparison.Ordinal));
        _ = dropDown.Should().NotBeNull();
        var flyout = (Flyout)dropDown!.Flyout;
        flyout.ShowAt(dropDown);

        // The list is built when the flyout opens; until then only the All check box is there.
        var content = (FrameworkElement)flyout.Content;
        await WaitUntilAsync(() => content.FindDescendants().OfType<CheckBox>().Count() == 3).ConfigureAwait(true);
        var boxes = content.FindDescendants().OfType<CheckBox>().ToDictionary(box => box.Content is TextBlock label ? label.Text : (string)box.Content, StringComparer.Ordinal);
        return (view, boxes);
    }

    private static TextBlock LabelOf(CheckBox box) => (TextBlock)box.Content;

    private static void AppendLines(OutputLogBuffer buffer, int count)
    {
        for (var i = 0; i < count; i++)
        {
            buffer.Append(new OutputLogEntry { Level = LogEventLevel.Information, Message = "line", Source = "Test.Engine" });
        }
    }

    // The row's first text cell is its prefix: [time LVL (Subsystem)].
    private static string PrefixOf(OutputLogRowView row)
        => string.Concat(row.FindDescendants().OfType<TextBlock>().First().Inlines.OfType<Microsoft.UI.Xaml.Documents.Run>().Select(run => run.Text));

    private static IReadOnlyList<OutputLogRow> Rows(ListView list) => (IReadOnlyList<OutputLogRow>)list.ItemsSource;

    private static OutputLogRow RowAt(ListView list, int index) => Rows(list)[index];

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        var timeout = Stopwatch.StartNew();
        while (!condition())
        {
            if (timeout.Elapsed > TimeSpan.FromSeconds(5))
            {
                throw new TimeoutException("The console did not reach the expected state.");
            }

            await WaitForRenderAsync().ConfigureAwait(true);
        }
    }
}
