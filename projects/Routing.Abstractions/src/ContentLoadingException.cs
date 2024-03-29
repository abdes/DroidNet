// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Exception thrown to report a failure to load the content for a ViewModel into a router outlet targeted by a route.
/// </summary>
public class ContentLoadingException : Exception
{
    private const string DefaultMessage = "could not load content for a route";

    private readonly Lazy<string> extendedMessage;

    public ContentLoadingException()
        : this(DefaultMessage)
    {
    }

    public ContentLoadingException(string message)
        : base(message)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    public ContentLoadingException(string message, Exception innerException)
        : base(message, innerException)
        => this.extendedMessage = new Lazy<string>(this.FormatMessage);

    /// <summary>
    /// Gets the name of the outlet, if already known, in which content was being loaded. This is usually the value used in the
    /// <see cref="IRoute.Outlet">outlet</see> property of the route.
    /// </summary>
    public string? OutletName { get; init; }

    /// <summary>
    /// Gets the ViewModel of the content. This is usually an instance of the type specified in the
    /// <see cref="IRoute.ViewModelType" /> property of the route.
    /// </summary>
    public object? ViewModel { get; init; }

    public override string Message => this.extendedMessage.Value;

    private string FormatMessage()
    {
        var message = base.Message;

        if (this.OutletName is null && this.ViewModel is null)
        {
            return message;
        }

        message += " (";

        if (this.OutletName is not null)
        {
            message += $"OutletName={this.OutletName}, ";
        }

        if (this.ViewModel is not null)
        {
            message += $"ViewModel={this.ViewModel.GetType().FullName}";
        }

        message += ")";

        return message;
    }
}
