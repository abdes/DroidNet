# Router for WinUI navigation

DroidNet.Routing is a sophisticated URL-based routing system for WinUI applications, inspired by Angular's routing architecture. It enables navigation between views using both URL-based routing and dynamic router state manipulation.

The library is designed for .NET 8.0 and integrates with WinUI applications, providing a modern and flexible routing solution that maintains familiarity for developers coming from Angular while embracing .NET patterns and practices.

## Core Features

1. **URL-Based Navigation**
- Parse and handle complex URL patterns including:
  - Basic paths (`/Home`)
  - Modal routes (`modal:About`)
  - Matrix parameters (`/Documentation/GettingStarted;toc=true`)
  - Nested outlets (`/Documentation(popup:Feedback)`)
  - Query parameters

2. **Route Configuration**
- Hierarchical route definitions
- Multiple outlet support (primary, modal, popup, etc.)
- Route path matching patterns (full or prefix)
- View model type association with routes
- Route validation

3. **Router State Management**
- Active route tracking
- Router state serialization
- Context management for different navigation targets

4. **Advanced URL Processing**
- Robust URL parsing with detailed error reporting
- URL serialization and deserialization
- Matrix and query parameter handling
- Relative URL resolution

5. **Navigation Features**
- Full navigation support
- Partial/child route navigation
- Route activation lifecycle management
- Navigation event system
