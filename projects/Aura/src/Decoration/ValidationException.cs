// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Exception thrown when <see cref="WindowDecorationOptions"/> validation fails.
/// </summary>
/// <remarks>
/// <para>
/// This exception is thrown by <see cref="WindowDecorationOptions.Validate"/> when the decoration
/// configuration contains invalid combinations or values. It provides clear, actionable error messages
/// to help developers identify and fix configuration issues during development.
/// </para>
/// <para>
/// Common scenarios that trigger this exception include:
/// <list type="bullet">
/// <item>Empty or whitespace-only window category</item>
/// <item>Menu specified when Aura chrome is disabled</item>
/// <item>Primary window without a Close button</item>
/// <item>Invalid title bar dimensions (zero or negative height, negative padding)</item>
/// <item>Empty menu provider ID when menu is configured</item>
/// </list>
/// </para>
/// </remarks>
[Serializable]
public class ValidationException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ValidationException"/> class.
    /// </summary>
    public ValidationException()
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ValidationException"/> class
    /// with a specified error message.
    /// </summary>
    /// <param name="message">The message that describes the validation error.</param>
    public ValidationException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ValidationException"/> class
    /// with a specified error message and a reference to the inner exception that is the cause
    /// of this exception.
    /// </summary>
    /// <param name="message">The message that describes the validation error.</param>
    /// <param name="innerException">
    /// The exception that is the cause of the current exception, or <see langword="null"/> if
    /// no inner exception is specified.
    /// </param>
    public ValidationException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
