// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Represents errors that occur while attempting to show a WinUI dialog.
/// </summary>
public sealed class DialogServiceException : InvalidOperationException
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="DialogServiceException"/> class.
    /// </summary>
    public DialogServiceException()
        : base("An error occurred while showing a dialog.")
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="DialogServiceException"/> class.
    /// </summary>
    /// <param name="message">The exception message.</param>
    public DialogServiceException(string message)
        : base(message)
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="DialogServiceException"/> class.
    /// </summary>
    /// <param name="message">The exception message.</param>
    /// <param name="innerException">The inner exception.</param>
    public DialogServiceException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
