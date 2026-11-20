#pragma once

constexpr unsigned maxStops = 12, maxPalettes = 8, maxSwatches = 20;
constexpr const char *interpolations[] = {"rgb", "hsv", "shortest", "longest"};
constexpr const char *styles[] = {"instant", "fade", "smooth", "crossfade"};
constexpr const char *curves[] = {"linear", "in", "out", "inout"};
struct Stop { RGB color; uint8_t position; bool enabled; };
struct Gradient {
  Stop stops[maxStops]{{{89,225,255},0,true},{{174,97,255},100,true}};
  uint8_t count = 2, interpolation = 0, position = 50, scale = 100;
  bool reverse = false;
};
struct Transition { uint16_t duration = 0; uint8_t style = 0, curve = 0; };
struct ColorSettings {
  RGB secondary{174,97,255}, previous{89,225,255};
  Gradient gradient;
  Transition transition;
  bool gradientMode = false;
};
struct Palette {
  char id[33], name[65], description[161], category[33];
  uint64_t created;
  RGB colors[12];
  uint8_t count;
  bool favorite;
};
struct HistoryEntry { uint64_t time; RGB color; };
struct Favorite { char name[65]; RGB color; };
struct StudioStorage {
  uint8_t version = 1, palettes = 0, favorites = 0, history = 0;
  char selected[33]{};
  ColorSettings color;
  Palette palette[maxPalettes]{};
  HistoryEntry recent[maxSwatches]{};
  Favorite favorite[maxSwatches]{};
};
StudioStorage studio;
int64_t studioSaveAt = 0, transitionAt = 0;
RGB transitionSource[maxPixels];
unsigned transitionSourceCount = 0;
Transition runningTransition;

bool sameColor(RGB a, RGB b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
void markStudio() { studioSaveAt = esp_timer_get_time() + 5000000; }

void loadStudio() {
  Preferences prefs;
  static StudioStorage saved;
  if (!prefs.begin("lumina-studio", true) || prefs.getBytesLength("phase4") != sizeof(saved) ||
      prefs.getBytes("phase4", &saved, sizeof(saved)) != sizeof(saved)) return;
  const auto &g = saved.color.gradient;
  const auto &t = saved.color.transition;
  if (saved.version != 1 || saved.palettes > maxPalettes || saved.favorites > maxSwatches ||
      saved.history > maxSwatches || !g.count || g.count > maxStops || g.interpolation >= 4 ||
      !g.scale || g.scale > 200 || g.position > 100 || t.style >= 4 || t.curve >= 4 || t.duration > 10000) return;
  for (unsigned i = 0; i < saved.palettes; ++i)
    if (!saved.palette[i].count || saved.palette[i].count > 12) return;
  studio = saved;
}

void persistStudio(int64_t now) {
  if (!studioSaveAt || now < studioSaveAt || transmitting) return;
  studioSaveAt = 0;
  Preferences prefs;
  if (!prefs.begin("lumina-studio", false) || prefs.putBytes("phase4", &studio, sizeof(studio)) != sizeof(studio))
    Serial.println("Studio settings save failed");
}

float unit(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float blend(float a, float b, float t) { return a + (b-a)*t; }
uint8_t byteColor(float v) { return uint8_t(v + 0.5f); }
RGB mixRgb(RGB a, RGB b, float t) {
  return {byteColor(blend(a.r,b.r,t)), byteColor(blend(a.g,b.g,t)), byteColor(blend(a.b,b.b,t))};
}
struct HSV { float h, s, v; };
HSV toHsv(RGB c) {
  const float r=c.r/255.f, g=c.g/255.f, b=c.b/255.f;
  const float hi=fmaxf(r,fmaxf(g,b)), lo=fminf(r,fminf(g,b)), d=hi-lo;
  float h=0;
  if (d) {
    h=(hi==r ? (g-b)/d : hi==g ? 2+(b-r)/d : 4+(r-g)/d)/6;
    if (h<0) h+=1;
  }
  return {h, hi ? d/hi : 0, hi};
}
RGB fromHsv(HSV c) {
  const float h=(c.h-floorf(c.h))*6;
  const unsigned sector=unsigned(h);
  const float f=h-sector, v=c.v*255, p=v*(1-c.s), q=v*(1-c.s*f), t=v*(1-c.s*(1-f));
  const float rgb[][3]={{v,t,p},{q,v,p},{p,v,t},{p,q,v},{t,p,v},{v,p,q}};
  const auto &out=rgb[sector%6];
  return {byteColor(out[0]),byteColor(out[1]),byteColor(out[2])};
}
RGB mixColor(RGB a, RGB b, float t, unsigned method) {
  if (!method) return mixRgb(a,b,t);
  HSV x=toHsv(a), y=toHsv(b);
  if (!x.s) x.h=y.h;
  if (!y.s) y.h=x.h;
  float d=y.h-x.h;
  if (method>=2) {
    if (d>0.5f) d-=1;
    if (d<-0.5f) d+=1;
    if (d==0.5f) d=-0.5f; // Tie: shortest = -180°
    if (d==-0.5f && method==3) d=0.5f; // Longest flips tie
    if (method==3 && d>0 && d<0.5f) d-=1;
    if (method==3 && d<0 && d>-0.5f) d+=1;
  }
  return fromHsv({x.h+d*t,blend(x.s,y.s,t),blend(x.v,y.v,t)});
}
RGB targetColor(unsigned i, unsigned count) {
  if (!studio.color.gradientMode) return light.color;
  const Gradient &g=studio.color.gradient;
  float xRaw=count>1 ? float(i)/(count-1) : 0.5f;
  if (g.reverse) xRaw=1-xRaw;
  const float x=fminf(100, fmaxf(0, (xRaw-0.5f)*10000/g.scale+g.position));
  const Stop *lo=nullptr, *hi=nullptr;
  for (unsigned j=0;j<g.count;++j) {
    const Stop &s=g.stops[j];
    if (!s.enabled) continue;
    if (s.position<=x && (!lo || s.position>=lo->position)) lo=&s;
    if (s.position>x && (!hi || s.position<hi->position)) hi=&s;
  }
  if (!lo) return hi ? hi->color : light.color;
  if (!hi) return lo->color;
  return mixColor(lo->color,hi->color,(x-lo->position)/(hi->position-lo->position),g.interpolation);
}
float transitionProgress(int64_t now) {
  return transitionAt ? unit(float(now-transitionAt)/(1000*runningTransition.duration)) : 1;
}
RGB studioPixel(unsigned i, unsigned count, float progress) {
  const RGB target=targetColor(i,count);
  if (progress>=1 || i>=transitionSourceCount) return target;
  float t=progress;
  switch (runningTransition.curve) {
    case 1: t=t*t; break;
    case 2: t=t*(2-t); break;
    case 3: t=t<0.5f ? 2*t*t : 1-2*(1-t)*(1-t); break;
  }
  const RGB source=transitionSource[i];
  switch (runningTransition.style) {
    case 1: return t<0.5f ? mixRgb(source,RGB{},2*t) : mixRgb(RGB{},target,2*t-1);
    case 2: return mixRgb(source,target,t*t*(3-2*t));
    default: return mixRgb(source,target,t);
  }
}
void captureTransition(int64_t now, const RGB *start) {
  const float progress=transitionProgress(now);
  for (unsigned i=0;i<light.count;++i) transitionSource[i]=start ? *start : studioPixel(i,light.count,progress);
  transitionSourceCount=light.count;
}
void legacySolid(RGB before) {
  if (!sameColor(before, light.color)) { studio.color.previous=before; markStudio(); }
  if (studio.color.gradientMode) { studio.color.gradientMode=false; markStudio(); }
  transitionAt=0;
  dirtyFrame=true;
}
