# Design: C++98-compatible default C++ output (issue #48)

Branch: feature/cpp98-default-output
Repo: agasattila/udisplay
Status: Step 2 (implementation design). The step-1 specification was approved
on the issue on 2026-10-13, with these decisions:

1. Default variant spells the enum type `Item::Value` / `Option::Value`.
2. `--modern` keeps `enum class` (type spelled `Item` / `Option`).
3. The dropdown range check applies to both variants; the generator must
   guarantee contiguous dropdown indices `0..N-1`.
4. Compile tests include C++20.
5. The default/modern type-spelling difference is documented together with
   the required changes to existing user code.

The specification (generated code, examples, caveats) is the issue comment;
this document covers *how* the code, tests and docs change.

## 1. `udisplay_gen/backends/cpp_backend.py`

### 1.1 One handler helper

`_cpp_base_classes()` and `_cpp_generated_classes()` each define an identical
nested `handler(sig)`. Both move to one module-level helper:

```python
def _cpp_handler(sig: str, variant: str) -> tuple[str, str | None]:
    """(member declaration, constructor initializer or None)"""
```

- `safe`: `("    void (*on_change)(bool state);", "on_change(0)")`
- `modern`: `("    std::function<void(bool)> on_change;", None)`
  (a `std::function` default-constructs empty; no initializer needed).

Each class with handlers builds its constructor's initializer list from the
base-class initializer plus the non-`None` handler initializers, in member
declaration order (so `-Wreorder` stays quiet). A small helper
`_cpp_ctor(cls, base_init, handler_inits)` renders
`Cls(udisplay_t* ctx, uint8_t id) : Base(ctx, id), on_x(0), ... {}`.

### 1.2 Base classes

- Derived-class constructors name the base explicitly:
  `OutputWidget<bool>(ctx, id)` instead of the injected `OutputWidget(ctx, id)`.
  (Both are valid C++98 for a non-dependent base; the explicit form reads
  the same everywhere, including inside the dropdown classes.)
- `ToggleWidget`, `SliderWidget`, `ButtonWidget`, `ButtonItem`,
  `TextRwWidget` get handler initializers per 1.1.

### 1.3 Generated classes

- **Dropdown:** explicit forwarding constructor in both variants (drops
  `using OutputWidget::OutputWidget;`, which is C++11). The enum is emitted by
  a helper `_cpp_enum(name, entries, variant)`:
  - `safe`: `struct Option { enum Value { a = 0u, b = 1u }; };`,
    `set(Option::Value v)`, handler `on_change(Option::Value selection)`.
  - `modern`: `enum class Option : uint8_t { ... };` (unchanged).
- **Button-group:** same helper for `Item` (values are the item widget IDs).
  `set(Item::Value v)` in `safe`, `set(Item v)` in `modern`.
- A helper `_cpp_enum_type(name, variant)` returns `"Option::Value"` or
  `"Option"`, used by `set()`, the handler signature and the dispatch cast.

### 1.4 `UDisplay`

- `safe`: `on_client_ready` / `on_comms_error` are plain pointers initialized
  in `UDisplay()`'s initializer list after the widget members (their
  declaration order). The list is emitted correctly when there are no
  widget members (the first entry gets `: `).
- `safe`: non-copyable via private, never-defined copy constructor and copy
  assignment. `modern`: keeps the four `= delete` declarations.

### 1.5 Dispatch (both variants)

```cpp
    case 0x08u:
        if (ev->selection_index < 2u && self->wifi_mode.on_change)
            self->wifi_mode.on_change(static_cast<WifiModeWidget::Option::Value>(ev->selection_index));
        break;
```

The bound is `len(items)`, from the same `collect_dropdown_items()` list the
enum is emitted from, so the check and the enumerators can't drift apart. With
exactly 256 items every byte is a valid index and the check is omitted: an
always-true `uint8_t < 256u` fails `-Wtype-limits` (`-Wextra -Werror`).

### 1.6 Contiguous dropdown indices

Indices are positions in `collect_dropdown_items()`'s list (YAML declaration
order, via `enumerate`), so they are `0..N-1` by construction; the schema
requires `N >= 2`. What the schema does not bound is `N <= 256`: with more
items, indices past 255 can't travel in the `uint8` selection byte, and the
range check would compare a `uint8_t` against a constant it can never reach.
`validate.py` gains a semantic check rejecting a dropdown with more than 256
items (applies to every backend, since the wire format is shared). Tests pin
the `0..N-1` emission and the bound `N` in the dispatch.

## 2. `libudisplay/include/libudisplay/udisplay.h`

Drop the trailing comma after `UDISPLAY_TRANSPORT_BLE = 2`.

## 3. Tests (`udisplay-gen/tests/`)

- `test_generated_compiles.py`
  - Default variant, parametrized over `c++98` (with
    `-pedantic-errors -Wall -Wextra -Werror`), `c++11`, `c++17`, `c++20`
    (same warning flags). A standard the host compiler doesn't accept is
    skipped, not failed.
  - Default-variant usage TU (no lambdas): handler assignment with free
    functions, `set(X::Item::name)`, `X::Item::Value` / `X::Option::Value`
    variables, `switch` over the enum, `==`, `set()` / `clear()`, the
    dropdown handler signature.
  - Negative compile checks (default variant, `c++98`): copying `UDisplay`,
    `set()` with a raw integer, and `set()` with another widget's enumerator
    must each fail to compile.
  - `--modern`, parametrized over `c++11` and `c++20`, with the lambda usage
    TU as today.
- `test_build.py`: the text assertions on `enum class` / `Option` move to
  per-variant expectations (struct form for the default, `enum class` for
  `--modern`), plus assertions for the handler initializers, private copy
  operations, and the range-checked dispatch.
- `test_validate.py`: >256 dropdown items rejected; 256 accepted.

## 4. Demos

- `demos/demo02/main.cpp`: the two type spellings from spec section 4.
- demo02 stays at `CMAKE_CXX_STANDARD 11`; the language-level guarantee is
  enforced by the pytest compile matrix, not the demo build.
- demo03 (`--modern`) is unchanged.

## 5. Docs

- `docs/building.md`, `udisplay-gen/README.md`: "C++11 compatible" ->
  "C++98 compatible"; `--modern` requires C++11.
- `docs/widgets.md`: default-variant button-group and dropdown snippets, a
  "Default vs `--modern` enum types" note with the before/after diff for
  existing code, and the spec's section-5 caveats.
- `docs/designs/button-group-exclusive-select.md`: note that its C++11
  constraint is superseded for the default variant by this design.
- `udisplay_gen/backends/_shared.py`: docstring of
  `_config_sequential_assignment()` (still correct reasoning, baseline now C++98).

## 6. Order of work (one commit each)

1. libudisplay header comma.
2. Backend changes (1.1-1.5) + `test_build.py` updates.
3. Dropdown item bound (1.6) + validation tests.
4. Compile-test matrix + usage/negative fixtures.
5. demo02 + docs.
