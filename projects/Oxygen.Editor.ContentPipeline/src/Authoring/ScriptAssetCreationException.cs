// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Authoring;

/// <summary>A new script or input file was not created; the message tells the user why.</summary>
public sealed class ScriptAssetCreationException : Exception
{
    /// <summary>Initializes a new instance of the <see cref="ScriptAssetCreationException"/> class.</summary>
    public ScriptAssetCreationException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="ScriptAssetCreationException"/> class.</summary>
    /// <param name="message">Why the file was not created.</param>
    public ScriptAssetCreationException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="ScriptAssetCreationException"/> class.</summary>
    /// <param name="message">Why the file was not created.</param>
    /// <param name="innerException">The underlying failure.</param>
    public ScriptAssetCreationException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
