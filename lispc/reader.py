import re
from enum import Enum, auto


class TokenType(Enum):
    INTEGER = auto()
    HEX = auto()
    STRING = auto()
    SYMBOL = auto()
    LPAREN = auto()
    RPAREN = auto()
    QUOTE = auto()
    BOOLEAN = auto()


class Token:
    __slots__ = ('type', 'value', 'line', 'col')

    def __init__(self, type_, value, line=1, col=1):
        self.type = type_
        self.value = value
        self.line = line
        self.col = col

    def __repr__(self):
        return f'Token({self.type}, {self.value!r})'


TOKEN_RE = re.compile(r'''
    \s*(?:
        ;[^\n]*                           |
        \(                                 |
        \)                                 |
        "'"                                |
        "(?:[^"\\]|\\.)*"                  |
        #t\b                               |
        #f\b                               |
        #x[0-9a-fA-F]+                     |
        [-\w*+<=>!?/@$%&|~^]+              |
        (\d+)
    )
''', re.VERBOSE)


def tokenize(source, filename='<string>'):
    tokens = []
    line = 1
    col = 1
    i = 0
    while i < len(source):
        ch = source[i]

        if ch in ' \t\r':
            col += 1
            i += 1
            continue

        if ch == '\n':
            line += 1
            col = 1
            i += 1
            continue

        if ch == ';':
            while i < len(source) and source[i] != '\n':
                i += 1
            continue

        if ch == '(':
            tokens.append(Token(TokenType.LPAREN, '(', line, col))
            col += 1
            i += 1
            continue

        if ch == ')':
            tokens.append(Token(TokenType.RPAREN, ')', line, col))
            col += 1
            i += 1
            continue

        if ch == "'":
            tokens.append(Token(TokenType.QUOTE, "'", line, col))
            col += 1
            i += 1
            continue

        if ch == '"':
            start = i
            i += 1
            while i < len(source) and source[i] != '"':
                if source[i] == '\\':
                    i += 1
                i += 1
            if i >= len(source):
                raise SyntaxError(f'{filename}:{line}:{col}: unterminated string')
            i += 1
            s = source[start:i]
            s = bytes(s[1:-1], 'utf-8').decode('unicode_escape')
            tokens.append(Token(TokenType.STRING, s, line, col))
            col += i - start
            continue

        if source[i:i+2] == '#t' and (i+2 >= len(source) or not source[i+2].isalnum()):
            tokens.append(Token(TokenType.BOOLEAN, True, line, col))
            col += 2
            i += 2
            continue

        if source[i:i+2] == '#f' and (i+2 >= len(source) or not source[i+2].isalnum()):
            tokens.append(Token(TokenType.BOOLEAN, False, line, col))
            col += 2
            i += 2
            continue

        if source[i:i+2] == '#x':
            j = i + 2
            while j < len(source) and source[j] in '0123456789abcdefABCDEF':
                j += 1
            if j == i + 2:
                raise SyntaxError(f'{filename}:{line}:{col}: bad hex literal')
            tokens.append(Token(TokenType.HEX, int(source[i+2:j], 16), line, col))
            col += j - i
            i = j
            continue

        m = re.match(r'[-\w*+<=>!?/@$%&|~^]+', source[i:])
        if m:
            word = m.group(0)
            if word == 'nil':
                tokens.append(Token(TokenType.SYMBOL, 'nil', line, col))
            elif re.match(r'^-?\d+$', word):
                tokens.append(Token(TokenType.INTEGER, int(word), line, col))
            else:
                tokens.append(Token(TokenType.SYMBOL, word, line, col))
            col += len(word)
            i += len(word)
            continue

        raise SyntaxError(f'{filename}:{line}:{col}: unexpected character {ch!r}')

    return tokens


class AST:
    pass


class Atom(AST):
    __slots__ = ('value',)
    def __init__(self, value):
        self.value = value
    def __repr__(self):
        return f'Atom({self.value!r})'

class StrAtom(AST):
    __slots__ = ('value',)
    def __init__(self, value):
        self.value = value
    def __repr__(self):
        return f'StrAtom({self.value!r})'


class List(AST):
    __slots__ = ('items',)
    def __init__(self, items=None):
        self.items = items if items is not None else []
    def __repr__(self):
        return f'List({self.items!r})'


class Quote(AST):
    __slots__ = ('expr',)
    def __init__(self, expr):
        self.expr = expr
    def __repr__(self):
        return f'Quote({self.expr!r})'


def parse(tokens):
    pos = [0]

    def peek():
        if pos[0] < len(tokens):
            return tokens[pos[0]]
        return None

    def consume():
        tok = tokens[pos[0]]
        pos[0] += 1
        return tok

    def parse_one():
        tok = peek()
        if tok is None:
            raise SyntaxError('unexpected EOF')

        if tok.type == TokenType.LPAREN:
            consume()
            items = []
            while True:
                t = peek()
                if t is None:
                    raise SyntaxError('unterminated list')
                if t.type == TokenType.RPAREN:
                    consume()
                    break
                items.append(parse_one())
            return List(items)

        if tok.type == TokenType.RPAREN:
            raise SyntaxError('unexpected )')

        if tok.type == TokenType.QUOTE:
            consume()
            return Quote(parse_one())

        consume()
        if tok.type in (TokenType.INTEGER, TokenType.HEX, TokenType.BOOLEAN):
            return Atom(tok.value)
        if tok.type == TokenType.STRING:
            return StrAtom(tok.value)
        if tok.type == TokenType.SYMBOL:
            return Atom(tok.value)
        raise SyntaxError(f'unexpected token {tok}')

    results = []
    while pos[0] < len(tokens):
        results.append(parse_one())
    return results


def parse_string(source, filename='<string>'):
    tokens = tokenize(source, filename)
    return parse(tokens)