# make.hh

__WORK IN PROGRES__

Make replacement for C++ project, that makes your project only dependent on C++ compiler.
Goal is single header library that can be included into your project by copy-pasting.
This not only makes building mechanism as cross platform as your project, but also allows for
code reusage between build script and project.

## Usage

To build your project:

```console
$ c++ -std++20 -o make make.cc
$ ./make
```

Each of invocations may rebuild `./make` if `make.cc` has changed.
If something gone wrong, you can access previous version by `./make.old`

## Planned features

- [x] Self rebuild when build script changes
- [x] Automatic dependency resolution from C++ sources
- [ ] Automatic compile database generation from builds
- [ ] Parallel builds

## Inspiration

Tsoding [nobuild](https://github.com/tsoding/nobuild)
