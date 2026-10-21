// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Assets;

/// <summary>
///     The project's assets in a workspace window's scope: the asset catalog, its folders and publication, and the
///     asset feed over it.
/// </summary>
/// <remarks>
///     The Content Explorer's scope reaches the same instances, created by the window's scope whichever scope asks
///     first. The project session owns these services once it exists.
/// </remarks>
public sealed class AssetsWindowModule : IScopeModule<IWindowLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<AssetCatalog>(Reuse.Singleton);
        scope.RegisterMapping<IAssetCatalog, AssetCatalog>();
        scope.RegisterMapping<IAssetFolderSource, AssetCatalog>();
        scope.RegisterMapping<IAssetPublicationTarget, AssetCatalog>();
        scope.Register<IAssetFeed, AssetFeed>(Reuse.Singleton);
    }
}
