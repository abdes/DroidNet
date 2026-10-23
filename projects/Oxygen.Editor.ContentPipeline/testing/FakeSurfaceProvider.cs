// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Testing;

/// <summary>
/// An <see cref="IGeometrySurfaceProvider"/> for tests: the cooked surface inventories a test publishes. A
/// geometry with no inventory reads as <see langword="null"/> (not cooked); one set to fail throws, as an unreadable
/// inventory does.
/// </summary>
/// <remarks>Inventories do not depend on the project a read names.</remarks>
public sealed partial class FakeSurfaceProvider : IGeometrySurfaceProvider, IDisposable
{
    private readonly Lock gate = new();
    private readonly Dictionary<AssetPath, GeometrySurfaceMetadata> inventories = [];
    private readonly Dictionary<AssetPath, Exception> failures = [];
    private SurfaceReadGate? held;

    /// <summary>
    /// Publishes the inventory of a geometry, replacing any earlier one.
    /// </summary>
    /// <param name="geometry">The geometry.</param>
    /// <param name="layoutRevision">The inventory's layout revision.</param>
    /// <param name="surfaces">
    /// The surface ids; each surface is visible and casts shadows unless a scene overrides it, and the geometry has the
    /// schema's default LOD policy.
    /// </param>
    public void Publish(AssetPath geometry, string layoutRevision, params IEnumerable<Guid> surfaces)
        => this.Publish(new GeometrySurfaceMetadata(
            geometry,
            NativeGeometryKey: Guid.Empty,
            layoutRevision,
            [.. surfaces.Select(static surface => new GeometrySurface(surface, surface.ToString(), Visible: true, CastsShadows: true, []))],
            SceneSchema.Geometry.Defaults.LodPolicy));

    /// <summary>
    /// Publishes an inventory of its geometry, replacing any earlier one.
    /// </summary>
    /// <param name="inventory">The inventory, with its surfaces' names and submesh ranges.</param>
    public void Publish(GeometrySurfaceMetadata inventory)
    {
        ArgumentNullException.ThrowIfNull(inventory);
        lock (this.gate)
        {
            this.inventories[inventory.GeometryUri] = inventory;
            _ = this.failures.Remove(inventory.GeometryUri);
        }
    }

    /// <summary>
    /// Removes the inventory of a geometry, so it reads as not cooked.
    /// </summary>
    /// <param name="geometry">The geometry.</param>
    public void Remove(AssetPath geometry)
    {
        lock (this.gate)
        {
            _ = this.inventories.Remove(geometry);
            _ = this.failures.Remove(geometry);
        }
    }

    /// <summary>
    /// Makes reads of a geometry's inventory throw.
    /// </summary>
    /// <param name="geometry">The geometry.</param>
    /// <param name="error">The error, such as an <see cref="IOException"/>.</param>
    public void Fail(AssetPath geometry, Exception error)
    {
        lock (this.gate)
        {
            this.failures[geometry] = error;
        }
    }

    /// <summary>
    /// Holds every read until the returned gate is released, so a test can change the scene while a read is pending.
    /// </summary>
    /// <returns>The gate. Disposing it releases the reads.</returns>
    public SurfaceReadGate HoldReads()
    {
        lock (this.gate)
        {
            if (this.held is not null)
            {
                throw new InvalidOperationException("Reads are already held.");
            }

            this.held = new SurfaceReadGate(this);
            return this.held;
        }
    }

    /// <summary>
    /// Releases held reads, so no read outlives the test.
    /// </summary>
    public void Dispose() => this.held?.Dispose();

    /// <inheritdoc/>
    public async Task<GeometrySurfaceMetadata?> ReadAsync(ProjectContext project, AssetPath geometryUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(geometryUri);
        SurfaceReadGate? wait;
        lock (this.gate)
        {
            wait = this.held;
        }

        if (wait is not null)
        {
            await wait.WaitAsync(cancellationToken).ConfigureAwait(true);
        }

        lock (this.gate)
        {
            return this.failures.TryGetValue(geometryUri, out var error)
                ? throw error
                : this.inventories.GetValueOrDefault(geometryUri);
        }
    }

    internal void Released(SurfaceReadGate gate)
    {
        lock (this.gate)
        {
            if (ReferenceEquals(this.held, gate))
            {
                this.held = null;
            }
        }
    }
}
