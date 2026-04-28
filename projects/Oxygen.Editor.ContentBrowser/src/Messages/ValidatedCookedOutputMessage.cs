// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
/// Message published after cooked output has passed validation and may be mounted by runtime hosts.
/// </summary>
/// <param name="CookedRoots">The validated cooked roots.</param>
public sealed record ValidatedCookedOutputMessage(IReadOnlyList<string> CookedRoots)
{
    public ValidatedCookedOutputMessage(string cookedRoot)
        : this([cookedRoot])
    {
    }
}
