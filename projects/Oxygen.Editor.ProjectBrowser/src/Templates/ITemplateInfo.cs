// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

public interface ITemplateInfo
{
    string? Name { get; set; }

    string? Description { get; set; }

    ProjectCategory? Category { get; }

    string? Location { get; set; }

    string? Icon { get; set; }

    IList<string> PreviewImages { get; set; }

    DateTime? LastUsedOn { get; set; }
}
