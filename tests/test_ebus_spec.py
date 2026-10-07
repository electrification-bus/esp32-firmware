"""Validate .ebus-spec.json, the specification-provenance lockfile at the repo root.

tests/ebus-spec.schema.json is copied from the JSON Schema in the specification's
conventions/spec-provenance.md; refresh it when that convention changes.
"""

import json
from pathlib import Path

import jsonschema

REPO = Path(__file__).resolve().parents[1]
LOCKFILE = REPO / ".ebus-spec.json"
SCHEMA = Path(__file__).resolve().parent / "ebus-spec.schema.json"


def _load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def test_lockfile_matches_schema():
    schema = _load(SCHEMA)
    validator = jsonschema.Draft202012Validator(
        schema, format_checker=jsonschema.Draft202012Validator.FORMAT_CHECKER
    )
    errors = sorted(validator.iter_errors(_load(LOCKFILE)), key=lambda e: list(e.path))
    assert not errors, "\n".join(f"{list(e.path)}: {e.message}" for e in errors)


def test_library_declares_mechanism_axis():
    lock = _load(LOCKFILE)
    assert lock["role"] == "library"
    assert lock["framework"]
    assert lock["supports"], "a library lists the framework features it implements"
    assert len(set(lock["supports"])) == len(lock["supports"]), "duplicate feature id"
