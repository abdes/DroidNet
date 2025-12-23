// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;

namespace Oxygen.Assets.Import;

/// <summary>
/// Collects diagnostics during an import operation.
/// </summary>
public sealed class ImportDiagnostics
{
    private readonly ConcurrentQueue<ImportDiagnostic> diagnostics = new();

    /// <summary>
    /// Adds a diagnostic.
    /// </summary>
    /// <param name="diagnostic">The diagnostic.</param>
    public void Add(ImportDiagnostic diagnostic)
    {
        ArgumentNullException.ThrowIfNull(diagnostic);
        this.diagnostics.Enqueue(diagnostic);
    }

    /// <summary>
    /// Adds a diagnostic.
    /// </summary>
    public void Add(
        ImportDiagnosticSeverity severity,
        string code,
        string message,
        string? sourcePath = null,
        string? virtualPath = null)
        => this.Add(new ImportDiagnostic(severity, code, message, sourcePath, virtualPath));

    /// <summary>
    /// Returns a snapshot of collected diagnostics.
    /// </summary>
    /// <returns>
    /// A read-only list containing all <see cref="ImportDiagnostic"/> instances collected so far.
    /// </returns>
    public IReadOnlyList<ImportDiagnostic> ToList() => [.. this.diagnostics];
}
