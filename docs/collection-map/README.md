# Collection map

A page describing the converter output: every collection with its domain, type
and provenance, the links between collections, and which samples populate what.
Published at
<https://delphi-fulldst-edm4hep.github.io/delphi-edm4hep/collection_map.html>.

## How it is built

    doxygen (XML only)                    -> docs/xml/
    extract.py --refs <references>/        -> collection_map.json
      --doxygen-xml docs/xml
    render.py  --map collection_map.json   -> collection_map.html

`extract.py` reads the blessed conversion-identity references, one
`<references>/<sample>/ref.edm4hep.root` per column of the "populated in"
table, and attributes each collection to the `src/<Domain>/` that writes it. It
needs uproot. `render.py` needs only the standard library, and `--publish DIR`
copies the page somewhere on the way out.

`--doxygen-xml` is optional. With it, `slotdocs.py` reads the per-collection
documentation tables out of doxygen's XML: an enum marked `@collection{NAME}`
next to its writer becomes the table for that collection, and `@bits{NAME,SLOT}`
attaches an enum to one slot of another table. This is how a bare VectorMember,
a packed bit word or a coded integer gets its meaning onto the page, since the
EDM4hep schema carries none of it.

Neither output is committed: CI builds both on the machine holding the
references, after the identity test has passed, so the page always describes
the output those tests accepted.

## Building it by hand

    python3 extract.py --refs "$DELPHI_CI_REFS" --out collection_map.json
    python3 render.py --map collection_map.json --out collection_map.html

`extract.py` fails if a collection cannot be attributed to a domain, or if the
samples disagree about the collection set.
