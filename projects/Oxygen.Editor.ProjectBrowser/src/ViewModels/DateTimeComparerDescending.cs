// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using DroidNet.Routing;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// Compares <see cref="DateTime"/> values in descending order.
/// </summary>
internal sealed class DateTimeComparerDescending : Comparer<DateTime>
{
    /// <inheritdoc/>
    public override int Compare(DateTime x, DateTime y) => y.CompareTo(x);
}
