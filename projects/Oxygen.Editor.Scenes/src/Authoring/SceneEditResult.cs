// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>What became of an edit, and what the user should know about it.</summary>
/// <remarks>
/// Engine outcomes are not part of it: the engine projector reports them. The edit succeeded when the scene changed.
/// </remarks>
/// <param name="Outcome">What became of the edit.</param>
/// <param name="Diagnostics">Why it stopped, or the warnings of an edit that applied.</param>
public sealed record SceneEditResult(SceneEditOutcome Outcome, IReadOnlyList<SceneDiagnostic> Diagnostics)
{
    /// <summary>Gets the result of an edit on a retired scene.</summary>
    public static SceneEditResult Retired { get; } = new(
        SceneEditOutcome.Retired,
        [SceneDiagnostic.Error(SceneEditCodes.Retired, "The scene was closed or replaced before the edit could apply.")]);

    /// <summary>Gets a value indicating whether the edit changed the scene.</summary>
    public bool Succeeded => this.Outcome == SceneEditOutcome.Applied;

    /// <summary>Creates the result a validation leads to.</summary>
    /// <param name="validation">The validation.</param>
    /// <returns>The result.</returns>
    public static SceneEditResult Of(SceneEditValidation validation)
    {
        ArgumentNullException.ThrowIfNull(validation);
        return new(validation.Outcome, validation.Diagnostics);
    }
}
