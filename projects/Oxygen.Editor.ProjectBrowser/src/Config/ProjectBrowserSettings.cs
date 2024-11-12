// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Config;

/// <summary>Configuration settings for the project browser.</summary>
/// These settings can be obtained only through the DI injector. They are
/// loaded from one of the application configuration files during the
/// application startup. They need to be placed within a section named
/// `ConfigSectionName`.
/// <see cref="Microsoft.Extensions.Configuration" />
public class ProjectBrowserSettings
{
    /// <summary>
    /// The name of the configuration section under which the project browser settings
    /// must appear.
    /// </summary>
    public static readonly string ConfigSectionName = "ProjectBrowserSettings";

    private readonly List<string> templates = [];

    /// <summary>
    /// Gets the list of built-in (i.e. come with the application) project templates.
    /// </summary>
    /// <value>
    /// The list of built-in project templates. Each item represents the path to the template description file. If the path is
    /// relative, it will be interpreted relative to the application's built-in templates root folder.
    /// </value>
    public IList<string> BuiltinTemplates
    {
        get => this.templates;
        init => this.templates = new HashSet<string>(value, StringComparer.Ordinal).ToList();
    }
}
