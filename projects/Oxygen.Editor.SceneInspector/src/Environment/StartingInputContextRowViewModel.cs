// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Environment;

/// <summary>
/// One starting input context of the Play section: its name, which shows it in the Content Browser, its priority,
/// shown as the context's own until overridden, and its removal.
/// </summary>
public sealed partial class StartingInputContextRowViewModel : ObservableObject
{
    private readonly PlaySectionViewModel owner;
    private bool updating;
    private bool inPrioritySession;

    /// <summary>Initializes a new instance of the <see cref="StartingInputContextRowViewModel"/> class.</summary>
    /// <param name="owner">The section that applies the row's edits.</param>
    /// <param name="context">The mapping context asset.</param>
    internal StartingInputContextRowViewModel(PlaySectionViewModel owner, AssetPath context)
    {
        this.owner = owner;
        this.Context = context;
    }

    /// <summary>Gets the mapping context asset.</summary>
    public AssetPath Context { get; }

    /// <summary>Gets the context name.</summary>
    [ObservableProperty]
    public partial string DisplayName { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the context no longer exists.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanReveal))]
    [NotifyCanExecuteChangedFor(nameof(RevealCommand))]
    public partial bool IsMissing { get; private set; }

    /// <summary>Gets or sets the priority the context starts with.</summary>
    [ObservableProperty]
    public partial double Priority { get; set; }

    /// <summary>Gets a value indicating whether the priority overrides the context's own.</summary>
    [ObservableProperty]
    public partial bool IsOverridden { get; private set; }

    /// <summary>Gets the reset button's explanation, which names the context's own priority.</summary>
    [ObservableProperty]
    public partial string ResetToolTip { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the row can be edited.</summary>
    [ObservableProperty]
    public partial bool CanEdit { get; private set; }

    /// <summary>Gets a value indicating whether the context can be shown in the Content Browser.</summary>
    public bool CanReveal => !this.IsMissing;

    /// <summary>Starts an interactive priority edit; the value is committed when it completes.</summary>
    public void BeginPrioritySession() => this.inPrioritySession = true;

    /// <summary>Completes an interactive priority edit.</summary>
    /// <param name="commit">Whether to commit the edited priority.</param>
    public void CompletePrioritySession(bool commit)
    {
        this.inPrioritySession = false;
        if (commit)
        {
            this.CommitPriority();
        }
        else
        {
            this.owner.Refresh();
        }
    }

    /// <summary>Shows the row's current state.</summary>
    /// <param name="state">The state.</param>
    internal void Update(RowState state)
    {
        this.updating = true;
        try
        {
            this.DisplayName = state.DisplayName;
            this.IsMissing = state.IsMissing;
            this.IsOverridden = state.Priority is not null;
            this.Priority = state.Priority ?? state.OwnPriority;
            this.ResetToolTip = string.Create(CultureInfo.CurrentCulture, $"Use the context's own priority ({state.OwnPriority})");
            this.CanEdit = state.CanEdit;
        }
        finally
        {
            this.updating = false;
        }
    }

    partial void OnPriorityChanged(double value)
    {
        _ = value;
        if (!this.updating && !this.inPrioritySession)
        {
            this.CommitPriority();
        }
    }

    [RelayCommand(CanExecute = nameof(CanReveal))]
    private Task RevealAsync() => this.owner.RevealContextAsync(this.Context);

    [RelayCommand]
    private Task RemoveAsync() => this.owner.RemoveContextAsync(this.Context);

    [RelayCommand]
    private Task ResetPriorityAsync() => this.owner.SetContextPriorityAsync(this.Context, priority: null);

    private void CommitPriority()
        => _ = this.owner.SetContextPriorityAsync(this.Context, (int)Math.Round(this.Priority));

    /// <summary>What a row shows.</summary>
    /// <param name="DisplayName">The context name.</param>
    /// <param name="IsMissing">Whether the context no longer exists.</param>
    /// <param name="Priority">The authored priority, or null for the context's own.</param>
    /// <param name="OwnPriority">The context's own priority.</param>
    /// <param name="CanEdit">Whether the scene can be edited.</param>
    internal sealed record RowState(string DisplayName, bool IsMissing, int? Priority, int OwnPriority, bool CanEdit);
}
