#!/usr/bin/env python3
"""Parse PythonModules.cpp and PythonAPI.h to generate typed .pyi stub files."""

import re
import argparse
from pathlib import Path


def _strip_cpp_name(name: str) -> str:
    """Convert a C++ struct name (e.g. PyGuiWidget) to its Python stub name."""
    return name[2:] if name.startswith('Py') and len(name) > 2 else name


def build_global_enum_map(cpp_text: str) -> dict:
    """Scan all py::enum_<TYPE>(m, "NAME") declarations and return {C++ type -> Python name}."""
    result = {}
    pattern = re.compile(r'py::enum_<([^>]*)>\s*\(\s*\w+\s*,\s*"([^"]+)"')
    for m in pattern.finditer(cpp_text):
        cpp_type = m.group(1).strip()
        py_name = m.group(2)
        result[cpp_type] = py_name
    return result


def cpp_to_py_type(cpp_type: str, is_param: bool = False, enum_map: dict = None) -> str:
    t = re.sub(r'\bconst\b', '', cpp_type).replace('&', '').strip()

    m = re.match(r'std::optional<(.+)>$', t)
    if m:
        return cpp_to_py_type(m.group(1), enum_map=enum_map) + ' | None'

    m = re.match(r'std::vector<(.+)>$', t)
    if m:
        inner = cpp_to_py_type(m.group(1).strip(), enum_map=enum_map)
        return f'list[{inner}]'

    m = re.match(r'std::map<(.+),\s*(.+)>$', t)
    if m:
        k = cpp_to_py_type(m.group(1).strip(), enum_map=enum_map)
        v = cpp_to_py_type(m.group(2).strip(), enum_map=enum_map)
        return f'dict[{k}, {v}]'

    if t == 'void': return 'Any' if is_param else 'None'
    if t == 'bool': return 'bool'
    if t in ('int', 'int32_t', 'int64_t', 'long long', 'size_t', 'uint32_t', 'uint64_t'): return 'int'
    if t in ('float', 'double'): return 'float'
    if t == 'std::string': return 'str'
    if t == 'py::dict': return 'dict[str, Any]'
    if t == 'py::list': return 'list[Any]'
    if t == 'py::object': return 'Any'

    # Resolve C++ enum/class types using the global enum map.
    # Works for both namespaced ("PythonAPI::Foo") and bare ("Foo") names.
    if enum_map:
        if t in enum_map:
            return enum_map[t]
        last = t.split('::')[-1]
        for k, v in enum_map.items():
            if k.split('::')[-1] == last:
                return v

    # Bare CamelCase name - strip Py prefix for struct types
    if re.match(r'^[A-Z]\w+$', t):
        return _strip_cpp_name(t)
    return 'Any'


def to_py_default(raw):
    raw = raw.strip().rstrip(',').strip()
    if raw in ('false', 'False'): return 'False'
    if raw in ('true', 'True'): return 'True'
    if re.match(r'^-?\d+$', raw): return raw
    if re.match(r'^-?\d+\.\d+$', raw): return raw
    if raw == '""': return '""'
    return '...'


def parse_header(header_text: str, enum_map: dict = None):
    """Return (method_map, struct_map).

    method_map: {method_name: (return_py_type, [(param_name, param_py_type, default_or_None)])}
    struct_map: {StructName: {field_name: py_type}}
    """
    method_map: dict = {}
    struct_map: dict = {}

    # Parse struct/class field types
    struct_pat = re.compile(
        r'(?:struct|class)\s+(\w+)\s*\{([^}]*)\}', re.DOTALL)
    for sm in struct_pat.finditer(header_text):
        sname = sm.group(1)
        body = sm.group(2)
        fields: dict = {}
        for line in body.splitlines():
            line = line.strip().rstrip(';')
            if not line or line.startswith('//') or line in ('public:', 'private:', 'protected:'):
                continue
            # Handle multi-var: "int x = 0, y = 0, width = 0" -> type is first token(s)
            tm = re.match(r'^([\w:<>,\s]+?)\s+(\w+)(?:\s*=\s*[^,;]+)?(?:,(.*))?$', line)
            if not tm:
                continue
            base_type = tm.group(1).strip()
            py_type = cpp_to_py_type(base_type, enum_map=enum_map)
            # First variable
            fields[tm.group(2)] = py_type
            # Additional variables on the same line (multi-decl)
            rest = tm.group(3)
            while rest:
                rest = rest.strip()
                vm = re.match(r'^(\w+)(?:\s*=\s*[^,;]+)?(?:,(.*))?$', rest)
                if not vm:
                    break
                fields[vm.group(1)] = py_type
                rest = vm.group(2)
        if fields:
            struct_map[sname] = fields

    # Parse static method declarations
    meth_pat = re.compile(
        r'static\s+([\w:<>]+(?:\s*\*)?)\s+(\w+)\s*\(([^)]*)\)\s*;')
    for mm in meth_pat.finditer(header_text):
        ret_cpp = mm.group(1).strip()
        mname = mm.group(2)
        params_raw = mm.group(3)

        ret_py = cpp_to_py_type(ret_cpp, enum_map=enum_map)

        params = []
        for part in params_raw.split(','):
            part = part.strip()
            if not part:
                continue
            default = None
            if '=' in part:
                part, dv = part.split('=', 1)
                default = to_py_default(dv.strip())
                part = part.strip()

            # Extract param name (last word)
            tokens = re.split(r'[\s*&]+', part)
            tokens = [t for t in tokens if t]
            if not tokens:
                continue
            param_name = tokens[-1]
            type_tokens = tokens[:-1]
            cpp_type = ' '.join(type_tokens)
            py_type = cpp_to_py_type(cpp_type, is_param=True, enum_map=enum_map) if cpp_type else 'Any'

            params.append((param_name, py_type, default))

        method_map.setdefault(mname, []).append((ret_py, params))

    return method_map, struct_map


def extract_module_blocks(text):
    modules = []
    pattern = re.compile(r'PYBIND11_EMBEDDED_MODULE\s*\(\s*(\w+)\s*,\s*\w+\s*\)\s*\{')
    for m in pattern.finditer(text):
        name = m.group(1)
        start = m.end()
        depth = 1
        i = start
        while i < len(text) and depth > 0:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
            i += 1
        modules.append((name, text[start:i - 1]))
    return modules


def extract_statements(block):
    # Strip line comments so they don't get prepended to the next statement,
    # which would break the re.match patterns used to classify statements.
    block = re.sub(r'//[^\n]*', '', block)

    statements = []
    depth = 0
    current = []
    for c in block:
        current.append(c)
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == ';' and depth == 0:
            stmt = ''.join(current).strip()
            if stmt:
                statements.append(stmt)
            current = []
    return statements


def concat_strings(text):
    def joiner(m):
        return f'"{m.group(1)}{m.group(2)}"'
    prev = None
    while prev != text:
        prev = text
        text = re.sub(r'"((?:[^"\\]|\\.)*)"\s*"((?:[^"\\]|\\.)*)"', joiner, text)
    return text


def extract_strings(text):
    text = concat_strings(text)
    return [m.group(1) for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', text)]


def parse_args(stmt):
    args = []
    for m in re.finditer(r'py::arg\s*\(\s*"([^"]+)"\s*\)\s*(?:=\s*([^,);]+))?', stmt):
        name = m.group(1)
        raw = m.group(2)
        if raw is None:
            args.append((name, None))
        else:
            args.append((name, to_py_default(raw.strip())))
    return args


def extract_cpp_method(stmt):
    """Extract method name from &PythonAPI::methodName in m.def(...)."""
    m = re.search(r'&\w+::(\w+)', stmt)
    return m.group(1) if m else None


def parse_function(stmt, method_map):
    m = re.match(r'\s*m\.def\s*\(\s*"([^"]+)"', stmt)
    if not m:
        return None
    name = m.group(1)

    strings = extract_strings(stmt)
    doc = strings[1] if len(strings) >= 2 else ''

    args = parse_args(stmt)
    cpp_method = extract_cpp_method(stmt)

    ret_type = 'Any'
    typed_params = None
    if cpp_method and cpp_method in method_map:
        overloads = method_map[cpp_method]
        # Pick the overload whose param count best matches the py::arg count
        best = min(overloads, key=lambda o: abs(len(o[1]) - len(args)))
        ret_type, header_params = best
        if args and header_params:
            typed_params = []
            for i, (py_name, py_default) in enumerate(args):
                if i < len(header_params):
                    _, h_type, _ = header_params[i]
                else:
                    h_type = 'Any'
                typed_params.append((py_name, h_type, py_default))

    return {
        'name': name,
        'doc': doc,
        'args': args,
        'ret_type': ret_type,
        'typed_params': typed_params,
    }


def extract_class_field_ref(stmt):
    """Extract (StructName, field_name) pairs from .def_readonly(...)."""
    pairs = []
    for m in re.finditer(r'\.def_readonly\s*\(\s*"([^"]+)"\s*,\s*&(\w+)::(\w+)', stmt):
        pairs.append((m.group(1), m.group(2), m.group(3)))
    return pairs


def parse_class(stmt, struct_map):
    m = re.search(r'py::class_<(\w+)>', stmt)
    if not m:
        return None
    cpp_class_name = m.group(1)

    nm = re.search(r'py::class_<[^>]*>\s*\(\s*\w+\s*,\s*"([^"]+)"', stmt)
    if not nm:
        return None
    py_name = nm.group(1)

    field_refs = extract_class_field_ref(stmt)
    cpp_fields = struct_map.get(cpp_class_name, {})

    attrs = []
    for (py_attr, _, cpp_field) in field_refs:
        py_type = cpp_fields.get(cpp_field, 'Any')
        attrs.append((py_attr, py_type))

    return {'name': py_name, 'attrs': attrs}


def parse_enum(stmt):
    m = re.search(r'py::enum_<([^>]*)>\s*\(\s*\w+\s*,\s*"([^"]+)"', stmt)
    if not m:
        return None
    cpp_type = m.group(1).strip()
    name = m.group(2)
    values = [vm.group(1) for vm in re.finditer(r'\.value\s*\(\s*"([^"]+)"', stmt)]
    return {'name': name, 'cpp_type': cpp_type, 'values': values}


def format_pyi(doc, classes, enums, functions):
    lines = ['from __future__ import annotations']
    lines.append('from typing import Any, ClassVar, overload')
    lines.append('')
    if doc:
        lines.append(f'"""{doc}"""')
        lines.append('')

    for enum in enums:
        lines.append(f'class {enum["name"]}(int):')
        for v in enum['values']:
            lines.append(f'    {v}: ClassVar[int] = ...')
        lines.append('')

    for cls in classes:
        lines.append(f'class {cls["name"]}:')
        if cls['attrs']:
            for (attr_name, attr_type) in cls['attrs']:
                lines.append(f'    {attr_name}: {attr_type}')
        else:
            lines.append('    ...')
        lines.append('')

    by_name: dict = {}
    for func in functions:
        by_name.setdefault(func['name'], []).append(func)

    for _, overloads in by_name.items():
        if len(overloads) > 1:
            for ol in overloads:
                lines.append('@overload')
                lines.append(format_func(ol))
                lines.append('')
        else:
            lines.append(format_func(overloads[0]))
            lines.append('')

    return '\n'.join(lines)


def format_func(func):
    name = func['name']
    doc = func['doc'].replace('\\n', ' ').replace('\\"', '"')
    ret = func.get('ret_type', 'Any')
    typed_params = func.get('typed_params')

    params = []
    if typed_params:
        for (py_name, py_type, default) in typed_params:
            if default is not None:
                params.append(f'{py_name}: {py_type} = {default}')
            else:
                params.append(f'{py_name}: {py_type}')
    else:
        for (arg_name, default) in func['args']:
            if default is not None:
                params.append(f'{arg_name}: Any = {default}')
            else:
                params.append(f'{arg_name}: Any')

    sig = f'def {name}({", ".join(params)}) -> {ret}: ...'
    if doc:
        sig = f'def {name}({", ".join(params)}) -> {ret}:\n    """{doc}"""'
    return sig


def main():
    parser = argparse.ArgumentParser(description='Generate .pyi stubs from PythonModules.cpp')
    parser.add_argument('input', help='Path to PythonModules.cpp')
    parser.add_argument('--header', '-H', default='', help='Path to PythonAPI.h for type info')
    parser.add_argument('--output', '-o', default='.', help='Output directory for .pyi files')
    args = parser.parse_args()

    text = Path(args.input).read_text()
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    # Build the global enum map before parsing the header so type resolution works.
    enum_map = build_global_enum_map(text)

    method_map: dict = {}
    struct_map: dict = {}
    if args.header:
        header_text = Path(args.header).read_text()
        method_map, struct_map = parse_header(header_text, enum_map=enum_map)

    modules = extract_module_blocks(text)
    if not modules:
        print('No PYBIND11_EMBEDDED_MODULE blocks found', flush=True)
        return

    for module_name, block in modules:
        statements = extract_statements(block)
        doc = ''
        classes = []
        enums = []
        functions = []

        for stmt in statements:
            s = stmt.strip()
            if re.match(r'm\.doc\s*\(\s*\)', s):
                strings = extract_strings(s)
                if strings:
                    doc = strings[0]
            elif re.match(r'm\.def\s*\(', s):
                f = parse_function(s, method_map)
                if f:
                    functions.append(f)
            elif re.match(r'py::class_<', s):
                c = parse_class(s, struct_map)
                if c:
                    classes.append(c)
            elif re.match(r'py::enum_<', s):
                e = parse_enum(s)
                if e:
                    enums.append(e)

        content = format_pyi(doc, classes, enums, functions)
        out_path = out_dir / f'{module_name}.pyi'
        out_path.write_text(content)
        print(f'  {out_path}')

    print(f'Generated {len(modules)} stub files in {out_dir}')


if __name__ == '__main__':
    main()
