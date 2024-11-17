// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Exception thrown when the router fails to load content for a view model into its designated outlet.
/// </summary>
/// <remarks>
/// This exception occurs during route activation, and usually indicates that an outlet container
/// could not be found, or the content could not be loaded into the outlet.
/// </remarks>
public class ContentLoadingException : Exception
{
    private const string DefaultMessage = "could not load content for a route";

    /// <summary>
    /// Lazy-initialized detailed message that includes outlet and view model details when available.
    /// </summary>
    private readonly Lazy<string> extendedMessage;

    /// <summary>
    /// Initializes a new instance of the <see cref="ContentLoadingException"/> class.
    /// Initializes a new instance with the default message.
    /// </summary>
    public ContentLoadingException()
        : this(DefaultMessage)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ContentLoadingException"/> class.
    /// Initializes a new instance with a specified error message.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    public ContentLoadingException(string message)
        : base(message)
    {
        this.extendedMessage = new Lazy<string>(this.FormatMessage);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ContentLoadingException"/> class.
    /// Initializes a new instance with a specified error message and inner exception.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    /// <param name="innerException">The exception that caused this exception.</param>
    public ContentLoadingException(string message, Exception innerException)
        : base(message, innerException)
    {
        this.extendedMessage = new Lazy<string>(this.FormatMessage);
    }

    /// <summary>
    /// Gets the name of the outlet where content loading failed.
    /// </summary>
    /// <remarks>
    /// Corresponds to the <see cref="IRoute.Outlet"/> property of the route being activated.
    /// </remarks>
    public string? OutletName { get; init; }

    /// <summary>
    /// Gets the view model whose content failed to load.
    /// </summary>
    /// <remarks>
    /// Instance of the type specified in <see cref="IRoute.ViewModelType"/> of the route being activated.
    /// </remarks>
    public object? ViewModel { get; init; }

    /// <summary>
    /// Gets the formatted exception message including outlet and view model details when available.
    /// </summary>
    public override string Message => this.extendedMessage.Value;

    /// <summary>
    /// Formats the exception message with outlet name and view model type if available.
    /// </summary>
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
