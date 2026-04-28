// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Validates the editor import-manifest model before native import execution.
/// </summary>
public interface IContentImportManifestValidator
{
    /// <summary>
    /// Validates the manifest and returns diagnostics using the supplied operation id.
    /// </summary>
    /// <param name="operationId">The operation id used to correlate diagnostics.</param>
    /// <param name="manifest">The manifest to validate.</param>
    /// <returns>Validation diagnostics. An empty list means the manifest is valid.</returns>
    public IReadOnlyList<DiagnosticRecord> Validate(Guid operationId, ContentImportManifest manifest);
}
