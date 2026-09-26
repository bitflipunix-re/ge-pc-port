#!/usr/bin/env python3
from pathlib import Path
import re
import sys

dts_path = Path(sys.argv[1] if len(sys.argv) > 1 else "cfw/board/r36s/rk3326-r36s.dts")
text = dts_path.read_text()

required_literals = [
    'model = "G80CA-MB V1.2-20250422 Panel 8";',
    'compatible = "bitflipunix,g80ca-r36s-panel8", "rockchip,rk3326";',
    'serial2 = &uart2;',
    'mmc1 = &sdio;',
    'vcc7-supply = <&vcc_3v0>;',
    'vcc9-supply = <&usb_midu>;',
    'gpio = <&gpio3 RK_PA3 GPIO_ACTIVE_HIGH>;',
    'reset-gpios = <&gpio3 RK_PD3 GPIO_ACTIVE_LOW>;',
    'gpio = <&gpio3 RK_PA4 GPIO_ACTIVE_HIGH>;',
    'io-channels = <&saradc 2>;',
    'mux-gpios = <&gpio2 RK_PB7 GPIO_ACTIVE_HIGH>,',
    '<&gpio2 RK_PC0 GPIO_ACTIVE_HIGH>;',
    'settle-time-us = <20>;',
    'regulator-name = "vcc_backlight";',
    'regulator-name = "vcc3v3_dvp";',
    'vccio1-supply = <&vcc_backlight>;',
    'vccio3-supply = <&vcc1v8_soc>;',
    'vccio6-supply = <&vcc1v8_soc>;',
    'cd-gpios = <&gpio0 RK_PA2 GPIO_ACTIVE_LOW>;',
    'vmmc-supply = <&vcc3v3_dvp>;',
    'vqmmc-supply = <&vcc_backlight>;',
    '"G size=153,85 delays=20,20,20,120,20 format=rgb888 lanes=4 flags=0xa03"',
    '"M clock=30000 horizontal=640,150,40,135 vertical=480,20,6,12 default=1"',
    '"M clock=37490 horizontal=640,150,40,135 vertical=480,20,6,12 default=0"',
    '"M clock=44988 horizontal=640,150,40,135 vertical=480,20,6,12 default=0"',
    '"M clock=49987 horizontal=640,150,40,135 vertical=480,20,6,12 default=0"',
]

for literal in required_literals:
    if literal not in text:
        raise SystemExit(f"missing G80CA invariant: {literal}")

expected_commands = [
    ("b9f11283", 0),
    ("b1000000da80", 0),
    ("b2001370", 0),
    ("b31010282803ff00000000", 0),
    ("b480", 0),
    ("b50a0a", 0),
    ("b68282", 0),
    ("b82662f063", 0),
    ("ba338105f90e0e2000000000000000442500900a0000014f01000037", 0),
    ("bc47", 0),
    ("bf021100", 0),
    ("c0737350500000125000", 0),
    ("c15300323277d1cccc77773333", 0),
    ("c68200bfff00ff", 0),
    ("c7b8000a000000", 0),
    ("c810401e02", 0),
    ("cc0b", 0),
    ("e000070d37353f4144060c0d0f111012141a00070d37353f4144060c0d0f111012141a", 0),
    ("e307070b0b0b0b00000000ff00c010", 0),
    ("e9c810020000b0b11131232880b0b127080004020000000004020000008888ba60240888888888888888ba713518888888888800000001000000000000000000", 0),
    ("ea970a820203070000000000008188ba17538888888888888088ba0642888888888888230000028000000000000000000000000000000000000000000000", 0),
    ("efffff01", 0),
    ("11", 200),
    ("29", 20),
]

actual = [
    (m.group(1).lower(), int(m.group(2) or 0))
    for m in re.finditer(r'"I seq=([0-9a-fA-F]+)(?: wait=([0-9]+))?"', text)
]

if actual != expected_commands:
    for i, (want, got) in enumerate(zip(expected_commands, actual)):
        if want != got:
            raise SystemExit(f"panel command {i} mismatch: expected {want}, got {got}")
    raise SystemExit(
        f"panel command count mismatch: expected {len(expected_commands)}, got {len(actual)}"
    )

if text.count('card-detect-delay = <800>;') != 2:
    raise SystemExit("G80CA must expose two SD slots with 800 ms detect delay")
if not re.search(r'&emmc\s*\{\s*status\s*=\s*"disabled";\s*\};', text, re.S):
    raise SystemExit("G80CA absent eMMC controller must remain disabled")

if text.count('RK_PC6 GPIO_ACTIVE_LOW') < 1:
    raise SystemExit("G80CA DPAD-UP wiring missing")
if 'RK_PD0 GPIO_ACTIVE_LOW' not in text or 'RK_PD1 GPIO_ACTIVE_LOW' not in text:
    raise SystemExit("G80CA Select/Start wiring missing")
if '<&joystick_mux 2>' not in text or '<&joystick_mux 3>' not in text:
    raise SystemExit("G80CA single-ADC mux mapping missing")

print("G80CA Panel-8 board invariants: PASS")
print(f"panel commands: {len(actual)}")
print(f"panel payload bytes: {sum(len(cmd) // 2 for cmd, _ in actual)}")
