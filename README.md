# String Scan
A simple string matching library suitable as a lightweight alternative
to regex.  Supports matching against raw byte strings as well as UTF-8
encoded character strings.

The root level of a pattern string is taken as literal text.

    pattern "Hello, World!" matches:
        - "Hello, World!"

A variable portion of the pattern must be given between brackets.

    pattern "Hello, ( 'World' | 'Person' )!" matches:
        - "Hello, World!"
        - "Hello, Person!"

Within bracketed groups literal text must be quoted between single,
double, or back quotes.  A vertical bar within bracketed groups
separate alternative subpatterns that'll satisfy the grouping.  These
are attempted in the order given, so rightmost alternatives have
higher priority.

The type of brackets around a pattern indicate the number of instances
that can be matched.

- `(...)` match only once
- `<...>` match one or more times
- `[...]` match zero ore one times
- `{...}` match zero or more times

<<<<<<< HEAD
Each type will match the maximum number of instances available in the
appropriate location of the input text.
=======
    "There are two ( 'apples' | 'oranges' )!" <= "There are two apples!" <= "There are two oranges!"
>>>>>>> 3658efdd75e7e68624703d5e83bdae48ac0ca73a

Named patterns can be references within bracket groups via standard
identifiers.

    pattern "I ate <digit> tacos." matches:
        - "I ate 1 tacos."
        - "I ate 2 tacos."
        - "I ate 50 tacos."

Specific character or byte codes can also be given as integer literals.

    pattern "(72)ello, World(33)" matches:
        - "Hello, World!"


Any other subpattern can be preceeded with `^` or `~`.  A pattern given
after a `^` is a lookahead pattern.  It doesn't advance the cursor when
matched, and is just used to make sure something is true about the next
input sequence.  A `~` also marks a lookahead pattern, but patterns marked
with this must FAIL for the greater (parent) pattern to match.

Subpatterns can also be labeled to make it easier to find their bounds
within the input text.

    pattern "I like ( 'tacos' | 'burritos' ):food." matches:
        - "I like tacos." with food = 7:12
        - "I like burritos." with food = 7:15

Each instance of a particular pattern match has its own naming scope.

    pattern "I < 'love ' >food." matches:
        - "I love food." with food[0] = 2..6
        - "I love love food." with food[0] = 2..6 and food[1] = 7..11

This makes it a bit easier to match and keep track of the parts of
multi-component patterns.

To make libss a bit more friendly to globbing the compiler recognizes
two special 'wildcard' characters: `*` and `?`.  These expand to the
patterns `(splat)` and `(quark)` respectively.  These pattern names
are undefined by default, so the user needs to define useful patterns
under these names to make use of the wildcards.