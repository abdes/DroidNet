// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Rendering;

using System.Security;

internal static class SpecialCharsEscaping
{
    public static string Apply(string value) => SecurityElement.Escape(value);
}
