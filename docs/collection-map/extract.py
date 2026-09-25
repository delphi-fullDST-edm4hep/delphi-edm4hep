#!/usr/bin/env python3
"""Build the collection map from blessed reference files.

Reads every <refs>/<sample>/ref.edm4hep.root and writes one JSON: each
collection with its domain, type and provenance, the links between
collections, and which samples populate what.

Links are read from the stored relations, so a relation resolves only where a
sample fills it; the rest are recorded as unfilled slots. A sample's kind and
DST flavour are taken from its content, not its name.

Usage:
    extract.py [--src src/] --refs references/ [--out map.json]
"""
import argparse
import json
import sys
from pathlib import Path

import awkward as ak
import uproot

import domains
import slotdocs

_TYPEINFO = "events___CollectionTypeInfo"

# A companion UserData array is index-aligned with an object collection: its
# name is the parent's plus a suffix, or carries the parent's kind as a token.
_PARENT_BY_KIND = {"Tracks": "TRAC_Tracks", "Particles": "MAIN_Particles"}


def _typeinfo(f):
    """collection ID -> name, and name -> EDM4hep type."""
    md = f["podio_metadata"]

    def column(field):
        return ak.to_list(md[f"{_TYPEINFO}/{_TYPEINFO}.{field}"].array())[0]

    ids, names, types = column("collectionID"), column("name"), column("dataType")
    return dict(zip(ids, names)), dict(zip(names, types))


def _provenance(f):
    """collection or parameter name -> transcribed / derived / custom."""
    md = f["metadata"]
    table = dict(zip(ak.to_list(md["GPStringKeys"].array())[0],
                     ak.to_list(md["GPStringValues"].array())[0]))
    return dict(zip(table["provenance_collection"], table["provenance_source"]))


def _parameters(tree):
    """Frame-parameter names, from the first event."""
    keys = set()
    for branch in ("GPIntKeys", "GPFloatKeys", "GPDoubleKeys", "GPStringKeys"):
        keys |= set(ak.to_list(ak.flatten(tree[branch].array(entry_stop=1),
                                          axis=None)))
    return keys


def _counts(tree, names):
    """Elements per collection, summed over the file."""
    member = {}
    for key in tree.keys():
        member.setdefault(key.split(".", 1)[0], key)
    out = {}
    for name in names:
        key = member.get(name)
        out[name] = 0 if key is None else len(
            ak.flatten(tree[key].array(), axis=None))
    return out


def _links(tree, id_to_name, names):
    """(collection, relation) -> targets seen, plus the relations nothing fills."""
    by_length = sorted(names, key=len, reverse=True)
    resolved, unfilled = {}, []
    for key in tree.keys():
        if not (key.startswith("_") and key.endswith(".collectionID")):
            continue
        stem = key.split("/")[0][1:]                  # <Collection>_<relation>
        owner = next((n for n in by_length if stem.startswith(n + "_")), None)
        if owner is None:
            continue
        relation = stem[len(owner) + 1:]
        targets = {id_to_name[v] for v in
                   set(ak.to_list(ak.flatten(tree[key].array(), axis=None)))
                   if v in id_to_name}
        if targets:
            resolved[(owner, relation)] = targets
        else:
            unfilled.append((owner, relation))
    return resolved, unfilled


def _companion_of(name, types):
    """The collection a UserData array runs parallel to, or None."""
    if "UserData" not in types[name]:
        return None
    stem = name.rsplit("_", 1)[0]
    if stem in types:
        return stem
    tokens = name.split("_")
    for token in tokens[1:]:
        if token in _PARENT_BY_KIND:
            parent = f"{tokens[0]}_{_PARENT_BY_KIND[token]}"
            if parent in types and parent != name:
                return parent
    return None


def _flavour(counts):
    """DST flavour, from the collection prefixes the sample actually fills."""
    filled = {n.split("_", 1)[0] for n, rows in counts.items() if rows}
    if "lDST" in filled:
        return "long"
    return "xshort" if "xsDST" in filled else "short"


def read_sample(path, domain_index):
    f = uproot.open(path)
    tree = f["events"]
    id_to_name, types = _typeinfo(f)
    names = set(types)
    parameters = _parameters(tree)
    domain_of, unattributed, ambiguous = domains.attribute(
        sorted(names | parameters), domain_index)
    counts = _counts(tree, names)
    resolved, unfilled = _links(tree, id_to_name, names)
    truth = any(rows for n, rows in counts.items()
                if domain_of.get(n) == "Truth")
    return {
        "events": tree.num_entries,
        "kind": "mc" if truth else "data",
        "flavour": _flavour(counts),
        "types": types,
        "parameters": parameters,
        "provenance": _provenance(f),
        "domain_of": domain_of,
        "unattributed": unattributed,
        "ambiguous": ambiguous,
        "counts": counts,
        "resolved": resolved,
        "unfilled": unfilled,
    }


def _labels(samples):
    """A short label per sample: its kind and flavour, or its id if that clashes."""
    labels = {i: f"{s['kind']}/{s['flavour']}" for i, s in samples.items()}
    seen = {}
    for i, label in labels.items():
        seen.setdefault(label, []).append(i)
    return {i: (label if len(seen[label]) == 1 else i)
            for i, label in labels.items()}


def build(samples, tables=None):
    """Merge per-sample readings into one map."""
    label = _labels(samples)
    names = sorted(set().union(*(set(s["types"]) for s in samples.values())))

    declared, resolved = set(), {}
    for i, s in samples.items():
        declared |= set(s["resolved"]) | set(map(tuple, s["unfilled"]))
        for edge, targets in s["resolved"].items():
            for target in targets:
                resolved.setdefault((*edge, target), []).append(label[i])

    unfilled = {}
    for owner, relation in sorted(declared - {e[:2] for e in resolved}):
        unfilled.setdefault(owner, []).append(relation)

    tables = tables or {}
    any_sample = next(iter(samples.values()))
    collections = {}
    for name in names:
        prefix, bank, readable = name.split("_", 2)
        collections[name] = {
            "domain": any_sample["domain_of"][name],
            "prefix": prefix,
            "bank": bank,
            "readable": readable,
            "type": any_sample["types"][name],
            "provenance": any_sample["provenance"].get(name),
            "companion_of": _companion_of(name, any_sample["types"]),
            "populated_in": [label[i] for i in sorted(samples)
                             if samples[i]["counts"].get(name)],
            "unfilled_relations": unfilled.get(name, []),
            # Documentation tables for conventions the schema cannot carry:
            # slot layouts, bit words, coded integers. Absent for most.
            "tables": tables.get(name.split("_", 1)[1], []),
        }

    parameters = {
        name: {"domain": any_sample["domain_of"][name],
               "bank": name.split("_", 2)[1],
               "provenance": any_sample["provenance"].get(name)}
        for name in sorted(any_sample["parameters"])
    }

    return {
        "schema": 1,
        "samples": [{"id": i, "label": label[i], "kind": samples[i]["kind"],
                     "flavour": samples[i]["flavour"],
                     "events": samples[i]["events"]}
                    for i in sorted(samples)],
        "domains": sorted({c["domain"] for c in collections.values()}
                          | {p["domain"] for p in parameters.values()}),
        "collections": collections,
        "parameters": parameters,
        "links": [{"from": owner, "relation": relation, "to": target,
                   "seen_in": sorted(where)}
                  for (owner, relation, target), where in sorted(resolved.items())],
    }


def main():
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--src", default=str(here / ".." / ".." /
                                             "delphi_edm4hep" / "src"))
    parser.add_argument("--refs", required=True)
    parser.add_argument("--out", default="collection_map.json")
    parser.add_argument("--doxygen-xml", default=None,
                        help="doxygen XML directory; adds documentation tables")
    args = parser.parse_args()

    domain_index = domains.index(args.src)
    samples = {}
    for path in sorted(Path(args.refs).iterdir()):
        ref = path / "ref.edm4hep.root"
        if ref.exists():
            samples[path.name] = read_sample(ref, domain_index)
    if not samples:
        sys.exit(f"no <sample>/ref.edm4hep.root under {args.refs}")

    failed = False
    for i, s in samples.items():
        for problem, entries in (("unattributed", s["unattributed"]),
                                 ("ambiguous", s["ambiguous"])):
            if entries:
                failed = True
                print(f"{i}: {problem}: {entries}", file=sys.stderr)
    sets = {i: set(s["types"]) for i, s in samples.items()}
    common = set.intersection(*sets.values())
    for i, names in sets.items():
        if names != common:
            failed = True
            print(f"{i}: collection set differs: {sorted(names ^ common)}",
                  file=sys.stderr)
    if failed:
        sys.exit(1)

    tables = slotdocs.read(args.doxygen_xml) if args.doxygen_xml else {}
    Path(args.out).write_text(
        json.dumps(build(samples, tables), indent=2) + "\n")
    print(f"{args.out}: {len(samples)} samples, "
          f"{len(common)} collections")


if __name__ == "__main__":
    main()
