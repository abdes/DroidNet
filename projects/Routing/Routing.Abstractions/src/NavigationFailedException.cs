// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents an error that occurs when the router fails to complete a navigation request.
/// </summary>
/// <remarks>
/// <para>
/// A navigation failure can occur at different stages of the navigation process, such as URL parsing,
/// route recognition, or state serialization. This exception captures the specific reason for the
/// failure, helping developers identify and correct navigation configuration issues.
/// </para>
/// <para>
/// Common scenarios where this exception occurs include attempting to navigate to URLs that don't
/// match any configured routes, or when changes to the router state cannot be properly serialized
/// back to a URL string.
/// </para>
/// </remarks>
public class NavigationFailedException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="NavigationFailedException"/> class.
    /// </summary>
    public NavigationFailedException()
        : base("Navigation failed")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="NavigationFailedException"/> class with a
    /// specific error message.
    /// </summary>
    /// <param name="message">A description of what caused the navigation to fail.</param>
    public NavigationFailedException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="NavigationFailedException"/> class with a
    /// message and inner exception.
    /// </summary>
    /// <param name="message">A description of what caused the navigation to fail.</param>
    /// <param name="innerException">
    /// The underlying exception that caused the navigation failure.
    /// </param>
    public NavigationFailedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}

// TODO(abdes): add extra information to this exception
/*
public class NavigationFailedException : Exception
{
    // Existing constructors...

    /// <summary>
    /// Gets the navigation stage where the failure occurred.
    /// </summary>
    public NavigationStage Stage { get; init; }

    /// <summary>
    /// Gets the URL that was being navigated to when the failure occurred.
    /// </summary>
    public string? TargetUrl { get; init; }

    /// <summary>
    /// Gets the navigation context in which the failure occurred.
    /// </summary>
    public INavigationContext? Context { get; init; }

    /// <summary>
    /// Gets the route that failed to activate, if the failure occurred during route activation.
    /// </summary>
    public IRoute? FailedRoute { get; init; }
}

/// <summary>
/// Identifies the stage of navigation where a failure occurred.
/// </summary>
public enum NavigationStage
{
    /// <summary>URL parsing and validation</summary>
    UrlParsing,

    /// <summary>Route recognition/matching</summary>
    RouteRecognition,

    /// <summary>View model resolution and activation</summary>
    RouteActivation,

    /// <summary>Content loading into outlets</summary>
    ContentLoading,

    /// <summary>State serialization</summary>
    StateUpdate
}
*/
