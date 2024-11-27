// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Converters;
using Oxygen.Editor.ProjectBrowser.Projects;

namespace Oxygen.Editor.ProjectBrowser.Converters;

/// <summary>
/// A specialized <see cref="DictionaryValueConverter{T}" /> converter for
/// <see cref="KnownLocation" />.
/// </summary>
internal sealed partial class KnownLocationByKeyConverter : DictionaryValueConverter<KnownLocation>;
