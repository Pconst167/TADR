#!/usr/bin/env python3
"""Verify the release feature choice and exclusion of cancelled move arrows."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('dll', type=Path)
parser.add_argument('--enabled', required=True, type=int, choices=(0, 1))
args = parser.parse_args()
data = args.dll.read_bytes()
marker = b'[UnitStatsPanel] build=bottom-bar-v1'
assert (marker in data) == bool(args.enabled), 'Incorrect unit statistics feature selection'
assert b'[MoveOrderFeedback]' not in data, 'Cancelled movement arrows must remain excluded'
print('Unit statistics feature selection verified (enabled=%d)' % args.enabled)
