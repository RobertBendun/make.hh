# make.hh

__WORK IN PROGRES__

Make replacement for C++ project, that makes your project only dependent on C++ compiler.
Goal is single header library that can be included into your project by copy-pasting.
This not only makes building mechanism as cross platform as your project, but also allows for
code reusage between build script and project.

## Planned features

- Automatic compile database generation from builds
- Automatic dependency resolution from C++ sources
- Parallel builds
- Self rebuild when build script changes

## Inspiration

Tsoding [nobuild](https://github.com/tsoding/nobuild)
