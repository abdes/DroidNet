// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Defines standard window category constants used for classifying windows.
/// </summary>
/// <remarks>
/// Window categories are semantic identifiers that describe the role or purpose of a window
/// within the application. These constants provide a standardized vocabulary for window
/// classification, enabling type-safe window management operations and queries.
/// </remarks>
public static class WindowCategory
{
    /// <summary>
    /// Represents the main application window.
    /// </summary>
    /// <remarks>
    /// The main window typically hosts the primary user interface and serves as the
    /// central hub for the application. Most applications have exactly one main window.
    /// </remarks>
    public const string Main = "Main";

    /// <summary>
    /// Represents a secondary window.
    /// </summary>
    /// <remarks>
    /// Secondary windows are additional windows that complement the main window,
    /// often used for auxiliary views or content that benefits from being displayed
    /// in a separate window. These are typically created through routing or navigation.
    /// </remarks>
    public const string Secondary = "Secondary";

    /// <summary>
    /// Represents a tool window (palette, inspector, etc.).
    /// </summary>
    /// <remarks>
    /// Tool windows are auxiliary windows that provide specialized functionality or
    /// information, such as property inspectors, toolboxes, or debug consoles.
    /// They typically remain visible alongside the main content area.
    /// </remarks>
    public const string Tool = "Tool";

    /// <summary>
    /// Represents a document window.
    /// </summary>
    /// <remarks>
    /// Document windows host editable content such as text files, images, or other
    /// user-created artifacts. Applications often support multiple document windows
    /// simultaneously in a multi-document interface (MDI) pattern.
    /// </remarks>
    public const string Document = "Document";

    /// <summary>
    /// Represents an unknown or unclassified window category.
    /// </summary>
    /// <remarks>
    /// This category serves as a fallback for windows that don't fit into the
    /// predefined categories or when the window type is not explicitly specified.
    /// </remarks>
    public const string Unknown = "Unknown";
}
