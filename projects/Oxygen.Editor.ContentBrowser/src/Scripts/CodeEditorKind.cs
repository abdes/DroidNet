// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// The code editor that opens scripts and input files.
/// </summary>
public enum CodeEditorKind
{
    /// <summary>Visual Studio Code, opened on the project folder at the file's line and column.</summary>
    VisualStudioCode,

    /// <summary>The application Windows associates with the file type.</summary>
    SystemDefault,

    /// <summary>A command line with <c>{file}</c>, <c>{line}</c>, <c>{column}</c> and <c>{project}</c> placeholders.</summary>
    Custom,
}
