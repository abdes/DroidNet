// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns background channel presentation and reset policy, borrowing scene command ownership.</summary>
public sealed partial class BackgroundSectionViewModel : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="BackgroundSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    internal BackgroundSectionViewModel(SceneEnvironmentEditOwner owner) => this.EditOwner = owner;

    [ObservableProperty]
    public partial float BackgroundR { get; set; }

    [ObservableProperty]
    public partial float BackgroundG { get; set; }

    [ObservableProperty]
    public partial float BackgroundB { get; set; }

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the authored linear RGB background color.</summary>
    public Vector3 BackgroundColor => new(this.BackgroundR, this.BackgroundG, this.BackgroundB);

    /// <summary>Gets canonical background feedback.</summary>
    public InspectorFieldDiagnostic BackgroundRDiagnostic => this.EditOwner.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BackgroundColor.Id);

    /// <summary>Gets canonical background feedback.</summary>
    public InspectorFieldDiagnostic BackgroundGDiagnostic => this.BackgroundRDiagnostic;

    /// <summary>Gets canonical background feedback.</summary>
    public InspectorFieldDiagnostic BackgroundBDiagnostic => this.BackgroundRDiagnostic;

    /// <summary>Authors a complete linear color as one scene edit.</summary>
    /// <param name="color">The authored linear color.</param>
    public void SetBackgroundColor(Vector3 color)
    {
        this.EditOwner.Refresh(() => this.Refresh(color));
        this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, color);
    }

    /// <summary>Restores the default color without losing a pending gesture's captured target.</summary>
    /// <returns>The reset completion.</returns>
    public async Task ResetBackgroundAsync()
    {
        var scene = this.EditOwner.Scene;
        if (!this.EditOwner.IsInputEnabled || scene is null)
        {
            return;
        }

        this.EditOwner.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await this.EditOwner.Pending.ConfigureAwait(true);
        if (this.EditOwner.IsInputEnabled && ReferenceEquals(scene, this.EditOwner.Scene))
        {
            this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, new SceneEnvironmentData().BackgroundColor);
            await this.EditOwner.Pending.ConfigureAwait(true);
        }
    }

    /// <summary>Displays authored channels under the parent's model-refresh guard.</summary>
    /// <param name="color">The authored linear color.</param>
    internal void Refresh(Vector3 color)
    {
        this.BackgroundR = color.X;
        this.BackgroundG = color.Y;
        this.BackgroundB = color.Z;
        this.OnPropertyChanged(nameof(this.BackgroundColor));
    }

    partial void OnBackgroundRChanged(float value) => this.ApplyColor(new(value, this.BackgroundG, this.BackgroundB));

    partial void OnBackgroundGChanged(float value) => this.ApplyColor(new(this.BackgroundR, value, this.BackgroundB));

    partial void OnBackgroundBChanged(float value) => this.ApplyColor(new(this.BackgroundR, this.BackgroundG, value));

    private void ApplyColor(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.BackgroundColor));
        this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, color);
    }
}
