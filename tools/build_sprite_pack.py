"""Build optional 32/64 sprite packs from a supplied artwork recipe.

Requires Pillow and NumPy only at build time. Recipe entries contain domain,
keys, source (relative to --root), sha256, optional kind='wall', and an
optional cell=[column,row,columns,rows] for an evenly divided source atlas,
or crop=[left,top,right,bottom] for explicitly measured source bounds.
Only explicitly listed, hash-matched inputs are used. No artwork is supplied
with the public source. Copyright (c) 2026 John Horton; source terms: LICENSE.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re

import numpy as np
from PIL import Image


def sha(data):
    return hashlib.sha256(data).hexdigest()


def portrait(image, size):
    """Foreground-only box averages, hard alpha, one transparent pixel margin."""
    rgba = np.array(image.convert('RGBA'), dtype=np.uint8)
    mask = rgba[:, :, 3] >= 128
    ys, xs = np.nonzero(mask)
    result = np.zeros((size, size, 4), dtype=np.uint8)
    if not len(xs):
        return Image.fromarray(result)
    rgba = rgba[ys.min():ys.max()+1, xs.min():xs.max()+1]
    mask = rgba[:, :, 3] >= 128
    h, w = mask.shape
    scale = (size - 2) / max(w, h)
    tw, th = max(1, round(w * scale)), max(1, round(h * scale))
    # Summed-area tables make each output pixel independent of source size.
    values = np.concatenate((rgba[:, :, :3].astype(np.uint64) * mask[:, :, None],
                             mask[:, :, None].astype(np.uint64)), axis=2)
    sums = np.pad(values.cumsum(0).cumsum(1), ((1, 0), (1, 0), (0, 0)))
    x0 = np.arange(tw) * w // tw
    x1 = np.maximum(x0 + 1, np.arange(1, tw+1) * w // tw)
    y0 = np.arange(th) * h // th
    y1 = np.maximum(y0 + 1, np.arange(1, th+1) * h // th)
    totals = (sums[y1[:, None], x1] + sums[y0[:, None], x0]
              - sums[y0[:, None], x1] - sums[y1[:, None], x0])
    count = totals[:, :, 3]
    area = (y1-y0)[:, None] * (x1-x0)
    opaque = (count > 0) & (count * 100 >= area * 12)
    tile = np.zeros((th, tw, 4), dtype=np.uint8)
    tile[:, :, :3] = (totals[:, :, :3] // np.maximum(1, count[:, :, None])).astype(np.uint8)
    tile[:, :, 3] = opaque * 255
    tile[~opaque] = 0
    oy, ox = (size-th)//2, (size-tw)//2
    result[oy:oy+th, ox:ox+tw] = tile
    return Image.fromarray(result)


def wall(image, size):
    rgb = np.array(image.convert('RGB').resize((size, size), Image.Resampling.BOX), dtype=np.uint16)
    rgb[:, 0] = rgb[:, -1] = (rgb[:, 0] + rgb[:, -1]) // 2
    rgb[0] = rgb[-1] = (rgb[0] + rgb[-1]) // 2
    return Image.fromarray(rgb.astype(np.uint8)).convert('RGBA')


def source_cell(image, cell=None, crop=None):
    """Extract an explicit atlas cell; reject invalid or empty rectangles."""
    if crop is not None:
        if (cell is not None or not isinstance(crop, list) or len(crop) != 4
                or any(type(v) is not int for v in crop)):
            raise ValueError('Use either a cell or an integer crop rectangle')
        left, top, right, bottom = crop
        if not (0 <= left < right <= image.width and 0 <= top < bottom <= image.height):
            raise ValueError('Sprite crop outside source atlas')
        return image.crop(tuple(crop))
    if cell is None:
        return image
    if (not isinstance(cell, list) or len(cell) != 4
            or any(type(v) is not int for v in cell)):
        raise ValueError('Sprite cell must be [column,row,columns,rows]')
    x, y, cols, rows = cell
    if (cols <= 0 or rows <= 0 or not 0 <= x < cols or not 0 <= y < rows
            or cols > image.width or rows > image.height):
        raise ValueError('Sprite cell outside source atlas')
    return image.crop((x*image.width//cols, y*image.height//rows,
                       (x+1)*image.width//cols, (y+1)*image.height//rows))


def build(root, entries):
    seen = set()
    output, provenance = {}, []
    groups = {domain: [] for domain in ('monster', 'object', 'terrain')}
    for entry in entries:
        domain = entry['domain']
        if domain not in groups or not entry['keys']:
            raise ValueError('Invalid sprite domain or empty keys')
        for key in entry['keys']:
            if not re.fullmatch('[a-z0-9]+(?:-[a-z0-9]+)*', key) or len(key) > 140:
                raise ValueError('Invalid sprite key: ' + key)
            if (domain, key) in seen:
                raise ValueError('Duplicate sprite key: ' + key)
            seen.add((domain, key))
        source = (root / entry['source']).resolve()
        source.relative_to(root.resolve())
        data = source.read_bytes()
        if sha(data) != entry['sha256']:
            raise ValueError('Source changed: ' + entry['source'])
        if entry.get('kind', 'portrait') not in ('portrait', 'wall'):
            raise ValueError('Invalid sprite conversion kind')
        with Image.open(io.BytesIO(data)) as image:
            image = source_cell(image, entry.get('cell'), entry.get('crop'))
            converter = wall if entry.get('kind') == 'wall' else portrait
            tiles = {size: converter(image, size) for size in (32, 64)}
        groups[domain].append((entry['keys'], tiles))
        provenance.append({k: entry[k] for k in ('domain', 'keys', 'sha256', 'cell', 'crop', 'kind') if k in entry})
    for size in (32, 64):
        manifest = []
        for domain, items in groups.items():
            for start in range(0, len(items), 1024):
                page = items[start:start+1024]
                columns = min(32, len(page))
                sheet = Image.new('RGBA', (columns*size, ((len(page)+columns-1)//columns)*size))
                index = []
                for cell, (keys, tiles) in enumerate(page):
                    x, y = cell % columns, cell // columns
                    sheet.paste(tiles[size], (x*size, y*size))
                    index += [f'{key} {x} {y}\n' for key in keys]
                stem = f'{domain}-{start//1024+1}'
                image_name, index_name = f'{stem}-{size}.png', f'{stem}.txt'
                image_bytes = io.BytesIO()
                sheet.save(image_bytes, format='PNG')
                output[image_name] = image_bytes.getvalue()
                output[index_name] = ''.join(index).encode('ascii')
                manifest.append(f'{domain} {image_name} {index_name}\n')
        output[f'pack-{size}.txt'] = ''.join(manifest).encode('ascii')
    output['inventory.json'] = (json.dumps({'sources': provenance,
        'files': {name: sha(data) for name, data in sorted(output.items())}}, indent=2) + '\n').encode()
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('.'))
    parser.add_argument('--recipe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = build(args.root, json.loads(args.recipe.read_text())['entries'])
    if not args.check:
        args.out.mkdir(parents=True, exist_ok=True)
    for name, data in output.items():
        target = args.out / name
        if args.check:
            if not target.is_file() or target.read_bytes() != data:
                raise SystemExit('Sprite output differs: ' + name)
        else:
            target.write_bytes(data)
    print(f'{len(output)} sprite pack files {"verified" if args.check else "built"}')


if __name__ == '__main__':
    main()
