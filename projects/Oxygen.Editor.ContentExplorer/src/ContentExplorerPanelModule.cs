// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.ContentExplorer.Panes.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.Shell.Desktop;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>
/// The Content Explorer's own scope: its panes, their layouts and dialogs, the selected assets' details, and the
/// creation and relocation workflows it runs. The explorer's state is the explorer's instance, registered when the scope
/// is created.
/// </summary>
public sealed class ContentExplorerPanelModule : IScopeModule<IPanelLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <summary>Registers layouts owned and disposed by their router outlet; session state is shared separately.</summary>
    /// <param name="scope">The explorer's scope.</param>
    internal static void RegisterAssetLayouts(IRegistrator scope)
    {
        scope.Register<ListLayoutViewModel>(Reuse.Transient, setup: Setup.With(allowDisposableTransient: true));
        scope.Register<ListLayoutView>(Reuse.Transient);
        scope.Register<TilesLayoutViewModel>(Reuse.Transient, setup: Setup.With(allowDisposableTransient: true));
        scope.Register<TilesLayoutView>(Reuse.Transient);
        scope.Register<DetailsLayoutViewModel>(Reuse.Transient, setup: Setup.With(allowDisposableTransient: true));
        scope.Register<DetailsLayoutView>(Reuse.Transient);
    }

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IDesktopIntegration, WindowsDesktopIntegration>(Reuse.Singleton);
        scope.Register<Relocation.IAssetRelocationWorkflow, Relocation.AssetRelocationWorkflow>(Reuse.Singleton);
        scope.Register<AssetDetailsViewModel>(Reuse.Singleton);
        scope.Register<Authoring.AuthoredAssetCreation>(Reuse.Singleton);

        scope.Register<ContentFoldersPane>(Reuse.Singleton);
        scope.RegisterMapping<IContentFoldersPane, ContentFoldersPane>();
        scope.Register<ContentFoldersViewModel>(Reuse.Transient, setup: Setup.With(allowDisposableTransient: true));
        scope.Register<ContentFoldersView>(Reuse.Transient);

        // Dialog views used via Aura's IDialogService + VmToViewConverter.
        scope.Register<LocalFolderMountDialogViewModel>(Reuse.Transient);
        scope.Register<LocalFolderMountDialogView>(Reuse.Transient);
        scope.Register<Importing.SceneImportDialogView>(Reuse.Transient);

        scope.Register<AssetsViewModel>(Reuse.Singleton);
        scope.Register<AssetsView>(Reuse.Transient);
        RegisterAssetLayouts(scope);
    }
}
