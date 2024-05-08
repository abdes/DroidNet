// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

public interface ITemplatesSource
{
    Task<ITemplateInfo> LoadLocalTemplateAsync(string path);
}
