// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Result of an import operation.
/// </summary>
/// <param name="Imported">The imported assets (canonical representation).</param>
/// <param name="Diagnostics">Collected diagnostics.</param>
/// <param name="Succeeded">True when the import completed successfully.</param>
public sealed record ImportResult(
    IReadOnlyList<ImportedAsset> Imported,
    IReadOnlyList<ImportDiagnostic> Diagnostics,
    bool Succeeded);
