// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Cooked loose-root validation result.
/// </summary>
/// <param name="CookedRoot">The validated cooked root.</param>
/// <param name="Succeeded">Whether validation succeeded.</param>
/// <param name="Diagnostics">Validation diagnostics.</param>
public sealed record CookValidationResult(
    string CookedRoot,
    bool Succeeded,
    IReadOnlyList<DiagnosticRecord> Diagnostics);
