// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Event arguments for source error events.
/// </summary>
public sealed class SourceErrorEventArgs : EventArgs
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SourceErrorEventArgs"/> class.
    /// </summary>
    /// <param name="sourceId">The identifier of the source that experienced an error.</param>
    /// <param name="errorMessage">The error message.</param>
    public SourceErrorEventArgs(string sourceId, string errorMessage)
    {
        this.SourceId = sourceId ?? throw new ArgumentNullException(nameof(sourceId));
        this.ErrorMessage = errorMessage ?? throw new ArgumentNullException(nameof(errorMessage));
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SourceErrorEventArgs"/> class.
    /// </summary>
    /// <param name="sourceId">The identifier of the source that experienced an error.</param>
    /// <param name="errorMessage">The error message.</param>
    /// <param name="exception">The exception that caused the error.</param>
    public SourceErrorEventArgs(string sourceId, string errorMessage, Exception? exception)
    {
        this.SourceId = sourceId ?? throw new ArgumentNullException(nameof(sourceId));
        this.ErrorMessage = errorMessage ?? throw new ArgumentNullException(nameof(errorMessage));
        this.Exception = exception;
    }

    /// <summary>
    /// Gets the identifier of the source that experienced an error.
    /// </summary>
    public string SourceId { get; }

    /// <summary>
    /// Gets the error message.
    /// </summary>
    public string ErrorMessage { get; }

    /// <summary>
    /// Gets the exception that caused the error, if available.
    /// </summary>
    public Exception? Exception { get; }
}
