from enum import Enum
from dataclasses import dataclass
import dataclasses
from pathlib import Path
from typing import ClassVar
import sys

import msgpack


def chr_range(start: str, end: str, inclusive: bool = True) -> set[str]:
    assert start < end
    return set(map(chr, range(ord(start), ord(end) + int(inclusive))))


JANET_SIMPLE_CHARS = {
    *chr_range("a", "z"),
    *chr_range("A", "Z"),
    *chr_range("0", "9"),
    "-",
    "_",
}


class StringType(Enum):
    BUFFER = "@"
    STRING = ""
    KEYWORD = ":"
    SYMBOL = "'"

    @property
    def prefix(self) -> str:
        return self.value

    def format(self, value: str):
        if self == StringType.BUFFER or self == StringType.STRING:
            # Reuse Python's string repr machinery. TODO: Is this ever wrong wrt escapes?
            #
            # Maybe we should switch to json.
            r = repr(value)
            if r.startswith("'"):
                assert r.endswith("'")
                r = r.replace('"', r"\"")
                return self.prefix + '"' + r[1:-1] + '"'
            else:
                assert r.startswith('"'), repr(r)
                assert r.endswith('"')
                return self.prefix + r
        elif all([(c in JANET_SIMPLE_CHARS) for c in value]) and len(value) >= 1:
            return f"{self.prefix}{value}"
        else:
            return f"({self.name.lower()} {StringType.STRING.format(value)})"


class Mutability(Enum):
    IMMUTABLE = False
    MUTABLE = True

    @property
    def prefix(self):
        return "@" if self == Mutability.MUTABLE else ""


@dataclass(frozen=True)
class JanetSettings:
    string_type: StringType = StringType.STRING
    map_type: Mutability = Mutability.MUTABLE
    array_type: Mutability = Mutability.MUTABLE
    map_key_type: StringType = StringType.KEYWORD

    DEFAULT: ClassVar["JanetSettings"] = None


JanetSettings.DEFAULT = JanetSettings()


def janetify(data: object, settings: JanetSettings = JanetSettings.DEFAULT):
    if isinstance(data, dict):
        res = [settings.map_type.prefix + "{"]
        first = True
        for k, v in data.items():
            if not first:
                res.append(" ")
            first = False
            res.append(
                janetify(
                    k,
                    settings=dataclasses.replace(
                        settings, string_type=settings.map_key_type
                    ),
                )
            )
            res.append(" ")
            res.append(janetify(v, settings=settings))
        res.append("}")
        return "".join(res)
    elif isinstance(data, (list, tuple)):
        return (
            settings.array_type.prefix
            + "["
            + (" ".join(janetify(item, settings=settings) for item in data))
            + "]"
        )
    elif isinstance(data, str):
        return settings.string_type.format(data)
    elif isinstance(data, bool):
        return repr(data).lower()
    elif isinstance(data, float):
        return repr(data)
    elif isinstance(data, int):
        if data in range(-(2**32 - 1), 2**32 - 1 + 1):
            # fits in int
            return str(data)
        else:
            tp = "s64" if data >= 0 else "u64"
            return f"(int/{tp} {StringType.STRING.format(str(data))})"
    elif data is None:
        return "nil"
    else:
        raise TypeError(f"Unknown type: {data!r}")


def main():
    try:
        target = Path(sys.argv[1])
    except IndexError:
        print("Expected an argument to specify mspgack file to read", file=sys.stderr)
        sys.exit(1)
    try:
        with open(target, "rb") as f:
            data = msgpack.load(f)
    except FileNotFoundError:
        print(f"Unable to open non-existent file: {target}", file=sys.stderr)
        sys.exit(1)
    print(janetify(data))


if __name__ == "__main__":
    main()
