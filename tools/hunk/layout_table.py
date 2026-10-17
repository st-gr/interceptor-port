#!/usr/bin/env python3
"""layout_table.py <map.json> -> Markdown table of hunks (index, type, address, size)."""
import json
import sys

m = json.load(open(sys.argv[1]))
print("| hunk | type | address | size |")
print("|-----:|------|--------:|-----:|")
for h in m["hunks"]:
    print(f"| {h['index']} | {h['type']} | 0x{h['addr']:06X} | {h['size']} |")
