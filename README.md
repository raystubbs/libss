# String Scan
A simple string matching library suitable as a lightweight alternative
to regex.  Supports multiple string encoding formats including UTF-8,
UTF-16, UTF-32, and raw bytes.

The root level contents of a string are taken as plain text, and matched
literally against the input string.  For example:

    "Hello, World!" <= "Hello, World!"

Actual variations in the pattern as enclosed in brackets.  With `(...)`
denoting a single occurance of the pattern and `<...>` indicating at
least one occurance up to as many as can be matched.

    "Hello, ( 'World!' )" <= "Hello, World!"
    "Hello, < 'World!' >" <= "Hello, World!" <= Hello, World! World!"

Within bracketed groups, pattern syntax changes.  Instead of matching
the text literally, a more structured syntax must be followed.  Literal
text must be expressed as strings, quoted by either single, double, or
back qoutes.  Whitespace is also ignored within these compound expressions,
and multiple sub-expressions can be given in sequence.

    "This text is literal ( ', but ' 'this ' 'is ' 'not' )!" <= "This text is literal, but this is not!"

These groups can also be divided into multiple alternatives via the <code>&vert;</code>
delimiter.

    "There are two ( 'apples' | 'oranges' )!" <= "There are two apples" <= "There are two oranges"

Alternatives within a grouping are given by order of priority, so unlike in regex, the
first matching alternative in the sequence is accepted instead of the longest.

Two more grouping types are `[...]` which matches zero or one instance of its pattern
and `{...}` which matches zero or more instances.  In addition the `~` prefix denotes
a 'not next' pattern, which doesn't consume any characters, but only matches if the
next sequence of characters doesn't match the given pattern.  The counterpart to this
is the `^` prefix, which denotes a 'has next' pattern; only matching if the next
sequence matches the pattern, but not consuming anything.

Patterns can also be labeled to allow for easy retrieval of the boundaries of a
subpattern match.  This is done with a label postfix:

    "There are two ( 'apples' | 'oranges' ):fruit!"

Specific character values can also be indicated with an integral literal, these
match particular unicode characters or byte values, depending on encoding.

    "(72)ello, World(33)"

