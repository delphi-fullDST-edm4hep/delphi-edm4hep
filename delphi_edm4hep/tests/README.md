# Conversion identity test

Checks that converting DELPHI data to EDM4hep still gives the same result. Runs
on a dedicated machine rather than in GitHub Actions, because it reads real
DELPHI files from EOS.

The CI job needs a registered, online self-hosted runner carrying the
`delphi-eos` label. It is disabled when that runner is unavailable so ordinary
CI does not remain queued for 24 hours. A maintainer can either select
`run_data_test` when manually dispatching the CI workflow, or set the repository
variable `DELPHI_DATA_CI_ENABLED=true` to run it automatically for pushes and
trusted, same-repository pull requests.

    bash .github/scripts/data-test.sh                  # all samples
    bash .github/scripts/data-test.sh data_94c_short   # one sample
    bash .github/scripts/data-test.sh --bless          # regenerate references

Samples are declared in samples.yaml by fatfind nickname. data-test.sh sets
DELPHI_DATA_ROOT=/eos/opendata/delphi, so they resolve to CERN Open Data.

## How files are compared

ROOT writes a UUID and timestamps into every file, so two conversions of the
same input are never byte-identical. Each file is instead reduced to a digest:
for every branch of the events tree, the element count, the storage type, and a
hash of the values.

The payload section of a digest, the events tree, must not change. The stack
section - podio, EDM4hep and key4hep versions - moves with the software and is
reported as a warning only.

A digest also records which files the converter opened, read back from its log.
Nicknames resolve through RunInfo and the PDL files on cvmfs, which are
maintained upstream, so the file behind a sample can change on its own. That is
reported as an input change rather than as a regression.

When something differs, both .root files are reopened and the differing values
printed with the run and event number they occur in; hashes are never shown.
Every differing collection is examined, and --max-collections and --max-entries
limit only what is printed.

## References

A reference is a blessed conversion: ref.digest.json and ref.edm4hep.root, held
under $DELPHI_CI_REFS on the test machine. They are 50-100 MB each and are not
in git. The .root is kept so that differences can be shown as values.

refs.lock is the committed half, recording what was blessed, from which commit,
against which stack, and from which input files. A change to it means a
reference was regenerated, and is reviewed like any other change.

## Files

    converter.py        runs delphi_sdst_pass for one selector
    edm4hep_digest.py   converted file -> digest
    input_manifest.py   converter log -> the input section of a digest
    compare_digest.py   comparison, and the value-level report
    equivalence.py      converts one file two ways and compares the results
    refs_lock.py        maintains refs.lock
    run_tests.py        driver: convert, then bless or compare

uproot, awkward, numpy and pyyaml all come from the key4hep stack.
