# Candis

ESP-IDF 6.2 BSP component for Candis, an ESP32-S31 development board with a
480×480 AMOLED display and CST9220 capacitive touch. The BSP targets hardware
revision 1.2 and can be used independently of an expansion board.

Include `bsp/esp-bsp.h`. The panel path uses a PSRAM composition buffer,
RGB565, TE synchronization and 32-row DMA transfers. The component also owns
the main I²C bus and supports sharing it with application-owned devices.

See the repository README for standalone examples and panel power
preconditions. The component is licensed under Apache-2.0.

Other APIs cover TG28 rail-state reads, RX8130CE calendar reads, SDMMC FAT
mounting, and optional ES8389 duplex audio. See the hardware notes for the
scope of hardware validation and separate expansion board components.
