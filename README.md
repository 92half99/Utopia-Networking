# Utopia-Networking

**Utopia-Networking** is an optional module for [Utopia](https://github.com/92half99/Utopia) that provides networking capabilities.

## Getting Started

To add **Utopia-Networking** to your [Utopia](https://github.com/92half99/Utopia) application:

1. Clone this repository as a submodule within the `Utopia/` directory. If placed correctly, Utopia's build scripts will automatically detect and include it in your project.

2. For manual integration, include the `Build-Utopia-Networking.lua` file in your build scripts, as shown below:

```lua
include "Utopia/Utopia-Networking/Build-Utopia-Networking.lua"
```

## Features

- **Cross-Platform Support:** Compatible with Windows and Linux.
- **Comprehensive Networking API:** Includes client/server functionality for both reliable and unreliable data transmission using Valve's [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) library.
- **Simplified Event Management:** Provides clean and efficient network event callbacks and connection management.
- **DNS Resolution Utility:** Includes a utility function (`Utopia::Utils::ResolveDomainName`) to translate domain names into IP addresses.

### Third-Party Libraries

- [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)
