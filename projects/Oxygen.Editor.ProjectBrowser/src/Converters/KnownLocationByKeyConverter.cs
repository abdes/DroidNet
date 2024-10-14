// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Converters;

using DroidNet.Converters;
using Oxygen.Editor.ProjectBrowser.Projects;

/// <summary>
/// A specialized <see cref="DictionaryValueConverter{T}" /> converter for
/// <see cref="KnownLocation" />.
/// </summary>
public partial class KnownLocationByKeyConverter : DictionaryValueConverter<KnownLocation>;
