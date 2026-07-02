import json

with open(r'C:/Users/tao20/Desktop/Algorithm/AIPlayerDesktop/tools/deep_output/deep_0143.json', encoding='utf-8') as f:
    d = json.load(f)

ds_list = d['debugSteps']
print(f'Total debug steps: {len(ds_list)}')

maze = d['maze']
# Print maze with path overlay from key steps
print('\nMaze with golds(G) traps(T):')
for r in range(15):
    line = ''
    for c in range(15):
        t = str(maze[r][c])
        line += t.replace(' ','.')
    print(f'{r:2d} {line}')

print(f'\nStart: {d["start"]}, Exit: {d["exit"]}\n')

# Print every step from 48 to 85 to understand the gold->trap->exit transition
for ds in ds_list:
    s = ds['step']
    if 48 <= s <= 85:
        pos = f"({ds['real_r']},{ds['real_c']})"
        print(f'--- Step {s}: pos={pos} alpha={ds["alpha"]:.2f} qEff={ds["qEff"]:.2f} obs={ds["observedRatio"]:.3f} dec={ds["decision"]} ---')
        cands = sorted(ds['candidates'], key=lambda x: -x['score'])
        for c in cands[:4]:
            sel = ' <<<' if c['selected'] else ''
            print(f'  ({c["real_r"]},{c["real_c"]}) tile={c["tile"]} score={c["score"]:.2f} dR={c["deltaR"]} I={c["Iproxy"]:.1f} tail={c["tailGain"]:.1f} len={c["pathLen"]} mar={c["margin"]:.1f} pR={c["projectedR"]}{sel}')
        if not cands:
            print('  (no candidates)')
