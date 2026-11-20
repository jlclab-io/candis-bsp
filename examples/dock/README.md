# Candis Dock example

Use this example with a Candis Dock rev. 1.1 or 1.2 and a stable USB supply. Connect both BTB connectors with power off. Leave the Dock expansion headers empty unless the module loads and shared GPIO have been verified. See the [Dock component](../../components/candis_dock/README.md) for hardware requirements.

```sh
idf.py --preview -C examples/dock build
```

Follow [USB and flashing](../../docs/en/usb.md) to install the application. Open the application CDC console and send one command per line.

| Command | Action |
| --- | --- |
| `power` | Read supply and charge status |
| `dock` | Open the Dock session and start card detection |
| `id` | Read the baseboard identity |
| `tf` | Browse the FAT card in read-only mode |
| `screen on` / `screen off` | Start or stop the AMOLED and touch |
| `tone dock` | Play a short tone on the CN3 channel for the detected board revision |
| `release` | Close the session and release resources |

Use a FAT-formatted TF card and wait for the browse command to finish before removing it. Connect the speaker to CN3 before playback. Use the left channel on rev. 1.1 and the right channel on rev. 1.2.
