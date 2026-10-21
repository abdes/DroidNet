// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>Logging helpers for <see cref="TemplatesService"/>.</summary>
public partial class TemplatesService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        EventId = 1000,
        Level = LogLevel.Error,
        Message = "Could not load template at `{templatePath}`; {error}")]
    private static partial void CouldNotLoadTemplate(ILogger logger, string templatePath, string error);

    private void CouldNotLoadTemplate(string templatePath, string error) => CouldNotLoadTemplate(this.logger, templatePath, error);
}
