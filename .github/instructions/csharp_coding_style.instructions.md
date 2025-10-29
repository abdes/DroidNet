---
applyTo: '**/*.cs'
---
# C# CODING STYLE INSTRUCTIONS

- Use C# 13 with preview features enabled.
- Respect The rules in my [editorconfig](../../.editorconfig).
- Use explicit access modifiers for all types and members (e.g., public, private, protected, internal).
- Always use braces `{}` for control statements (if, else, for, while, etc.), even for single-line statements.
- Use `var` for local variable declarations when the type is obvious from the right-hand side. Otherwise, use explicit types.
- Use expression-bodied members for simple properties and methods where appropriate.
- Always use `this.` to reference instance members to improve code readability.

## WinUI 3 Specific Guidelines
- Follow the MVVM (Model-View-ViewModel) pattern for UI development.
- Use data binding for UI elements instead of code-behind manipulation whenever possible.
- Keep the code-behind files minimal; most logic should reside in ViewModels.
- Act as an experienced WinUI 3 developer, providing best practices and patterns for building modern Windows applications.
- Use theme-aware resources for colors, fonts, and styles to ensure consistency across light and dark modes.
- Know and follow the latest WinUI 3 design and style guidelines and recommendations from Microsoft.
