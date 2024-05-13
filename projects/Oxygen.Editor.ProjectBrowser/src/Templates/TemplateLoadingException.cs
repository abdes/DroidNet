// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

public class TemplateLoadingException : Exception
{
    public TemplateLoadingException()
        : base("Failed to load a project template.")
    {
    }

    public TemplateLoadingException(string? message)
        : base(message)
    {
    }

    public TemplateLoadingException(string? message, Exception? innerException)
        : base(message, innerException)
    {
    }
}
