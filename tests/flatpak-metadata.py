import ctypes
import gzip
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

APP_ID = "local.plan.tasks"
lib = ctypes.CDLL("libflatpak.so.0")


def call(name, result, *args):
    function = getattr(lib, name)
    function.argtypes = args
    function.restype = result
    return function


pointer = ctypes.c_void_p
number = ctypes.c_size_t
file_new = call("g_file_new_for_path", pointer, ctypes.c_char_p)
bundle_new = call("flatpak_bundle_ref_new", pointer, pointer, pointer)
get_appstream = call("flatpak_bundle_ref_get_appstream", pointer, pointer)
get_icon = call("flatpak_bundle_ref_get_icon", pointer, pointer, ctypes.c_int)
get_bytes = call("g_bytes_get_data", pointer, pointer, ctypes.POINTER(number))


def bytes_from(handle):
    if not handle:
        return b""
    length = number()
    data = get_bytes(handle, ctypes.byref(length))
    return ctypes.string_at(data, length.value)


def verify(path):
    file = file_new(os.fsencode(Path(path).resolve()))
    bundle = bundle_new(file, None)
    assert bundle, "Cannot open Flatpak bundle"
    metadata = bytes_from(get_appstream(bundle))
    assert metadata, "Flatpak bundle is missing AppStream metadata"
    catalog = ET.fromstring(gzip.decompress(metadata))
    components = catalog.findall("component")
    assert len(components) == 1, "Expected exactly one Flatpak AppStream component"
    component = components[0]
    assert component.findtext("id") == APP_ID
    names = {name.get("{http://www.w3.org/XML/1998/namespace}lang"): name.text
             for name in component.findall("name")}
    assert names.get(None) == "Plan Tasks" and names.get("ru") == "План задач", names
    assert component.findtext("launchable[@type='desktop-id']") == APP_ID + ".desktop"
    assert any(bytes_from(get_icon(bundle, size)).startswith(b"\x89PNG\r\n\x1a\n") for size in (128, 64)), "Missing bundled icon"


if __name__ == "__main__":
    verify(sys.argv[1])
    print("Flatpak bundle contains the translated name, matching launcher and icon.")
