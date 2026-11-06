#!/usr/bin/env python3
"""Moves untranslated strings of qmlui_<lang>.ts out to JSON and translated JSON back in.

    tsjson.py export <lang> [out.json]   # unfinished messages -> [{id, ctx, src, comment?, plural?}]
    tsjson.py apply  <lang> in.json      # {id: "text"} or {id: ["form1", ...]} -> .ts (validated)

Ids are "<context>|<source>|<comment>". apply checks placeholders (%1, %n, %L1...), HTML tags,
'&' mnemonics and the number of plural forms; bad entries are skipped and listed.
"""
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
PH = re.compile(r"%L?\d+|%n")
TAG = re.compile(r"</?[a-zA-Z][^>]*>")


def ts_path(lang):
    return HERE / f"qmlui_{lang}.ts"


def msg_id(ctx, msg):
    return "|".join([ctx, msg.findtext("source") or "", msg.findtext("comment") or ""])


def plural_forms(tree):
    # lupdate writes the right number of empty <numerusform> for the language
    for msg in tree.iter("message"):
        if msg.get("numerus") == "yes":
            return len(msg.find("translation").findall("numerusform"))
    return 1


def iter_messages(tree):
    for ctx in tree.getroot().iter("context"):
        name = ctx.findtext("name")
        for msg in ctx.findall("message"):
            yield name, msg


def export(lang, out):
    tree = ET.parse(ts_path(lang))
    forms = plural_forms(tree)
    items = []
    for ctx, msg in iter_messages(tree):
        tr = msg.find("translation")
        if tr.get("type") != "unfinished":
            continue
        item = {"id": msg_id(ctx, msg), "ctx": ctx, "src": msg.findtext("source")}
        if msg.findtext("comment"):
            item["comment"] = msg.findtext("comment")
        if msg.get("numerus") == "yes":
            item["plural_forms"] = forms
        items.append(item)
    data = json.dumps(items, ensure_ascii=False, indent=1)
    if out:
        Path(out).write_text(data, encoding="utf-8")
    else:
        print(data)
    print(f"{lang}: {len(items)} unfinished", file=sys.stderr)


def problems(src, text):
    errs = []
    if sorted(PH.findall(src)) != sorted(PH.findall(text)):
        errs.append(f"placeholders {PH.findall(src)} vs {PH.findall(text)}")
    if sorted(TAG.findall(src)) != sorted(TAG.findall(text)):
        errs.append("html tags differ")
    if not text.strip():
        errs.append("empty")
    return errs


def apply(lang, path):
    tree = ET.parse(ts_path(lang))
    forms = plural_forms(tree)
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if isinstance(data, list):  # [{id, text}] also accepted
        data = {d["id"]: d.get("text", d.get("translation")) for d in data}
    done, bad = 0, []
    for ctx, msg in iter_messages(tree):
        mid = msg_id(ctx, msg)
        if mid not in data:
            continue
        src = msg.findtext("source")
        val = data[mid]
        tr = msg.find("translation")
        if msg.get("numerus") == "yes":
            vals = val if isinstance(val, list) else [val]
            if len(vals) == 1 and forms > 1:
                vals = vals * forms
            if len(vals) != forms:
                bad.append((mid, f"{len(vals)} plural forms, need {forms}"))
                continue
            # %n may be dropped in a singular-only form in some languages; check the last form only
            errs = problems(src, vals[-1])
            if errs:
                bad.append((mid, "; ".join(errs)))
                continue
            for f in list(tr):
                tr.remove(f)
            for v in vals:
                ET.SubElement(tr, "numerusform").text = v
            tr.text = None
        else:
            if isinstance(val, list):
                val = val[0]
            errs = problems(src, val)
            if errs:
                bad.append((mid, "; ".join(errs)))
                continue
            tr.text = val
        tr.attrib.pop("type", None)
        done += 1
    ET.indent(tree, space="    ")
    body = ET.tostring(tree.getroot(), encoding="unicode")
    ts_path(lang).write_text('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + body + "\n", encoding="utf-8")
    print(f"{lang}: applied {done}, rejected {len(bad)}")
    for mid, why in bad:
        print(f"  ! {mid}: {why}")


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in ("export", "apply"):
        sys.exit(__doc__)
    if sys.argv[1] == "export":
        export(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    else:
        apply(sys.argv[2], sys.argv[3])
