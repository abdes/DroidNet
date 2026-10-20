// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Data of <see cref="IScripts.Changed"/>.
/// </summary>
/// <param name="script">The script that changed or went away; <see langword="null"/> when every script may have
/// changed, as when the project changed.</param>
public sealed class ScriptChangedEventArgs(AssetPath? script) : EventArgs
{
    /// <summary>Gets the script that changed or went away; <see langword="null"/> when every script may have changed.</summary>
    public AssetPath? Script { get; } = script;
}
