// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Thrown when no <see cref="IRoute">route</see> was found in the <see cref="IRouter">router</see> configuration, which path
/// matches the path specified by the segments that were being recognized.
/// </summary>
public class NoRouteForSegmentsException : Exception
{
    private const string DefaultMessage = "no route matched the segments";

    private readonly object? segments;
    private readonly Lazy<string> extendedMessage;

    public NoRouteForSegmentsException()
        : this(DefaultMessage)
    {
    }

    public NoRouteForSegmentsException(string? message)
        : base(message)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    public NoRouteForSegmentsException(string? message, Exception? innerException)
        : base(message, innerException)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    /// <summary>
    /// Gets the ViewModel type, which resolution attempt resulted in this exception being thrown.
    /// </summary>
    public required object Segments
    {
        get => this.segments!;
        init
        {
            if (value is IEnumerable<string> segmentsCollection)
            {
                this.segments = string.Join('/', segmentsCollection);
            }
            else
            {
                this.segments = value;
            }
        }
    }

    /// <summary>
    /// Gets the root of the pared <see cref="IUrlTree" /> to which the unmatched segments belong.
    /// </summary>
    public required IUrlSegmentGroup UrlTreeRoot { get; init; }

    public override string Message => this.extendedMessage.Value;

    private string FormatMessage() => base.Message + $" (Segments={this.Segments}";
}
