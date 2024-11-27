// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// Represents errors that occur during the loading of a project template.
/// </summary>
public class TemplateLoadingException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TemplateLoadingException"/> class with a default error message.
    /// </summary>
    public TemplateLoadingException()
        : base("Failed to load a project template.")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="TemplateLoadingException"/> class with a specified error message.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    public TemplateLoadingException(string? message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="TemplateLoadingException"/> class with a specified error message and a reference to the inner exception that is the cause of this exception.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    /// <param name="innerException">The exception that is the cause of the current exception, or a null reference if no inner exception is specified.</param>
    public TemplateLoadingException(string? message, Exception? innerException)
        : base(message, innerException)
    {
    }
}
