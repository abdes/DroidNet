// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Thrown when a <see cref="IRoute">route</see> configuration does not satisfy the required criteria.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="RoutesConfigurationException"/> is used to signal issues with route
/// configurations during the setup of the routing system. This exception ensures that only valid
/// and properly configured routes are integrated into the router, maintaining the integrity and
/// reliability of the navigation infrastructure.
/// </para>
/// <para>
/// Common scenarios that trigger this exception include attempting to configure a route without
/// specifying a <see cref="IRoute.Path"/> while using the default matcher, resulting in a missing
/// path, or defining a route's path that begins with a forward slash ('/'), which is an invalid
/// path format.
/// </para>
/// </remarks>
///
/// <example>
/// <strong>Example Usage</strong>
/// <code><![CDATA[
/// try
/// {
///     var routes = new Routes(new List<Route>
///     {
///         new Route
///         {
///             Matcher = Route.DefaultMatcher,
///             // Path is missing, will trigger exception
///             Outlet = "main",
///             ViewModelType = typeof(MainViewModel)
///         }
///     });
/// }
/// catch (RoutesConfigurationException ex)
/// {
///     Console.WriteLine($"Route configuration failed: {ex.Message}");
///     // Handle exception, possibly logging or providing feedback to the developer
/// }
/// ]]></code>
/// <para>
/// In the above example, attempting to add a route without specifying a path while using the
/// default matcher results in a <see cref="RoutesConfigurationException"/>. This helps developers
/// quickly identify and rectify configuration issues during the development phase.
/// </para>
/// <para>
/// To avoid such exceptions, ensure that all routes are fully and correctly configured before
/// adding them to the <see cref="IRoutes"/> collection. Validate that required properties like
/// <see cref="IRoute.Path"/> are set appropriately based on the chosen matcher.
/// </para>
/// </example>
public class RoutesConfigurationException : Exception
{
    private const string DefaultMessage = "route configuration is not valid";

    private readonly Lazy<string> extendedMessage;

    /// <summary>
    /// Initializes a new instance of the <see cref="RoutesConfigurationException"/> class with a default message.
    /// </summary>
    public RoutesConfigurationException()
        : this(DefaultMessage)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="RoutesConfigurationException"/> class with a specified error message.
    /// </summary>
    /// <param name="message">
    /// A descriptive error message explaining the reason for the exception.
    /// </param>
    public RoutesConfigurationException(string? message)
        : base(message)
    {
        this.extendedMessage = new Lazy<string>(this.FormatMessage);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="RoutesConfigurationException"/> class with a specified
    /// error message and a reference to the inner exception that is the cause of this exception.
    /// </summary>
    /// <param name="message">
    /// A descriptive error message explaining the reason for the exception.
    /// </param>
    /// <param name="innerException">
    /// The exception that is the cause of the current exception. If the <paramref name="innerException"/>
    /// parameter is not <see langword="null"/>, the current exception is raised in a catch block that
    /// handles the inner exception.
    /// </param>
    public RoutesConfigurationException(string? message, Exception? innerException)
        : base(message, innerException)
    {
        this.extendedMessage = new Lazy<string>(this.FormatMessage);
    }

    /// <summary>
    /// Gets the <see cref="IRoute"/> object that failed validation.
    /// </summary>
    /// <value>
    /// An instance of <see cref="IRoute"/> representing the route that did not meet the required
    /// configuration criteria.
    /// </value>
    public required IRoute FailedRoute { get; init; }

    /// <summary>
    /// Gets the error message that describes the current exception, including details about the failed route.
    /// </summary>
    /// <value>
    /// A string that combines the base error message with information about the <see cref="FailedRoute"/>.
    /// </value>
    public override string Message => this.extendedMessage.Value;

    /// <summary>
    /// Formats the exception message by combining the base message with details of the failed route.
    /// </summary>
    /// <returns>
    /// A formatted string that includes both the base error message and the string representation
    /// of the <see cref="FailedRoute"/>.
    /// </returns>
    private string FormatMessage() => base.Message + $" (FailedRoute={this.FailedRoute})";
}
