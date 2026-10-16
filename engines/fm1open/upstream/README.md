# FM1 Open V12

English · [简体中文](README.zh-CN.md)

8 tracks, 8 sample slots, 16 FX demos; SDK included.

[Workbench and development kit](https://open.m-vave.com/fm1-workbench) · [Official forum](https://forum.m-vave.com)

Select "Source + SDK" in the workbench to download the development kit matching the online release.

## Build

Requires Windows x64, PowerShell, `curl.exe` and `tar.exe`. Firmware builds also require the [JieLi PI32v2 clang 4.0.1 r3 compiler](https://doc.zh-jieli.com/Tools/zh-cn/dev_tools/dev_env/jl_toolchain.html); its folder must contain `bin/clang.exe`.

Set `FM1_SOURCE_URL` to the HTTPS URL of this build's source archive, including its changes, license notices and build/install scripts. Web and package builds stop if it is missing.

~~~powershell
.\build.bat --web
$env:FM1_PI32_TOOLCHAIN = 'C:\JL\pi32'
.\build.bat --package
~~~

The web version builds from the included C sources. The first build downloads WASI SDK 27; set `FM1_WASI_SDK` to reuse an existing installation.

Outputs: `build/preview/index.html` (preview), `build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc` (firmware), `build/official-web/FM1-Official-Open-Source.html` (complete workbench).

Hardware drivers are in `app/board/`. The audio interrupt calls `fm1_engine_render()`; `app/audio_output.c` handles USB/Bluetooth mixing and USB recording.

APP: [GPLv3 with an SDK linking exception](LICENSE.md). The native engine is supplied as a binary; [SDK and third-party terms](THIRD_PARTY.md) apply separately.

Back up projects and samples before flashing.
