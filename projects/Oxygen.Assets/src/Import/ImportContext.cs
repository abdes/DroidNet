// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Provides per-input context to an importer.
/// </summary>
/// <param name="Files">File access abstraction.</param>
/// <param name="Input">The import input.</param>
/// <param name="Identity">Identity policy.</param>
/// <param name="Options">Import options.</param>
/// <param name="Diagnostics">Diagnostic sink.</param>
public sealed record ImportContext(
    IImportFileAccess Files,
    ImportInput Input,
    IAssetIdentityPolicy Identity,
    ImportOptions Options,
    ImportDiagnostics Diagnostics);
