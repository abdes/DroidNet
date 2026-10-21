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
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<ContentExplorerViewModel>(Reuse.Transient, setup: Setup.With(allowDisposableTransient: true));
        scope.Register<ContentExplorerView>(Reuse.Transient);

        // The window's explorer shows the assets that parts ask to reveal, and creates the scripts they ask for.
        scope.Register<ContentExplorerAssetReveal>(Reuse.Singleton);
        scope.RegisterMapping<IAssetReveal, ContentExplorerAssetReveal>();
        scope.Register<ContentExplorerScriptCreation>(Reuse.Singleton);
        scope.RegisterMapping<IScriptCreation, ContentExplorerScriptCreation>();
    }
}
