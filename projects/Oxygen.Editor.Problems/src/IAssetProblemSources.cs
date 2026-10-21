// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Problems;

/// <summary>The project session's problem sources, in the order the session registers them.</summary>
/// <remarks>
///     The session lists its own sources: the script facts, the input facts, then the running scene's problems. A
///     window's panel reads this list, since a collection it resolved itself would see only the window's registrations.
/// </remarks>
public interface IAssetProblemSources
{
    /// <summary>Gets the problem sources, each once, in registration order.</summary>
    public IReadOnlyList<IAssetProblemSource> Sources { get; }
}
