// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Maintains a set of importers and selects the best match for a given probe.
/// </summary>
public sealed class ImporterRegistry
{
    private readonly List<IAssetImporter> importers = [];
    private bool isSorted;

    /// <summary>
    /// Registers an importer.
    /// </summary>
    /// <param name="importer">The importer to add.</param>
    public void Register(IAssetImporter importer)
    {
        ArgumentNullException.ThrowIfNull(importer);
        this.importers.Add(importer);
        this.isSorted = false;
    }

    /// <summary>
    /// Selects the highest-priority importer that can handle the probe.
    /// </summary>
    /// <param name="probe">The probe.</param>
    /// <returns>The selected importer, or <see langword="null"/> if none match.</returns>
    public IAssetImporter? Select(ImportProbe probe)
    {
        ArgumentNullException.ThrowIfNull(probe);

        this.EnsureSorted();

        foreach (var importer in this.importers)
        {
            if (importer.CanImport(probe))
            {
                return importer;
            }
        }

        return null;
    }

    private void EnsureSorted()
    {
        if (this.isSorted)
        {
            return;
        }

        // Deterministic order: priority desc then name asc.
        this.importers.Sort(
            static (a, b) =>
            {
                var p = b.Priority.CompareTo(a.Priority);
                return p != 0
                    ? p
                    : string.Compare(a.Name, b.Name, StringComparison.Ordinal);
            });

        this.isSorted = true;
    }
}
