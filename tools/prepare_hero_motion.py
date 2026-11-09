#!/usr/bin/env python3
"""Keep Rodin's motion export intact; prepare in-place, single-cycle game clips.

Requires NumPy. Only the audited idle, walk and straight jog are shipped.
The sideways Run2 and obstacle-clearing Jumping stay in the untouched export.
Ordinary jumps use velocity-driven gameplay poses, not a fixed stunt clip.
"""
import json
import struct
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1] / 'art/character-v3/animations'
b = (ROOT / 'hero-stock.glb').read_bytes()
n = struct.unpack_from('<I', b, 12)[0]
d = json.loads(b[20:20+n])
data = bytearray(b[28+n:])

def read(i):
    a = d['accessors'][i]
    v = d['bufferViews'][a['bufferView']]
    return np.frombuffer(data, '<f4', a['count'] * {'SCALAR':1, 'VEC3':3, 'VEC4':4}[a['type']],
                         v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(a['count'], -1).copy()

def append(values, kind):
    values = np.asarray(values, dtype='<f4')
    while len(data) % 4: data.append(0)
    d['bufferViews'].append({'buffer':0, 'byteOffset':len(data), 'byteLength':values.nbytes})
    data.extend(values.tobytes())
    d['accessors'].append({'bufferView':len(d['bufferViews'])-1, 'componentType':5126,
                           'count':len(values), 'type':kind,
                           'min':values.min(axis=0).tolist(), 'max':values.max(axis=0).tolist()})
    return len(d['accessors'])-1

def close_idle_loop(times, values, rotation):
    """Sew both sides of the idle loop with matching value and tangent.

    Changing only the last two keys closes position but leaves a velocity
    discontinuity that kicks attached cloth once each 8.333-second loop.
    A Hermite bridge replaces 0.4 seconds on either side of the wrap, retaining
    the measured pose and tangent where it rejoins the untouched breathing.
    """
    t = times.ravel().astype(np.float64)
    v = values.astype(np.float64).copy()
    if rotation:
        v /= np.linalg.norm(v, axis=1)[:, None]
        for i in range(1, len(v)):
            if np.dot(v[i-1], v[i]) < 0:
                v[i] *= -1
    first = int(np.searchsorted(t, t[0] + .4))
    last = int(np.searchsorted(t, t[-1] - .4, side='right')) - 1
    start, end = v[last].copy(), v[first].copy()
    start_tangent = (v[last+1] - v[last-1]) / (t[last+1] - t[last-1])
    end_tangent = (v[first+1] - v[first-1]) / (t[first+1] - t[first-1])
    if rotation:
        if np.dot(start, end) < 0:
            end, end_tangent = -end, -end_tangent
        start_tangent -= start * np.dot(start, start_tangent)
        end_tangent -= end * np.dot(end, end_tangent)
    duration = t[-1] - t[0]
    span = duration - t[last] + t[first]
    indices = np.r_[np.arange(last, len(v)), np.arange(first+1)]
    wrapped = np.where(indices >= last, t[indices], t[indices] + duration)
    u = (wrapped - t[last]) / span
    h00, h10 = 2*u**3 - 3*u**2 + 1, u**3 - 2*u**2 + u
    h01, h11 = -2*u**3 + 3*u**2, u**3 - u**2
    v[indices] = (h00[:, None] * start + h10[:, None] * span * start_tangent
                  + h01[:, None] * end + h11[:, None] * span * end_tangent)
    if rotation:
        v /= np.linalg.norm(v, axis=1)[:, None]
    v[-1] = v[0]
    return v.astype(np.float32)

accepted = {'HumanSpIdle', 'HumanSpWalking', 'HumanSpJogging'}
d['animations'] = [a for a in d['animations'] if a['name'] in accepted]
for animation in d['animations']:
    loop = True
    for channel in animation['channels']:
        sampler = animation['samplers'][channel['sampler']]
        times, values = read(sampler['input']), read(sampler['output'])
        if animation['name'] == 'HumanSpJogging':
            # The export contains three strides; blend trees need one per loop.
            times, values = times[:27], values[:27]
        if channel['target']['path'] == 'translation' and d['nodes'][channel['target']['node']]['name'] == 'Hips':
            rest = np.array(d['nodes'][channel['target']['node']]['translation'])
            values[:, 0] = rest[0]
            values[:, 2] += rest[2] - values[:, 2].mean()
        if animation['name'] == 'HumanSpIdle':
            values = close_idle_loop(times, values, channel['target']['path'] == 'rotation')
        elif loop:
            # Close the short capture's seam across its final two frames.
            if channel['target']['path'] == 'rotation':
                end = values[0] if np.dot(values[-2], values[0]) >= 0 else -values[0]
                values[-2] = (values[-2] + end) * .5
                values[-2] /= np.linalg.norm(values[-2])
                values[-1] = end
            else:
                values[-2] = (values[-2] + values[0]) * .5
                values[-1] = values[0]
        sampler['input'] = append(times, 'SCALAR')
        sampler['output'] = append(values, 'VEC4' if values.shape[1] == 4 else 'VEC3')

d['buffers'][0]['byteLength'] = len(data)
j = json.dumps(d, separators=(',', ':')).encode()
j += b' ' * (-len(j) % 4)
data += bytes(-len(data) % 4)
out = struct.pack('<III', 0x46546c67, 2, 12+8+len(j)+8+len(data))
out += struct.pack('<II', len(j), 0x4e4f534a) + j + struct.pack('<II', len(data), 0x004e4942) + data
(ROOT / 'hero-motion.glb').write_bytes(out)
settings = json.loads((ROOT / 'hero-stock.anim.json').read_text())
settings['clips'] = {k: v for k, v in settings['clips'].items() if k in accepted}
for name, clip in settings['clips'].items():
    clip.pop('rootMotion', None)
    if name == 'HumanSpJogging': clip['events'] = clip['events'][:2]
(ROOT / 'hero-motion.anim.json').write_text(json.dumps(settings, indent=2)+'\n')
print('Prepared three audited in-place clips; sideways run and obstacle jump excluded.')
