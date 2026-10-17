// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>Whether an edit can apply to the scene as it is, and why not.</summary>
public sealed record SceneEditValidation
{
    private SceneEditValidation(SceneEditOutcome outcome, IReadOnlyList<SceneDiagnostic> diagnostics)
    {
        this.Outcome = outcome;
        this.Diagnostics = diagnostics;
    }

    /// <summary>Gets a validation that passed with nothing to report.</summary>
    public static SceneEditValidation Valid { get; } = new(SceneEditOutcome.Applied, []);

    /// <summary>
    /// Gets the outcome the validation leads to: <see cref="SceneEditOutcome.Applied"/> when it passed,
    /// <see cref="SceneEditOutcome.Refused"/> or <see cref="SceneEditOutcome.Invalid"/> when it failed.
    /// </summary>
    public SceneEditOutcome Outcome { get; }

    /// <summary>Gets the errors that failed it, or the warnings of a validation that passed.</summary>
    public IReadOnlyList<SceneDiagnostic> Diagnostics { get; }

    /// <summary>Gets a value indicating whether the edit can apply.</summary>
    public bool IsValid => this.Outcome == SceneEditOutcome.Applied;

    /// <summary>Creates a validation that passed with warnings.</summary>
    /// <param name="warnings">What the user should know.</param>
    /// <returns>The validation.</returns>
    public static SceneEditValidation Warned(params IReadOnlyList<SceneDiagnostic> warnings)
    {
        ArgumentNullException.ThrowIfNull(warnings);
        return warnings.Count == 0 ? Valid : new(SceneEditOutcome.Applied, warnings);
    }

    /// <summary>Creates a validation that failed because a target is missing or locked.</summary>
    /// <param name="errors">Why.</param>
    /// <returns>The validation.</returns>
    public static SceneEditValidation Refused(params IReadOnlyList<SceneDiagnostic> errors) => new(SceneEditOutcome.Refused, errors);

    /// <summary>Creates a validation that failed because the edit does not fit the scene.</summary>
    /// <param name="errors">Why.</param>
    /// <returns>The validation.</returns>
    public static SceneEditValidation Invalid(params IReadOnlyList<SceneDiagnostic> errors) => new(SceneEditOutcome.Invalid, errors);
}
