janet-msgpack
==============
An implementation of [msgpack](https://msgpack.org) for [Janet](https://janet-lang.org).

## Features
- Supports encoding most Janet types (tables, arrays, primitives, etc...)
- Uses ludocdoe/mpack for decode, hand-coded encoding.
- Automated testing with comparison to Python impl

## TODO
- [American Fuzzy Lop](https://lcamtuf.coredump.cx/afl/)

## Credits
I.E: Libraries and copied code

- [janet/json](https://github.com/janet-lang/json) by @bakpakin - Code for Janet interop largely copied from here
- [ludocode/mpack](https://github.com/ludocode/mpack/) - Used for msgpack *decoding*
