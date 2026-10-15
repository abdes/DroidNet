// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>A player command failed, or the connection closed before it replied.</summary>
public sealed class PlayerCommandException : Exception
{
    /// <summary>The code used when the connection closed before the reply.</summary>
    public const int ConnectionClosedCode = -1;

    /// <summary>Initializes a new instance of the <see cref="PlayerCommandException"/> class.</summary>
    public PlayerCommandException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="PlayerCommandException"/> class.</summary>
    /// <param name="message">The failure.</param>
    public PlayerCommandException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="PlayerCommandException"/> class.</summary>
    /// <param name="message">The failure.</param>
    /// <param name="innerException">The cause.</param>
    public PlayerCommandException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="PlayerCommandException"/> class.</summary>
    /// <param name="code">The JSON-RPC error code.</param>
    /// <param name="message">The failure the command reported.</param>
    /// <param name="data">The command's error data, such as its output and scene issues.</param>
    public PlayerCommandException(int code, string message, JsonNode? data)
        : base(message)
    {
        this.Code = code;
        this.ErrorData = data;
    }

    /// <summary>Gets the JSON-RPC error code.</summary>
    public int Code { get; }

    /// <summary>Gets the command's error data.</summary>
    public JsonNode? ErrorData { get; }
}
