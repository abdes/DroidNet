// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.LevelEditor;

/// <summary>One pane arrangement offered by the viewport layout picker.</summary>
public sealed partial class ViewportLayoutOption : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="ViewportLayoutOption"/> class.</summary>
    /// <param name="layout">The arrangement.</param>
    /// <param name="label">The label within its group.</param>
    /// <param name="choose">Switches the scene to the arrangement.</param>
    public ViewportLayoutOption(SceneViewLayout layout, string label, Action<SceneViewLayout> choose)
    {
        this.Layout = layout;
        this.Label = label;
        this.ChooseCommand = new RelayCommand(() => choose(layout));
    }

    /// <summary>Gets the arrangement.</summary>
    public SceneViewLayout Layout { get; }

    /// <summary>Gets the label within its group.</summary>
    public string Label { get; }

    /// <summary>Gets the command that switches the scene to the arrangement.</summary>
    public IRelayCommand ChooseCommand { get; }

    /// <summary>Gets or sets a value indicating whether the scene currently uses the arrangement.</summary>
    [ObservableProperty]
    public partial bool IsSelected { get; set; }
}

/// <summary>A titled group of pane arrangements in the layout picker.</summary>
/// <param name="Title">The group title.</param>
/// <param name="Options">The arrangements, in display order.</param>
public sealed record ViewportLayoutGroup(string Title, IReadOnlyList<ViewportLayoutOption> Options);
