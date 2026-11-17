#!/usr/bin/env python3
"""Validate the selected combined-construction feature in the native DLL."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('dll',type=Path)
p.add_argument('--enabled',type=int,choices=(0,1),required=True)
a=p.parse_args();data=a.dll.read_bytes()
assert (b'[MultiBuild] build=combined-v1' in data)==bool(a.enabled), 'Wrong combined-construction feature'
assert b'[MainMapRadar]' not in data, 'Cancelled radar feature must remain absent'
print('Verified combined construction:',a.enabled)
