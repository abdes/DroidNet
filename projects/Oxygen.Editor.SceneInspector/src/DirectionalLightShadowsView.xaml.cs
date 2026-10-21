// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Composes shadow/cascade presentation using the same owning light model.</summary>
[ViewModel(typeof(DirectionalLightViewModel))]
public sealed partial class DirectionalLightShadowsView
{
    /// <summary>Initializes a new instance of the <see cref="DirectionalLightShadowsView"/> class.</summary>
    public DirectionalLightShadowsView() => this.InitializeComponent();

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args) => this.ViewModel?.CompleteEditSession(args);
}
