// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Result returned by the engine content-pipeline adapter.
/// </summary>
/// <param name="Succeeded">Whether native import succeeded.</param>
/// <param name="Diagnostics">Adapted native diagnostics.</param>
public sealed record NativeImportResult(bool Succeeded, IReadOnlyList<DiagnosticRecord> Diagnostics);
