// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns curve-key editing policy while borrowing the scene's existing edit owner.</summary>
public sealed partial class ExposureCompensationCurveEditorViewModel : ObservableObject, IDisposable
{
    private readonly Action<ImmutableArray<ExposureCompensationKeyData>> submit;
    private readonly Func<bool> inputEnabled;
    private readonly IInspectorEditSessionOwner editOwner;
    private ImmutableArray<ExposureCompensationKeyData> curve = [];
    private bool synchronizing;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ExposureCompensationCurveEditorViewModel"/> class.</summary>
    /// <param name="submit">The owning section's canonical curve submission.</param>
    /// <param name="inputEnabled">The owning section's current input gate.</param>
    /// <param name="editOwner">The borrowed scene edit-session owner.</param>
    /// <param name="diagnostic">The scene owner's canonical curve feedback.</param>
    internal ExposureCompensationCurveEditorViewModel(
        Action<ImmutableArray<ExposureCompensationKeyData>> submit,
        Func<bool> inputEnabled,
        IInspectorEditSessionOwner editOwner,
        InspectorFieldDiagnostic diagnostic)
    {
        this.submit = submit;
        this.inputEnabled = inputEnabled;
        this.editOwner = editOwner;
        this.Diagnostic = diagnostic;
    }

    /// <summary>Gets the authored curve, without view-owned projected points.</summary>
    public ImmutableArray<ExposureCompensationKeyData> Curve
    {
        get => this.curve;
        private set => this.SetProperty(ref this.curve, value);
    }

    /// <summary>Gets canonical field feedback borrowed from the scene owner.</summary>
    public InspectorFieldDiagnostic Diagnostic { get; }

    /// <summary>Gets the stable editable key rows.</summary>
    internal ObservableCollection<ExposureCompensationKeyViewModel> Keys { get; } = [];

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        foreach (var key in this.Keys)
        {
            key.Changed -= this.OnKeyChanged;
        }

        this.disposed = true;
    }

    /// <summary>Refreshes stable rows after authored changes, rejection or history navigation.</summary>
    /// <param name="value">The current authored curve.</param>
    internal void Refresh(ImmutableArray<ExposureCompensationKeyData> value)
    {
        if (this.disposed)
        {
            return;
        }

        this.synchronizing = true;
        try
        {
            this.Curve = value.IsDefault ? [] : value;
            while (this.Keys.Count > this.Curve.Length)
            {
                var removed = this.Keys[^1];
                removed.Changed -= this.OnKeyChanged;
                this.Keys.RemoveAt(this.Keys.Count - 1);
            }

            for (var index = 0; index < this.Curve.Length; index++)
            {
                if (index < this.Keys.Count)
                {
                    var existing = this.Keys[index];
                    existing.MeteredEv = this.Curve[index].MeteredEv;
                    existing.CompensationEv = this.Curve[index].CompensationEv;
                }
                else
                {
                    var key = new ExposureCompensationKeyViewModel(this.Curve[index]);
                    key.Changed += this.OnKeyChanged;
                    this.Keys.Add(key);
                }
            }
        }
        finally
        {
            this.synchronizing = false;
        }
    }

    /// <summary>Begins the existing scene gesture for either key coordinate.</summary>
    /// <param name="args">The original numeric interaction.</param>
    internal void Begin(NumberBoxEditSessionEventArgs args)
        => this.editOwner.BeginEditSession("AutoExposureCompensationCurve", args.InteractionKind);

    /// <summary>Completes the borrowed scene gesture without creating another transaction owner.</summary>
    /// <param name="args">The original numeric completion.</param>
    internal void Complete(NumberBoxEditSessionEventArgs args)
        => this.editOwner.CompleteEditSession(args);

    /// <summary>Validates finite key values and strict adjacent EV ordering.</summary>
    /// <param name="key">The edited stable key row.</param>
    /// <param name="editMeteredEv">Whether the edited coordinate is metered EV.</param>
    /// <param name="candidate">The proposed numeric value.</param>
    /// <returns>Whether the current row can accept the value.</returns>
    internal bool Validate(ExposureCompensationKeyViewModel key, bool editMeteredEv, float candidate)
    {
        if (this.disposed || !this.inputEnabled() || !float.IsFinite(candidate))
        {
            return false;
        }

        if (!editMeteredEv)
        {
            return this.Keys.Contains(key);
        }

        var index = this.Keys.IndexOf(key);
        return index >= 0
            && (index == 0 || candidate > this.Keys[index - 1].MeteredEv)
            && (index == this.Keys.Count - 1 || candidate < this.Keys[index + 1].MeteredEv);
    }

    private void OnKeyChanged(object? sender, EventArgs args)
    {
        if (!this.synchronizing && !this.disposed && this.inputEnabled())
        {
            this.submit([.. this.Keys.Select(static key => key.ToData())]);
        }
    }

    [RelayCommand]
    private void AddKey()
    {
        if (this.disposed || !this.inputEnabled() || this.Keys.Count >= 64)
        {
            return;
        }

        var meteredEv = this.Keys.Count == 0 ? 0 : this.Keys[^1].MeteredEv + 1;
        var key = new ExposureCompensationKeyViewModel(new(meteredEv, 0));
        key.Changed += this.OnKeyChanged;
        this.Keys.Add(key);
        this.submit([.. this.Keys.Select(static row => row.ToData())]);
    }

    [RelayCommand]
    private void RemoveKey(object? parameter)
    {
        if (this.disposed || !this.inputEnabled() || parameter is not ExposureCompensationKeyViewModel key || !this.Keys.Remove(key))
        {
            return;
        }

        key.Changed -= this.OnKeyChanged;
        this.submit([.. this.Keys.Select(static row => row.ToData())]);
    }
}
