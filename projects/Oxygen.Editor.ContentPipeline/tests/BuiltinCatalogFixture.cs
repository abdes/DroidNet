// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Loads descriptor payloads exported by the native builtin-catalog command.</summary>
internal sealed class BuiltinCatalogFixture : IBuiltinGeometryCatalogProvider
{
    /// <inheritdoc />
    public async Task<BuiltinGeometryCatalog> GetBuiltinGeometryCatalogAsync(string projectRoot, string mountName, CancellationToken cancellationToken, Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts = null)
    {
        var json = await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), cancellationToken).ConfigureAwait(false);
        var catalog = BuiltinGeometryCatalog.Parse(json);
        return string.Equals(mountName, catalog.MountName, StringComparison.Ordinal)
            ? catalog : throw new InvalidOperationException("The exported test fixture uses the Content mount.");
    }
}
