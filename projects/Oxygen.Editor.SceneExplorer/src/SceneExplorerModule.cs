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
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // One view model per window, which its views share and the window disposes.
        scope.Register<SceneExplorerViewModel>(Reuse.Singleton);
        scope.Register<SceneExplorerView>(Reuse.Transient);
    }
}
