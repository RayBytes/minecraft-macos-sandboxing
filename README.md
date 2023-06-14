# minecraft-macos-sandboxing

*Documentation in progress*

# What is this?
This repository contains a sandbox-exec profile for MacOS to completely [sandbox](https://en.wikipedia.org/wiki/Sandbox_(computer_security)) Minecraft. This will disable minecraft from accessing any harmful data which it could gain access to without this profile. The profile will* only give access to files which minecraft needs to run.

# Why was this created?

As many may of heard, recently the [Fractureiser](https://github.com/fractureiser-investigation/fractureiser/) virus has been spread all accross modding sites, causing havoc in the modding community. This has shown the true abilitiy and reach of mods and how badly their ability of aribtrary code execution can be exploited. Due to this virus, the community as a whole has begun finding ways to limit the affect of possible future minecraft-mod based malware. While Fractureiser did not target MacOS, this likely will not happen in future malware, and so conseqently it was neccesary to find a way to make sure MacOS is completely or at least, as safe as possible, from these mods.

# How does it work?

It uses MacOS's inbuilt `sandbox-exec` command to work, as sandbox-exec is a fully native-to-MacOS way to securely sandbox apps.

# Usage

Run the command:
`sandbox-exec -f Path/To/The/Sandbox/Profile/minecraft-sandbox.sb /Applications/Minecraft.app/Contents/MacOS/launcher`

# Patcher

A python application has been created to patch in the $HOME directory to where it needs to be for your system. Since the username of the computer changes per-system and since some file paths need to know the username, this patcher has been made. You do not need to run the patcher for minecraft version 1.6< and up, however any versions lower you will need to run the patcher. Any files in the launchers section will by default also need to run the patcher.

## How to run the patcher?

python patcher.py --dir=/path/to/sandbox/profile.sb --user-patch

# Launchers

If you want to sandbox a **launcher**, then look at the launchers folder in the repository. Here you will find any and all common launchers. If a launcher isn't there, create a issue and it will be added.


*Note: This project is still in development and may not function as it should, some extra files may still be given access to Minecraft which will be removed in future versions. Be vary of this before using this project.*
