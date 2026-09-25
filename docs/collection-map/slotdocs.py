"""Per-collection documentation tables, read from doxygen's XML.

A collection whose meaning lives in convention rather than schema -- a bare
float VectorMember, a packed bit word, a coded integer -- can have that
convention declared as an enum next to its writer. Doxygen has already parsed
those enumerators and their descriptions; this turns them into tables for the
collection map.

An enum opts in with `@collection{NAME}`. Several enums may claim the same
collection; each becomes one titled table. `@bits{NAME,SLOT}` instead attaches
the enum to a single row of another table, for a slot that is itself a bit
field.
"""
import re
import xml.etree.ElementTree as ET
from pathlib import Path


def _text(node):
    """Flattened text of a doxygen description element."""
    if node is None:
        return ""
    return re.sub(r"\s+", " ", "".join(node.itertext())).strip()


def _par(member, title):
    """Body of a `@par <title>` block on this member, or None."""
    for sect in member.iter("simplesect"):
        if sect.get("kind") != "par":
            continue
        head = sect.find("title")
        if head is not None and _text(head) == title:
            body = " ".join(_text(p) for p in sect.findall("para")).strip()
            return body or None
    return None


def _kind(rows):
    """How to present the left-hand column of a table.

    A shift or a hex literal means the enumerators are masks; any other
    initialiser means they are values worth showing; none at all means the
    position is the slot number.
    """
    inits = [r["value"] for r in rows if r["value"]]
    # A lone `= 0` on the first enumerator only anchors the start of a
    # positional layout; it does not make the values meaningful in themselves.
    if len(inits) <= 1:
        return "index"
    return "mask" if any("<<" in v or "0x" in v.lower() for v in inits) else "value"


def _rows(member):
    """Enumerators of one enum, with the writer's size guard dropped."""
    rows = []
    for i, value in enumerate(member.findall("enumvalue")):
        rows.append({
            "index": i,
            "name": _text(value.find("name")),
            "brief": _text(value.find("briefdescription")) or
                     _text(value.find("detaileddescription")),
            # Qualified names are noise in a table: AtIP reads better than
            # edm4hep::TrackState::AtIP, and the enumerator name is alongside.
            "value": _text(value.find("initializer"))
                     .lstrip("= ").strip().rsplit("::", 1)[-1],
        })
    if rows and rows[-1]["name"].endswith("Count"):
        rows.pop()
    if not rows:
        return None
    kind = _kind(rows)
    for r in rows:
        r["label"] = r["value"] if kind != "index" else str(r["index"])
    return {"kind": kind, "rows": rows}


def _title(member, table):
    """A heading for the table: the enum's brief, else a default by kind."""
    # `///` blocks put everything in the detailed description unless
    # JAVADOC_AUTOBRIEF is on, so fall back to its first sentence.
    text = (_text(member.find("briefdescription"))
            or _text(member.find("detaileddescription")))
    head = text.split(". ")[0].strip().rstrip(".")
    if head:
        return head
    return {"mask": "bits", "value": "values"}.get(table["kind"], "parameters")


def read(xml_dir):
    """{collection: [table, ...]}, empty if the XML is absent.

    Each table is {title, kind, rows}; `kind` is "index" for a positional
    layout, "mask" for a bit field and "value" for a coded integer, and each
    row's `label` follows it.
    """
    xml_dir = Path(xml_dir)
    if not xml_dir.is_dir():
        return {}
    tables, bits = {}, []
    for path in sorted(xml_dir.glob("*.xml")):
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        for member in root.iter("memberdef"):
            if member.get("kind") != "enum":
                continue
            table = _rows(member)
            if table is None:
                continue
            collection = _par(member, "Collection")
            if collection:
                table["title"] = _title(member, table)
                tables.setdefault(collection, []).append(table)
                continue
            claim = _par(member, "Bits")
            if claim and "," in claim:
                target, _, slot = claim.partition(",")
                bits.append((target.strip(), slot.strip(), table["rows"]))

    # Attach each bit table to the row it explains.
    for collection, slot, rows in bits:
        for table in tables.get(collection, []):
            for row in table["rows"]:
                if row["name"] == slot:
                    row["bits"] = rows
    return tables
