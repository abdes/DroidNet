// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Represents a pluggable source importer.
/// </summary>
public interface IAssetImporter
{
    /// <summary>
    /// Gets the importer name.
    /// </summary>
    public string Name { get; }

    /// <summary>
    /// Gets the importer priority. Higher values win when multiple importers match.
    /// </summary>
    public int Priority { get; }

    /// <summary>
    /// Determines whether the importer can handle the given probe.
    /// </summary>
    /// <param name="probe">The probe information.</param>
    /// <returns>True when this importer can import the source.</returns>
    public bool CanImport(ImportProbe probe);

    /// <summary>
    /// Performs an import operation.
    /// </summary>
    /// <param name="context">The import context.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The imported assets.</returns>
    public Task<IReadOnlyList<ImportedAsset>> ImportAsync(ImportContext context, CancellationToken cancellationToken);
}
