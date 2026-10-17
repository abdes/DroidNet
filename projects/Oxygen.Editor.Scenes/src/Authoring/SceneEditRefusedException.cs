// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>Thrown when a call that returns no result, such as beginning a gesture, is refused.</summary>
public sealed class SceneEditRefusedException : Exception
{
    /// <summary>Initializes a new instance of the <see cref="SceneEditRefusedException"/> class.</summary>
    public SceneEditRefusedException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="SceneEditRefusedException"/> class.</summary>
    /// <param name="message">Why the call was refused.</param>
    public SceneEditRefusedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="SceneEditRefusedException"/> class.</summary>
    /// <param name="message">Why the call was refused.</param>
    /// <param name="innerException">The exception that caused it.</param>
    public SceneEditRefusedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="SceneEditRefusedException"/> class.</summary>
    /// <param name="result">Why the call was refused.</param>
    internal SceneEditRefusedException(SceneEditResult result)
        : base(string.Join(' ', result.Diagnostics.Select(static diagnostic => diagnostic.Message)))
    {
        this.Result = result;
    }

    /// <summary>Gets why the call was refused, when the scene authoring refused it.</summary>
    public SceneEditResult? Result { get; }
}
