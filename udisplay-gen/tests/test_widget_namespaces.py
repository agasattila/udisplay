"""Opt-in namespaces for generated names (PR #44 review; docs/designs/
widget-id-for-every-widget.md, "Revision 2: opt-in namespaces").

A section/row/grid is transparent to its children's generated names unless
it sets `namespace: true`; dpad, button-group and a button's face are always
namespaces. Only generated identifiers follow this rule: wire IDs stay
numbered by the full structural path."""
import pytest
import yaml as pyyaml

from udisplay_gen.backends import BuildContext, c_backend, cpp_backend, python_backend
from udisplay_gen.merkle import compute
from udisplay_gen.validate import load_schema, validate
from udisplay_gen.widget_ids import (
    assign, collect_types, name_path_collisions, name_paths, scope_members, widget_tree,
)

HEADER = "device:\n  name: ns\n  capabilities: [layout-v2]\nwidgets:\n"


def _doc(body: str) -> dict:
    return pyyaml.safe_load(HEADER + body)


def _ctx(body: str) -> BuildContext:
    text = (HEADER + body).encode()
    blob, root, hashes = compute(text)
    widgets = pyyaml.safe_load(text)["widgets"]
    return BuildContext(
        widget_ids=assign(widgets), blob=blob, root=root, hashes=hashes,
        source="ns.yaml", widget_types=collect_types(widgets), widgets_yaml=widgets,
    )


def _outputs(body: str) -> dict:
    """lang -> generated text of the file firmware code includes."""
    ctx = _ctx(body)
    return {
        "c": c_backend._generate_header(ctx),
        "cpp": cpp_backend._generate_header_cpp(ctx),
        "py": python_backend._generate_ui_py(ctx),
    }


# The same `temp` slider placed four ways; firmware names must not care.
PLACEMENTS = {
    "top level": "  temp:\n    type: slider\n    min: 0\n    max: 1\n"
                 "  other:\n    type: row\n    widgets:\n      x:\n        type: led\n",
    "in a row": "  other:\n    type: row\n    widgets:\n      x:\n        type: led\n"
                "      temp:\n        type: slider\n        min: 0\n        max: 1\n",
    "in a grid": "  other:\n    type: row\n    widgets:\n      x:\n        type: led\n"
                 "  g:\n    type: grid\n    columns: 1\n    widgets:\n"
                 "      temp:\n        type: slider\n        min: 0\n        max: 1\n",
    "in a section's row": "  other:\n    type: row\n    widgets:\n      x:\n        type: led\n"
                          "  s:\n    type: section\n    widgets:\n      r:\n        type: row\n"
                          "        widgets:\n          temp:\n            type: slider\n"
                          "            min: 0\n            max: 1\n",
}


class TestLayoutIndependence:
    @pytest.mark.parametrize("where", list(PLACEMENTS))
    def test_identifiers_do_not_depend_on_transparent_containers(self, where):
        out = _outputs(PLACEMENTS[where])
        assert "#define WIDGET_ID_TEMP " in out["c"]
        assert "static inline void set_temp(udisplay_t* ctx, float v)" in out["c"]
        assert "    void (*on_temp_change)(float value);" in out["c"]
        assert "    SliderWidget temp;" in out["cpp"]
        assert "self->temp.on_change(ev->slider_value)" in out["cpp"]
        assert "        self.temp = SliderWidget(self._device, WIDGET_ID_TEMP)" in out["py"]
        assert "self.temp.on_change(value)" in out["py"]

    def test_wire_ids_still_follow_the_structural_path(self):
        ids = assign(_doc(PLACEMENTS["in a section's row"])["widgets"])
        assert set(ids) == {"other", "other.x", "s", "s.r", "s.r.temp"}
        assert [ids[p] for p in sorted(ids)] == list(range(0x10, 0x15))

    def test_crossing_a_namespace_boundary_renames(self):
        body = PLACEMENTS["in a section's row"].replace(
            "    type: section\n", "    type: section\n    namespace: true\n")
        out = _outputs(body)
        assert "#define WIDGET_ID_S_TEMP " in out["c"]
        assert "WIDGET_ID_TEMP " not in out["c"]
        assert "class SWidget : public Widget {" in out["cpp"]
        assert "self->s.temp.on_change" in out["cpp"]
        assert "self.s.temp = SliderWidget(" in out["py"]


NAMESPACES = """\
  indoor:
    type: section
    namespace: true
    widgets:
      temperature:
        type: display
  outdoor:
    type: row
    namespace: true
    widgets:
      temperature:
        type: display
  nav_a:
    type: dpad
    widgets:
      up:
        type: button
        position: top
  nav_b:
    type: dpad
    widgets:
      up:
        type: button
        position: top
"""


class TestNamespaces:
    def test_valid(self):
        assert validate(_doc(NAMESPACES)) == []

    def test_names(self):
        out = _outputs(NAMESPACES)
        for macro in ("INDOOR_TEMPERATURE", "OUTDOOR_TEMPERATURE", "NAV_A_UP", "NAV_B_UP"):
            assert f"#define WIDGET_ID_{macro} " in out["c"], macro
        assert "WIDGET_ID_TEMPERATURE " not in out["c"]
        assert "    IndoorWidget indoor;" in out["cpp"]
        assert "    NavAWidget nav_a;" in out["cpp"]
        assert "self->nav_b.up.on_click()" in out["cpp"]
        assert "    DisplayWidget temperature;" in out["cpp"]  # inside the classes
        assert "self.indoor.temperature = DisplayWidget(" in out["py"]
        assert "self.temperature" not in out["py"]

    def test_nested_namespaces_and_transparent_row_inside_a_namespace(self):
        body = """\
  a:
    type: section
    namespace: true
    widgets:
      b:
        type: grid
        columns: 1
        namespace: true
        widgets:
          leaf:
            type: led
      r:
        type: row
        widgets:
          other:
            type: led
"""
        assert name_paths(_doc(body)["widgets"]) == {
            "a": "a", "a.b": "a.b", "a.b.leaf": "a.b.leaf",
            "a.r": "a.r", "a.r.other": "a.other",
        }
        assert validate(_doc(body)) == []

    def test_scope_members_inline_transparent_children(self):
        tree = widget_tree(_doc(PLACEMENTS["in a section's row"])["widgets"])
        assert [n.key for n in scope_members(tree)] == ["other", "x", "s", "r", "temp"]


class TestDuplicateNames:
    BODY = """\
  r1:
    type: row
    widgets:
      temp:
        type: display
  r2:
    type: row
    widgets:
      temp:
        type: display
"""

    def test_collision_reported(self):
        assert name_path_collisions(_doc(self.BODY)["widgets"]) == [
            ("temp", ["r1.temp", "r2.temp"])]

    @pytest.mark.parametrize("backend", [c_backend, cpp_backend, python_backend])
    def test_every_backend_rejects_it_without_validation(self, backend):
        with pytest.raises(ValueError, match="widgets.r1.temp, widgets.r2.temp: all named 'temp'"):
            backend.generate(_ctx(self.BODY))


class TestPythonReservedNamesFollowScope:
    def test_transparent_row_child_named_like_ui_method_rejected(self):
        body = "  r:\n    type: row\n    widgets:\n      feed:\n        type: led\n"
        with pytest.raises(ValueError, match="'feed' collides"):
            python_backend.generate(_ctx(body))

    def test_transparent_row_child_named_id_accepted(self):
        """`id` is reserved on Widget, not on UI, where this leaf lands."""
        body = "  r:\n    type: row\n    widgets:\n      id:\n        type: led\n"
        assert "self.id = LedWidget(" in python_backend._generate_ui_py(_ctx(body))


class TestSchema:
    @pytest.mark.parametrize("container", [
        "    type: section\n",
        "    type: row\n",
        "    type: grid\n    columns: 1\n",
    ])
    @pytest.mark.parametrize("value", [True, False])
    def test_flag_accepted_on_section_row_grid(self, container, value):
        body = f"  c:\n{container}    namespace: {str(value).lower()}\n" \
               "    widgets:\n      x:\n        type: led\n"
        assert validate(_doc(body), load_schema()) == []

    def test_flag_accepted_on_a_button_face_row(self):
        body = ("  b:\n    type: button\n    widgets:\n      face:\n        type: row\n"
                "        namespace: true\n        widgets:\n          x:\n            type: led\n")
        assert validate(_doc(body), load_schema()) == []

    @pytest.mark.parametrize("widget", [
        "    type: dpad\n    widgets:\n      up:\n        type: button\n        position: top\n",
        "    type: button\n    widgets:\n      x:\n        type: led\n",
        "    type: button-group\n    items:\n      a:\n        label: A\n",
    ])
    def test_flag_rejected_on_compound_widgets(self, widget):
        body = f"  c:\n{widget}    namespace: true\n"
        errs = validate(_doc(body), load_schema())
        assert errs and any("namespace" in e for e in errs), errs

    def test_non_boolean_rejected(self):
        body = "  c:\n    type: row\n    namespace: yes please\n" \
               "    widgets:\n      x:\n        type: led\n"
        assert validate(_doc(body), load_schema()) != []
