// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

using DroidNet.Resources;

public class Template : ITemplateInfo
{
    private string? description;
    private string? name;

    public string? Name
    {
        get => this.name;
        set => this.name = value?.GetLocalizedMine() ?? string.Empty;
    }

    public string? Description
    {
        get => this.description;
        set => this.description = value?.GetLocalizedMine() ?? string.Empty;
    }

    public ProjectCategory? Category { get; set; }

    public string? Location { get; set; }

    public string? Icon { get; set; }

    public List<string> PreviewImages { get; set; } = new();

    public DateTime? LastUsedOn { get; set; }
}
