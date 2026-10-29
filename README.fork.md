# fexcore-darwin

FEXCore (the x86-64 to ARM64 JIT from [FEX-Emu](https://github.com/FEX-Emu/FEX))
adapted to run natively on Apple Silicon, with no
Rosetta and no Linux runtime.

This is a fork maintained for a custom KytyPS5 fork,
a PS5 emulator fork for iOS and macOS, which uses it as its guest-CPU
backend. It is built to work with that emulator.

## Lineage

1. FEX-Emu/FEX @ `f2b679f6028c` (upstream, MIT)
2. Bachata-S4 (JICA98) - Darwin port and `BUILD_FEXCORE_ONLY` option
3. AetherPS4 (Leviidev), branch `fex-snapshot` @ `220ab1e` - snapshot and later changes
4. This fork - Apple Silicon JIT, allocator and lookup-cache fixes

Git history: FEX upstream history, then
`Import AetherPS4 fexcore-darwin @220ab1e`, then
`FEXCore: Apple Silicon JIT, allocator and lookup-cache fixes`, then the
README changes. The patch commit message describes the code changes in detail.
See also `README.aethercore-darwin.md` for the Darwin port notes.

## AI disclosure / upstream contribution

The modifications in this fork's own commits were written with AI
assistance. FEX-Emu does not accept AI-generated contributions.

## License

MIT, same as upstream FEX (see `LICENSE`).

## Updating from upstream

```sh
git remote add upstream https://github.com/FEX-Emu/FEX.git
git fetch upstream
git rebase --onto <new-fex-commit> f2b679f60 main
```

This replays the fork's commits (Aether import, patches, README) onto a newer
FEX commit.
