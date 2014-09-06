# Based on the ttypager and supporting functions from pydoc, under the Python
# license, with various improvements.

import bits
import contextlib
import pager
import redirect
import string
import sys
import textwrap
from cStringIO import StringIO

def getpager():
    return ttypager

def plain(text):
    """Remove boldface formatting from text."""
    import re
    return re.sub('.\b', '', text)

def ttypager(text):
    """Page through text on a text terminal."""
    lines = string.split(plain(text), '\n')
    if redirect.state != redirect.NOLOG_STATE:
        with redirect.logonly():
            sys.stdout.write(string.join(lines, '\n') + '\n')
        if redirect.state == redirect.LOGONLY_STATE:
            return
    with pager.nopager():
        with redirect.nolog():
            height = min(bits.get_width_height(term)[1] for term in range(bits.get_term_count()))
            r = inc = height - 1
            sys.stdout.write(string.join(lines[:inc], '\n') + '\n')
            while True:
                if lines[r:]:
                    prompt = '-- any key to advance; PgUp to page up; q to quit --'
                else:
                    prompt = '-- END; PgUp to page up; q to quit --'
                prompt_len = len(prompt)
                sys.stdout.write(prompt)
                c = bits.get_key()
                # Write the spaces one at a time to defeat word-wrap
                sys.stdout.write('\r')
                for i in range(prompt_len):
                    sys.stdout.write(' ')
                sys.stdout.write('\r')
                if c in (ord('q'), ord('Q')):
                    break
                elif c in (ord('\r'), ord('\n'), bits.KEY_DOWN, bits.MOD_CTRL | ord('n')):
                    if lines[r:]:
                        sys.stdout.write(lines[r] + '\n')
                        r = r + 1
                    continue
                if c == bits.KEY_HOME:
                    bits.clear_screen()
                    r = 0
                if c == bits.KEY_END:
                    bits.clear_screen()
                    r = len(lines) - inc
                    if r < 0:
                        r = 0
                if c in (bits.KEY_UP, bits.MOD_CTRL | ord('p')):
                    bits.clear_screen()
                    r = r - 1 - inc
                    if r < 0:
                        r = 0
                if c in (bits.KEY_PAGE_UP, ord('b'), ord('B')):
                    bits.clear_screen()
                    r = r - inc - inc
                    if r < 0:
                        r = 0
                if lines[r:]:
                    sys.stdout.write(string.join(lines[r:r+inc], '\n') + '\n')
                    r = r + inc
                    if not lines[r:]:
                        r = len(lines)

_wrapper = textwrap.TextWrapper(width=77, subsequent_indent='  ')
_wrapper_indentall = textwrap.TextWrapper(width=77, initial_indent='  ', subsequent_indent='  ')

def _wrap(str, indent=True):
    def __wrap():
        wrapper = _wrapper
        for line in str.split("\n"):
            # Preserve blank lines, for which wrapper emits an empty list
            if not line:
                yield ""
            for wrapped_line in wrapper.wrap(line):
                yield wrapped_line
            if indent:
                wrapper = _wrapper_indentall
    return '\n'.join(__wrap())

def ttypager_wrap(text, indent=True):
    ttypager(_wrap(text, indent))

@contextlib.contextmanager
def page():
    """Capture output to stdout/stderr, and send it through ttypager when done"""
    out = StringIO()
    with redirect._redirect_stdout(out):
        with redirect._redirect_stderr(out):
            try:
                yield
            except:
                import traceback
                traceback.print_exc()
    ttypager_wrap(out.getvalue(), indent=False)
