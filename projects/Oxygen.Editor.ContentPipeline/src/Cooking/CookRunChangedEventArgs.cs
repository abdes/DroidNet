// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Publishes a revised cook and whether an explicit request should reveal it.</summary>
/// <param name="run">The immutable run state.</param>
/// <param name="reveal">Whether the user explicitly requested this run.</param>
public sealed class CookRunChangedEventArgs(CookRunSnapshot run, bool reveal = false) : EventArgs
{
    /// <summary>Gets the revised run.</summary>
    public CookRunSnapshot Run { get; } = run;

    /// <summary>Gets a value indicating whether the panel should reveal and select the run.</summary>
    public bool Reveal { get; } = reveal;
}
