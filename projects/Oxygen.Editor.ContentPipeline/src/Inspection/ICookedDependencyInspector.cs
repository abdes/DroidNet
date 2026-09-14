// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Decodes native dependency identities without starting the rendering engine.</summary>
public interface ICookedDependencyInspector
{
    /// <summary>Inspects one protected library under the owned native-worker contract.</summary>
    /// <param name="operationRoot">Private scratch directory owned by the operation.</param>
    /// <param name="cookedRoot">The library whose bytes the caller protects.</param>
    /// <param name="cancellationToken">Cancels the worker and awaits its drain.</param>
    /// <param name="artifacts">Optional existing ownership of the installed native dependencies.</param>
    /// <returns>The validated native report.</returns>
    public Task<CookedDependencyReport> InspectDependenciesAsync(string operationRoot, string cookedRoot, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null);
}
