# udisplay-gen

Code generator for [uDisplay](https://github.com/agasattila/udisplay), the
universal remote UI for embedded devices.

You describe a device's UI in a YAML file. `udisplay-gen` validates it against
the uDisplay schema and generates firmware-side code: widget IDs, event handler
bindings, and the compressed UI blob that the device sends to the client.

## Install

Python 3.10 or later is required.

```bash
pip install ./udisplay-gen
```

For development (editable install with test dependencies):

```bash
cd udisplay-gen
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"
```

## Usage

```bash
# Create a starter YAML definition interactively
udisplay-gen init -o device.yaml

# Validate a YAML definition against the schema
udisplay-gen validate device.yaml

# Generate C code (default)
udisplay-gen build device.yaml -o ./generated

# Generate C++ code (C++11 compatible)
udisplay-gen build device.yaml --lang cpp -o ./generated

# Generate C++ code with std::function handlers
udisplay-gen build device.yaml --lang cpp --modern -o ./generated

# Generate MicroPython code
udisplay-gen build device.yaml --lang micropython -o ./generated
```

`python -m udisplay_gen` runs the same CLI without installing it, from a
directory that contains the `udisplay_gen` package.

### Output

| Command | Files |
|---|---|
| `build` | `udisplay_ui.h`, `udisplay_ui.c`, `udisplay_ui.bin` |
| `build --lang cpp` | `udisplay_ui.hpp`, `udisplay_ui.bin` |
| `build --lang micropython` | `ui.py`, `udisplay_runtime.py` |

- The C and C++ output builds against **libudisplay**, the firmware-side
  protocol library. `udisplay_ui.bin` is the raw compressed UI blob, for
  flashing to device ROM.
- `ui.py` is generated glue: never edit it by hand. Write your own `main.py`
  that imports it. `udisplay_runtime.py` is the MicroPython protocol runtime,
  copied verbatim. It supports TCP only, with no authentication yet.

### Multiple instances in one firmware

To link two separately generated UIs into one firmware image, give each one a
distinct `--namespace`. It must be a valid C identifier.

```bash
udisplay-gen build ble_ui.yaml --namespace ble -o ./generated
udisplay-gen build wifi_ui.yaml --namespace wifi -o ./generated
```

The namespace prefixes the widget-ID macros, the blob data arrays and the
bind/init helper. For example, `WIDGET_ID_*` becomes `BLE_WIDGET_ID_*` and
`udisplay_ui_init` becomes `udisplay_ble_ui_init`. The MicroPython backend does
not support `--namespace` yet.

## Tests

The test suite is in the repository, not in the framework package.

```bash
cd udisplay-gen
python3 -m pytest tests/
```

## Documentation

- [Quickstart](https://github.com/agasattila/udisplay/blob/main/docs/quickstart.md):
  from YAML to a running device
- [Widgets](https://github.com/agasattila/udisplay/blob/main/docs/widgets.md):
  the YAML widget reference
- [Building from source](https://github.com/agasattila/udisplay/blob/main/docs/building.md)

## License

`udisplay-gen` is licensed under the Mozilla Public License 2.0 (see `LICENSE`).
The MicroPython runtime (`udisplay_runtime.py`) is licensed under Apache 2.0, and
the code that `udisplay-gen` generates is licensed under MIT.
