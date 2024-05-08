// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT).
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using Microsoft.UI.Xaml;

public interface IThemeSelectorService
{
    ElementTheme Theme
    {
        get;
    }

    /// <summary></summary>
    /// <param name="theme"></param>
    /// <returns>
    /// A <see cref="Task" /> representing the result of the asynchronous
    /// operation.
    /// </returns>
    Task SetThemeAsync(ElementTheme theme);

    void ApplyTheme();
}
