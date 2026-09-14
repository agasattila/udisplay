# uDisplay Naming Conventions

This document defines the terminology used throughout the uDisplay specification, documentation, implementation, and wire protocol.

The purpose of these conventions is to distinguish between the YAML representation, the declarative uDisplay model, and the runtime state of a UI.

## 1. YAML terminology

When referring specifically to YAML syntax, standard YAML terminology should be used.

### Mapping

A collection of key-value pairs.

```yaml
temperature:
  type: display
  label: Temperature
  align: right
```

The value associated with `temperature` is a mapping.

### Key

A key identifies an entry in a mapping.

In the example above, `type`, `label`, and `align` are mapping keys.

### Value

The value associated with a mapping key.

For example:

```yaml
align: right
```

`align` is the key and `right` is its value.

### Sequence

An ordered collection of values.

```yaml
items:
  - Off
  - Auto
  - On
```

### Scalar

A single YAML value such as a string, number, or boolean.

```yaml
label: Temperature
visible: true
precision: 1
```

`Temperature`, `true`, and `1` are scalar values.

### Node

A general YAML term for an element of the YAML representation. Scalars, mappings, and sequences are all nodes.

## 2. uDisplay declarative model

The uDisplay documentation describes a domain model represented using YAML. Domain-specific terminology should be preferred when discussing this model.

### Widget

A declaratively defined UI element.

```yaml
temperature:
  type: display
  label: Temperature
  unit: °C
```

Here, `temperature` identifies a widget.

### Widget type

The kind of widget, specified by the `type` attribute.

Examples include `display`, `label`, `button`, `toggle`, and `slider`.

### Widget attribute

A named configuration field in a widget definition.

For example:

```yaml
temperature:
  type: display
  label: Temperature
  unit: °C
  align: right
  style: warning
  displayStyle: large
```

`type`, `label`, `unit`, `align`, `style`, and `displayStyle` are widget attributes.

In the YAML representation these are mapping keys, but in the uDisplay domain model they are referred to as **attributes**.

Therefore, documentation describing widgets should normally use *attribute* rather than *YAML key*.

For example:

> The `align` attribute controls the alignment of the widget.

rather than:

> The `align` YAML key controls the alignment of the widget.

The latter may still be appropriate when discussing the YAML representation itself.

## 3. Widget state

A widget's **state** is its dynamic application value.

Examples include:

* the numeric value displayed by a `display` widget;
* the boolean state of a `toggle`;
* the selected item of a `dropdown`;
* the current text of a dynamic text widget;
* the current value of a `slider`.

Widget state is transferred through the wire protocol using state-related messages such as `STATE_UPDATE`.

Conceptually:

> **State describes the application value represented by a widget.**

Changing a widget's state does not modify its declarative configuration.

## 4. Widget property

A **property** is a runtime-overridable characteristic of a widget other than its primary state value.

Examples may include:

* `visible`;
* `enabled`;
* the active stylesheet;
* other runtime-configurable presentation or behavior settings.

Properties are modified through protocol operations such as `SET_PROPERTY`.

Conceptually:

> **A property describes how a widget behaves or is presented at runtime, while state describes the application value represented by the widget.**

An attribute and a property are not synonymous.

A YAML attribute defines part of the declarative widget configuration. Some attributes may have corresponding runtime properties, while others may be immutable after the UI definition has been loaded.

For example:

```yaml
status:
  type: label
  visible: true
```

`visible` is a widget **attribute** in the declarative model.

At runtime, the same characteristic may be changed through a `VISIBLE` **property**:

```text
SET_PROPERTY(status, VISIBLE, false)
```

Thus, the terms describe different layers even when they refer to the same conceptual characteristic.

## 5. Style and stylesheet

The term **stylesheet** refers to a named global visual configuration.

Widgets reference a stylesheet through their `style` attribute.

For example:

```yaml
style:
  default:
    # ...
  warning:
    # ...

widgets:
  temperature:
    type: display
    style: warning
```

Here:

* `warning` is a stylesheet;
* `style` is a widget attribute referencing that stylesheet.

The `style` attribute should have the same meaning for all widget types.

Widget-specific presentation variants should use separate, explicitly named attributes such as:

```yaml
displayStyle: large
labelStyle: heading
```

This avoids overloading `style` with both global stylesheet selection and widget-specific presentation modes.

If stylesheet selection is runtime-configurable, the corresponding runtime property should represent the same concept:

```text
STYLE
```

Widget-specific presentation variants, if they become runtime-configurable, should use separate properties rather than overloading `STYLE`.

## 6. Terminology by layer

The same UI definition can be discussed at several layers:

| Layer                      | Preferred terminology                       |
| -------------------------- | ------------------------------------------- |
| YAML representation        | mapping, key, value, sequence, scalar, node |
| Declarative uDisplay model | widget, widget type, attribute, stylesheet  |
| Runtime model              | widget, state/value, property, event        |
| Wire protocol              | state update, property update, event        |

For example:

```yaml
temperature:
  type: display
  align: right
  style: warning
```

From the YAML perspective, `align` and `style` are **mapping keys**.

From the uDisplay declarative-model perspective, they are **widget attributes**.

If `style` can be changed after the UI has been instantiated, its runtime counterpart is a **widget property**.

## 7. Summary

Use the following terminology consistently:

**Attribute**
A field of a declarative widget definition.

**State / value**
The dynamic application value represented by a widget.

**Property**
A runtime-overridable characteristic of a widget other than its primary state value.

**Style**
A widget attribute referring to a global stylesheet.

**Stylesheet**
A named global visual configuration.

**Widget-specific style/variant**
A widget-specific presentation choice represented by an explicitly named attribute such as `displayStyle` or `labelStyle`.

The fundamental distinction is:

> **Attributes define the declarative UI. State carries application data. Properties modify the instantiated UI at runtime.**
