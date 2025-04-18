![RPAsyncTCP](https://raw.githubusercontent.com/ayushsharma82/RPAsyncTCP/master/docs/splash-dark.png#gh-dark-mode-only)
![RPAsyncTCP](https://raw.githubusercontent.com/ayushsharma82/RPAsyncTCP/master/docs/splash-light.png#gh-light-mode-only)

[![GitHub release](https://img.shields.io/github/release/ayushsharma82/RPAsyncTCP.svg)](https://github.com/ayushsharma82/RPAsyncTCP/releases)
[![GitHub issues](https://img.shields.io/github/issues/ayushsharma82/RPAsyncTCP.svg)](http://github.com/ayushsharma82/RPAsyncTCP/issues)
[![arduino-library-badge](https://www.ardu-badge.com/badge/RPAsyncTCP.svg)](https://www.ardu-badge.com/RPAsyncTCP)


> [!TIP]
> This library is a fork of **AsyncTCP_RP2040W** and is a drop-in replacement for it. If you were previously using `AsyncTCP_RP2040W`, you will need to update your `include` directives to `#include <RPAsyncTCP.h>`.


## Table of Contents

- [What is RPAsyncTCP?](#what-is-rpasynctcp)
  - [Features](#i-features)
  - [Supported Boards](#ii-supported-boards)
- [Installation](#installation)
  - [Using Arduino Library Manager](#i-using-arduino-library-manager)
  - [Using PlatformIO](#ii-using-platformio)
  - [Manual Installation](#iii-manual-installation)
- [Examples](#examples)
- [Debugging](#debugging)
- [License](#license)
- [Authors & Maintainers](#authors--maintainers)

<br/>

## What is RPAsyncTCP?

RPAsyncTCP brings the **asynchronous** networking power of ESPAsyncTCP to **RP2040+W and RP2350+W MCUs**, serving as the foundation for libraries like `ESPAsyncWebServer` which allows for advanced routing and better performance compared to syncronous `WebServer` library.

### I. Features

- Handles multiple connections simultaneously
- Callbacks are triggered when requests are ready
- Faster response handling and improved efficiency
- Support for WebSockets, EventSource (Server-Sent Events), and URL Rewriting
- Static file serving with cache and indexing support
- Simple template processing

### II. Supported Boards

RPAsyncTCP supports [earlephilhower/arduino-pico](https://github.com/earlephilhower/arduino-pico) Arduino Board.

- **RP2040** + CYW43439 WiFi (Example: RaspberryPi Pico W)
- **RP2350** + CYW43439 WiFi (Example: RaspberryPi Pico 2W)

<br/>

## Installation

### I. Using Arduino Library Manager

Search for **RPAsyncTCP** in the Arduino Library Manager and install the latest version.


### II. Using PlatformIO

Search for **RPAsyncTCP** in the PlatformIO Library Manager and install in your project.


### III. Manual Installation

If you would like to install manually, you can follow these steps:

1. Download the latest release from [GitHub](https://github.com/ayushsharma82/RPAsyncTCP)
2. For Arduino IDE: Extract and place the folder in `~/Arduino/libraries/`
3. For PlatformIO, you can place the folder in your `libs` folder of your project.

<br/>

## Examples

Check out the example projects:

- [Client](https://github.com/ayushsharma82/RPAsyncTCP/tree/main/examples/Client/Client.ino)
- [Server](https://github.com/ayushsharma82/RPAsyncTCP/tree/main/examples/Server/Server.ino)

<br/>

## Debugging

Debugging is enabled by default on Serial. You can adjust the log level by modifying `_RPAsyncTCP_LOGLEVEL_` in the library files:

```cpp
// 0: DISABLED: no logging
// 1: ERROR: errors
#define _RPAsyncTCP_LOGLEVEL_ 1
```

<br/>

## License

This library is licensed under the [LGPL-3.0 License](LICENSE).

<br/>

## Authors & Maintainers

Thanks to [Khoi Hoang](https://github.com/khoih-prog) for the original fork to add support for RP2040+W. You can support him for his original work [here](https://www.buymeacoffee.com/khoihprog6).

- **Previous Authors:** Hristo Gochkov (2016), Khoi Hoang (2022)
- **Current Maintainer:** Ayush Sharma (2025)
