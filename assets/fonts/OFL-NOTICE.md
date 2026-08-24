# Font licences

Both TTFs in this directory are redistributed verbatim under the SIL Open Font
License 1.1. OFL 1.1 §2 requires the full licence text to accompany the font
binaries, so the complete licence is included alongside them as
[`OFL.txt`](OFL.txt) (fetched verbatim from <https://openfontlicense.org/documents/OFL.txt>).

| File                  | Copyright                                    | Reserved Font Name |
| --------------------- | -------------------------------------------- | ------------------ |
| `Literata.ttf`        | (c) The Literata Project Authors             | Literata           |
| `LiterataItalic.ttf`  | (c) The Literata Project Authors             | Literata           |
| `SpaceGrotesk.ttf`    | (c) Florian Karsten                          | Space Grotesk      |

`LiterataItalic.ttf` is the upstream `Literata-Italic[opsz,wght].ttf`, redistributed
verbatim and renamed only to match this directory's convention (no separator, as
`SpaceGrotesk.ttf`). Renaming a file is not a Modified Version under OFL 1.1, so it
keeps the Reserved Font Name. It is a TRUE ITALIC -- different letterforms, not a
slant: `post.italicAngle` is only -2 degrees, which is why an oblique transform of
the roman was never a substitute for it. It carries the same two axes as the roman
(`opsz` 7..72, `wght` 200..900) and the same `unitsPerEm` of 1000, which is what lets
a line mix the two faces without their advances disagreeing.

The `.rfnt` files under `assets/built/` and the embedded `shell/src/font_*.h`
headers generated from them (`font_meta.h`, `font_label.h`, `font_value.h`,
`font_body.h`, `font_title.h`)
are Modified Versions in the OFL sense: bitmap renderings produced from these
TTFs by `tools/fontc.py`, at a pinned instance of each font's variation axes.
They carry the same licence and, per OFL 1.1 §3, do not use the Reserved Font
Names.
