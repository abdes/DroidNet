// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class NativeIblImagesSupport
{
    internal static readonly JsonSerializerOptions IblImageJsonOptions = new()
    {
        WriteIndented = true,
        IncludeFields = true,
    };
}
