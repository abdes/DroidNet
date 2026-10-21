// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>
/// The Content Explorer panel: the project's content folders and mounts, and the assets of the folders it shows. The
/// window's other parts show assets and create scripts through it.
/// </summary>
public sealed class ContentExplorerModule : IFeatureModule
{
    /// <summary>The Content Explorer panel's identity.</summary>
    public const string PanelId = "content";

    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [new(PanelId, "Content Explorer", typeof(ContentExplorerViewModel))];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

    /// <inheritdoc />
    public void RegisterWindowServices(IContainer window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window.Register<ContentExplorerViewModel>(Reuse.Transient);
        window.Register<ContentExplorerView>(Reuse.Transient);

        // The window's explorer shows the assets that parts ask to reveal, and creates the scripts they ask for.
        window.Register<ContentExplorerAssetReveal>(Reuse.Singleton);
        window.RegisterMapping<IAssetReveal, ContentExplorerAssetReveal>();
        window.Register<ContentExplorerScriptCreation>(Reuse.Singleton);
        window.RegisterMapping<IScriptCreation, ContentExplorerScriptCreation>();
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window) => ArgumentNullException.ThrowIfNull(window);
}
