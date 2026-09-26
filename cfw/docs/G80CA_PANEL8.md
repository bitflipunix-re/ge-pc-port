# G80CA-MB V1.2-20250422 Panel 8 hardware contract

This CFW targets the exact R36S clone identified by the existing hardware probe:

`G80CA-MB V1.2-20250422 Panel 8`

This is not treated as interchangeable with generic R36S / Panel-4 hardware.

## Reference DTB

Known-good vendor reference:

- repository: `southoz/dArkOSRE-R36`
- path: `files/BOOT/dtb/clone/G80CA-MB V1.2-20250422 Panel 8/rk3326-r36s-linux.dtb`
- Git blob SHA: `6897c5ff81d7947f3ad933cf8806e105ca9524b4`
- vendor kernel family: Rockchip 4.4-era tree

We decode hardware facts from that DTB and re-express them with Linux 6.12 interfaces.

## Display

- controller-compatible identity in vendor DTB: `elida,kd35t133`
- active area: 640 x 480
- pixel clock: 30,000,000 Hz (vendor/default ~60 Hz)
- experimental refresh ladder with unchanged porch geometry:
  - 75 Hz: 37.490 MHz
  - 90 Hz: 44.988 MHz
  - 100 Hz: 49.987 MHz
- 60 Hz remains preferred until 100 Hz is proven stable on real Panel-8 hardware
- horizontal: active 640, front porch 150, sync 40, back porch 135
- vertical: active 480, front porch 20, sync 6, back porch 12
- DSI lanes: 4
- format: RGB888
- mode flags: `0xa03`
- physical size: 153 x 85 mm
- reset: GPIO3_D3, active low
- panel power enable: GPIO3_A3, active high
- panel rail: 1.8 V
- backlight PWM: PWM1, 25 us period
- backlight rail: RK817 LDO7
- captured panel program: 24 commands / 370-byte encoded vendor property

The CFW panel description is byte-for-byte checked against the known-good vendor command payloads by `cfw/scripts/verify-g80ca-board.py`.

## Digital controls

The clone wiring differs materially from the generic R36S mainline DTS.

| Control | GPIO |
| --- | --- |
| D-pad Up | GPIO3_C6 |
| D-pad Down | GPIO3_C7 |
| D-pad Left | GPIO3_C4 |
| D-pad Right | GPIO3_C5 |
| A | GPIO3_C0 |
| B | GPIO3_C1 |
| X | GPIO3_C2 |
| Y | GPIO3_C3 |
| L3 | GPIO2_B5 |
| R3 | GPIO2_B6 |
| Fn | GPIO3_B2 |
| L1 | GPIO3_B5 |
| R1 | GPIO3_B3 |
| L2 | GPIO3_B6 |
| R2 | GPIO3_B4 |
| Select | GPIO3_D0 |
| Start | GPIO3_D1 |

All are active-low.

## Volume ladder

Volume buttons are not ordinary GPIO buttons on this board.

- SARADC channel: 2
- reference: 1.8 V
- vendor raw Volume Up value: 9 / 1023 (~16 mV)
- vendor raw Volume Down value: 166 / 1023 (~292 mV)

Linux 6.12 uses the upstream `adc-keys` driver for these.

## Analogue sticks

The board uses one SARADC input behind an analogue multiplexer.

- SARADC channel: 1
- mux select A: GPIO2_C0, active low
- mux select B: GPIO2_B7, active low
- mux enable: GPIO2_B3, active low
- vendor logical mapping: `<2 3 1 0>`
- logical order represented by that mapping: RY, RX, Y, X
- vendor ADC scale: 2
- vendor deadzone: 64
- vendor fuzz: 32
- vendor flat: 32

For the first mainline bring-up the CFW expresses the mux through upstream IIO mux + `adc-joystick` primitives rather than importing the entire Rockchip 4.4 joypad driver.

## Power / USB

- system rail: 3.8 V fixed
- USB host rail: 5 V
- USB host enable: GPIO3_A4, active high
- USB host source: RK817 boost regulator

## Console

The reference DTB maps:

- `serial2` -> UART2
- UART2 base: `0xff160000`

The CFW therefore makes `serial2 = &uart2` explicit and uses `ttyS2` for the bring-up serial console.

## GPU

- SoC: RK3326
- GPU: Mali-G31 / Bifrost class
- CFW kernel driver: mainline Panfrost
- userspace target: Mesa 26.2.3
- rendering path: DRM/KMS -> GBM -> EGL -> OpenGL ES/OpenGL

The old vendor Mali kernel driver is not carried forward.
