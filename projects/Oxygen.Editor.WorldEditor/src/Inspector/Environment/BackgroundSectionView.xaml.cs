// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector.Controls;
using Oxygen.Editor.World.Inspector.Presentation;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Composes the parent-owned background section without creating another editor lifetime.</summary>
[ViewModel(typeof(BackgroundSectionViewModel))]
public sealed partial class BackgroundSectionView
{
    /// <summary>Initializes a new instance of the <see cref="BackgroundSectionView"/> class.</summary>
    public BackgroundSectionView() => this.InitializeComponent();

    /// <summary>Registers this view's controls by stable browsing identity.</summary>
    /// <param name="registry">The host's presentation registry.</param>
    internal void Register(InspectorSceneFieldRegistry registry)
    {
        registry.Section("Background", this, this.BackgroundSection);
        registry.Field("BackgroundColor", this.BackgroundColorCard);
    }

    private async void ResetBackground_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.ResetBackgroundAsync().ConfigureAwait(true);
        }
    }

    private void OnRgbColorPicked(object? sender, InspectorRgbColorPickedEventArgs args)
    {
        if (args.Owner is SceneEnvironmentEditOwner owner)
        {
            owner.Apply(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, args.Color);
        }
    }

    private void VectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.ViewModel?.EditOwner.BeginEditSession($"BackgroundColor.{args.Component}", args.InteractionKind);

    private void VectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.ViewModel?.EditOwner.CompleteEditSession(new(args.InteractionKind, args.CompletionKind));
}
