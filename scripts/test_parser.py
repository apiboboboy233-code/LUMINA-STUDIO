"""Test the production request parser using a native C++ compiler."""
from pathlib import Path
import os
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'LUMINA_STUDIO.ino').read_text()
parser = source[source.index('void skipSpace('):source.index('esp_err_t putLed(')]
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp) / 'test.cpp'
    path.write_text('#include <cassert>\n#include <cstring>\n' + parser + r'''
int main() {
  Field rgb[]={{"r",'n',0,{}},{"g",'n',0,{}},{"b",'n',0,{}}};
  assert(parseFields(R"({"g":80,"r":255,"b":0})",rgb,3));
  assert(rgb[0].n==255 && rgb[1].n==80 && rgb[2].n==0);
  for (auto bad : {R"({"r":256,"g":0,"b":0})",R"({"r":1,"r":0,"b":0})",
    R"({"r":-1,"g":0,"b":0})",R"({"r":1.5,"g":0,"b":0})",R"({"r":0,"g":0,"b":0}!)",
    R"({"r":01,"g":0,"b":0})",R"({"r":1,"g":0})",R"({"r":1,"g":0,"b":"0"})"})
    assert(!parseFields(bad,rgb,3));
  Field power[]={{"power",'b',0,{}}};
  assert(parseFields(R"({"power":true})",power,1) && power[0].n);
  assert(!parseFields(R"({"power":1})",power,1));
  Field order[]={{"color_order",'s',0,{}}};
  assert(parseFields(R"({"color_order":"GRB"})",order,1));
  assert(!parseFields(R"({"color_order":"too-long-for-buffer"})",order,1));
}
'''.replace('int main()', '#include <initializer_list>\nint main()'))
    exe = Path(temp) / 'test'
    subprocess.run([os.getenv('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', path, '-o', exe], check=True)
    subprocess.run([exe], check=True)
    print('LED request parser tests passed')
