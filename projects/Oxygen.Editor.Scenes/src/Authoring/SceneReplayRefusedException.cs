// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>
/// Thrown by a history entry whose edit cannot be replayed on the scene as it is, before the entry records any inverse,
/// so the history keeps the entry where it was.
/// </summary>
public sealed class SceneReplayRefusedException : Exception
{
    /// <summary>Initializes a new instance of the <see cref="SceneReplayRefusedException"/> class.</summary>
    public SceneReplayRefusedException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="SceneReplayRefusedException"/> class.</summary>
    /// <param name="message">Why the replay was refused.</param>
    public SceneReplayRefusedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="SceneReplayRefusedException"/> class.</summary>
    /// <param name="message">Why the replay was refused.</param>
    /// <param name="innerException">The exception that caused it.</param>
    public SceneReplayRefusedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="SceneReplayRefusedException"/> class.</summary>
    /// <param name="result">What became of the replayed edit.</param>
    internal SceneReplayRefusedException(SceneEditResult result)
        : base(string.Join(' ', result.Diagnostics.Select(static diagnostic => diagnostic.Message)))
    {
        this.Result = result;
    }

    /// <summary>Gets what became of the replayed edit, when the scene authoring refused it.</summary>
    public SceneEditResult? Result { get; }
}
