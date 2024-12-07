// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Converters;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public class PropertiesEditorViewModel(ViewModelToView vmToViewConverter)
{
    /// <summary>
    /// Gets a viewmodel to view converter provided by the local Ioc container, which can resolve
    /// view from viewmodels registered locally. This converter must be used instead of the default
    /// Application converter.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; } = vmToViewConverter;

    public IList<IPropertiesViewModel> PropertyEditors =
    [
        new TransformViewModel(),
    ];
}
