// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.CompilerServices;
using DroidNet.Controls.OutputConsole.Model;

namespace Oxygen.Editor.WorldEditor.ViewModels;

/// <summary>
///     The ViewModel for managing and displaying logs in the application.
/// </summary>
public class LogsViewModel : INotifyPropertyChanged
{
    public LogsViewModel(OutputLogBuffer buffer)
    {
        this.Buffer = buffer;
    }

    public OutputLogBuffer Buffer { get; }

    // Persisted console view settings (two-way bound to the view)
    private LevelMask levelFilter = LevelMask.Information | LevelMask.Warning | LevelMask.Error | LevelMask.Fatal; // sensible initial default
    private string textFilter = string.Empty;
    private bool followTail = true;
    private bool isPaused;
    private bool showTimestamps;
    private bool wordWrap;

    public LevelMask LevelFilter
    {
        get => this.levelFilter;
        set => this.SetField(ref this.levelFilter, value);
    }

    public string TextFilter
    {
        get => this.textFilter;
        set => this.SetField(ref this.textFilter, value ?? string.Empty);
    }

    public bool FollowTail
    {
        get => this.followTail;
        set => this.SetField(ref this.followTail, value);
    }

    public bool IsPaused
    {
        get => this.isPaused;
        set => this.SetField(ref this.isPaused, value);
    }

    public bool ShowTimestamps
    {
        get => this.showTimestamps;
        set => this.SetField(ref this.showTimestamps, value);
    }

    public bool WordWrap
    {
        get => this.wordWrap;
        set => this.SetField(ref this.wordWrap, value);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? name = null) => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        this.OnPropertyChanged(name);
        return true;
    }
}
