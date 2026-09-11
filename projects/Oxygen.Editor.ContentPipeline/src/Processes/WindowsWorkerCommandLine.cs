// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Text;

namespace Oxygen.Editor.ContentPipeline.Processes;

/// <summary>Encodes structured arguments at the CreateProcessW boundary.</summary>
internal static class WindowsWorkerCommandLine
{
    /// <summary>Creates the mutable, null-terminated native command line.</summary>
    /// <param name="startInfo">The executable and structured argument list.</param>
    /// <returns>The native UTF-16 command line buffer.</returns>
    internal static char[] Create(ProcessStartInfo startInfo)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(startInfo.FileName, nameof(startInfo));
        if (startInfo.FileName.Contains('"', StringComparison.Ordinal)
            || startInfo.FileName.Contains('\0', StringComparison.Ordinal))
        {
            throw new ArgumentException("The worker executable path contains an invalid character.", nameof(startInfo));
        }

        // CreateProcessW accepts one mutable command line. Keep ArgumentList
        // structured until this native boundary, then encode CRT quoting rules.
        var line = new StringBuilder().Append('"').Append(startInfo.FileName).Append('"');
        foreach (var argument in startInfo.ArgumentList)
        {
            if (argument.Contains('\0', StringComparison.Ordinal))
            {
                throw new ArgumentException("A worker argument contains a null character.", nameof(startInfo));
            }

            _ = line.Append(' ').Append('"');
            var slashes = 0;
            foreach (var character in argument)
            {
                if (character == '\\')
                {
                    slashes++;
                    continue;
                }

                _ = line.Append('\\', character == '"' ? (slashes * 2) + 1 : slashes);
                _ = line.Append(character);
                slashes = 0;
            }

            _ = line.Append('\\', slashes * 2).Append('"');
        }

        return line.Append('\0').ToString().ToCharArray();
    }
}
