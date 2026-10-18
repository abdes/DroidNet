// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The cooked material slot inventories of geometries. A geometry with no inventory reads as <see langword="null"/>
/// (not cooked); one set to fail throws, as an unreadable inventory does.
/// </summary>
public sealed partial class FakeMaterialSlotProvider : IGeometryMaterialSlotProvider, IDisposable
{
    private readonly Lock gate = new();
    private readonly Dictionary<AssetPath, GeometryMaterialSlotMetadata> inventories = [];
    private readonly Dictionary<AssetPath, Exception> failures = [];
    private MaterialSlotReadGate? held;

    /// <summary>
    /// Gets a project context for the edits that read inventories; this provider ignores it.
    /// </summary>
    public static ProjectContext Project { get; } = new()
    {
        ProjectId = Guid.Empty,
        Name = "Test",
        Category = Category.Games,
        ProjectRoot = FileSystemPath.From(Path.GetTempPath()),
        AuthoringMounts = [],
        LocalFolderMounts = [],
    };

    /// <summary>
    /// Publishes the inventory of a geometry, replacing any earlier one.
    /// </summary>
    /// <param name="geometry">The geometry.</param>
    /// <param name="layoutRevision">The inventory's layout revision.</param>
    /// <param name="slots">The slot ids.</param>
    public void Publish(AssetPath geometry, string layoutRevision, params IEnumerable<Guid> slots)
    {
        var metadata = new GeometryMaterialSlotMetadata(
            geometry,
            NativeGeometryKey: Guid.Empty,
            layoutRevision,
            [.. slots.Select(static slot => new GeometryMaterialSlot(slot, slot.ToString(), []))]);
        lock (this.gate)
        {
            this.inventories[geometry] = metadata;
            _ = this.failures.Remove(geometry);
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
    public MaterialSlotReadGate HoldReads()
    {
        lock (this.gate)
        {
            if (this.held is not null)
            {
                throw new InvalidOperationException("Reads are already held.");
            }

            this.held = new MaterialSlotReadGate(this);
            return this.held;
        }
    }

    /// <summary>
    /// Releases held reads, so no read outlives the test.
    /// </summary>
    public void Dispose() => this.held?.Dispose();

    /// <inheritdoc/>
    public async Task<GeometryMaterialSlotMetadata?> ReadAsync(ProjectContext project, AssetPath geometryUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(geometryUri);
        MaterialSlotReadGate? wait;
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

    internal void Released(MaterialSlotReadGate gate)
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
