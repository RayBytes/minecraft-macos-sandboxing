# minecraft-macos-sandboxing

Sandboxing for modded Minecraft on macOS using Prism Launcher.

## What is this?

This project runs Minecraft through macOS `sandbox-exec`. It limits which files
mods can access and keeps the Minecraft access token outside the Java process.

Integration is provided through Prism's wrapper command.

Currently, only Prism Launcher & other MultiMC Launchers may work. Support for more launchers may be added in the future.

## How does it work?

The wrapper removes the access token from Minecraft's launch arguments and gives
Java a placeholder token. A small native broker holds the real token and handles
the requests needed to join online servers.

A Java agent redirects Mojang authlib's server join and profile certificate
requests to the broker. The modded JVM only has access to these limited requests.

The sandbox allows Minecraft to access its instance, Java runtime, Prism
libraries, temporary files, graphics, audio, and the network. It blocks Prism
account files, browser data, SSH keys, Keychain files, and other common private
data.

To my knowledge, this is one of the first ways to block the minecraft JVM instance on MacOS from accessing a user's account session token.

## Requirements

- macOS
- Prism Launcher
- Java 17 or newer
- Xcode Command Line Tools

## Installation

Close Prism Launcher and run:

```sh
./bin/install-prism-wrapper
```

Open Prism and launch Minecraft normally.

To remove the wrapper, close Prism and run:

```sh
./bin/install-prism-wrapper --uninstall
```

## Optional permissions

These can be added to Prism's global environment settings when needed:

```text
MC_SANDBOX_MICROPHONE=1
MC_SANDBOX_DISCORD=1
MC_SANDBOX_STEAM=1
```

## Limitations

A malicious mod still controls the running game. It can read Minecraft data,
send chat messages, change gameplay, and access files inside the instance.

There may still be errors around, & mods which modify the game significantly/require things in the filesystem make break.

While the game is running, a mod can ask the broker to authenticate multiplayer
connections. This could be used to join another server as the account during that
launch. The broker limits the number of requests and stops when Minecraft exits. This still provides a higher level of security which a malicious developer would need to design around, however we do need to improve this.

Modern Minecraft also places a temporary chat signing key inside Java. A mod can
copy and misuse that key until it expires.

`sandbox-exec` is deprecated by Apple and may change in future macOS releases.
