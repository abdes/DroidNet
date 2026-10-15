// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Player;

/// <summary>The player could not be started or reached.</summary>
public sealed class PlayerLaunchException : Exception
{
    /// <summary>Initializes a new instance of the <see cref="PlayerLaunchException"/> class.</summary>
    public PlayerLaunchException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="PlayerLaunchException"/> class.</summary>
    /// <param name="message">What failed, for the user.</param>
    public PlayerLaunchException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="PlayerLaunchException"/> class.</summary>
    /// <param name="message">What failed, for the user.</param>
    /// <param name="innerException">The cause.</param>
    public PlayerLaunchException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Gets the player's last lines of standard output and error, which say why it ended.</summary>
    public IReadOnlyList<string> PlayerOutput { get; init; } = [];
}
