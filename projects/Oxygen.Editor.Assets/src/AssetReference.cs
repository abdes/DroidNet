// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.Core;

namespace Oxygen.Editor.Assets;

/// <summary>
/// Represents a serializable reference to an asset of type <typeparamref name="T"/>.
/// </summary>
/// <typeparam name="T">The type of asset being referenced, must derive from <see cref="Asset"/>.</typeparam>
/// <remarks>
/// <para>
/// This class provides a lightweight, observable container for asset references. It maintains
/// synchronization between the <see cref="Uri"/> (serialized) and the <see cref="Asset"/> (runtime-only)
/// to prevent inconsistent states.
/// </para>
/// <para>
/// The class inherits from <see cref="ScopedObservableObject"/> to provide property change notifications
/// and the ability to suppress notifications during batch operations.
/// </para>
/// </remarks>
public sealed class AssetReference<T> : ScopedObservableObject
    where T : Asset
{
    private string? uri;
    private T? asset;

    /// <summary>
    /// Gets or sets the URI of the referenced asset.
    /// </summary>
    /// <value>
    /// The asset URI in the format <c>asset://{MountPoint}/{Path}</c>.
    /// Setting this property will invalidate <see cref="Asset"/> if the new URI does not match
    /// the current asset's URI.
    /// </value>
    public string? Uri
    {
        get => this.uri;
        set
        {
            // SetProperty already checks equality and returns true only if changed
            if (this.SetProperty(ref this.uri, value))
            {
                // Invalidate the asset when URI changes (unless it matches the current asset's URI)
                if (this.asset is not null && this.asset.Uri != value)
                {
                    this.Asset = null;
                }
            }
        }
    }

    /// <summary>
    /// Gets or sets the resolved asset instance.
    /// </summary>
    /// <value>
    /// The loaded asset metadata. This property is not serialized (runtime-only).
    /// Setting this property will synchronize <see cref="Uri"/> to match the asset's URI.
    /// </value>
    [JsonIgnore]
    public T? Asset
    {
        get => this.asset;
        set
        {
            // SetProperty already checks equality and returns true only if changed
            if (this.SetProperty(ref this.asset, value))
            {
                // Sync URI when Asset is set
                if (value is not null && this.Uri != value.Uri)
                {
                    this.Uri = value.Uri;
                }
            }
        }
    }
}
