### Usage
See [Homebrew_Applications](https://switchbrew.org/wiki/Homebrew_Applications) for SD layout and applications, etc. See [Switchbrew](https://switchbrew.org/wiki/Homebrew_Menu) for hbmenu docs.

### Shortcuts

Place `shortcuts.txt` in `/switch/` (next to hbmenu's app folders). Each line
adds a shortcut at the hbmenu root:

```text
RetroArch=sdmc:/switch/retroarch/retroarch_switch.nro
xStarbound=sdmc:/switch/brewstore/apps/xstarbound/xStarbound.nro
```

Blank lines and lines starting with `#` are ignored. The right side must be
an absolute `sdmc:/` path to an NRO. The shortcut uses the target NRO's
embedded icon and metadata.

### Download
The latest release is available from the [releases](https://github.com/switchbrew/nx-hbmenu/releases/latest) page.

### Building
Build for the Nintendo Switch with ```make nx``` and for the PC with ```make pc```.
Running ```make``` builds for both systems.

### Switch 2 HOME UI
On Switch 2 / kaiHEN the menu uses the Switch 2 HOME-style carousel, All
Software grid and full-screen Settings from
[ImDella/Switch2-HBmenu-Home](https://github.com/ImDella/Switch2-HBmenu-Home),
with additional Switch 2 and PS4 UI styles. The original project is by
[ImDella](https://github.com/ImDella). kaiHEN plugins can expose their own
options under Settings -> Plugins through the API in
[kaihen/plugin_settings.h](https://github.com/HerXayah/okaihen/blob/main/libkaihen/include/kaihen/plugin_settings.h).
Plugins appear automatically once they publish a service with
`pluginRuntimeInitializeWithService` and serve the reserved settings
commands; boolean options render as live On/Off toggles, changes apply
instantly without a reboot, and pressing Y in the tab refreshes the plugin
list. Plugins can also hot-swap their own code without a reboot by splitting
into a thin host `.nso` plus a reloadable payload `.nro`
([kaihen/plugin_payload.h](https://github.com/HerXayah/okaihen/blob/main/libkaihen/include/kaihen/plugin_payload.h)).

The following [pacman packages](https://devkitpro.org/wiki/devkitPro_pacman) are required to build for Switch:
- `switch-dev`
- `switch-freetype`
- `switch-libconfig`
- `switch-libjpeg-turbo`
- `switch-physfs`

The following libraries are required to build for PC:
- `libfreetype`
- `libconfig`
- `libjpeg-turbo`
- `libphysfs`
- `sdl2`

Building for Switch/PC requires `zip`.

Since C11 threads are used, building for the PC may fail if C11 threads are not available.

#### Credits

* This uses code based on 3DS [new-hbmenu](https://github.com/fincs/new-hbmenu).
* Switch 2 HOME UI, Settings and tactile SFX: [ImDella](https://github.com/ImDella), from [Switch2-HBmenu-Home](https://github.com/ImDella/Switch2-HBmenu-Home).
* Switch 2 port, toolchain and HEN: [ounce-hbmenu](https://github.com/ouncebrew/ounce-hbmenu), [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) and [kaiHEN](https://github.com/HerXayah/okaihen).
* `libjpeg-turbo` is used for handling JPEG icons. This library doesn't support lossless JPEG (likewise for official sw which uses `libjpeg-turbo`).
* `c11threads` is used for implement C11 Threads on Windows and macOS.