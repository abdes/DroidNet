// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>Turns the values a person entered into the values an inspector edit commits.</summary>
/// <remarks>
/// An inspector edit is a commit of outside values: a finite value outside its property's range is clamped into it where
/// the scene schema declares <c>commit: clamp</c> (<see cref="PropertyChecks.Commit"/>). Every other value goes to the
/// scene edit as entered, and the edit refuses one that is out of range. Every inspector edit passes here once, before
/// it is submitted.
/// </remarks>
internal static class InspectorCommits
{
    /// <summary>Gets the values an edit commits.</summary>
    /// <param name="edit">The entered values.</param>
    /// <returns>The values to submit.</returns>
    public static PropertyEdit Commit(PropertyEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        return PropertyEdit.Of(edit.Select(static entry => KeyValuePair.Create(entry.Key, PropertyChecks.Commit(entry.Key, entry.Value))));
    }

    /// <summary>Gets the values a per-target edit commits.</summary>
    /// <param name="edits">The entered values, by target.</param>
    /// <returns>The values to submit, by target.</returns>
    public static IReadOnlyDictionary<NodeReference, PropertyEdit> Commit(IReadOnlyDictionary<NodeReference, PropertyEdit> edits)
    {
        ArgumentNullException.ThrowIfNull(edits);
        return edits.ToDictionary(static pair => pair.Key, static pair => Commit(pair.Value));
    }
}
