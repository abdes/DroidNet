// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scripting;

/// <summary>
/// What the engine's analyzer reads from one script source: its declared parameters and its problems, without running
/// or compiling it.
/// </summary>
public sealed record ScriptAnalysis
{
    /// <summary>Gets the declared parameters, in declaration order; none when the declaration has errors.</summary>
    public IReadOnlyList<ScriptParameter> Parameters { get; init; } = [];

    /// <summary>Gets the problems, in source order, without their file, which the analyzer does not know.</summary>
    public IReadOnlyList<ScriptProblem> Problems { get; init; } = [];

    /// <summary>Gets a value indicating whether the source has a parameter declaration, valid or not.</summary>
    public bool HasDeclaration { get; init; }

    /// <summary>Gets a value indicating whether the analyzer could not run, such as when it cannot be loaded.</summary>
    public bool IsUnavailable { get; init; }

    /// <summary>Gets why the analyzer could not run; <see langword="null"/> when it ran.</summary>
    public string? UnavailableReason { get; init; }

    /// <summary>Gets the analysis of an analyzer that could not run.</summary>
    /// <param name="reason">Why.</param>
    /// <returns>The analysis, with nothing read.</returns>
    public static ScriptAnalysis Unavailable(string reason) => new() { IsUnavailable = true, UnavailableReason = reason };
}
