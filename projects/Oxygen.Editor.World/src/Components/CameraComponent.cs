// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Base class for camera components.
/// Camera domain objects implement Hydrate/Dehydrate via <see cref="ComponentData"/> and are created
/// by factories registered on concrete DTO types (see <c>PerspectiveCamera</c> and <c>OrthographicCamera</c>).
/// </summary>
public abstract partial class CameraComponent : GameComponent
{
    /// <summary>
    /// Gets or sets the near clipping plane distance.
    /// </summary>
    public float NearPlane { get; set; } = 0.1f;

    /// <summary>
    /// Gets or sets the far clipping plane distance.
    /// </summary>
    public float FarPlane { get; set; } = 1000f;

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);

        if (data is not CameraComponentData cd)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.NearPlane = cd.NearPlane;
            this.FarPlane = cd.FarPlane;
        }
    }

    /// <summary>
    /// Dehydrate is implemented by concrete camera components which return the
    /// correct concrete DTO type (e.g. <see cref="PerspectiveCameraData"/>).
    /// </summary>
    /// <returns>
    /// Throws <see cref="System.NotSupportedException"/> in the base class. Concrete camera components return the appropriate <see cref="ComponentData"/> DTO.
    /// </returns>
    public override ComponentData Dehydrate() => throw new System.NotSupportedException();

    // Intentionally no IPersistent<TData> on the domain types; concrete components register
    // factories for their DTO types and Hydrate/Dehydrate using ComponentData-based methods.
}
