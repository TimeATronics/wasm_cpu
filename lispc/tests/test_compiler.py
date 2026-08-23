import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from lispc.reader import tokenize, parse, parse_string
from lispc.compiler import Compiler, CompileError


def compile_source(src):
    ast = parse_string(src)
    c = Compiler()
    bytecode = c.compile(ast)
    return bytes(bytecode), c


def test_tokenizer():
    tokens = tokenize('(+ 1 2)')
    assert len(tokens) == 5
    assert tokens[0].value == '('
    assert tokens[1].value == '+'
    assert tokens[2].value == 1
    assert tokens[3].value == 2
    assert tokens[4].value == ')'

    tokens = tokenize('(define x 42)')
    assert tokens[1].value == 'define'
    assert tokens[2].value == 'x'
    assert tokens[3].value == 42

    tokens = tokenize('; comment\n(+ 1 2)\n')
    assert len(tokens) == 5
    assert tokens[1].value == '+'


def test_parser():
    ast = parse_string('(+ 1 2)')
    assert len(ast) == 1
    assert ast[0].items[0].value == '+'
    assert ast[0].items[1].value == 1
    assert ast[0].items[2].value == 2

    ast = parse_string('(if (< n 2) 1 (* n (fact (- n 1))))')
    assert len(ast) == 1
    assert ast[0].items[0].value == 'if'

    ast = parse_string('(let ((x 5)) (+ x 1))')
    assert ast[0].items[0].value == 'let'
    assert ast[0].items[1].items[0].items[0].value == 'x'


def test_nested_lists():
    ast = parse_string('(a (b c) d)')
    assert len(ast) == 1
    assert len(ast[0].items) == 3
    assert ast[0].items[1].items[0].value == 'b'
    assert ast[0].items[1].items[1].value == 'c'
    assert ast[0].items[2].value == 'd'


def test_simple_define():
    bc, c = compile_source('(define x 42)')
    assert len(bc) > 0
    assert 'x' in c.globals


def test_simple_arith():
    bc, c = compile_source('(+ 3 4)')
    assert len(bc) > 0
    # push3, push4, add, drop, halt
    assert bc[0] == 0x01  # push
    assert bc[5] == 0x01  # push
    assert bc[10] == 0x02  # add


def test_fact():
    src = '''
    (defun fact (n)
      (if (< n 2) 1
        (* n (fact (- n 1)))))
    (print (fact 5))
    '''
    bc, c = compile_source(src)
    assert 'fact' in c.functions
    assert len(bc) > 0


def test_fib():
    src = '''
    (defun fib (n)
      (if (< n 2) n
        (+ (fib (- n 1)) (fib (- n 2)))))
    (print (fib 10))
    '''
    bc, c = compile_source(src)
    assert 'fib' in c.functions


def test_multiple_functions():
    src = '''
    (defun double (x) (* x 2))
    (defun triple (x) (* x 3))
    (print (double 5))
    (print (triple 3))
    '''
    bc, c = compile_source(src)
    assert 'double' in c.functions
    assert 'triple' in c.functions


def test_define_and_use():
    src = '''
    (define x 42)
    (define y 100)
    '''
    bc, c = compile_source(src)
    assert 'x' in c.globals
    assert 'y' in c.globals


def test_let():
    src = '(let ((x 5)) x)'
    bc, c = compile_source(src)
    assert len(bc) > 0


def test_while():
    src = '''
    (define i 0)
    (while (< i 10)
      (print i)
      (set! i (+ i 1)))
    '''
    bc, c = compile_source(src)
    assert 'i' in c.globals


def test_comparison():
    bc, c = compile_source('(< 3 4)')
    assert bc[10] == 0x0a  # lt_s
    bc2, c2 = compile_source('(= 3 4)')
    assert bc2[10] == 0x09  # eq
    bc3, c3 = compile_source('(> 3 4)')
    assert bc3[10] == 0x0b  # gt_s


def test_nested_arith():
    bc, c = compile_source('(* (+ 1 2) 3)')
    assert len(bc) > 0


def test_comment():
    src = '(+ 1 2) ; this is a comment\n(+ 3 4)'
    bc, c = compile_source(src)
    assert len(bc) > 0


def test_self_recursion():
    src = '''
    (defun fact (n)
      (if (< n 2) 1
        (* n (fact (- n 1)))))
    (fact 5)
    '''
    bc, c = compile_source(src)
    assert 'fact' in c.functions

    # Fact body should have save/restore ops
    fact_start = c.functions['fact']
    fact_body = bc[fact_start:]
    # Should have TO_R (0x30) and FROM_R (0x31) for save/restore
    # Since fact has 1 local, there should be at least 1 TO_R and 1 FROM_R
    assert 0x30 in fact_body  # TO_R
    assert 0x31 in fact_body  # FROM_R


def test_string_literal():
    src = '(print "hello")'
    bc, c = compile_source(src)
    assert len(bc) > 0


def test_boolean_literal():
    bc, c = compile_source('#t')
    assert len(bc) > 0
    bc2, c2 = compile_source('#f')
    assert len(bc2) > 0


def test_nil_literal():
    bc, c = compile_source('()')
    assert len(bc) > 0


def test_undefined_variable():
    try:
        compile_source('undefined_var')
        assert False, 'expected CompileError'
    except CompileError:
        pass


def test_binary_emission():
    src = '''
    (defun identity (x) x)
    (print (identity 42))
    '''
    bc, c = compile_source(src)
    assert 'identity' in c.functions
    assert len(bc) > 0


if __name__ == '__main__':
    test_tokenizer()
    test_parser()
    test_nested_lists()
    test_simple_define()
    test_simple_arith()
    test_fact()
    test_fib()
    test_multiple_functions()
    test_define_and_use()
    test_let()
    test_while()
    test_comparison()
    test_nested_arith()
    test_comment()
    test_self_recursion()
    test_string_literal()
    test_boolean_literal()
    test_nil_literal()
    test_undefined_variable()
    test_binary_emission()
    print('All tests passed!')