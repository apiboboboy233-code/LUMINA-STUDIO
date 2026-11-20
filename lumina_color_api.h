#pragma once

bool skipSpace(const char *&p) { while (*p && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) ++p; return true; }
bool take(const char *&p,char c) { skipSpace(p); if (*p!=c) return false; ++p; return true; }
bool takeQuote(const char *&p) { return take(p,'"'); }
bool takeMember(const char *&p) { return takeQuote(p); }
bool word(const char *&p,char *out,size_t cap) {
  if (!takeQuote(p)) return false;
  size_t n=0;
  while (*p && *p!='"') {
    if (n+1>=cap || *p<32 || *p=='\\') return false;
    out[n++]=*p++;
  }
  out[n]=0;
  return *p && *p++=='"';
}
bool keyword(const char *&p,const char *expected) {
  char buf[24];
  if (!word(p,buf,sizeof(buf))||strcmp(buf,expected)) return false;
  return true;
}
bool studioNumber(const char *&p,unsigned &v) {
  skipSpace(p);
  if (*p<'0'||*p>'9'||(*p=='0'&&p[1]>='0'&&p[1]<='9')) return false;
  v=0;
  while (*p>='0'&&*p<='9') { const unsigned u=v*10+unsigned(*p++-'0'); if (u>65535) return false; v=u; }
  return true;
}
bool boolean(const char *&p,bool &v) {
  skipSpace(p);
  if (!strncmp(p,"true",4)) { p+=4; v=true; return true; }
  if (!strncmp(p,"false",5)) { p+=5; v=false; return true; }
  return false;
}
bool hexColor(const char *&p,RGB &c) {
  skipSpace(p);
  if (!takeQuote(p)||*p!='#') return false;
  ++p; char hex[7]{}; for (unsigned i=0;i<6;++i) {
    if (!strchr("0123456789abcdefABCDEF",p[i])) return false;
    hex[i]=p[i];
  }
  p+=6;
  if (!takeQuote(p)) return false;
  const unsigned long v=strtoul(hex,nullptr,16);
  c={uint8_t(v>>16),uint8_t(v>>8),uint8_t(v)};
  return true;
}
bool rgbObject(const char *&p,RGB &c) {
  if (!take(p,'{')) return false;
  unsigned r,g,b; bool ok=false;
  for (unsigned i=0;i<3;++i) {
    if (i&&!take(p,',')) return false;
    if (!takeMember(p)||!take(p,':')) return false;
    const char *key=p-2;
    unsigned *dest=*key=='r'?&r:*key=='g'?&g:*key=='b'?&b:nullptr;
    if (!dest||!studioNumber(p,*dest)||*dest>255) return false;
    if (i==2) ok=true;
  }
  if (!ok||!take(p,'}')) return false;
  c={uint8_t(r),uint8_t(g),uint8_t(b)};
  return true;
}
bool stopObject(const char *&p,Stop &s) {
  if (!take(p,'{')) return false;
  unsigned pos=0; bool foundColor=false, foundPos=false, foundEn=false;
  s.enabled=true;
  for (unsigned i=0;i<3;++i) {
    if (i&&!take(p,',')) return false;
    char key[16];
    if (!word(p,key,sizeof(key))||!take(p,':')) return false;
    if (!strcmp(key,"color")) {
      if (foundColor||!rgbObject(p,s.color)) return false;
      foundColor=true;
    } else if (!strcmp(key,"position")) {
      if (foundPos||!studioNumber(p,pos)||pos>100) return false;
      s.position=pos; foundPos=true;
    } else if (!strcmp(key,"enabled")) {
      if (foundEn||!boolean(p,s.enabled)) return false;
      foundEn=true;
    } else return false;
  }
  return take(p,'}')&&foundColor&&foundPos;
}
bool gradientObject(const char *&p,Gradient &g) {
  if (!take(p,'{')) return false;
  g={};
  g.count=0; g.interpolation=0; g.position=50; g.scale=100; g.reverse=false;
  unsigned members=0;
  while (members<6) {
    if (members&&!take(p,',')) break;
    char key[16];
    if (!word(p,key,sizeof(key))||!take(p,':')) return false;
    if (!strcmp(key,"stops")) {
      if (!take(p,'[')) return false;
      for (unsigned i=0;i<maxStops;++i) {
        if (i&&!take(p,',')) break;
        if (!stopObject(p,g.stops[g.count])) return false;
        ++g.count;
      }
      if (!take(p,']')) return false;
    } else if (!strcmp(key,"interpolation")) {
      char val[12];
      if (!word(p,val,sizeof(val))) return false;
      const int idx=lookup(val,interpolations,4);
      if (idx<0) return false;
      g.interpolation=unsigned(idx);
    } else if (!strcmp(key,"reverse")) {
      if (!boolean(p,g.reverse)) return false;
    } else if (!strcmp(key,"position")) {
      unsigned v; if (!studioNumber(p,v)||v>100) return false;
      g.position=v;
    } else if (!strcmp(key,"scale")) {
      unsigned v; if (!studioNumber(p,v)||!v||v>200) return false;
      g.scale=v;
    } else return false;
    ++members;
  }
  return take(p,'}')&&g.count>0;
}
bool transitionObject(const char *&p,Transition &t) {
  if (!take(p,'{')) return false;
  t={}; unsigned members=0;
  while (members<3) {
    if (members&&!take(p,',')) break;
    char key[12];
    if (!word(p,key,sizeof(key))||!take(p,':')) return false;
    if (!strcmp(key,"duration")) {
      unsigned v; if (!studioNumber(p,v)||v>10000) return false;
      t.duration=v;
    } else if (!strcmp(key,"style")) {
      char val[12];
      if (!word(p,val,sizeof(val))) return false;
      const int idx=lookup(val,styles,4);
      if (idx<0) return false;
      t.style=unsigned(idx);
    } else if (!strcmp(key,"curve")) {
      char val[12];
      if (!word(p,val,sizeof(val))) return false;
      const int idx=lookup(val,curves,4);
      if (idx<0) return false;
      t.curve=unsigned(idx);
    } else return false;
    ++members;
  }
  return take(p,'}');
}
bool paletteObject(const char *&p,Palette &pal) {
  if (!take(p,'{')) return false;
  pal={};
  while (true) {
    char key[16];
    if (!word(p,key,sizeof(key))||!take(p,':')) return false;
    if (!strcmp(key,"id")) { if (!word(p,pal.id,sizeof(pal.id))) return false; }
    else if (!strcmp(key,"name")) { if (!word(p,pal.name,sizeof(pal.name))) return false; }
    else if (!strcmp(key,"description")) { if (!word(p,pal.description,sizeof(pal.description))) return false; }
    else if (!strcmp(key,"category")) { if (!word(p,pal.category,sizeof(pal.category))) return false; }
    else if (!strcmp(key,"favorite")) { if (!boolean(p,pal.favorite)) return false; }
    else if (!strcmp(key,"created")) {
      skipSpace(p);
      if (*p<'0'||*p>'9') return false;
      while (*p>='0'&&*p<='9') {
        if (pal.created>900719925474099ULL) return false;
        pal.created=pal.created*10+unsigned(*p++-'0');
      }
    }
    else if (!strcmp(key,"colors")) {
      if (!take(p,'[')) return false;
      pal.count=0;
      while (pal.count<12) {
        if (pal.count&&!take(p,',')) break;
        if (!hexColor(p,pal.colors[pal.count])) return false;
        ++pal.count;
      }
      if (!take(p,']')) return false;
    } else return false;
    if (!take(p,',')) break;
  }
  return take(p,'}')&&pal.id[0]&&pal.name[0]&&pal.count;
}

esp_err_t getColor(httpd_req_t *r) {
  if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","LED engine busy");
  static char body[8192];
  const ColorSettings &c=studio.color;
  const Gradient &g=c.gradient;
  unsigned n=snprintf(body,sizeof(body),DATA "\"primary\":{\"r\":%u,\"g\":%u,\"b\":%u},"
    "\"secondary\":{\"r\":%u,\"g\":%u,\"b\":%u},\"previous\":{\"r\":%u,\"g\":%u,\"b\":%u},"
    "\"mode\":\"%s\",\"gradient\":{\"stops\":[",
    unsigned(light.color.r),unsigned(light.color.g),unsigned(light.color.b),
    unsigned(c.secondary.r),unsigned(c.secondary.g),unsigned(c.secondary.b),
    unsigned(c.previous.r),unsigned(c.previous.g),unsigned(c.previous.b),
    c.gradientMode?"gradient":"solid");
  for (unsigned i=0;i<g.count;++i) {
    const Stop &s=g.stops[i];
    n+=snprintf(body+n,sizeof(body)-n,"%s{\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"position\":%u,\"enabled\":%s}",
      i?",":"",unsigned(s.color.r),unsigned(s.color.g),unsigned(s.color.b),unsigned(s.position),boolean(s.enabled));
  }
  n+=snprintf(body+n,sizeof(body)-n,"],\"interpolation\":\"%s\",\"reverse\":%s,\"position\":%u,\"scale\":%u},"
    "\"transition\":{\"style\":\"%s\",\"duration\":%u,\"curve\":\"%s\"}",
    interpolations[g.interpolation],boolean(g.reverse),unsigned(g.position),unsigned(g.scale),
    styles[c.transition.style],unsigned(c.transition.duration),curves[c.transition.curve]);
  const int64_t now=esp_timer_get_time();
  const float progress=transitionProgress(now);
  const bool complete=progress>=1 && (!transitionAt || now-transitionAt>1000*runningTransition.duration);
  n+=snprintf(body+n,sizeof(body)-n,",\"frames\":[");
  for (unsigned i=0;i<light.count;++i) {
    const RGB px=completedPixels[i];
    n+=snprintf(body+n,sizeof(body)-n,"%s{\"r\":%u,\"g\":%u,\"b\":%u}",i?",":"",unsigned(px.r),unsigned(px.g),unsigned(px.b));
  }
  n+=snprintf(body+n,sizeof(body)-n,"],\"transition_complete\":%s",boolean(complete));
  snprintf(body+n,sizeof(body)-n,END);
  xSemaphoreGive(ledMutex);
  return json(r,"200 OK",body);
}

esp_err_t postColor(httpd_req_t *r) {
  static char body[3072]; char type[48], host[24];
  if (httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK||
    (strcmp(host,"192.168.4.1")&&strcmp(host,"192.168.4.1:80")))
    return failure(r,"403 Forbidden","Use IP");
  if (r->content_len>=sizeof(body)) return failure(r,"413 Content Too Large","Body limit 3KB");
  if (httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||
    (strcmp(type,"application/json")&&strcmp(type,"application/json; charset=utf-8")))
    return failure(r,"415 Unsupported Media Type","JSON required");
  size_t received=0;
  const int64_t deadline=esp_timer_get_time()+1500000;
  while (received<r->content_len) {
    if (esp_timer_get_time()>=deadline) return failure(r,"408 Request Timeout","Body read timeout");
    const int n=httpd_req_recv(r,body+received,r->content_len-received);
    if (n<=0) return failure(r,"408 Request Timeout","Incomplete body");
    received+=size_t(n);
  }
  body[received]=0;
  if (memchr(body,0,received)) return failure(r,"400 Bad Request","Embedded null");
  const char *p=body;
  if (!take(p,'{')) return failure(r,"400 Bad Request","Expected object");
  unsigned channelsSeen=0; bool record=false, isGradient=false;
  RGB primary{}; ColorSettings cs=studio.color;
  Transition trans={};
  while (true) {
    char key[16];
    if (!word(p,key,sizeof(key))||!take(p,':')) return failure(r,"400 Bad Request","Invalid key");
    if (!strcmp(key,"r")||!strcmp(key,"g")||!strcmp(key,"b")) {
      unsigned v; if (!studioNumber(p,v)||v>255) return failure(r,"400 Bad Request","RGB 0-255");
      if (*key=='r') primary.r=v; else if (*key=='g') primary.g=v; else primary.b=v;
      const unsigned bit=*key=='r'?1:*key=='g'?2:4;
      if (channelsSeen & bit) return failure(r,"400 Bad Request","Duplicate RGB field");
      channelsSeen |= bit;
    } else if (!strcmp(key,"record")) {
      if (!boolean(p,record)) return failure(r,"400 Bad Request","Boolean required");
    } else if (!strcmp(key,"secondary")) {
      if (!rgbObject(p,cs.secondary)) return failure(r,"400 Bad Request","Invalid secondary");
    } else if (!strcmp(key,"gradient")) {
      if (!gradientObject(p,cs.gradient)) return failure(r,"400 Bad Request","Invalid gradient");
      isGradient=true;
    } else if (!strcmp(key,"transition")) {
      if (!transitionObject(p,trans)) return failure(r,"400 Bad Request","Invalid transition");
    } else return failure(r,"400 Bad Request","Unknown field");
    if (!take(p,',')) break;
  }
  if (!take(p,'}')) return failure(r,"400 Bad Request","Trailing content");
  skipSpace(p); if (*p) return failure(r,"400 Bad Request","Garbage after object");
  if (channelsSeen!=7) return failure(r,"400 Bad Request","RGB required");
  if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","LED engine busy");
  if (!ready) { xSemaphoreGive(ledMutex); return failure(r,"503 Service Unavailable","LED unavailable"); }
  const RGB before=light.color;
  if (record && !sameColor(before,primary)) {
    for (unsigned i=studio.history<maxSwatches ? studio.history : maxSwatches-1;i>0;--i) studio.recent[i]=studio.recent[i-1];
    studio.recent[0]={uint64_t(esp_timer_get_time()/1000),before};
    if (studio.history<maxSwatches) ++studio.history;
  }
  const bool colorChange=!sameColor(light.color,primary);
  const bool modeChange=cs.gradientMode!=isGradient;
  if (colorChange||modeChange||isGradient||trans.duration||trans.style||!sameColor(cs.secondary,studio.color.secondary)) {
    captureTransition(esp_timer_get_time(),nullptr);
    cs.transition=trans;
    saveAt=esp_timer_get_time()+5000000;
    if (!modeChange&&!cs.gradientMode) cs.previous=before;
    light.color=primary;
    cs.gradientMode=isGradient;
    studio.color=cs;
    if (trans.duration && trans.style) {
      runningTransition=trans;
      transitionAt=esp_timer_get_time();
    } else transitionAt=0;
    dirtyFrame=true;
    markStudio();
  }
  xSemaphoreGive(ledMutex);
  return getColor(r);
}

void writeHex(char *out,RGB c) { sprintf(out,"#%02X%02X%02X",c.r,c.g,c.b); }
esp_err_t getPalettes(httpd_req_t *r) {
  if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","Busy");
  static char body[6144];
  unsigned n=snprintf(body,sizeof(body),DATA "\"palettes\":[");
  for (unsigned i=0;i<studio.palettes;++i) {
    const Palette &pal=studio.palette[i];
    n+=snprintf(body+n,sizeof(body)-n,"%s{\"id\":\"%s\",\"name\":\"%s\",\"description\":\"%s\","
      "\"category\":\"%s\",\"favorite\":%s,\"created\":%llu,\"colors\":[",
      i?",":"",pal.id,pal.name,pal.description,pal.category,boolean(pal.favorite),
      static_cast<unsigned long long>(pal.created));
    for (unsigned j=0;j<pal.count;++j) {
      char hex[8]; writeHex(hex,pal.colors[j]);
      n+=snprintf(body+n,sizeof(body)-n,"%s\"%s\"",j?",":"",hex);
    }
    n+=snprintf(body+n,sizeof(body)-n,"]}");
  }
  n+=snprintf(body+n,sizeof(body)-n,"],\"selected\":\"%s\"" END,studio.selected);
  xSemaphoreGive(ledMutex);
  return json(r,"200 OK",body);
}

esp_err_t postPalettes(httpd_req_t *r) {
  char body[2560], type[48], host[24];
  if (httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK||
    (strcmp(host,"192.168.4.1")&&strcmp(host,"192.168.4.1:80")))
    return failure(r,"403 Forbidden","Use IP");
  if (r->content_len>=sizeof(body)) return failure(r,"413 Content Too Large","Body limit");
  if (httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||
    (strcmp(type,"application/json")&&strcmp(type,"application/json; charset=utf-8")))
    return failure(r,"415 Unsupported Media Type","JSON required");
  size_t received=0;
  const int64_t deadline=esp_timer_get_time()+1500000;
  while (received<r->content_len) {
    if (esp_timer_get_time()>=deadline) return failure(r,"408 Request Timeout","Read timeout");
    const int n=httpd_req_recv(r,body+received,r->content_len-received);
    if (n<=0) return failure(r,"408 Request Timeout","Incomplete");
    received+=size_t(n);
  }
  body[received]=0;
  if (memchr(body,0,received)) return failure(r,"400 Bad Request","Null byte");
  const char *p=body;
  if (!take(p,'{')) return failure(r,"400 Bad Request","Object required");
  char key[16];
  if (!word(p,key,sizeof(key))||!take(p,':')) return failure(r,"400 Bad Request","Missing key");
  if (!strcmp(key,"selected")) {
    char id[33];
    if (!word(p,id,sizeof(id))||!take(p,'}')) return failure(r,"400 Bad Request","ID required");
    if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
      return failure(r,"503 Service Unavailable","Busy");
    strncpy(studio.selected,id,sizeof(studio.selected)-1);
    markStudio();
    xSemaphoreGive(ledMutex);
    return getPalettes(r);
  }
  Palette pal;
  const char *saved=p-strlen(key)-3;
  if (!paletteObject(saved,pal)) return failure(r,"400 Bad Request","Invalid palette");
  if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","Busy");
  unsigned slot=studio.palettes;
  for (unsigned i=0;i<studio.palettes;++i)
    if (!strcmp(studio.palette[i].id,pal.id)) { slot=i; break; }
  if (slot==studio.palettes && studio.palettes>=maxPalettes)
    { xSemaphoreGive(ledMutex); return failure(r,"400 Bad Request","Max 8 palettes"); }
  studio.palette[slot]=pal;
  if (slot==studio.palettes) ++studio.palettes;
  markStudio();
  xSemaphoreGive(ledMutex);
  return getPalettes(r);
}

esp_err_t deletePalettes(httpd_req_t *r) {
  char body[128], type[48], host[24];
  if (httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK||
    (strcmp(host,"192.168.4.1")&&strcmp(host,"192.168.4.1:80")))
    return failure(r,"403 Forbidden","Use IP");
  if (r->content_len>=sizeof(body)) return failure(r,"413 Content Too Large","Body limit");
  if (httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||
    (strcmp(type,"application/json")&&strcmp(type,"application/json; charset=utf-8")))
    return failure(r,"415 Unsupported Media Type","JSON required");
  size_t received=0;
  const int64_t deadline=esp_timer_get_time()+1500000;
  while (received<r->content_len) {
    if (esp_timer_get_time()>=deadline) return failure(r,"408 Request Timeout","Read timeout");
    const int n=httpd_req_recv(r,body+received,r->content_len-received);
    if (n<=0) return failure(r,"408 Request Timeout","Incomplete");
    received+=size_t(n);
  }
  body[received]=0;
  if (memchr(body,0,received)) return failure(r,"400 Bad Request","Null byte");
  const char *p=body;
  char id[33];
  if (!take(p,'{') || !keyword(p,"id") || !take(p,':') || !word(p,id,sizeof(id)) || !take(p,'}'))
    return failure(r,"400 Bad Request","Expected {\"id\":\"...\"}");
  if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","Busy");
  unsigned slot=studio.palettes;
  for (unsigned i=0;i<studio.palettes;++i) if (!strcmp(studio.palette[i].id,id)) { slot=i; break; }
  if (slot==studio.palettes)
    { xSemaphoreGive(ledMutex); return failure(r,"404 Not Found","Palette not found"); }
  for (unsigned i=slot;i+1<studio.palettes;++i) studio.palette[i]=studio.palette[i+1];
  --studio.palettes;
  if (!strcmp(studio.selected,id)) studio.selected[0]=0;
  markStudio();
  xSemaphoreGive(ledMutex);
  return getPalettes(r);
}

esp_err_t getHistory(httpd_req_t *r) {
  if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","Busy");
  char body[1024];
  unsigned n=snprintf(body,sizeof(body),DATA "\"history\":[");
  for (unsigned i=0;i<studio.history;++i) {
    const HistoryEntry &e=studio.recent[i];
    char hex[8]; writeHex(hex,e.color);
    n+=snprintf(body+n,sizeof(body)-n,"%s{\"hex\":\"%s\",\"time\":%llu}",
      i?",":"",hex,static_cast<unsigned long long>(e.time));
  }
  snprintf(body+n,sizeof(body)-n,"]" END);
  xSemaphoreGive(ledMutex);
  return json(r,"200 OK",body);
}

esp_err_t postHistory(httpd_req_t *r) {
  char body[128], type[48], host[24];
  if (httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK||
    (strcmp(host,"192.168.4.1")&&strcmp(host,"192.168.4.1:80")))
    return failure(r,"403 Forbidden","Use IP");
  if (r->content_len>=sizeof(body)) return failure(r,"413 Content Too Large","Limit");
  if (httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||
    (strcmp(type,"application/json")&&strcmp(type,"application/json; charset=utf-8")))
    return failure(r,"415 Unsupported Media Type","JSON required");
  size_t received=0;
  const int64_t deadline=esp_timer_get_time()+1500000;
  while (received<r->content_len) {
    if (esp_timer_get_time()>=deadline) return failure(r,"408 Request Timeout","Timeout");
    const int n=httpd_req_recv(r,body+received,r->content_len-received);
    if (n<=0) return failure(r,"408 Request Timeout","Incomplete");
    received+=size_t(n);
  }
  body[received]=0;
  if (memchr(body,0,received)) return failure(r,"400 Bad Request","Null");
  const char *p=body;
  if (!take(p,'{') || !keyword(p,"action") || !take(p,':')) return failure(r,"400 Bad Request","action required");
  char act[12];
  if (!word(p,act,sizeof(act))) return failure(r,"400 Bad Request","Invalid action");
  if (!strcmp(act,"clear")) {
    if (!take(p,'}')) return failure(r,"400 Bad Request","Unexpected fields");
    if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
      return failure(r,"503 Service Unavailable","Busy");
    studio.history=0;
    markStudio();
    xSemaphoreGive(ledMutex);
    return getHistory(r);
  } else if (!strcmp(act,"remove")) {
    if (!take(p,',') || !keyword(p,"hex") || !take(p,':')) return failure(r,"400 Bad Request","hex required");
    RGB target;
    if (!hexColor(p,target) || !take(p,'}')) return failure(r,"400 Bad Request","Invalid hex");
    if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
      return failure(r,"503 Service Unavailable","Busy");
    unsigned slot=studio.history;
    for (unsigned i=0;i<studio.history;++i)
      if (sameColor(studio.recent[i].color,target)) { slot=i; break; }
    if (slot<studio.history) {
      for (unsigned i=slot;i+1<studio.history;++i) studio.recent[i]=studio.recent[i+1];
      --studio.history;
      markStudio();
    }
    xSemaphoreGive(ledMutex);
    return getHistory(r);
  }
  return failure(r,"400 Bad Request","Unknown action");
}

esp_err_t getFavorites(httpd_req_t *r) {
  if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","Busy");
  static char body[4096];
  unsigned n=snprintf(body,sizeof(body),DATA "\"favorites\":[");
  for (unsigned i=0;i<studio.favorites;++i) {
    const Favorite &f=studio.favorite[i];
    char hex[8]; writeHex(hex,f.color);
    n+=snprintf(body+n,sizeof(body)-n,"%s{\"hex\":\"%s\",\"name\":\"%s\"}",
      i?",":"",hex,f.name);
  }
  snprintf(body+n,sizeof(body)-n,"]" END);
  xSemaphoreGive(ledMutex);
  return json(r,"200 OK",body);
}

esp_err_t postFavorites(httpd_req_t *r) {
  char body[256], type[48], host[24];
  if (httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK||
    (strcmp(host,"192.168.4.1")&&strcmp(host,"192.168.4.1:80")))
    return failure(r,"403 Forbidden","Use IP");
  if (r->content_len>=sizeof(body)) return failure(r,"413 Content Too Large","Limit");
  if (httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||
    (strcmp(type,"application/json")&&strcmp(type,"application/json; charset=utf-8")))
    return failure(r,"415 Unsupported Media Type","JSON required");
  size_t received=0;
  const int64_t deadline=esp_timer_get_time()+1500000;
  while (received<r->content_len) {
    if (esp_timer_get_time()>=deadline) return failure(r,"408 Request Timeout","Timeout");
    const int n=httpd_req_recv(r,body+received,r->content_len-received);
    if (n<=0) return failure(r,"408 Request Timeout","Incomplete");
    received+=size_t(n);
  }
  body[received]=0;
  if (memchr(body,0,received)) return failure(r,"400 Bad Request","Null");
  const char *p=body;
  if (!take(p,'{') || !keyword(p,"hex") || !take(p,':')) return failure(r,"400 Bad Request","hex required");
  RGB target;
  if (!hexColor(p,target)) return failure(r,"400 Bad Request","Invalid hex");
  bool remove=false;
  char name[65]={};
  if (take(p,',')) {
    char key[12];
    if (!word(p,key,sizeof(key)) || !take(p,':')) return failure(r,"400 Bad Request","Invalid field");
    if (!strcmp(key,"name")) {
      if (!word(p,name,sizeof(name))) return failure(r,"400 Bad Request","Invalid name");
    } else if (!strcmp(key,"remove")) {
      if (!boolean(p,remove)) return failure(r,"400 Bad Request","Boolean required");
    } else return failure(r,"400 Bad Request","Unknown field");
  }
  if (!take(p,'}')) return failure(r,"400 Bad Request","Trailing content");
  if (!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","Busy");
  unsigned slot=studio.favorites;
  for (unsigned i=0;i<studio.favorites;++i)
    if (sameColor(studio.favorite[i].color,target)) { slot=i; break; }
  if (remove) {
    if (slot<studio.favorites) {
      for (unsigned i=slot;i+1<studio.favorites;++i) studio.favorite[i]=studio.favorite[i+1];
      --studio.favorites;
      markStudio();
    }
  } else {
    if (slot==studio.favorites) {
      if (studio.favorites>=maxSwatches)
        { xSemaphoreGive(ledMutex); return failure(r,"400 Bad Request","Max 20 favorites"); }
      ++studio.favorites;
    }
    studio.favorite[slot]={};
    strncpy(studio.favorite[slot].name,name[0]?name:"Favorite",sizeof(studio.favorite[slot].name)-1);
    studio.favorite[slot].color=target;
    markStudio();
  }
  xSemaphoreGive(ledMutex);
  return getFavorites(r);
}
