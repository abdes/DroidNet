// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.WorldEditor.Editors.Scene;

/// <summary>
/// Converts between the engine logging verbosity integer and the Segmented control index.
/// Index map used by the compact Segmented control:
/// 0 => ERR (-2), 1 => WARN (-1), 2 => INF (0), 3 => VERBOSE (>0)
/// </summary>
public sealed partial class LoggingVerbositySegmentIndexConverter : IValueConverter
{
    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is int v)
        {
            if (v <= -2)
            {
                return 0;
            }

            if (v == -1)
            {
                return 1;
            }

            if (v == 0)
            {
                return 2;
            }

            // positive verbosity values map to the verbose segment index
            if (v >= 1)
            {
                return 3;
            }

            return 2; // default to INF
        }

        return 2; // default INF
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
    {
        if (value is int idx)
        {
            if (idx <= 0)
            {
                return -2;
            }

            if (idx == 1)
            {
                return -1;
            }

            if (idx == 2)
            {
                return 0;
            }

            // verbose segment selected => choose a default of 1
            if (idx == 3)
            {
                return 1;
            }
        }

        return 0; // fallback to INFO
    }
}
