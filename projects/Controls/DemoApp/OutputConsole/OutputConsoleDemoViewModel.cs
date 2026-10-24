// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.OutputConsole.Model;
using Serilog;
using Serilog.Events;

namespace DroidNet.Controls.Demo.OutputConsole;

/// <summary>
///     View model for the OutputConsole demo.
/// </summary>
/// <param name="buffer">The <see cref="OutputLogBuffer"/> to use. The view model does not manage the buffer's lifetime.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "must be public due to source generated ViewModel property")]
public partial class OutputConsoleDemoViewModel(OutputLogBuffer buffer) : ObservableObject
{
    private const string EngineSource = "Demo.Engine";
    private const string CookerSource = "Demo.Cooker";
    private const string EditorSource = "Demo.Editor";
    private const int BurstSize = 20_000;
    private const int RepeatCount = 50;

    /// <summary>
    ///     Gets the demo's subsystem names: the map the demo passes when it sets up the output console.
    /// </summary>
    public static SubsystemMap Subsystems { get; } = new(
    [
        new(EngineSource, "Engine"),
        new(CookerSource, "Cooker"),
        new(EditorSource, "Editor"),
    ]);

    /// <summary>
    ///     Gets the output log buffer used by the demo UI.
    /// </summary>
    public OutputLogBuffer Buffer { get; } = buffer;

    /// <summary>
    ///     Gets or sets the level filter for the output console.
    /// </summary>
    [ObservableProperty]
    public partial LevelMask LevelFilter { get; set; } = LevelMask.Information | LevelMask.Warning | LevelMask.Error | LevelMask.Fatal;

    /// <summary>
    ///     Gets or sets the text filter for the output console.
    /// </summary>
    [ObservableProperty]
    public partial string TextFilter { get; set; } = string.Empty;

    /// <summary>
    ///     Gets or sets a value indicating whether timestamps are shown in the output console.
    /// </summary>
    [ObservableProperty]
    public partial bool ShowTimestamps { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether word wrap is enabled in the output console.
    /// </summary>
    [ObservableProperty]
    public partial bool WordWrap { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the output console follows the tail (auto-scrolls to bottom).
    /// </summary>
    [ObservableProperty]
    public partial bool FollowTail { get; set; } = true;

    /// <summary>
    ///     Gets or sets a value indicating whether the output console is paused.
    /// </summary>
    [ObservableProperty]
    public partial bool IsPaused { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether lines show their subsystem.
    /// </summary>
    [ObservableProperty]
    public partial bool ShowSubsystem { get; set; } = true;

    /// <summary>
    ///     Gets or sets the hidden subsystems.
    /// </summary>
    [ObservableProperty]
    public partial IReadOnlyCollection<string> HiddenSubsystems { get; set; } = [];

    /// <summary>
    ///     Gets or sets a value indicating whether repeated lines fold.
    /// </summary>
    [ObservableProperty]
    public partial bool FoldDuplicates { get; set; } = true;

    /// <summary>
    ///     Gets or sets what the search text does.
    /// </summary>
    [ObservableProperty]
    public partial OutputSearchMode SearchMode { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the details pane is open.
    /// </summary>
    [ObservableProperty]
    public partial bool IsDetailsPaneOpen { get; set; }

    /// <summary>
    /// Writes a single Serilog event at the specified log level.
    /// </summary>
    /// <param name="logLevel">A string representing a <see cref="LogEventLevel"/> value (case-insensitive).</param>
    /// <remarks>
    /// If <paramref name="logLevel"/> parses to a valid <see cref="LogEventLevel"/>, a single
    /// log entry is written using Serilog's static <see cref="Log"/> API. The demo harness
    /// should configure a sink that forwards entries into <see cref="OutputLogBuffer"/>.
    /// </remarks>
    [RelayCommand]
    private static void MakeLog(string logLevel)
    {
        if (Enum.TryParse(logLevel, out LogEventLevel level))
        {
            Log.ForContext(Serilog.Core.Constants.SourceContextPropertyName, EditorSource)
                .Write(level, "{Level} log message", level);
        }
    }

    /// <summary>
    /// Logs a burst of lines from a background thread, across subsystems, to exercise capture and eviction.
    /// </summary>
    /// <returns>A task that completes when the burst is logged.</returns>
    [RelayCommand]
    private static Task LogBurstAsync() => Task.Run(() =>
    {
        var engine = Log.ForContext(Serilog.Core.Constants.SourceContextPropertyName, EngineSource);
        var cooker = Log.ForContext(Serilog.Core.Constants.SourceContextPropertyName, CookerSource);
        for (var i = 0; i < BurstSize; i++)
        {
            (i % 3 == 0 ? cooker : engine).Information("Burst line {Index} of {Total}", i + 1, BurstSize);
        }
    });

    /// <summary>
    /// Logs the same warning repeatedly, to exercise folding.
    /// </summary>
    [RelayCommand]
    private static void LogRepeated()
    {
        var engine = Log.ForContext(Serilog.Core.Constants.SourceContextPropertyName, EngineSource);
        for (var i = 0; i < RepeatCount; i++)
        {
            engine.Warning("Frame time exceeds the target");
        }
    }

    /// <summary>
    /// Logs an error with an exception and its stack trace, to exercise the details pane.
    /// </summary>
    [RelayCommand]
    private static void LogException()
    {
        try
        {
            throw new InvalidDataException("Vertex attribute TANGENT uses an unexpected format.");
        }
        catch (InvalidDataException ex)
        {
            Log.ForContext(Serilog.Core.Constants.SourceContextPropertyName, CookerSource)
                .Error(ex, "Cooking failed for {Count} changed sources.", 1);
        }
    }
}
