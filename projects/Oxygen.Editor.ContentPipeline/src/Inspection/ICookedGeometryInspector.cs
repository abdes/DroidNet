// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Reads geometry slot inventories through the installed native Inspector.</summary>
public interface ICookedGeometryInspector
{
    /// <summary>Inspects one virtual geometry path in a protected cooked root.</summary>
    /// <param name="operationRoot">Private native scratch output.</param>
    /// <param name="cookedRoot">The root held by the caller's read lease.</param>
    /// <param name="virtualPath">The native index path to inspect.</param>
    /// <param name="cancellationToken">Cancels the native inspection.</param>
    /// <param name="artifacts">Optional compatible artifacts retained by the caller.</param>
    /// <returns>The schema-validated native report.</returns>
    public Task<CookedGeometryReport> InspectGeometryAsync(string operationRoot, string cookedRoot, string virtualPath, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null);
}
