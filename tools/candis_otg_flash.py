#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Flash the built application's partition through the Candis USB OTG port."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time

import esptool
from esptool.util import FatalError
from serial import Serial
from serial.tools import list_ports


VID_ESPRESSIF = 0x303A
PID_APPLICATION = 0x4001
PID_S31_ROM = 0x0020
# ESP32-S31 LP_SYSTEM_REG_SYS_CTRL_REG, preserved across a digital reset.
LP_SYS_CTRL = 0x20700008
FORCE_DOWNLOAD_BOOT = 1 << 2
# esp_app_desc_t begins after the ESP image header and first segment header.
APP_DESCRIPTOR_OFFSET = 32
APP_DESCRIPTOR_MAGIC = b'\x32\x54\xcd\xab'


def normalize_serial(value):
    return re.sub(r'[:-]', '', value or '').lower()


def port_location(port):
    return (port.location or '').split(':', 1)[0]


def find_port(pid, serial, location=''):
    ports = [port for port in list_ports.comports()
             if port.vid == VID_ESPRESSIF and port.pid == pid
             and normalize_serial(port.serial_number) == serial
             and (not location or port_location(port) == location)]
    if len(ports) > 1:
        raise RuntimeError(f'Multiple matching USB devices for PID {pid:04x}')
    return ports[0] if ports else None


def wait_for_application(serial, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        application = find_port(PID_APPLICATION, serial)
        if application is not None:
            return application
        time.sleep(0.02)
    raise RuntimeError('Candis USB OTG application port did not enumerate')


def wait_for_rom(serial, location):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        port = find_port(PID_S31_ROM, serial, location)
        if port is not None:
            return port
        time.sleep(0.1)
    raise RuntimeError('Matching ESP32-S31 OTG ROM port did not enumerate')


def request_rom(port):
    with Serial(baudrate=115200, timeout=0.2) as connection:
        connection.dtr = False
        connection.rts = False
        connection.port = port.device
        connection.open()
        time.sleep(0.1)
        connection.rts = True
        time.sleep(0.1)
        connection.dtr = True
        connection.rts = False
        time.sleep(0.05)
        connection.dtr = False
        connection.rts = False


def app_artifact(build_dir, installed_table):
    root = build_dir.resolve()
    args = json.loads((root / 'flasher_args.json').read_text())
    app = args['app']
    address = int(app['offset'], 0)
    image = (root / app['file']).resolve(strict=True)
    if not image.is_relative_to(root) or address < 0x10000 or address % 0x10000:
        raise ValueError('Expected an application image inside the build and an aligned app address')
    with image.open('rb') as file:
        header = file.read(APP_DESCRIPTOR_OFFSET + len(APP_DESCRIPTOR_MAGIC))
        if header[:1] != b'\xe9' or header[APP_DESCRIPTOR_OFFSET:] != APP_DESCRIPTOR_MAGIC:
            raise ValueError('Image is missing the ESP-IDF application descriptor')
    if app['encrypted'] not in (False, 'false'):
        raise ValueError('Encrypted application images are outside this diagnostic workflow')
    if 'IDF_PATH' not in os.environ:
        raise RuntimeError('Activate the ESP-IDF v6.2 environment first')
    sys.path.append(str(Path(os.environ['IDF_PATH']) / 'components/partition_table'))
    from gen_esp32part import APP_TYPE, InputError, MAX_PARTITION_LENGTH, PartitionTable

    table = args['partition-table']
    table_address = int(table['offset'], 0)
    table_path = installed_table or (root / table['file'])
    table_data = table_path.read_bytes()[:MAX_PARTITION_LENGTH]
    try:
        entries = PartitionTable.from_binary(table_data)
    except InputError as exc:
        raise ValueError(f'Invalid partition table: {exc}') from exc
    # This application-only tool neither changes OTA selection nor stages a new layout.
    image_size = image.stat().st_size
    matches = [entry for entry in entries if entry.type == APP_TYPE and entry.subtype == 0
               and entry.offset == address and entry.size >= image_size and not entry.encrypted and not entry.readonly]
    if len(matches) != 1 or table_address % 0x1000 or address < table_address + 0x1000:
        raise ValueError('Image does not fit an unencrypted factory app partition at the requested address')
    return address, image, table_address, table_data


def exit_download(chip):
    # Changing this volatile bit exits the app-requested download mode.
    chip.write_reg(LP_SYS_CTRL, 0, FORCE_DOWNLOAD_BOOT)
    if chip.read_reg(LP_SYS_CTRL) & FORCE_DOWNLOAD_BOOT:
        raise RuntimeError('Unable to clear FORCE_DOWNLOAD_BOOT')
    esptool.reset_chip(chip)


def flash_application(rom, serial, address, image, table_address, table_data):
    print(f'S31 ROM matched: {rom.device}.', flush=True)
    with esptool.detect_chip(rom.device, connect_mode='no-reset') as chip:
        if chip.CHIP_NAME != 'ESP32-S31' or not chip.uses_usb_otg() or chip.secure_download_mode:
            raise RuntimeError('Expected a standard ESP32-S31 USB OTG ROM connection')
        if bytes(chip.read_mac()).hex() != serial:
            raise RuntimeError('ROM MAC does not match the selected device')
        esptool.attach_flash(chip)
        expected = hashlib.md5(table_data, usedforsecurity=False).hexdigest()
        if chip.flash_md5sum(table_address, len(table_data)).lower() != expected:
            exit_download(chip)
            raise RuntimeError('Installed partition table differs. Write cancelled, supply its verified binary.')
        print(f'Factory partition verified. Writing {image.name} at {address:#x}.', flush=True)
        esptool.write_flash(chip, [(address, str(image))])
        exit_download(chip)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, help='Candis USB serial or MAC address')
    parser.add_argument('--build-dir', required=True, type=Path, help='ESP-IDF build with the installed partition layout')
    parser.add_argument('--installed-table', type=Path, help='Verified installed partition-table binary if it differs from the build')
    args = parser.parse_args()
    serial = normalize_serial(args.serial)
    if not re.fullmatch(r'[0-9a-f]{12}', serial):
        parser.error('--serial must be six hexadecimal bytes')
    try:
        app = app_artifact(args.build_dir, args.installed_table)
        rom = find_port(PID_S31_ROM, serial)
        if rom is None:
            application = find_port(PID_APPLICATION, serial)
            if application is None:
                application = wait_for_application(serial)
            try:
                request_rom(application)
            except OSError:
                # The control request itself may disconnect the port. Check ROM identity next.
                pass
            rom = wait_for_rom(serial, port_location(application))
        flash_application(rom, serial, *app)
    except (FatalError, OSError, ValueError, KeyError, RuntimeError) as exc:
        parser.exit(1, f'Candis OTG: {exc}\n')


if __name__ == '__main__':
    main()
