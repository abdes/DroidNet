// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>The Scene Explorer panel: the tree of the active scene document's nodes and explorer folders.</summary>
public sealed class SceneExplorerModule : IFeatureModule
{
    /// <summary>The Scene Explorer panel's identity.</summary>
    public const string PanelId = "se";

    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [new(PanelId, "Scene Explorer", typeof(SceneExplorerViewModel))];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands => SceneExplorerCommands.All;

    /// <inheritdoc />
    public void RegisterWindowServices(IContainer window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window.Register<SceneExplorerViewModel>(Reuse.Transient);
        window.Register<SceneExplorerView>(Reuse.Transient);
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window) => ArgumentNullException.ThrowIfNull(window);
}
