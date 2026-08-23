from .opcodes import (
    OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DROP, OP_PRINT, OP_EQ,
    OP_LT_S, OP_GT_S, OP_BR_IF, OP_JUMP, OP_CALL, OP_RETURN,
    OP_AND, OP_OR, OP_XOR, OP_NOT, OP_SHL, OP_SHR_S,
    OP_LOAD, OP_STORE, OP_LOAD8_U, OP_STORE8,
    OP_LOCAL_GET, OP_LOCAL_SET, OP_GET_FP, OP_HALT, OP_EQZ,
    OP_DUP, OP_SWAP, OP_DIV_S, OP_TO_R, OP_FROM_R, OP_CALL_IND,
    OP_OVER,
)
from .reader import AST, Atom, List, Quote, StrAtom


class CompileError(Exception):
    pass


class Compiler:
    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.pending_patches = []
        self.functions = {}
        self.locals = {}
        self.next_local = 0
        self.next_global = 0x200
        self.globals = {}
        self.globals_init = {}
        self.string_table = {}
        self.strings_data = bytearray()
        self.label_counter = 0
        self.current_loop_end = None
        self.in_function = False
        self.func_param_count = 0

    def emit(self, opcode):
        self.code.append(opcode)

    def emit32(self, val):
        v = int(val)
        # Convert to signed 32-bit for to_bytes
        if v >= 0x80000000:
            v -= 0x100000000
        self.code.extend(v.to_bytes(4, 'little', signed=True))

    def emit8(self, val):
        self.code.append(int(val) & 0xFF)

    def new_label(self, prefix='.L'):
        self.label_counter += 1
        return f'{prefix}{self.label_counter}'

    def define_label(self, name):
        self.labels[name] = len(self.code)

    def emit_jump(self, opcode, label_name):
        self.emit(opcode)
        self.pending_patches.append((label_name, len(self.code)))
        self.emit32(0)

    def patch_all(self):
        for label_name, offset in self.pending_patches:
            if label_name not in self.labels:
                raise CompileError(f'undefined label: {label_name}')
            addr = self.labels[label_name]
            self.code[offset:offset+4] = int(addr).to_bytes(4, 'little', signed=True)
        self.pending_patches.clear()

    def add_string(self, s):
        if s in self.string_table:
            return self.string_table[s]
        addr = 0x400 + len(self.strings_data)
        self.string_table[s] = addr
        self.strings_data.extend(s.encode('utf-8'))
        self.strings_data.append(0)
        return addr

    def alloc_local(self):
        slot = self.next_local
        self.next_local += 1
        if self.next_local > 63:
            raise CompileError('too many local variables (max 64)')
        return slot

    def compile(self, ast_list):
        self._scan_defuns(ast_list)
        funcs = []
        exprs = []
        for ast in ast_list:
            if isinstance(ast, List) and len(ast.items) >= 2:
                if isinstance(ast.items[0], Atom) and ast.items[0].value == 'defun':
                    funcs.append(ast)
                    continue
            exprs.append(ast)
        for ast in exprs:
            self._compile_top(ast)
        self.emit(OP_HALT)
        for f in funcs:
            self._compile_defun(f.items[1], f.items[2],
                                f.items[3:] if len(f.items) > 3 else [])
        self.patch_all()
        return bytes(self.code)

    def _scan_defuns(self, ast_list):
        for ast in ast_list:
            if isinstance(ast, List) and len(ast.items) >= 2:
                if isinstance(ast.items[0], Atom) and ast.items[0].value == 'defun':
                    name = ast.items[1].value
                    if name not in self.functions:
                        self.functions[name] = None

    def _compile_top(self, ast):
        if isinstance(ast, List) and len(ast.items) >= 2:
            if isinstance(ast.items[0], Atom):
                op = ast.items[0].value
                if op == 'defun':
                    return self._compile_defun(ast.items[1], ast.items[2],
                                               ast.items[3:] if len(ast.items) > 3 else [])
                elif op == 'define':
                    return self._compile_define(ast.items[1], ast.items[2])
                elif op == 'progn' or op == 'begin':
                    for e in ast.items[1:-1]:
                        self._compile_expr(e)
                        self.emit(OP_DROP)
                    if len(ast.items) > 1:
                        self._compile_expr(ast.items[-1])
                        self.emit(OP_DROP)
                    return
        self._compile_expr(ast)
        self.emit(OP_DROP)

    def _compile_defun(self, name_ast, params_ast, body_exprs):
        name = name_ast.value
        self.functions[name] = len(self.code)
        self.labels[name] = len(self.code)

        saved_locals = self.locals.copy()
        saved_next_local = self.next_local

        param_count = len(params_ast.items)
        param_slots = []
        for param in params_ast.items:
            slot = self.alloc_local()
            param_slots.append(slot)
            self.locals[param.value] = slot

        for slot in param_slots:
            self._emit_local_set_i(slot)

        self.in_function = True
        self.func_param_count = len(param_slots)

        for expr in body_exprs[:-1]:
            self._compile_expr(expr)
            self.emit(OP_DROP)
        if body_exprs:
            self._compile_expr(body_exprs[-1])
        else:
            self.emit(OP_PUSH)
            self.emit32(0)

        self.emit(OP_RETURN)
        self.in_function = False

        self.locals = saved_locals
        self.next_local = saved_next_local

    def _compile_define(self, name_ast, val_ast):
        name = name_ast.value
        if name not in self.globals:
            self.globals[name] = self.next_global
            self.next_global += 1
        addr = self.globals[name]
        self._compile_expr(val_ast)
        self.emit(OP_PUSH)
        self.emit32(addr)
        self.emit(OP_STORE)
        self.emit(OP_DROP)

    def _compile_fn_star(self, params_ast, body_exprs):
        end_label = self.new_label('.lend')

        # Push placeholder for function address
        self.emit(OP_PUSH)
        push_pos = len(self.code)
        self.emit32(0xCAFE)  # placeholder, patched below

        # Jump over function body
        self.emit_jump(OP_JUMP, end_label)

        # Patch the placeholder to current position (start of body)
        fn_addr = len(self.code)
        patched = (fn_addr & 0xFFFFFFFF).to_bytes(4, 'little', signed=False)
        self.code[push_pos:push_pos+4] = patched

        # Fresh local scope for the function
        saved_locals = self.locals.copy()
        saved_next = self.next_local
        self.locals = {}
        self.next_local = 0

        # Bind parameters from data stack
        for param in params_ast.items:
            slot = self.alloc_local()
            self.locals[param.value] = slot
            self._emit_local_set_i(slot)

        for expr in body_exprs[:-1]:
            self._compile_expr(expr)
            self.emit(OP_DROP)
        if body_exprs:
            self._compile_expr(body_exprs[-1])
        else:
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFF)  # nil

        self.emit(OP_RETURN)

        self.locals = saved_locals
        self.next_local = saved_next

        self.define_label(end_label)

    def _compile_expr(self, ast):
        if isinstance(ast, Atom):
            self._compile_atom(ast)
        elif isinstance(ast, StrAtom):
            addr = self.add_string(ast.value)
            self.emit(OP_PUSH)
            self.emit32(addr)
        elif isinstance(ast, Quote):
            self.emit(OP_PUSH)
            self.emit32(0)
        elif isinstance(ast, List):
            self._compile_list(ast)
        else:
            raise CompileError(f'unknown AST node: {ast}')

    def _compile_atom(self, ast):
        val = ast.value
        if isinstance(val, int):
            self.emit(OP_PUSH)
            self.emit32(val)
        elif isinstance(val, bool):
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFE if val else 0xFFFFFFFC)
        elif val in self.locals:
            self._emit_local_get_i(self.locals[val])
        elif val in self.functions:
            pass
        elif val in self.globals:
            addr = self.globals[val]
            self.emit(OP_PUSH)
            self.emit32(addr)
            self.emit(OP_LOAD)
        elif val == 'nil':
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFF)
        elif val == 't' or val == 'true':
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFE)
        elif val == 'f' or val == 'false':
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFC)
        else:
            raise CompileError(f'undefined symbol: {val}')

    def _compile_list(self, ast):
        if not ast.items:
            self.emit(OP_PUSH)
            self.emit32(0)
            return

        head = ast.items[0]
        if not isinstance(head, Atom):
            self._compile_call(ast)
            return

        op = head.value

        if op in ('+', '-', '*', '/', '%'):
            self._compile_arithmetic(op, ast.items[1:])
        elif op in ('=', '<', '>', '<=', '>='):
            self._compile_comparison(op, ast.items[1:])
        elif op == 'zero?':
            self._compile_expr(ast.items[1])
            self.emit(OP_EQZ)
        elif op == 'null?':
            self._compile_expr(ast.items[1])
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFF)
            self.emit(OP_EQ)
        elif op == 'if':
            self._compile_if(ast.items[1], ast.items[2],
                             ast.items[3] if len(ast.items) > 3 else None)
        elif op in ('let', 'let*'):
            self._compile_let(ast.items[1], ast.items[2:])
        elif op in ('while',):
            self._compile_while(ast.items[1], ast.items[2:])
        elif op in ('progn', 'begin', 'do'):
            self._compile_progn(ast.items[1:])
        elif op == 'set!':
            self._compile_set(ast.items[1], ast.items[2])
        elif op in ('define', 'def!'):
            self._compile_define(ast.items[1], ast.items[2])
        elif op == 'fn*':
            self._compile_fn_star(ast.items[1], ast.items[2:])
        elif op == 'defun':
            self._compile_defun(ast.items[1], ast.items[2],
                                ast.items[3:] if len(ast.items) > 3 else [])
        elif op == 'print':
            self._compile_expr(ast.items[1])
            self.emit(OP_PRINT)
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFF)  # nil
        elif op == 'print-char':
            self._compile_expr(ast.items[1])
            self.emit(OP_PRINT)
        elif op == 'read-char':
            self.emit(0x1F)
        elif op == 'peek':
            self._compile_expr(ast.items[1])
            self.emit(OP_LOAD)
        elif op == 'poke':
            self._compile_expr(ast.items[2])  # value
            self._compile_expr(ast.items[1])  # address
            self.emit(OP_STORE)
        elif op == 'peekb':
            self._compile_expr(ast.items[1])
            self.emit(OP_LOAD8_U)
        elif op == 'pokeb':
            self._compile_expr(ast.items[2])  # value
            self._compile_expr(ast.items[1])  # address
            self.emit(OP_STORE8)
        elif op == 'logand':
            self._compile_binary_bitwise(OP_AND, ast.items[1:])
        elif op == 'logor':
            self._compile_binary_bitwise(OP_OR, ast.items[1:])
        elif op == 'logxor':
            self._compile_binary_bitwise(OP_XOR, ast.items[1:])
        elif op == 'lognot':
            self._compile_expr(ast.items[1])
            self.emit(OP_NOT)
        elif op == '<<':
            self._compile_binary_bitwise(OP_SHL, ast.items[1:])
        elif op == '>>':
            self._compile_binary_bitwise(OP_SHR_S, ast.items[1:])
        elif op == 'cons':
            self._compile_expr(ast.items[1])
            self._compile_expr(ast.items[2])
            self.emit(OP_SWAP)
            self.emit(OP_STORE)
            self._compile_expr(ast.items[1])
        elif op == 'car':
            self._compile_expr(ast.items[1])
            self.emit(OP_LOAD)
        elif op == 'cdr':
            self._compile_expr(ast.items[1])
            self.emit(OP_PUSH)
            self.emit32(4)
            self.emit(OP_ADD)
            self.emit(OP_LOAD)
        elif op == 'and':
            # (and a b c) → (if a (if b c b) a)
            vals = ast.items[1:]
            if not vals:
                self.emit(OP_PUSH)
                self.emit32(0xFFFFFFFE)  # true
                return
            result = vals[-1]
            for i in range(len(vals) - 2, -1, -1):
                result = List([Atom('if'), vals[i], result, vals[i]])
            self._compile_expr(result)
        elif op == 'or':
            # (or a b c) → (if a a (if b b c))
            vals = ast.items[1:]
            if not vals:
                self.emit(OP_PUSH)
                self.emit32(0xFFFFFFFF)  # nil
                return
            result = vals[-1]
            for i in range(len(vals) - 2, -1, -1):
                result = List([Atom('if'), vals[i], vals[i], result])
            self._compile_expr(result)
        elif op == 'not':
            # not: return true if arg is false/nil, false otherwise
            # Check if false
            self._compile_expr(ast.items[1])
            self.emit(OP_DUP)
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFC)
            self.emit(OP_EQ)
            is_falsy = self.new_label('.notf')
            end = self.new_label('.notend')
            self.emit_jump(OP_BR_IF, is_falsy)
            # Check if nil
            self.emit(OP_DUP)
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFF)
            self.emit(OP_EQ)
            self.emit_jump(OP_BR_IF, is_falsy)
            # Truthy → not(falsy) = true → return false
            self.emit(OP_DROP)
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFC)  # false
            self.emit_jump(OP_JUMP, end)
            self.define_label(is_falsy)
            self.emit(OP_DROP)
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFE)  # true
            self.define_label(end)
        elif op in self.functions:
            self._compile_call(ast)
        elif op in self.globals:
            self._compile_call(ast)
        else:
            self._compile_call(ast)

    def _compile_arithmetic(self, op, args):
        if len(args) < 2:
            raise CompileError(f'{op} requires at least 2 arguments')
        self._compile_expr(args[0])
        self._compile_expr(args[1])
        self._emit_binop(op)
        for i in range(2, len(args)):
            self._compile_expr(args[i])
            self._emit_binop(op)

    def _emit_binop(self, op):
        if op == '+':
            self.emit(OP_ADD)
        elif op == '-':
            self.emit(OP_SUB)
        elif op == '*':
            self.emit(OP_MUL)
        elif op == '/':
            self.emit(OP_DIV_S)
        elif op == '%':
            # a % b = a - (a/b)*b  (stack: a b)
            # dup    → a b b
            # >r     → a b       (save b)
            # over   → a b a
            # swap   → a a b
            # div_s  → a (a/b)
            # r>     → a (a/b) b
            # mul    → a (a/b*b)
            # swap   → (a/b*b) a
            # sub    → a - (a/b*b)
            self.emit(OP_DUP)
            self.emit(OP_TO_R)
            self.emit(OP_OVER)
            self.emit(OP_SWAP)
            self.emit(OP_DIV_S)
            self.emit(OP_FROM_R)
            self.emit(OP_MUL)
            self.emit(OP_SUB)

    def _emit_conj_boolean(self):
        """Convert a 0/1 comparison result to false/true boolean values."""
        self.emit(OP_DUP)
        self.emit(OP_ADD)    # * 2: 0 or 2
        self.emit(OP_PUSH)
        self.emit32(0xFFFFFFFC)  # false
        self.emit(OP_ADD)    # 0xFFFFFFFC (false) or 0xFFFFFFFE (true)

    def _emit_is_falsy(self):
        """Leave 1 on stack if TOS is falsy (false or nil), 0 if truthy.

        In mal: only false and nil are falsy; everything else (0, '') is truthy.
        """
        is_falsy = self.new_label('.isfalsy')
        end = self.new_label('.fend')

        # Check if false
        self.emit(OP_DUP)
        self.emit(OP_PUSH)
        self.emit32(0xFFFFFFFC)
        self.emit(OP_EQ)
        self.emit_jump(OP_BR_IF, is_falsy)

        # Check if nil
        self.emit(OP_DUP)
        self.emit(OP_PUSH)
        self.emit32(0xFFFFFFFF)
        self.emit(OP_EQ)
        self.emit_jump(OP_BR_IF, is_falsy)

        # Truthy - drop original, push 0
        self.emit(OP_DROP)
        self.emit(OP_PUSH)
        self.emit32(0)
        self.emit_jump(OP_JUMP, end)

        # Falsy
        self.define_label(is_falsy)
        self.emit(OP_DROP)
        self.emit(OP_PUSH)
        self.emit32(1)

        self.define_label(end)

    def _compile_comparison(self, op, args):
        if len(args) != 2:
            raise CompileError(f'{op} requires exactly 2 arguments')
        self._compile_expr(args[0])
        self._compile_expr(args[1])
        if op == '=':
            self.emit(OP_EQ)
        elif op == '<':
            self.emit(OP_LT_S)
        elif op == '>':
            self.emit(OP_GT_S)
        elif op == '<=':
            self.emit(OP_GT_S)
            self.emit(OP_EQZ)
        elif op == '>=':
            self.emit(OP_LT_S)
            self.emit(OP_EQZ)
        self._emit_conj_boolean()

    def _compile_binary_bitwise(self, opcode, args):
        if len(args) != 2:
            raise CompileError('bitwise op requires exactly 2 arguments')
        self._compile_expr(args[0])
        self._compile_expr(args[1])
        self.emit(opcode)

    def _compile_if(self, cond_ast, then_ast, else_ast):
        else_label = self.new_label('.else')
        end_label = self.new_label('.endif')

        self._compile_expr(cond_ast)

        # Check if false
        self.emit(OP_DUP)
        self.emit(OP_PUSH)
        self.emit32(0xFFFFFFFC)
        self.emit(OP_EQ)
        self.emit_jump(OP_BR_IF, else_label)

        # Check if nil
        self.emit(OP_DUP)
        self.emit(OP_PUSH)
        self.emit32(0xFFFFFFFF)
        self.emit(OP_EQ)
        self.emit_jump(OP_BR_IF, else_label)

        # Truthy - drop original, compile then
        self.emit(OP_DROP)
        self._compile_expr(then_ast)
        self.emit_jump(OP_JUMP, end_label)

        self.define_label(else_label)
        self.emit(OP_DROP)  # drop original pushed via dup
        if else_ast:
            self._compile_expr(else_ast)
        else:
            self.emit(OP_PUSH)
            self.emit32(0xFFFFFFFF)  # nil
        self.define_label(end_label)

    def _compile_let(self, bindings_ast, body_exprs):
        saved = self.locals.copy()
        saved_next = self.next_local

        slots = []
        if isinstance(bindings_ast, List):
            # Support both flat format (x 9 y 10) and nested ((x 9) (y 10))
            items = bindings_ast.items
            if items and isinstance(items[0], List) and len(items[0].items) == 2:
                # Nested format: ((x 9) (y 10))
                for binding in items:
                    if isinstance(binding, List) and len(binding.items) == 2:
                        name = binding.items[0].value
                        self._compile_expr(binding.items[1])
                        slot = self.alloc_local()
                        self.locals[name] = slot
                        self._emit_local_set_i(slot)
                        slots.append(slot)
                    else:
                        raise CompileError(f'bad let binding: {binding}')
            else:
                # Flat format: (x 9 y 10)
                if len(items) % 2 != 0:
                    raise CompileError('odd number of let bindings')
                for i in range(0, len(items), 2):
                    name = items[i].value
                    self._compile_expr(items[i + 1])
                    slot = self.alloc_local()
                    self.locals[name] = slot
                    self._emit_local_set_i(slot)
                    slots.append(slot)

        for expr in body_exprs[:-1]:
            self._compile_expr(expr)
            if isinstance(expr, List) and len(expr.items) >= 2:
                if isinstance(expr.items[0], Atom) and expr.items[0].value in ('define', 'def!'):
                    continue
            self.emit(OP_DROP)
        if body_exprs:
            self._compile_expr(body_exprs[-1])
        else:
            self.emit(OP_PUSH)
            self.emit32(0)

        self.locals = saved
        self.next_local = saved_next

    def _compile_while(self, cond_ast, body_exprs):
        loop_start = len(self.code)
        self._compile_expr(cond_ast)
        end_label = self.new_label('.whileend')

        # Check if condition is falsy (false or nil)
        self.emit(OP_DUP)
        self.emit(OP_PUSH)
        self.emit32(0xFFFFFFFC)  # false
        self.emit(OP_EQ)
        self.emit_jump(OP_BR_IF, end_label)
        self.emit(OP_DUP)
        self.emit(OP_PUSH)
        self.emit32(0xFFFFFFFF)  # nil
        self.emit(OP_EQ)
        self.emit_jump(OP_BR_IF, end_label)
        self.emit(OP_DROP)

        for expr in body_exprs:
            self._compile_expr(expr)
            if isinstance(expr, List) and len(expr.items) >= 2:
                if isinstance(expr.items[0], Atom) and expr.items[0].value in ('define', 'def!'):
                    continue
            self.emit(OP_DROP)
        self.emit(OP_JUMP)
        self.emit32(loop_start)
        self.define_label(end_label)
        self.emit(OP_DROP)
        self.emit(OP_PUSH)
        self.emit32(0)

    def _compile_progn(self, exprs):
        for i, expr in enumerate(exprs):
            if isinstance(expr, List) and len(expr.items) >= 2:
                if isinstance(expr.items[0], Atom) and expr.items[0].value in ('define', 'def!'):
                    self._compile_define(expr.items[1], expr.items[2])
                    continue
            self._compile_expr(expr)
            if i < len(exprs) - 1:
                self.emit(OP_DROP)

    def _compile_body(self, exprs):
        for expr in exprs[:-1]:
            self._compile_expr(expr)
            self.emit(OP_DROP)
        if exprs:
            self._compile_expr(exprs[-1])
        else:
            self.emit(OP_PUSH)
            self.emit32(0)

    def _compile_set(self, name_ast, val_ast):
        name = name_ast.value
        if name in self.locals:
            self._compile_expr(val_ast)
            self.emit(OP_DUP)
            self._emit_local_set_i(self.locals[name])
        elif name in self.globals:
            self._compile_expr(val_ast)
            addr = self.globals[name]
            self.emit(OP_PUSH)
            self.emit32(addr)
            self.emit(OP_STORE)
            self.emit(OP_DUP)  # leave value on stack
        else:
            raise CompileError(f'undefined variable for set!: {name}')

    def _compile_call(self, ast):
        head = ast.items[0]
        func_name = head.value if isinstance(head, Atom) else None
        is_indirect = not isinstance(head, Atom)

        # Push arguments in reverse
        for arg in reversed(ast.items[1:]):
            self._compile_expr(arg)

        if func_name and func_name in self.functions:
            live_slots = self.next_local
            for s in range(live_slots):
                self._emit_local_get_i(s)
                self.emit(OP_TO_R)
            self.emit_jump(OP_CALL, func_name)
            for s in range(live_slots - 1, -1, -1):
                self.emit(OP_FROM_R)
                self._emit_local_set_i(s)
        elif is_indirect:
            live_slots = self.next_local
            for s in range(live_slots):
                self._emit_local_get_i(s)
                self.emit(OP_TO_R)
            self._compile_expr(head)
            self.emit(OP_CALL_IND)
            for s in range(live_slots - 1, -1, -1):
                self.emit(OP_FROM_R)
                self._emit_local_set_i(s)
        elif func_name and func_name in self.locals:
            live_slots = self.next_local
            for s in range(live_slots):
                self._emit_local_get_i(s)
                self.emit(OP_TO_R)
            self._compile_expr(head)
            self.emit(OP_CALL_IND)
            for s in range(live_slots - 1, -1, -1):
                self.emit(OP_FROM_R)
                self._emit_local_set_i(s)
        elif func_name and func_name in self.globals:
            live_slots = self.next_local
            for s in range(live_slots):
                self._emit_local_get_i(s)
                self.emit(OP_TO_R)
            self._compile_expr(head)
            self.emit(OP_CALL_IND)
            for s in range(live_slots - 1, -1, -1):
                self.emit(OP_FROM_R)
                self._emit_local_set_i(s)
        elif func_name:
            raise CompileError(f'unknown function: {func_name}')
        else:
            raise CompileError(f'invalid function call: {head}')

    def _emit_local_get_i(self, slot):
        self.emit(OP_LOCAL_GET)
        self.emit8(slot)

    def _emit_local_set_i(self, slot):
        self.emit(OP_LOCAL_SET)
        self.emit8(slot)

    def emit_local_set(self, slot):
        self._emit_local_set_i(slot)

    def get_output(self):
        return bytes(self.code)