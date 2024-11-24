// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.RegularExpressions;

namespace Oxygen.Editor.Core;

/// <summary>
/// Provides input validation helpers.
/// </summary>
public static partial class InputValidation
{
    /// <summary>
    /// A regular expression pattern to validate a suggested scene name. It checks
    /// that the name can be used for a file name.
    /// </summary>
    /// <see href="https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file?redirectedfrom=MSDN" />
    private const string ValidFileNamePattern
        = @"^(?!^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]|CLOCK\$|\.{2,})$)(?!^\.\.)([^<>:""/\\|?*\x00-\x1F]+[^<>:""/\\|?*\x00-\x1F\ .])$";

    /// <summary>
    /// Validates if the provided name is a valid file name.
    /// </summary>
    /// <param name="name">The name to validate.</param>
    /// <returns><see langword="true"/> if the name is valid; otherwise, <see langword="false"/>.</returns>
    public static bool IsValidFileName(string name) => ValidFileNameMatcher().IsMatch(name);

    /// <summary>
    /// Creates a Regex object for validating file names.
    /// </summary>
    /// <returns>A Regex object configured with the file name validation pattern.</returns>
    [GeneratedRegex(ValidFileNamePattern, RegexOptions.ExplicitCapture, matchTimeoutMilliseconds: 1000)]
    private static partial Regex ValidFileNameMatcher();
}
