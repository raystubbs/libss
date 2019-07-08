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
are attempted in the order given, so leftmost alternatives have
higher priority.

The type of brackets around a pattern indicate the number of instances
that can be matched.

- `(...)` match only once
- `<...>` match one or more times
- `[...]` match zero ore one times
- `{...}` match zero or more times

Each type will match the maximum number of instances available in the
appropriate location of the input text.

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

    pattern "I ( { 'really':adverb ' ' }:g 'love':verb | `don't`:adverb ' ' 'like':verb ):verbal food." matches:
        - "I love food." with verbal.verb = 2..6
        - "I really love food." with verbal.g[0].adverb = 2..8, verbal.verb = 9..13
        - "I really really love food." with with verbal.g[0].adverb = 2..8, verbal.g[1].adverb = 9..15, verbal.verb = 16..20
        - "I don't like food." with verbal.adverb = 2..7, verbal.verb = 8..12

This makes it a bit easier to match and keep track of the parts of
multi-component patterns.

Character literals are a shorter syntax for expressing a single character
pattern, either within the root level text or a bracketed group.  These
consist of a backslash `\` followed by a single character or
byte, and can have a label attached like any other pattern.

	pattern "This is \(not\) very interesting." matches:
		- "This is (not) very interesting."

Note that in C string literals the backslash is used as an escape character,
so must itself be escaped.  Character literals are also useful for breaking
a pattern's label between alphanumeric characters.

	pattern "These are my ( 'dog' | 'cat' ):pet\s." matches:
		- "These are my dogs."
		- "These are my cats."

To make libss a bit more friendly to globbing the compiler recognizes
two special 'wildcard' characters: `*` and `?`.  These expand to the
patterns `(splat)` and `(quark)` respectively.  These pattern names
are undefined by default, so the user needs to define useful patterns
under these names to make use of the wildcards.
