#!/usr/bin/env python3
"""Check the public Git index before committing a development-workspace sync."""
from pathlib import Path
from d_code_checks.public_distribution import check_public_distribution

if __name__ == "__main__":
    check_public_distribution(Path(__file__).resolve().parents[3])
    print("Public distribution: PASS (private records excluded; calendar notices retained)")
