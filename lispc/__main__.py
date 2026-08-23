import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from lispc.reader import parse_string
from lispc.compiler import Compiler, CompileError


def compile_source(source, filename='<string>'):
    ast_list = parse_string(source, filename)
    compiler = Compiler()
    bytecode = compiler.compile(ast_list)
    output = compiler.get_output()
    return output, compiler


def main():
    if len(sys.argv) < 2:
        print(f'Usage: python -m lispc <input.lisp> [output.bin]', file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else input_file.rsplit('.', 1)[0] + '.bin'

    with open(input_file, 'r', encoding='utf-8') as f:
        source = f.read()

    try:
        output, compiler = compile_source(source, input_file)
    except CompileError as e:
        print(f'Error: {e}', file=sys.stderr)
        sys.exit(1)
    except SyntaxError as e:
        print(f'Error: {e}', file=sys.stderr)
        sys.exit(1)

    with open(output_file, 'wb') as f:
        f.write(output)

    print(f'Compiled {input_file} -> {output_file} ({len(output)} bytes)')

    for name, addr in sorted(compiler.functions.items(), key=lambda x: x[1] or 0):
        if addr is not None:
            print(f'  fn {name}: 0x{addr:04x}')

    for name, addr in sorted(compiler.globals.items(), key=lambda x: x[1]):
        print(f'  var {name}: 0x{addr:04x}')


if __name__ == '__main__':
    main()