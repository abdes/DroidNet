// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Core;

namespace Oxygen.Assets.Model;

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
[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "multiple constructors")]
public sealed class AssetReference<T> : ScopedObservableObject
where T : Asset
{
    private Uri uri;
    private T? asset;

    /// <summary>
    /// Initializes a new instance of the <see cref="AssetReference{T}"/> class with the specified asset URI.
    /// </summary>
    /// <param name="uri">The <see cref="Uri"/> of the referenced asset.</param>
    public AssetReference(Uri uri)
    {
        this.uri = uri;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="AssetReference{T}"/> class from the specified URI string.
    /// </summary>
    /// <param name="uriString">A string that represents the URI of the referenced asset.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="uriString"/> is null.</exception>
    /// <exception cref="UriFormatException">Thrown when <paramref name="uriString"/> is not a valid URI.</exception>
    public AssetReference(string uriString)
        : this(new Uri(uriString))
    {
    }

    /// <summary>
    /// Gets or sets the URI of the referenced asset.
    /// </summary>
    /// <value>
    /// The asset URI in the format <c>asset:///{MountPoint}/{Path}</c>.
    /// Setting this property will invalidate <see cref="Asset"/> if the new URI does not match the current asset's URI.
    /// </value>
    public Uri Uri
    {
        get => this.uri;
        set
        {
            ArgumentException.ThrowIfNullOrEmpty(value.ToString(), nameof(value));

            // SetProperty already checks equality and returns true only if changed
            if (this.SetProperty(ref this.uri, value))
            {
                // Invalidate the asset when URI changes (unless it matches the current asset's URI)
                if (this.asset?.Uri.Equals(value) == false)
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
                if (value?.Uri.Equals(this.Uri) == false)
                {
                    this.Uri = value.Uri;
                }
            }
        }
    }
}
