// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using DroidNet.Aura.Documents;
using DroidNet.Aura.Windowing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// A document window for content editing.
/// </summary>
/// <remarks>
/// Theme synchronization is handled centrally by the WindowManagerService.
/// This window focuses solely on its document editing UI and behavior.
/// </remarks>
public sealed partial class DocumentWindow : Window
{
    private readonly IDocumentService? documentService;
    private readonly IWindowManagerService? windowManagerService;
    private readonly ILogger logger;

    public DocumentWindow(IDocumentService? documentService = null, IWindowManagerService? windowManagerService = null, ILoggerFactory? loggerFactory = null)
    {
        this.InitializeComponent();
        this.Title = "Document Window";

        this.documentService = documentService;
        this.windowManagerService = windowManagerService;
        this.logger = loggerFactory?.CreateLogger<DocumentWindow>() ?? NullLogger<DocumentWindow>.Instance;

        // Set a comfortable size for document editing
        this.AppWindow.Resize(new Windows.Graphics.SizeInt32(800, 600));

        // Set the title bar spacer as the drag region when it loads
        this.TitleBarSpacer.Loaded += (s, e) =>
        {
            try
            {
                this.SetTitleBar(this.TitleBarSpacer);
            }
            catch (Exception ex)
            {
                this.logger.LogWarning(ex, "Failed to set title bar for DocumentWindow");
            }
        };
    }
}
