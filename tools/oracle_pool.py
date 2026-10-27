"""Runs oracle work on several processes, each with its own emulator, and reports progress.

    results = oracle_pool.run(items, init, work, jobs=8, label="act")

init() runs once in each worker and returns its state (an Emulator set up for the job);
work(state, item) runs for each item and returns a picklable result. Both must be
module-level functions so that worker processes can import them. Results come back in
the order of items.
"""
import multiprocessing
import os
import sys
import time

_state = None
_work = None


def _initialize(init, work):
    global _state, _work
    _state = init()
    _work = work


def _run(item):
    return _work(_state, item)


def default_jobs():
    return max(1, (os.cpu_count() or 2) - 1)


def run(items, init, work, jobs=None, label="items"):
    items = list(items)
    jobs = jobs or default_jobs()
    start = time.time()
    results = []
    with multiprocessing.get_context("spawn").Pool(jobs, _initialize, (init, work)) as pool:
        for k, result in enumerate(pool.imap(_run, items, chunksize=4), start=1):
            results.append(result)
            if k % 100 == 0 or k == len(items):
                elapsed = time.time() - start
                print(f"\r{label}: {k}/{len(items)}, {elapsed:.0f} s", end="", file=sys.stderr, flush=True)
    print(file=sys.stderr)
    return results
