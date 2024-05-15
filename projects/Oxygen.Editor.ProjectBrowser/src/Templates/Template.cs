// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

using System.Text.Json.Serialization;
using DroidNet.Resources;

[method: JsonConstructor]
public class Template(string description, string name, ProjectCategory category) : ITemplateInfo
{
    private string description = description.TryGetLocalizedMine();
    private string name = name.TryGetLocalizedMine();

    public required string Name
    {
        get => this.name;
        set => this.name = value.TryGetLocalizedMine();
    }

    public string Description
    {
        get => this.description;
        set => this.description = value.TryGetLocalizedMine();
    }

    public ProjectCategory Category { get; set; } = category;

    public string? Location { get; set; }

    public string? Icon { get; set; }

    public IList<string> PreviewImages { get; set; } = new List<string>();

    public DateTime? LastUsedOn { get; set; }
}
