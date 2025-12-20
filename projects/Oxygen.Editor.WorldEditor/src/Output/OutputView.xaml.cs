// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputConsole;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.World.Output;

/// <summary>
///     Represents the view for displaying logs in the World Editor.
/// </summary>
[ViewModel(typeof(OutputViewModel))]
public sealed partial class OutputView
{
    private long followTailToken = -1;
    private long isPausedToken = -1;

    private long levelFilterToken = -1;
    private long showTimestampsToken = -1;
    private long textFilterToken = -1;
    private long wordWrapToken = -1;

    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputView" /> class.
    /// </summary>
    public OutputView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // Ensure persisted settings are applied AFTER the control's own OnLoaded logic runs.
        if (this.ViewModel is OutputViewModel vm)
        {
            // Reapply LevelFilter if control mutated it (e.g., replaced All with subset).
            if (vm.LevelFilter != this.OutputConsole.LevelFilter)
            {
                this.OutputConsole.LevelFilter = vm.LevelFilter;
            }

            this.OutputConsole.TextFilter = vm.TextFilter;
            this.OutputConsole.FollowTail = vm.FollowTail;
            this.OutputConsole.IsPaused = vm.IsPaused;
            this.OutputConsole.ShowTimestamps = vm.ShowTimestamps;
            this.OutputConsole.WordWrap = vm.WordWrap;

            // Register property change callbacks to keep ViewModel updated if user changes via UI toggles not covered by binding (safety net)
            this.RegisterCallbacks();
        }
    }

    private void RegisterCallbacks()
    {
        // Only register once per view lifetime
        if (this.levelFilterToken != -1)
        {
            return;
        }

        this.levelFilterToken =
            this.OutputConsole.RegisterPropertyChangedCallback(
                OutputConsoleView.LevelFilterProperty,
                this.OnConsolePropertyChanged);
        this.textFilterToken = this.OutputConsole.RegisterPropertyChangedCallback(
            OutputConsoleView.TextFilterProperty,
            this.OnConsolePropertyChanged);
        this.followTailToken = this.OutputConsole.RegisterPropertyChangedCallback(
            OutputConsoleView.FollowTailProperty,
            this.OnConsolePropertyChanged);
        this.isPausedToken =
            this.OutputConsole.RegisterPropertyChangedCallback(
                OutputConsoleView.IsPausedProperty,
                this.OnConsolePropertyChanged);
        this.showTimestampsToken =
            this.OutputConsole.RegisterPropertyChangedCallback(
                OutputConsoleView.ShowTimestampsProperty,
                this.OnConsolePropertyChanged);
        this.wordWrapToken =
            this.OutputConsole.RegisterPropertyChangedCallback(
                OutputConsoleView.WordWrapProperty,
                this.OnConsolePropertyChanged);
    }

    private void UnregisterCallbacks()
    {
        if (this.levelFilterToken != -1)
        {
            this.OutputConsole.UnregisterPropertyChangedCallback(
                OutputConsoleView.LevelFilterProperty,
                this.levelFilterToken);
            this.OutputConsole.UnregisterPropertyChangedCallback(
                OutputConsoleView.TextFilterProperty,
                this.textFilterToken);
            this.OutputConsole.UnregisterPropertyChangedCallback(
                OutputConsoleView.FollowTailProperty,
                this.followTailToken);
            this.OutputConsole.UnregisterPropertyChangedCallback(
                OutputConsoleView.IsPausedProperty,
                this.isPausedToken);
            this.OutputConsole.UnregisterPropertyChangedCallback(
                OutputConsoleView.ShowTimestampsProperty,
                this.showTimestampsToken);
            this.OutputConsole.UnregisterPropertyChangedCallback(
                OutputConsoleView.WordWrapProperty,
                this.wordWrapToken);
        }

        this.levelFilterToken = this.textFilterToken = this.followTailToken =
            this.isPausedToken = this.showTimestampsToken = this.wordWrapToken = -1;
    }

    private void OnConsolePropertyChanged(DependencyObject sender, DependencyProperty dp)
    {
        if (this.DataContext is not OutputViewModel vm)
        {
            return;
        }

        if (dp == OutputConsoleView.LevelFilterProperty)
        {
            vm.LevelFilter = this.OutputConsole.LevelFilter;
        }
        else if (dp == OutputConsoleView.TextFilterProperty)
        {
            vm.TextFilter = this.OutputConsole.TextFilter;
        }
        else if (dp == OutputConsoleView.FollowTailProperty)
        {
            vm.FollowTail = this.OutputConsole.FollowTail;
        }
        else if (dp == OutputConsoleView.IsPausedProperty)
        {
            vm.IsPaused = this.OutputConsole.IsPaused;
        }
        else if (dp == OutputConsoleView.ShowTimestampsProperty)
        {
            vm.ShowTimestamps = this.OutputConsole.ShowTimestamps;
        }
        else if (dp == OutputConsoleView.WordWrapProperty)
        {
            vm.WordWrap = this.OutputConsole.WordWrap;
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs e) => this.UnregisterCallbacks();
}
