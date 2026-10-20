// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scripting;

/// <summary>
/// The result of reading a library script: the script, nothing when no mounted library has it, or why it could not be
/// read.
/// </summary>
public sealed record ScriptRead
{
    private ScriptRead(Script? script, string? failure)
    {
        this.Script = script;
        this.Failure = failure;
    }

    /// <summary>Gets the read of a script no mounted library has.</summary>
    public static ScriptRead NotFound { get; } = new(script: null, failure: null);

    /// <summary>Gets the script; <see langword="null"/> when it was not found or could not be read.</summary>
    public Script? Script { get; }

    /// <summary>Gets why the script could not be read; <see langword="null"/> otherwise.</summary>
    public string? Failure { get; }

    /// <summary>Gets the read of a script.</summary>
    /// <param name="script">The script.</param>
    /// <returns>The read.</returns>
    public static ScriptRead Found(Script script)
    {
        ArgumentNullException.ThrowIfNull(script);
        return new(script, failure: null);
    }

    /// <summary>Gets the read of a script that could not be read.</summary>
    /// <param name="reason">Why, for the user.</param>
    /// <returns>The read.</returns>
    public static ScriptRead Unavailable(string reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(reason);
        return new(script: null, reason);
    }
}
