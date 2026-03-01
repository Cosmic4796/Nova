# Nova

A modern, elegant programming language built in C.

Nova is a dynamically-typed, interpreted language designed for clarity and simplicity. It features clean syntax, a rich standard library, a built-in package manager, and module namespaces — all in a fast, lightweight runtime with zero dependencies beyond a C compiler.

## Quick Example

```
import "math"
import "string"

func greet(name) do
    let msg = string.to_upper("hello, " + name + "!")
    print(msg)
done

greet("world")
print("PI = {math.PI}")
print("sqrt(144) = {math.sqrt(144)}")
```

## Features

- **Clean syntax** — `do`/`done` blocks, string interpolation with `{expr}`, pipe operator `|>`
- **12 standard library modules** — math, io, string, os, random, json, collections, path, datetime, csv, regex, http
- **Module namespaces** — `import "math"` gives you `math.sqrt()`, no naming collisions
- **Package manager** — `nova install <git-url>` with transitive dependency resolution
- **Classes and OOP** — constructors, methods, inheritance
- **First-class functions** — lambdas, closures, higher-order functions
- **Pattern matching** — ternary expressions, if/else chains
- **Error handling** — try/catch with runtime error recovery
- **Built-in tooling** — REPL, test runner, code formatter, project scaffolding
- **Pipe operator** — `value |> func()` for readable data pipelines

## Installation

### Build from Source

Requires a C compiler (gcc or clang) and CMake 3.10+.

```bash
git clone https://github.com/user/nova-c.git
cd nova-c
mkdir build && cd build
cmake ..
cmake --build .
```

The `nova` binary will be in `build/`. To install system-wide:

```bash
sudo cp nova /usr/local/bin/
sudo mkdir -p /usr/local/bin/stdlib
sudo cp stdlib/*.dylib /usr/local/bin/stdlib/   # macOS
# sudo cp stdlib/*.so /usr/local/bin/stdlib/    # Linux
```

### One-Line Install (macOS/Linux)

```bash
curl -fsSL https://raw.githubusercontent.com/user/nova-c/main/install.sh | sh
```

## Usage

```bash
nova                        # Start the REPL
nova script.nova            # Run a file
nova run                    # Run main from nova.json
nova run --watch file.nova  # Re-run on file changes
nova test [dir]             # Run *_test.nova files
nova init [name]            # Create a new project
nova fmt <file|dir>         # Format source files
nova install <git-url>      # Install a package
```

## Language Tour

### Variables and Types

```
let name = "Nova"
let age = 1
let pi = 3.14159
let active = true
let items = [1, 2, 3]
let config = {"debug": true, "level": 5}
```

### Control Flow

```
if x > 0 do
    print("positive")
else if x < 0 do
    print("negative")
else do
    print("zero")
done

for item in items do
    print(item)
done

while condition do
    # ...
done
```

### Functions and Lambdas

```
func add(a, b) do
    return a + b
done

let double = (x) => x * 2
let result = [1, 2, 3] |> collections.map((x) => x * 10)
```

### Classes

```
class Dog do
    init(name, breed) do
        self.name = name
        self.breed = breed
    done

    func speak() do
        print("{self.name} says Woof!")
    done
done

let rex = Dog("Rex", "Labrador")
rex.speak()
```

### Modules

```
import "math"
import "string"
import "json"

print(math.sqrt(16))                    # 4
print(string.to_upper("hello"))         # HELLO
let data = json.parse("{\"x\": 42}")
print(data["x"])                        # 42
```

### Pipe Operator

```
import "string"
import "math"

"  hello world  " |> string.trim() |> string.to_upper() |> print
-42 |> math.abs() |> math.sqrt() |> print
```

### Error Handling

```
try do
    let result = 10 / 0
catch err do
    print("Error: {err}")
done
```

## Standard Library

| Module | Description |
|--------|-------------|
| `math` | abs, sqrt, pow, sin, cos, tan, log, floor, ceil, round, min, max, PI |
| `io` | read_file, write_file, file_exists, remove_file, list_dir, mkdir |
| `string` | to_upper, to_lower, trim, split, join, replace, starts_with, ends_with, contains, substr, index_of, pad_left, pad_right |
| `os` | clock, exit, getenv, system |
| `random` | random, randint, seed, choice, shuffle |
| `json` | parse, dump |
| `collections` | map, filter, reduce, sort, sort_by, reverse, zip, enumerate, find, every, some, flat |
| `path` | join, basename, dirname, extension, exists, is_dir, is_file, absolute |
| `datetime` | now, timestamp, format, parse |
| `csv` | parse, dump, parse_file |
| `regex` | match, find_all, replace, test |
| `http` | get, post, download (requires libcurl) |

## Creating Packages

A Nova package is a git repository with Nova source files. Optionally include a `nova.json`:

```json
{
    "name": "my-package",
    "version": "1.0.0",
    "main": "lib.nova",
    "dependencies": {
        "other-pkg": "https://github.com/user/other-pkg.git"
    }
}
```

Users install with `nova install <git-url>`, which clones to `nova_packages/` and automatically installs transitive dependencies.

## Project Structure

```
nova-c/
├── src/            # Core interpreter source
│   ├── main.c      # CLI entry point (REPL, run, test, init, fmt, install)
│   ├── lexer.c     # Tokenizer
│   ├── parser.c    # Recursive descent parser
│   ├── interpreter.c  # Tree-walking interpreter
│   ├── builtins.c  # Built-in functions (print, len, range, etc.)
│   ├── module.c    # Module loader with namespace support
│   └── ...
├── include/        # Header files
├── stdlib/         # Standard library C modules (.dylib/.so)
├── examples/       # 17 example programs
├── docs/           # Documentation website
├── install.sh      # macOS/Linux installer
├── install.ps1     # Windows installer
└── CMakeLists.txt  # Build system
```

## License

This project is licensed under the GNU General Public License v3.0 — see [LICENSE](LICENSE) for details.

**Ethical Use:** This software is provided for constructive, non-malicious purposes. By using Nova, you agree not to use it to develop software intended to cause harm, damage systems, or engage in any malicious activity.
