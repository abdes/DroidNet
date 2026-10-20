// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Diagnostics;

/// <summary>
/// Reports the problems of a family's authored files, as its facts currently know them.
/// </summary>
/// <remarks>
/// The Problems panel shows every source; each family reports its own problem type, which specializes
/// <see cref="AssetProblem"/>.
/// </remarks>
public interface IAssetProblemSource
{
    /// <summary>
    /// Occurs when <see cref="Problems"/> changes. Raised on the <see cref="SynchronizationContext"/> that was current
    /// when the handler subscribed, or on the thread pool when there was none.
    /// </summary>
    public event EventHandler? ProblemsChanged;

    /// <summary>Gets the current problems.</summary>
    public IReadOnlyList<AssetProblem> Problems { get; }
}
