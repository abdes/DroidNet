// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Core;

using System.Text.RegularExpressions;

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
        = """^(?!^(PRN|AUX|CLOCK\$|NUL|CON|COM\d|LPT\d|\.\.|\.)(\.|\..|[^ ]*\.{1,2})?$)([^<>:"/\\|?*\x00-\x1F]+[^<>:"/\\|?*\x00-\x1F\ .])$""";

    public static bool IsValidFileName(string name) => ValidFileNameMatcher().IsMatch(name);

    [GeneratedRegex(ValidFileNamePattern, RegexOptions.ExplicitCapture, matchTimeoutMilliseconds: 1000)]
    private static partial Regex ValidFileNameMatcher();
}
