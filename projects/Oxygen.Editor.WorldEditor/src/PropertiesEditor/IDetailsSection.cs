// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public interface IDetailsSection
{
    public string Header { get; }

    public string Description { get; }

    public bool IsExpanded { get; }
}
