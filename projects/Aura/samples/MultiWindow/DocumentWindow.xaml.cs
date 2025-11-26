// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
///     A document window for content editing.
/// </summary>
[SuppressMessage("Usage", "CA1812:AvoidUninstantiatedInternalClasses", Justification = "Instantiated via DI")]
internal sealed partial class DocumentWindow : Window
{
    private readonly IDocumentService? documentService;
    private readonly IWindowManagerService? windowManagerService;
    private readonly ILogger logger;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentWindow"/> class.
    /// </summary>
    /// <param name="documentService">The service used for document management operations.</param>
    /// <param name="windowManagerService">The service used for window management operations.</param>
    /// <param name="loggerFactory">The factory used to create logger instances.</param>
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
        this.TitleBarSpacer.Loaded += (s, e) => this.SetTitleBar(this.TitleBarSpacer);
    }
}
