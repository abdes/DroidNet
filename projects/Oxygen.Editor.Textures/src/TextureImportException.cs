// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.Textures;

/// <summary>An image was not imported: <see cref="Code"/> says why, and the message tells the user.</summary>
[SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "A refused import always carries its code.")]
public sealed class TextureImportException : Exception
{
    /// <summary>Initializes a new instance of the <see cref="TextureImportException"/> class.</summary>
    /// <param name="code">A <see cref="TextureImportCodes"/> code.</param>
    /// <param name="message">Why the image was not imported, for the user.</param>
    public TextureImportException(string code, string message)
        : base(message)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(code);
        this.Code = code;
    }

    /// <summary>Gets the <see cref="TextureImportCodes"/> code of the refusal.</summary>
    public string Code { get; }
}
