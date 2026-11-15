#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the checked-in PE32 OpenGL thunk metadata from pinned gl.xml."""
import argparse
import hashlib
import json
import pathlib
import sys
import urllib.request
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "src/tools/x86-gl-manifest.json"
DEFAULT_OUTPUT = ROOT / "src/xbox-test/app/host/x86/x86_gl_generated.h"
WIDE_TYPES = {"GLdouble", "GLclampd", "GLint64", "GLuint64", "GLint64EXT", "GLuint64EXT"}
FLOAT_TYPES = {"GLfloat", "GLclampf"}
SCALAR_ALIASES = {"GLhandleARB"}


def registry_bytes(manifest, path):
    if path:
        data = pathlib.Path(path).read_bytes()
    else:
        request = urllib.request.Request(manifest["registry"]["url"], headers={"User-Agent": "xboxnative-gl-thunk-generator"})
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != manifest["registry"]["sha256"]:
        raise ValueError(f"gl.xml SHA-256 mismatch: expected {manifest['registry']['sha256']}, got {digest}")
    return data


def generate(data, manifest):
    root = ET.fromstring(data)
    commands = {}
    type_declarations = {}
    for type_node in root.findall("./types/type"):
        type_name = type_node.find("name")
        if type_name is not None and type_name.text:
            type_declarations[type_name.text] = "".join(type_node.itertext())
    for command in root.findall("./commands/command"):
        name_node = command.find("proto/name")
        if name_node is not None and name_node.text:
            commands[name_node.text] = command
    selected = set()
    for feature in root.findall("./feature"):
        if feature.get("api") != "gl" or tuple(map(int, feature.get("number", "0.0").split("."))) > (4, 6):
            continue
        for command in feature.findall("./require/command"):
            if command.get("name"):
                selected.add(command.get("name"))
    for extension in root.findall("./extensions/extension"):
        name = extension.get("name", "")
        supported = set(extension.get("supported", "").split("|"))
        if (name.startswith("GL_ARB_") or name.startswith("GL_EXT_")) and supported.intersection({"gl", "glcore"}):
            selected.update(command.get("name") for command in extension.findall("./require/command") if command.get("name"))
    for item in manifest.get("functions", []):
        selected.add(item["name"])

    def command_for(name):
        command = commands.get(name)
        if command is not None and command.find("proto") is not None:
            return command
        alias = command.get("alias") if command is not None else None
        return commands.get(alias) if alias else command

    expected = {item["name"]: item["argc"] for item in manifest.get("functions", [])}
    rows, parameters, parameter_sizes, signature_ids, signatures = [], [], [], [], []
    signature_map = {}
    actual_slots = {}
    for name in sorted(selected):
        command = command_for(name)
        if command is None or command.find("proto") is None:
            continue
        offset, slots, kinds = len(parameters), 0, []
        for param in command.findall("param"):
            ptype_node, name_node = param.find("ptype"), param.find("name")
            ptype = ptype_node.text if ptype_node is not None and ptype_node.text else ""
            pname = name_node.text if name_node is not None and name_node.text else ""
            text = "".join(param.itertext())
            explicit_pointer = "*" in text
            pointer = explicit_pointer or "*" in type_declarations.get(ptype, "")
            const = "const" in text.split("*")[0]
            wide = ptype in WIDE_TYPES and not pointer
            kind = "Pointer" if pointer else "Float" if ptype in FLOAT_TYPES else "Double" if ptype in WIDE_TYPES and ptype in {"GLdouble", "GLclampd"} else "Int64" if wide else "Scalar" if ptype in type_declarations or ptype in SCALAR_ALIASES else "Unsupported"
            if explicit_pointer and not const:
                kind = "OutputPointer"
            arg_slots = 2 if wide else 1
            slots += arg_slots
            kinds.append(kind)
            length = param.get("len", "")
            if ptype in {"GLbyte", "GLubyte", "GLchar", "GLcharARB", "GLboolean", "GLubyteEXT"}:
                element_size = 1
            elif ptype in {"GLshort", "GLushort", "GLhalf", "GLhalfARB", "GLhalfNV"}:
                element_size = 2
            elif ptype in {"GLdouble", "GLclampd", "GLint64", "GLuint64", "GLint64EXT", "GLuint64EXT"}:
                element_size = 8
            elif ptype in {"GLvoid", "void"} or (pointer and "void" in text):
                element_size = 1
            else:
                element_size = 4
            if pointer and text.count("*") > 1:
                element_size = 4
            parameter_sizes.append(element_size)
            parameters.append(f'    {{"{pname}", GlArgKind::{kind}, {arg_slots}, {json.dumps(length)}}}')
        actual_slots[name] = slots
        rows.append(f'    {{"{name}", {slots}, {offset}, {len(command.findall("param"))}}}')
        proto = command.find("proto")
        return_type_node = proto.find("ptype")
        return_type = return_type_node.text if return_type_node is not None and return_type_node.text else "void"
        return_pointer = "*" in "".join(proto.itertext()) or "*" in type_declarations.get(return_type, "")
        return_kind = "Void" if return_type == "void" else "Pointer" if return_pointer else "Float" if return_type in FLOAT_TYPES else "Double" if return_type in {"GLdouble", "GLclampd"} else "Int64" if return_type in WIDE_TYPES else "Scalar"
        shape = (return_kind, tuple(kinds))
        if "Unsupported" in kinds:
            signature_ids.append(65535)
        else:
            if shape not in signature_map:
                signature_map[shape] = len(signatures)
                signatures.append(shape)
            signature_ids.append(signature_map[shape])
    for name, expected_slots in expected.items():
        actual = actual_slots.get(name)
        if actual is None:
            raise ValueError(f"{name} is absent from selected pinned GL registry commands")
        if actual != expected_slots:
            raise ValueError(f"{name}: manifest arity {expected_slots} does not match registry stack slots {actual}")
    if not rows:
        raise ValueError("pinned gl.xml yielded no GL commands")
    cpp_types = {"Scalar": "uint32_t", "Float": "float", "Double": "double", "Int64": "uint64_t", "Pointer": "void*", "OutputPointer": "void*"}
    adapters = []
    for signature_id, (return_kind, kinds) in enumerate(signatures):
        argument_types = [cpp_types[kind] for kind in kinds]
        return_type = {"Void": "void", "Pointer": "void*", "Float": "float", "Double": "double", "Int64": "uint64_t", "Scalar": "uint32_t"}[return_kind]
        slot = 0
        arguments = []
        for kind in kinds:
            if kind == "Float":
                arguments.append(f"gl_arg_float(a[{slot}])")
            elif kind == "Double":
                arguments.append(f"gl_arg_double(a[{slot}], a[{slot + 1}])")
            elif kind == "Int64":
                arguments.append(f"gl_arg_u64(a[{slot}], a[{slot + 1}])")
            elif kind in {"Pointer", "OutputPointer"}:
                arguments.append(f"reinterpret_cast<void*>(a[{slot}])")
            else:
                arguments.append(f"uint32_t(a[{slot}])")
            slot += 2 if kind in {"Double", "Int64"} else 1
        call = f"fn({', '.join(arguments)})"
        result = f"fn({', '.join(arguments)}); return 0;" if return_kind == "Void" else f"return gl_return_value({call});"
        adapters.append(f"inline uintptr_t gl_call_shape_{signature_id}(uintptr_t p, const uintptr_t* a) {{ (void)a; using Fn = {return_type}(*)({', '.join(argument_types)}); auto fn = reinterpret_cast<Fn>(p); if (!fn) return 0; {result} }}")
    id_rows = ["    " + ", ".join(str(value) for value in signature_ids[i:i + 16]) for i in range(0, len(signature_ids), 16)]
    size_rows = ["    " + ", ".join(str(value) for value in parameter_sizes[i:i + 16]) for i in range(0, len(parameter_sizes), 16)]
    dispatch = [f"        case {i}: return gl_call_shape_{i}(p, a);" for i in range(len(signatures))]
    return """// SPDX-License-Identifier: GPL-3.0-or-later
// Generated by src/tools/gen-x86-gl.py from the pinned Khronos OpenGL registry.
#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>
namespace x86 {
enum class GlArgKind : unsigned char { Scalar, Float, Double, Int64, Pointer, OutputPointer, Unsupported };
struct GlArg { const char* name; GlArgKind kind; unsigned slots; const char* length; };
struct GlExport { const char* name; unsigned argc; unsigned first_arg; unsigned arg_count; };
inline float gl_arg_float(uintptr_t word) { uint32_t bits = uint32_t(word); float value; std::memcpy(&value, &bits, sizeof(value)); return value; }
inline uint64_t gl_arg_u64(uintptr_t lo, uintptr_t hi) { return uint64_t(uint32_t(lo)) | (uint64_t(uint32_t(hi)) << 32); }
inline double gl_arg_double(uintptr_t lo, uintptr_t hi) { uint64_t bits = gl_arg_u64(lo, hi); double value; std::memcpy(&value, &bits, sizeof(value)); return value; }
template<class T> inline uintptr_t gl_return_value(T value) { if constexpr (std::is_pointer_v<T>) return reinterpret_cast<uintptr_t>(value); else if constexpr (std::is_floating_point_v<T>) { uintptr_t bits = 0; std::memcpy(&bits, &value, sizeof(value)); return bits; } else return uintptr_t(value); }
inline constexpr GlArg kGlArgs[] = {
""" + ",\n".join(parameters) + "\n};\ninline constexpr GlExport kGlExports[] = {\n" + ",\n".join(rows) + "\n};\ninline constexpr unsigned short kGlSignatureIds[] = {\n" + ",\n".join(id_rows) + "\n};\ninline constexpr unsigned char kGlArgSizes[] = {\n" + ",\n".join(size_rows) + "\n};\n" + "\n".join(adapters) + "\ninline uintptr_t gl_call_generated(unsigned short signature, uintptr_t p, const uintptr_t* a) { switch (signature) {\n" + "\n".join(dispatch) + "\n        default: return 0; } }\n}\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", help="local gl.xml; omitted to fetch the pinned revision")
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--check", action="store_true", help="fail if output differs instead of writing it")
    args = parser.parse_args()
    try:
        manifest = json.loads(MANIFEST.read_text())
        rendered = generate(registry_bytes(manifest, args.registry), manifest)
        output = pathlib.Path(args.output)
        if args.check:
            if not output.is_file() or output.read_text() != rendered:
                raise ValueError(f"generated table is stale: {output}")
            print("X86-GL GENERATOR PASS pinned registry and table match")
        else:
            output.write_text(rendered)
            print(f"X86-GL GENERATOR PASS wrote {output}")
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"X86-GL GENERATOR FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
