// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Assets;

/// <summary>
///     The project's assets in a project session's scope: the asset catalog, its folders and publication, and the asset
///     feed over it.
/// </summary>
/// <remarks>
///     Every window of the session lists the same assets. A change of the session's content mounts lists them again,
///     and closing the session disposes them.
/// </remarks>
public sealed class AssetsSessionModule : IScopeModule<ISessionLayer>
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
