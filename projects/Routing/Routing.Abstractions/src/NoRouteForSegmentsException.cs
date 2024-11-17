// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents an error that occurs during route recognition when URL segments
/// cannot be matched to any configured route.
/// </summary>
/// <remarks>
/// <para>
/// During URL recognition, the router processes segments sequentially, attempting to match them
/// against configured routes. This exception indicates that a segment or group of segments in the
/// URL could not be matched to any route in the configuration. This often occurs when navigating to
/// URLs that don't align with the application's routing structure.
/// </para>
/// <para>
/// The exception captures the unmatched segments to help diagnose routing issues. For example, in a
/// URL like `"/users/(details:profile/unknown)"`, if no route is configured to handle `"unknown"`
/// within the `"details"` outlet, this exception would be thrown with those segments captured in
/// the <see cref="Segments"/> property.
/// </para>
/// </remarks>
public class NoRouteForSegmentsException : Exception
{
    private const string DefaultMessage = "no route matched the segments";
    private readonly object? segments;
    private readonly Lazy<string> extendedMessage;

    /// <summary>
    /// Initializes a new instance of the <see cref="NoRouteForSegmentsException"/> class.
    /// </summary>
    public NoRouteForSegmentsException()
        : this(DefaultMessage)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="NoRouteForSegmentsException"/> class with a custom message.
    /// </summary>
    /// <param name="message">A description of the route matching failure.</param>
    public NoRouteForSegmentsException(string? message)
        : base(message)
    {
        this.extendedMessage = new Lazy<string>(this.FormatMessage);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="NoRouteForSegmentsException"/> class with a message and inner exception.
    /// </summary>
    /// <param name="message">A description of the route matching failure.</param>
    /// <param name="innerException">The exception that caused the route matching failure.</param>
    public NoRouteForSegmentsException(string? message, Exception? innerException)
        : base(message, innerException)
    {
        this.extendedMessage = new Lazy<string>(this.FormatMessage);
    }

    /// <summary>
    /// Gets the formatted exception message including the unmatched segments.
    /// </summary>
    public override string Message => this.extendedMessage.Value;

    /// <summary>
    /// Gets the URL segments that could not be matched to any route.
    /// </summary>
    /// <remarks>
    /// When set to an <see cref="IEnumerable{T}"/> of segments, they are joined with forward slashes
    /// to create a readable path representation. Otherwise, the value is stored as-is for error reporting.
    /// </remarks>
    public required object Segments
    {
        get => this.segments!;
        init => this.segments = value is IEnumerable<object> segmentsCollection ? string.Join('/', segmentsCollection) : value;
    }

    /// <summary>
    /// Formats the exception message by combining the base message with the unmatched segments.
    /// </summary>
    /// <returns>A formatted message string that includes both the error description and the problematic segments.</returns>
    private string FormatMessage() => base.Message + $" '{this.Segments}'";
}
