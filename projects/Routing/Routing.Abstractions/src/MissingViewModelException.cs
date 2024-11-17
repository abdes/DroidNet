// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents an error that occurs when a view model cannot be resolved during route activation.
/// </summary>
/// <remarks>
/// <para>
/// During navigation, when the router attempts to activate routes with associated view models, this
/// exception indicates that the specified view model type could not be obtained from the application's
/// service container. This typically occurs when the view model type is not properly registered with
/// the dependency injection system.
/// </para>
/// <para>
/// The exception captures the view model type that failed to resolve, providing developers with the
/// information needed to correct service registration or route configuration issues.
/// </para>
/// </remarks>
public class MissingViewModelException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MissingViewModelException"/> class.
    /// </summary>
    public MissingViewModelException()
        : base("Route configuration is missing the view model type")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="MissingViewModelException"/> class with a
    /// specific error message.
    /// </summary>
    /// <param name="message">A detailed description of the view model resolution failure.</param>
    public MissingViewModelException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="MissingViewModelException"/> class with a
    /// message and inner exception.
    /// </summary>
    /// <param name="message">A detailed description of the view model resolution failure.</param>
    /// <param name="innerException">
    /// The exception that prevented view model resolution, if any.
    /// </param>
    public MissingViewModelException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>
    /// Gets or sets the view model type that could not be resolved.
    /// </summary>
    public Type? ViewModelType { get; set; }
}
