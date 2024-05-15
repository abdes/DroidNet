// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Helpers;

using Microsoft.UI.Xaml.Controls;

public static class FrameExtensions
{
    public static object? GetPageViewModel(this Frame frame) => frame.Content.GetType()
        .GetProperty("ViewModel")
        ?.GetValue(frame.Content, index: null);
}
