import shutil, os, subprocess, json, random, sys, time
from pathlib import Path

base = Path(__file__).resolve().parent.parent
tmp_file = base / 'tools' / 'all_tmp_single.json'
exe = base / 'build' / 'one_shot.exe'

gen_file = base / 'data' / 'generated_mazes_1000.json'
with open(str(gen_file), encoding='utf-8') as f:
    raw = json.load(f)

# Build maze list (sorted by key)
maze_keys = sorted(raw.keys(), key=lambda k: int(k.replace('maze', '')))
all_mazes = [(k, raw[k]['maze']) for k in maze_keys]

meta = {
    'C': [[2, 0]], 'B': [11, 13, 9, 15],
    'PlayerSkills': [[8, 4], [2, 0], [4, 2], [6, 3]],
    'L': '54a76d5a60849cbe4a6e7f75d830fe73f413586f329cec620eaf69bea2ade132'
}

random.seed(42)
random.shuffle(all_mazes)

# Split
splits = {'train': (0, 800), 'val': (800, 900), 'test': (900, 1000)}
results = []
failed_mazes = []
start_time = time.time()
timeout_per = 30  # 30s per maze

for sub, (s, e) in splits.items():
    for i in range(s, e):
        idx = i - s + 1
        fname = f'{sub}_{idx:04d}'
        record = {'maze': all_mazes[i][1]}
        record.update(meta)

        # Write single temp file
        with open(str(tmp_file), 'w', encoding='utf-8') as f:
            json.dump(record, f, ensure_ascii=False)

        try:
            r = subprocess.run([str(exe), str(tmp_file)], capture_output=True, text=True, timeout=timeout_per)
            for line in r.stdout.strip().split('\n'):
                line = line.strip()
                if line.startswith('{'):
                    d = json.loads(line)
                    d['file'] = fname
                    results.append(d)
                    break
        except subprocess.TimeoutExpired:
            results.append({'file': fname, 'R': 0, 'L': 999, 'ratio': 0.0,
                          'reached_exit': False, 'golds': 8, 'traps': 4, 'timeout': True})
        except Exception as ex:
            results.append({'file': fname, 'R': 0, 'L': 0, 'ratio': -1,
                          'reached_exit': False, 'golds': 8, 'traps': 4, 'error': str(ex)})

        if i % 50 == 0:
            elapsed = time.time() - start_time
            print(f'{fname}... {len(results)}/{1000} done, {elapsed:.0f}s elapsed')

elapsed = time.time() - start_time
print(f'Done in {elapsed:.0f}s')

results.sort(key=lambda x: x['ratio'])

out_sorted = base / 'data' / 'all_ratio_sorted.jsonl'
out_failed = base / 'data' / 'all_ratio_failed.jsonl'

with open(str(out_sorted), 'w', encoding='utf-8') as f:
    for r in results:
        f.write(json.dumps(r, ensure_ascii=False) + '\n')

failed = [r for r in results if not r.get('reached_exit', False)]
with open(str(out_failed), 'w', encoding='utf-8') as f:
    for r in failed:
        f.write(json.dumps(r, ensure_ascii=False) + '\n')

print(f'Sorted: {out_sorted} ({len(results)} lines)')
print(f'Failed: {out_failed} ({len(failed)} lines)')
if results:
    valid = [r for r in results if r['ratio'] >= 0]
    if valid:
        print(f'Ratio: {valid[0]["ratio"]:.4f} ~ {valid[-1]["ratio"]:.4f}, mean={sum(r["ratio"] for r in valid)/len(valid):.4f}')
print(f'Reached exit: {len(results)-len(failed)}/{len(results)}')
if tmp_file.exists():
    tmp_file.unlink()
print('Done.')
