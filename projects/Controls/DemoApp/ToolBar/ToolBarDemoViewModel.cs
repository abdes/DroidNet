// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Demo.ToolBar;

/// <summary>
/// ViewModel for the <see cref="ToolBarDemoView"/>.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public")]
public partial class ToolBarDemoViewModel(ILoggerFactory loggerFactory) : ObservableObject
{
    /// <summary>
    /// Gets the <see cref="ILoggerFactory"/> instance used for logging.
    /// </summary>
    public ILoggerFactory LoggerFactory { get; } = loggerFactory;

    [ObservableProperty]
    public partial bool IsCompact { get; set; } = false;

    [ObservableProperty]
    public partial ToolBarLabelPosition DefaultLabelPosition { get; set; } = ToolBarLabelPosition.Right;

    [ObservableProperty]
    public partial string StatusMessage { get; set; } = "Ready";

    [ObservableProperty]
    public partial bool IsToggleChecked { get; set; } = false;

    [RelayCommand]
    private void ExecuteNew() => this.StatusMessage = "New command executed";

    [RelayCommand]
    private void ExecuteOpen() => this.StatusMessage = "Open command executed";

    [RelayCommand]
    private void ExecuteSave() => this.StatusMessage = "Save command executed";

    [RelayCommand]
    private void ExecuteCut() => this.StatusMessage = "Cut command executed";

    [RelayCommand]
    private void ExecuteCopy() => this.StatusMessage = "Copy command executed";

    [RelayCommand]
    private void ExecutePaste() => this.StatusMessage = "Paste command executed";

    [RelayCommand]
    private void ExecuteUndo() => this.StatusMessage = "Undo command executed";

    [RelayCommand]
    private void ExecuteRedo() => this.StatusMessage = "Redo command executed";
}
