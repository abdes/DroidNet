// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Resolves candidate virtual paths through the engine-owned identity policy.</summary>
public interface ICookedAssetKeyProvider
{
    /// <summary>Maps a batch of canonical native paths without cooking their content.</summary>
    /// <param name="operationRoot">Private operation scratch space.</param>
    /// <param name="virtualPaths">Canonical candidate output paths.</param>
    /// <param name="cancellationToken">Cancels the owned worker and awaits drain.</param>
    /// <param name="artifacts">Optional borrowed native artifact ownership.</param>
    /// <returns>The validated native identity map.</returns>
    public Task<CookedAssetKeyMap> ResolveAssetKeysAsync(string operationRoot, IReadOnlyList<string> virtualPaths, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null);
}
