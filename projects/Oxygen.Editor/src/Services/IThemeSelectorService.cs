// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using Microsoft.UI.Xaml;

public interface IThemeSelectorService
{
    ElementTheme Theme { get; }

    Task SetThemeAsync(ElementTheme theme);

    void ApplyTheme();
}
