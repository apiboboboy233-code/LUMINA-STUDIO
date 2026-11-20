"""Build the editable, dependency-free browser preview from the studio modules."""
from pathlib import Path
import sys
import json

ROOT = Path(__file__).resolve().parents[1]


def bundle():
    studio = ROOT / 'studio'
    def read(name):
        return (studio / name).read_text(encoding='utf-8')
    base = read('base.html')
    base = base.replace('<!-- studio workspaces -->', '\n'.join(read(name) for name in ('workspace.html', 'effects.html', 'library.html')), 1)
    base = base.replace('</main><script>', '<style>\n' + read('views.css') + '\n</style></main><script>', 1)
    base = base.replace('</main><script>', '</main><script>\n' + read('color.js') + '\n</script><script>', 1)
    catalog = json.loads(read('effects-catalog.json'))
    if isinstance(catalog, dict):
        catalog = catalog['effects']
    catalog_js = json.dumps(catalog, ensure_ascii=True, separators=(',', ':')).replace('<', '\\u003c')
    scripts = [read('workspace.js'), read('library.js'), 'const EFFECT_CATALOG=' + catalog_js + ';', read('effects.js')]
    return base.replace('</body>', ''.join('<script>\n' + script + '\n</script>' for script in scripts) + '</body>', 1)


if __name__ == '__main__':
    output = ROOT / 'LUMINA_STUDIO.html'
    text = bundle().encode('utf-8')
    if '--check' in sys.argv:
        if output.read_bytes() != text:
            raise SystemExit('Standalone HTML is stale; run python3 scripts/bundle.py')
    else:
        output.write_bytes(text)
