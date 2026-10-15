// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Text.Json;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>
/// Where a running Oxygen host application accepts control clients: the loopback port and token its control server
/// publishes in its discovery file.
/// </summary>
/// <param name="Port">The loopback TCP port.</param>
/// <param name="Token">The token the first message must present.</param>
public sealed record PlayerControlEndpoint(int Port, string Token)
{
    /// <summary>The control protocol version this editor speaks.</summary>
    public const int Protocol = 1;

    private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(100);

    /// <summary>Gets the folder where control servers publish their discovery files.</summary>
    public static string DiscoveryFolder { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Oxygen", "control");

    /// <summary>Waits for a process this editor started to publish its discovery file.</summary>
    /// <remarks>
    /// The server writes the file once it listens; nothing notifies another process of that, so the file is checked
    /// until it can be read, the process exits or the time runs out.
    /// </remarks>
    /// <param name="process">The started process.</param>
    /// <param name="timeout">How long the process may take to start its server.</param>
    /// <param name="cancellationToken">Cancels the wait.</param>
    /// <returns>The endpoint.</returns>
    /// <exception cref="PlayerLaunchException">The process exited, published another protocol or did not start its
    /// server in time.</exception>
    public static async Task<PlayerControlEndpoint> WaitForAsync(Process process, TimeSpan timeout, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(process);
        var path = Path.Combine(DiscoveryFolder, process.Id.ToString(System.Globalization.CultureInfo.InvariantCulture) + ".json");
        var deadline = Stopwatch.StartNew();
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (TryRead(path) is { } endpoint)
            {
                return endpoint;
            }

            if (process.HasExited)
            {
                throw new PlayerLaunchException($"The player exited with code {process.ExitCode} before it accepted the editor.");
            }

            if (deadline.Elapsed > timeout)
            {
                throw new PlayerLaunchException($"The player did not start its control server within {timeout.TotalSeconds:0} seconds.");
            }

            await Task.Delay(PollInterval, cancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>Reads a discovery file.</summary>
    /// <param name="path">The file path.</param>
    /// <returns>The endpoint, or <see langword="null"/> while the file is missing or still being written.</returns>
    /// <exception cref="PlayerLaunchException">The file names another protocol version.</exception>
    internal static PlayerControlEndpoint? TryRead(string path)
    {
        // The player publishes the file whole, by renaming it into place, so a file that exists is complete.
        if (!File.Exists(path))
        {
            return null;
        }

        string text;
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream);
            text = reader.ReadToEnd();
        }
        catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException or IOException or UnauthorizedAccessException)
        {
            // The player removed the file as it ended.
            return null;
        }

        try
        {
            using var document = JsonDocument.Parse(text);
            var root = document.RootElement;
            if (!root.TryGetProperty("protocol", out var protocol) || !root.TryGetProperty("port", out var port)
                || !root.TryGetProperty("token", out var token) || token.GetString() is not { Length: > 0 } value)
            {
                return null;
            }

            return protocol.GetInt32() == Protocol
                ? new PlayerControlEndpoint(port.GetInt32(), value)
                : throw new PlayerLaunchException($"The player speaks control protocol {protocol.GetInt32()}; this editor speaks {Protocol}. Install a matching SDK.");
        }
        catch (Exception exception) when (exception is JsonException or InvalidOperationException or FormatException)
        {
            return null;
        }
    }
}
