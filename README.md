# tubedownloader

A lightweight Windows desktop app built to download YouTube videos quickly and painlessly. It's written in C++ with Qt 6 for the GUI, and uses yt-dlp under the hood to handle the actual downloading. When YouTube splits the video and audio into separate streams (which happens for most high-res videos), it automatically uses FFmpeg to merge them back together. 

No fancy setups, no Visual Studio required. It just works.

## Features
- **Simple UI**: Paste a link, hit "Get Video Info", pick your resolution, and download.
- **Auto-merge**: Downloads the best video and audio streams and merges them into an MP4.
- **Smart file handling**: Sanitizes filenames automatically and never overwrites your existing files (adds (1), (2), etc. instead).
- **Progress Tracking**: Shows real-time progress, download speed, ETA, and size.
- **Standalone**: Everything is bundled. You can move the folder anywhere.

## How to use it

Just grab the compiled folder (or build it yourself) and run TubeDownloader.exe.

Make sure you have these three tools inside a 	ools/ folder right next to the .exe:
- yt-dlp.exe (handles the downloads)
- fmpeg.exe (merges the streams)
- deno.exe (optional but recommended: a JS runtime that helps yt-dlp bypass YouTube's extraction protections)

*Pro tip: YouTube changes their systems all the time. If downloads stop working, open a terminal in the tools folder and run yt-dlp.exe -U to update it.*

## Building from source

You don't need Visual Studio for this. The project is set up to build with CMake and the MinGW toolchain. 

1. Install Qt 6 (MinGW version), CMake, and Ninja.
2. Run the build script:
`powershell
.\build.ps1
`
3. The self-contained output will be placed in the dist\TubeDownloader\ folder.

## Disclaimer
Please only use this app for videos you own, have permission to download, or are otherwise authorized to save.
