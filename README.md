# RAGE 2 DLC unlock

This small proxy DLL marks that content as owned **from within the game itself**.

- Does **not** touch Steam, Epic or any other launcher or store

## Install

1. Download `RAGE2-DLC-Fix-v1.0.zip` from the **Releases** page.
2. Back up your saves.
3. Copy `XINPUT9_1_0.dll` next to `RAGE2.exe` and start the game.
4. `rage2_dlcfix.ini` and `rage2_dlcfix.log` are created in the same folder.

```ini
[DLC]
RiseOfTheGhosts=1   ; Rise of the Ghosts expansion
Terrormania=1       ; Terrormania expansion
OtherItems=1        ; other entitlement content (bonus mission, weapons, etc.)
```

Set any value to `0` to leave that part to the game. **Uninstall:** delete `XINPUT9_1_0.dll`.

## FAQ

**Which versions work?** Tested on the Steam build of `RAGE2.exe` (v1.09). The DLL checks the exe by byte signature and does nothing on a different build; if so, `rage2_dlcfix.log` says "Signature mismatch".

**Does it change my saves?** No. It only sets the in-memory entitlement flags the game would normally get from Bethesda.net.

## How it works

RAGE 2 has a built-in entitlement system that normally fills a table of owned content from Bethesda.net's response. This DLL stands in as `XINPUT9_1_0.dll` (forwarding the real XInput calls), hooks the game's UI refresh, and calls the game's own grant function for the DLC entitlements. That fills the same table the game would have filled itself.

## Build

```
x86_64-w64-mingw32-gcc -shared -O2 -Wall -o XINPUT9_1_0.dll src/rage2_dlcfix.c -static-libgcc -Wl,--kill-at
```

Or run the **build** GitHub Action (Actions tab), which uploads the DLL as an artifact.

## License

See [LICENSE](LICENSE): free to use, share and modify.
