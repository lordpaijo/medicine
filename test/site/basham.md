# BASHAM!

A simple bash script to manage your assembly project.

## Dependencies
- `binutils`
- `curl`
- `git`
- `nasm` (x86_64)
- `arm-none-eabi-as` (armv7l)
- `arm-none-eabi-ld` (armv7l)
- `aarch64-linux-gnu-as` (aarch64)
- `aarch64-linux-gnu-ld` (aarch64)

## Install

Using `install.sh` installer script:
```sh
$ git clone https://github.com/lordpaijo/basham.git && cd basham/
$ chmod +x install.sh
$ ./install.sh            # Local install (to /usr/local/bin)
$ ./install.sh --shared   # Shared install (to /usr/bin) [ root permission required ]
```

Or if the installer script doesn't cover, you could install it via `curl` (requires dependencies installed manually):
```
$ sudo curl -fsSL -o /usr/local/bin/basham.sh "https://raw.githubusercontent.com/lordpaijo/basham/refs/heads/master/src/basham.sh"  # local
$ sudo curl -fsSL -o /usr/bin/basham.sh "https://raw.githubusercontent.com/lordpaijo/basham/refs/heads/master/src/basham.sh"  # shared

$ sudo chmod +x /usr/local/bin/basham.sh    # local
$ sudo chmod +x /usr/bin/basham.sh          # shared [ root permission required ]
```

And you're good to go.

## Usage

It's as simple as calling `basham.sh` followed by an argument.

```sh
$ basham.sh new blablablablablabla
$ basham.sh build --arch x86_64
$ basham.sh run --arch x86_64
```

## Nix Usage
If you don't want to install it, you can execute via `nix run`
```sh
$ nix run github:lordpaijo/basham -- new blablablablablabla
```
Or you can install it via `nix profile`
```sh
$ nix profile install github:lordpaijo/basham
$ basham.sh new blablablablablabla
```

### Arguments

List of available arguments:
| Arguments | Functions |
|-----------|-----------|
| `new` | Initiate a new project. |
| `upgrade` | Upgrade the script system wide. |
| `delete` | Delete an existing project. |
| `update` | Update an existing project. |
| `search` | Search for assembly files. |
| `build` | Build the main executable of the project and putting it into `build` directory. |
| `test` | Build the test executable and putting it into `test` directory before running it. |
| `run` | Rebuild and run the main executable . |
| `clean` | Clean the build and test directory. |
| `install` | Install the executable system wide. |

### Specifiers

Fortunately, there are more than one specifier (which is unfortunate for me to maintain).
| Specifiers | Functions |
|------------|-----------|
| `--help`   | Show help message with usage instructions. |
| `--arch`   | Specify the build architecture. |
| `--git`    | Integrate git with the passed arguments. |
| `--local`  | Storing in `/usr/local/bin/` |
| `--shared` | Storing in `/usr/bin/` |


Have a full documentation here -> [click me please daddy~](https://github.com/lordpaijo/basham/blob/master/documentation.md)
---

And have fun~...
