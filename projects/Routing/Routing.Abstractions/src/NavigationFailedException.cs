// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

// TODO(abdes): add extra information to this exception

/// <summary>Exception thrown when a <see cref="IRouter">router</see> navigation fails.</summary>
public class NavigationFailedException : Exception
{
    private const string DefaultMessage = "navigation failed";

    public NavigationFailedException()
        : this(DefaultMessage)
    {
    }

    public NavigationFailedException(string? message)
        : base(message)
    {
    }

    public NavigationFailedException(string? message, Exception? innerException)
        : base(message, innerException)
    {
    }
}
