#!/usr/bin/env python3
"""Lays out the SD card folder: <out>/switch/sh4-nx/ with the NRO, the exe and the game's data folders.

Usage: make_sd.py <out dir> --game "<PC game folder>" [--movies]
Files already present and the same size are not copied again. movie/ (the FMVs, 620 MB) is skipped unless
--movies is given: the port cannot play them yet.
"""
import argparse, os, shutil

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def copy(src, dst):
    if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src):
        return 0
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)  # no copystat: MTP (DBI) refuses file times
    return 1


def copy_tree(src, dst):
    n = 0
    for dp, _, fs in os.walk(src):
        for f in fs:
            n += copy(os.path.join(dp, f), os.path.join(dst, os.path.relpath(os.path.join(dp, f), src)))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--game', default=os.path.join(ROOT, 'game'))
    ap.add_argument('--movies', action='store_true')
    a = ap.parse_args()
    dst = os.path.join(a.out, 'switch', 'sh4-nx')
    n = copy(os.path.join(ROOT, 'build', 'switch', 'sh4-nx.nro'), os.path.join(dst, 'sh4-nx.nro'))
    n += copy(os.path.join(a.game, 'SILENT HILL 4.exe'), os.path.join(dst, 'sh4.exe'))
    for f in ('input.ini', 'sh4_caps.dat'):
        if os.path.exists(os.path.join(a.game, f)):
            n += copy(os.path.join(a.game, f), os.path.join(dst, f))
    for d in ('data', 'demo', 'sound') + (('movie',) if a.movies else ()):
        n += copy_tree(os.path.join(a.game, d), os.path.join(dst, d))
    os.makedirs(os.path.join(dst, 'save'), exist_ok=True)
    print(f'{n} files copied to {dst}')


if __name__ == '__main__':
    main()
