// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls.OutputConsole.Model;

namespace Oxygen.Editor.WorldEditor.Output;

/* TODO: persist and load settings */

/// <summary>
///     The ViewModel for managing and displaying logs in the application.
/// </summary>
/// <param name="buffer">The buffer that stores the output log entries.</param>
/// <remarks>
///     The <see cref="OutputLogBuffer"/> is typically a signleton service shared across the
///     application, configured in the DI and auto-injected with this ViewModel is resolved.
/// </remarks>
public partial class OutputViewModel(OutputLogBuffer buffer) : ObservableObject
{
    /// <summary>
    ///     Gets the buffer that stores the output log entries.
    /// </summary>
    public OutputLogBuffer Buffer { get; } = buffer;

    [ObservableProperty]
    public partial LevelMask LevelFilter { get; set; } = LevelMask.Information | LevelMask.Warning | LevelMask.Error | LevelMask.Fatal;

    [ObservableProperty]
    public partial string TextFilter { get; set; } = string.Empty;

    [ObservableProperty]
    public partial bool FollowTail { get; set; } = true;

    [ObservableProperty]
    public partial bool IsPaused { get; set; }

    [ObservableProperty]
    public partial bool ShowTimestamps { get; set; }

    [ObservableProperty]
    public partial bool WordWrap { get; set; }
}
