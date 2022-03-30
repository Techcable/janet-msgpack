[WIP] janet-msgpack
==============
An implementation of [msgpack](https://msgpack.org) for janet.

## Features
- Supports encoding most Janet types (tables, arrays, primitives, etc...)
- Uses ludocdoe/mpack for decode (*TODO*), hand-coded encoding.
## TODO
- Support decoding values (not just encoding)
- Support configurable string/buffer encoding (either msgpack bytes or msgpack strings)
- Automated testing with comparison to Python impl
- [American Fuzzy Lop](https://lcamtuf.coredump.cx/afl/)


## Credits
I.E: Libraries and copied code

- [janet/json](https://github.com/janet-lang/json) by @bakpakin - Code for Janet interop largely copied from here
- [ludocode/mpack](https://github.com/ludocode/mpack/) - Used for msgpack *decoding*
