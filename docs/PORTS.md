# Ports and packages

Cervus is an operating system. Other people's programs are not part of
it, and their source has no business in this repository. This describes
where they go instead, and how somebody who is not us gets a program in
front of Cervus users without either of us paying for a server.

## Three repositories, not one

**`cervus`** — this one. Kernel, libc, libcervus, the base utilities,
the installer. Everything written for the project. This is what "the
operating system" means and it should stay small enough to read.

**`cervus-ports`** — recipes. One directory per program, each holding a
short file saying where the upstream tarball lives, its checksum, what
it needs, and how to build it. Patches live beside it when upstream
needs persuading. **No upstream source is committed**, only the URL and
the hash. A recipe is a few kilobytes; a source tree is not.

**Releases** — built packages, attached to GitHub Releases on the ports
repository. Binary artifacts never go in git.

`usr/tcc` and `usr/cross` are in the wrong repository today. They are
somebody else's source, vendored, and they are most of the bytes here.
Moving them to ports is the first thing to do once this exists.

## Naming

The recipe tree is `cervus-ports`, following the name BSD gave the idea
forty years ago; anyone who has met FreeBSD knows what it holds without
being told.

The command is `herd`. It is short, it types easily, a herd is what a
collection of deer is called, and nothing else in this corner of the
world is called that - unlike `pkg`, which four systems already use and
which would make every search result somebody else's documentation.

    herd search png
    herd install nasm
    herd remove nasm
    herd list
    herd update

## What a package is

A tarball and a manifest, nothing cleverer:

    nasm-2.16.03-x86_64.tar.gz     the files, rooted at /
    nasm-2.16.03-x86_64.manifest   what it is
    nasm-2.16.03-x86_64.sig        Ed25519 over the manifest

The manifest is lines of `key: value`:

    name: nasm
    version: 2.16.03
    arch: x86_64
    size: 1849302
    sha256: 9e1f4f...
    depends: libc
    summary: the Netwide Assembler
    license: BSD-2-Clause

The repository index is every manifest concatenated, signed once. A
client fetches one file to know everything available.

Signing matters more here than in most places: a package manager
downloads code and runs it as root. The public key ships with the
system, `herd` refuses anything that does not verify, and the private
key never leaves the machine that signs releases. Ed25519 is already in
libcervus.

## Hosting, without paying anyone

GitHub Releases has no size limit worth worrying about for public
repositories and no bandwidth charge. That is the whole hosting story:

- a maintainer opens a pull request against `cervus-ports` adding a
  recipe directory;
- CI builds it against the cross toolchain and attaches the tarball to a
  release;
- the index is regenerated, signed, and attached too;
- `herd update` fetches
  `https://github.com/<org>/cervus-ports/releases/download/index/INDEX`
  and `herd install` fetches the tarball named in it.

Nothing here needs a server, a domain, or a bill. Codeberg and GitLab
both work the same way if GitHub ever becomes unattractive.

With a server, the only things that change are worth having but none of
them are required: the index can be generated per-architecture rather
than fetched whole, downloads can be counted, and packages can be
private. A static file host is enough - the client speaks plain HTTP
GET and nothing else.

## A recipe

    cervus-ports/nasm/recipe

    name: nasm
    version: 2.16.03
    source: https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/nasm-2.16.03.tar.gz
    sha256: 5bc940dd8a4245686976a8f7e96ba9340a0915f2d5b88356874890e207bdb581
    license: BSD-2-Clause
    summary: the Netwide Assembler
    depends: libc
    build: |
      ./configure --host=x86_64-cervus --prefix=/usr
      make
      make DESTDIR=$PKGDIR install

`build` is handed to sh(1), which is why sh had to come first. A recipe
that needs patches lists them and they sit in the same directory.

## What has to exist before any of this runs

`herd` itself is a small program: fetch, verify a signature, unpack a
tarball, record what was installed so it can be removed again. The parts
it stands on are the ones that took the work, and most are now there -
sh, make, tar that can create as well as extract, HTTP that does not
truncate, Ed25519.

The one thing still missing for the *building* half is a compiler that
runs on Cervus. Recipes can be cross-built on Linux and shipped as
binaries from day one, which is what the scheme above describes and what
Alpine did for years. Building on the machine itself comes later and
needs binutils and gcc, which need pthreads and libstdc++.

Installing packages does not wait for any of that.
