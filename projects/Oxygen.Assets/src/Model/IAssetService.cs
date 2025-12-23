// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets;

/// <summary>
/// Provides asset loading and resolution services.
/// </summary>
/// <remarks>
/// <para>
/// The asset service acts as the central orchestrator for asset resolution, maintaining
/// a registry of <see cref="IAssetResolver"/> implementations and delegating requests
/// based on the URI's authority component.
/// </para>
/// <para>
/// Implementations of this service are responsible for:
/// <list type="bullet">
/// <item>Managing the resolver registry</item>
/// <item>Parsing asset URIs to extract the authority</item>
/// <item>Selecting the appropriate resolver</item>
/// <item>Invoking the resolver and returning the result</item>
/// </list>
/// </para>
/// </remarks>
public interface IAssetService
{
    /// <summary>
    /// Registers an asset resolver with the service.
    /// </summary>
    /// <param name="resolver">The resolver to register.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="resolver"/> is <see langword="null"/>.</exception>
    public void RegisterResolver(IAssetResolver resolver);

    /// <summary>
    /// Asynchronously loads an asset of type <typeparamref name="T"/> from the specified URI.
    /// </summary>
    /// <typeparam name="T">The type of asset to load, must derive from <see cref="Asset"/>.</typeparam>
    /// <param name="uri">The asset URI to resolve (e.g., "asset://Generated/BasicShapes/Cube").</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result contains the loaded asset
    /// of type <typeparamref name="T"/>, or <see langword="null"/> if the asset could not be found,
    /// loaded, or is not of the expected type.
    /// </returns>
    public Task<T?> LoadAssetAsync<T>(Uri uri)
        where T : Asset;
}
