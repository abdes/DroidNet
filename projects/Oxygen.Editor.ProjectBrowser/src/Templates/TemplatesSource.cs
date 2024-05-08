// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// A composite template source that invokes the specific template source based on
/// the template uri.
/// </summary>
public class TemplatesSource : ITemplatesSource
{
    private readonly LocalTemplatesSource localSource;

    public TemplatesSource(LocalTemplatesSource localSource)
        => this.localSource = localSource;

    public Task<ITemplateInfo> LoadLocalTemplateAsync(string path)
        => this.localSource.LoadTemplateAsync(path);
}
