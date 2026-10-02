// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Disposables;
using System.Runtime.InteropServices;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.TestHelpers;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class NativeIblImagesSupport
{
    internal static readonly JsonSerializerOptions IblImageJsonOptions = new()
    {
        WriteIndented = true,
        IncludeFields = true
    };
}
