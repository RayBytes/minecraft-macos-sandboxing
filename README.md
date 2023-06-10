# minecraft-macos-sandboxing

*Documentation in progress*

# What is this?
This repository contains a sandbox-exec profile for MacOS to completely [sandbox](https://en.wikipedia.org/wiki/Sandbox_(computer_security)) Minecraft. This will disable minecraft from accessing any harmful data which it could gain access to without this profile. The profile will* only give access to files which minecraft needs to run.

# Why was this created?

As many have may of heard, recently the [Fractureiser](https://github.com/fractureiser-investigation/fractureiser/) virus has been spread all accorss modding sites, causing havoc in the modding community. This has shown the true abilitiy and reach of mods and how badly their ability of aribtrary code execution can be exploited. Due to this virus, the community as a whole has begun finding ways to limit the affect of possible future minecraft-mod based malware. While Fractureiser did not target MacOS, this likely will not happen in future malware, and so conseqently it was neccesary to find a way to make sure MacOS is completely or at least, as safe as possible, from these mods.

# How does it work?

It uses MacOS's inbuilt `sandbox-exec` command to work, as sandbox-exec is a fully native-to-MacOS way to securely sandbox apps.

# Usage

Run the command:
`sandbox-exec -f Path/To/The/Sandbox/Profile/minecraft-sandbox.sb /Applications/Minecraft.app/Contents/MacOS/launcher`
