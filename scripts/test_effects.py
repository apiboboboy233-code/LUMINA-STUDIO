"""Build and exercise the effects engine without Arduino or hardware."""
import json
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
struct RGB { uint8_t r, g, b; };
#include "lumina_effects.h"

bool equal(RGB x, RGB y) { return x.r == y.r && x.g == y.g && x.b == y.b; }
int main() {
  const RGB primary{230, 42, 17}, secondary{15, 121, 243};
  static_assert(Fx::effectCount == 16 && Fx::Solid == 0 && Fx::Gradient == 1, "mode contract");
  static_assert(!Fx::isAnimated(Fx::effectCount), "Invalid mode is not animated");
  for (unsigned mode = 0; mode < Fx::effectCount; ++mode) {
    assert(Fx::isAnimated(mode) == (mode > Fx::Gradient));
    const auto &info = Fx::metadata[mode];
    std::printf("%s\t%s\t%s\t%s\n", info.id, info.name, info.category, info.description);
    auto defaults = Fx::defaults(mode);
    Fx::Params low{}, high{};
    for (auto &value : high.values) value = UINT16_MAX;
    Fx::clampParams(mode, low);
    Fx::clampParams(mode, high);
    for (unsigned j = 0; j < info.paramCount; ++j) {
      const auto &p = info.params[j];
      std::printf("\t%s\t%s\t%u\t%u\t%u\n", p.id, p.name, unsigned(p.min), unsigned(p.max), unsigned(p.defaultValue));
      assert(low.values[j] == p.min && high.values[j] == p.max);
      assert(defaults.values[j] >= p.min && defaults.values[j] <= p.max);
    }
    Fx::Engine plain, wrap;
    plain.update(mode, defaults, 0);
    wrap.update(mode, defaults, UINT32_MAX - 50);
    RGB initial[150];
    for (unsigned i = 0; i < 150; ++i) initial[i] = plain.render(i, 150, primary, secondary);
    bool changed = false;
    for (uint32_t t = 47; t < 7000; t += 47) {
      plain.update(mode, defaults, t);
      wrap.update(mode, defaults, UINT32_MAX - 50 + t);
      assert(equal(plain.render(0, 0, primary, secondary), {}));
      for (auto count : {1u, 150u}) {
        for (unsigned i = 0; i < count; ++i) {
          const auto pixel = plain.render(i, count, primary, secondary);
          assert(equal(pixel, wrap.render(i, count, primary, secondary)));
          if (count == 150) changed |= !equal(pixel, initial[i]);
        }
      }
    }
    assert(changed == (mode > Fx::Gradient));
    plain.reset();
    assert(equal(plain.render(0, 150, primary, secondary), primary));
  }
}
'''


def main():
    with TemporaryDirectory() as tmp:
        source = Path(tmp) / 'test.cpp'
        exe = Path(tmp) / 'test'
        source.write_text(SOURCE)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                        '-Werror', '-I', str(ROOT), str(source), '-o', str(exe)], check=True)
        rows = subprocess.check_output([str(exe)], text=True).splitlines()
    effects = []
    for row in rows:
        if row.startswith('\t'):
            key, name, low, high, default = row[1:].split('\t')
            effects[-1]['params'].append(dict(id=key, name=name, min=int(low), max=int(high), default=int(default)))
        else:
            key, name, category, description = row.split('\t')
            effects.append(dict(id=key, name=name, category=category, description=description, params=[]))
    assert json.loads((ROOT / 'studio/effects-catalog.json').read_text()) == {'effects': effects}
    print('Effects engine and catalog checks passed')


if __name__ == '__main__':
    main()
