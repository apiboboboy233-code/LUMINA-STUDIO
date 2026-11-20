#pragma once

enum EffectAction { SetEffect, SetParameters, ResetEffect, StopEffect };
EffectAction effectActions[] = {SetEffect, SetParameters, ResetEffect, StopEffect};

namespace EffectParser {
bool number(const char *&p, unsigned &v, unsigned max = 65535) {
  skipSpace(p);
  if (*p < '0' || *p > '9' || (*p == '0' && p[1] >= '0' && p[1] <= '9')) return false;
  v = 0;
  while (*p >= '0' && *p <= '9') {
    const unsigned u = v * 10 + unsigned(*p++ - '0');
    if (u > max) return false;
    v = u;
  }
  return true;
}

bool effectObject(const char *&p, EffectSettings &s) {
  if (!take(p, '{')) return false;
  char effectID[32]{};
  unsigned transMs = s.transitionMs;
  bool seenEffect = false, seenTrans = false;
  for (unsigned i = 0; i < 2; ++i) {
    if (i && !take(p, ',')) break;
    char key[16];
    if (!readJsonString(p, key, sizeof(key)) || !take(p, ':')) return false;
    if (!strcmp(key, "effect")) {
      if (seenEffect || !readJsonString(p, effectID, sizeof(effectID))) return false;
      seenEffect = true;
    } else if (!strcmp(key, "transition_ms")) {
      if (seenTrans || !number(p, transMs, maxEffectTransition)) return false;
      seenTrans = true;
    } else return false;
  }
  if (!take(p, '}') || !seenEffect) return false;
  int idx = -1;
  for (unsigned j = 0; j < Fx::effectCount; ++j)
    if (!strcmp(Fx::metadata[j].id, effectID)) { idx = int(j); break; }
  if (idx < 0 || idx == int(Fx::Gradient)) return false;
  s.mode = uint8_t(idx);
  s.transitionMs = uint16_t(transMs);
  return true;
}

bool parametersObject(const char *&p, EffectSettings &s, bool &seenFavorite, char favID[33]) {
  if (!take(p, '{')) return false;
  Fx::Params params = s.parameters;
  unsigned fps = s.fps, transMs = s.transitionMs;
  seenFavorite = false;
  unsigned paramsSeen = 0;
  while (true) {
    char key[16];
    if (!readJsonString(p, key, sizeof(key)) || !take(p, ':')) return false;
    if (!strcmp(key, "speed")) {
      unsigned v; if (!number(p, v, 10000) || (paramsSeen & 1)) return false;
      params.values[0] = v; paramsSeen |= 1;
    } else if (!strcmp(key, "intensity")) {
      unsigned v; if (!number(p, v, 10000) || (paramsSeen & 2)) return false;
      params.values[1] = v; paramsSeen |= 2;
    } else if (!strcmp(key, "scale")) {
      unsigned v; if (!number(p, v, 10000) || (paramsSeen & 4)) return false;
      params.values[2] = v; paramsSeen |= 4;
    } else if (!strcmp(key, "fps")) {
      if (!number(p, fps, maxEffectFps)) return false;
      fps = fps < minEffectFps ? minEffectFps : fps;
    } else if (!strcmp(key, "transition_ms")) {
      if (!number(p, transMs, maxEffectTransition)) return false;
    } else if (!strcmp(key, "favorite")) {
      if (seenFavorite || !take(p, '{')) return false;
      char fkey[12];
      if (!readJsonString(p, fkey, sizeof(fkey)) || strcmp(fkey, "effect") || !take(p, ':') ||
          !readJsonString(p, favID, 33) || !take(p, ',') || !readJsonString(p, fkey, sizeof(fkey)) ||
          strcmp(fkey, "enabled") || !take(p, ':')) return false;
      skipSpace(p);
      bool enabled = false;
      if (!strncmp(p, "true", 4)) { p += 4; enabled = true; }
      else if (strncmp(p, "false", 5)) return false; else p += 5;
      if (!take(p, '}')) return false;
      seenFavorite = enabled;
    } else return false;
    if (!take(p, ',')) break;
  }
  if (!take(p, '}')) return false;
  s.parameters = params;
  s.fps = uint8_t(fps);
  s.transitionMs = uint16_t(transMs);
  return true;
}
}  // namespace EffectParser

esp_err_t getEffects(httpd_req_t *r) {
  static char body[4096];
  unsigned n = snprintf(body, sizeof(body), DATA "\"effects\":[");
  for (unsigned i = 0; i < Fx::effectCount; ++i) {
    const auto &fx = Fx::metadata[i];
    n += snprintf(body + n, sizeof(body) - n,
      "%s{\"id\":\"%s\",\"name\":\"%s\",\"description\":\"%s\",\"category\":\"%s\","
      "\"preview\":\"\",\"directional\":false,\"colors\":\"%s\",\"parameters\":[",
      i ? "," : "", fx.id, fx.name, fx.description, fx.category,
      i == Fx::Gradient ? "none" : i == Fx::Rainbow ? "none" : i <= Fx::Breathe ? "single" : "dual");
    for (unsigned j = 0; j < fx.paramCount; ++j) {
      const auto &p = fx.params[j];
      n += snprintf(body + n, sizeof(body) - n, "%s{\"id\":\"%s\",\"name\":\"%s\","
        "\"min\":%u,\"max\":%u,\"default\":%u}",
        j ? "," : "", p.id, p.name, unsigned(p.min), unsigned(p.max), unsigned(p.defaultValue));
    }
    n += snprintf(body + n, sizeof(body) - n, "]}");
  }
  n += snprintf(body + n, sizeof(body) - n, "]" END);
  return json(r, "200 OK", body);
}

esp_err_t getEffectState(httpd_req_t *r) {
  if (!ledMutex || xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    return failure(r, "503 Service Unavailable", "LED engine busy");
  const unsigned mode = activeEffect();
  const auto &fx = Fx::metadata[mode];
  const Fx::Params &params = effectSettings.parameters;
  static char body[768], favBits[96];
  unsigned n = snprintf(body, sizeof(body), DATA "\"effect\":\"%s\",\"parameters\":{", fx.id);
  for (unsigned j = 0; j < fx.paramCount; ++j) {
    const auto &p = fx.params[j];
    n += snprintf(body + n, sizeof(body) - n, "%s\"%s\":%u",
      j ? "," : "", p.id, unsigned(params.values[j]));
  }
  n += snprintf(body + n, sizeof(body) - n,
    "},\"fps\":%u,\"transition_ms\":%u,\"favorites\":[",
    unsigned(effectSettings.fps), unsigned(effectSettings.transitionMs));
  unsigned favCount = 0;
  for (unsigned i = 0; i < Fx::effectCount; ++i) {
    if (effectSettings.favorites & (1UL << i)) {
      n += snprintf(body + n, sizeof(body) - n, "%s\"%s\"",
        favCount++ ? "," : "", Fx::metadata[i].id);
    }
  }
  const bool running = effectRunning();
  const int64_t now = esp_timer_get_time();
  const float progress = transitionProgress(now);
  const bool complete = progress >= 1 && (!transitionAt || now - transitionAt > 1000 * runningTransition.duration);
  n += snprintf(body + n, sizeof(body) - n,
    "],\"running\":%s,\"power\":%s,\"led_count\":%u,\"brightness\":%u,\"max_brightness\":%u,"
    "\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"secondary\":{\"r\":%u,\"g\":%u,\"b\":%u},"
    "\"hardware_ready\":%s,\"transition_complete\":%s" END,
    boolean(running), boolean(light.power), light.count, light.brightness, light.limit,
    unsigned(light.color.r), unsigned(light.color.g), unsigned(light.color.b),
    unsigned(studio.color.secondary.r), unsigned(studio.color.secondary.g), unsigned(studio.color.secondary.b),
    boolean(ready), boolean(complete));
  xSemaphoreGive(ledMutex);
  return json(r, "200 OK", body);
}

esp_err_t postEffect(httpd_req_t *r) {
  static char body[512];
  char type[48], host[24];
  if (httpd_req_get_hdr_value_str(r, "Host", host, sizeof(host)) != ESP_OK ||
      (strcmp(host, "192.168.4.1") && strcmp(host, "192.168.4.1:80")))
    return failure(r, "403 Forbidden", "Use IP");
  if (r->content_len >= sizeof(body))
    return failure(r, "413 Content Too Large", "Body limit 512B");
  if (httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof(type)) != ESP_OK ||
      (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8")))
    return failure(r, "415 Unsupported Media Type", "JSON required");
  size_t received = 0;
  const int64_t deadline = esp_timer_get_time() + 1500000;
  while (received < r->content_len) {
    if (esp_timer_get_time() >= deadline) return failure(r, "408 Request Timeout", "Body timeout");
    const int n = httpd_req_recv(r, body + received, r->content_len - received);
    if (n <= 0) return failure(r, "408 Request Timeout", "Incomplete body");
    received += size_t(n);
  }
  body[received] = 0;
  if (memchr(body, 0, received)) return failure(r, "400 Bad Request", "Embedded null");

  const EffectAction action = r->user_ctx ? *static_cast<const EffectAction *>(r->user_ctx) : SetEffect;
  const char *p = body;
  EffectSettings edit = effectSettings;

  if (action == ResetEffect || action == StopEffect) {
    skipSpace(p);
    if (strcmp(p, "{}")) return failure(r, "400 Bad Request", "Expected empty object");
    edit.mode = action == ResetEffect ? effectSettings.mode : uint8_t(Fx::Solid);
    edit.parameters = Fx::defaults(edit.mode);
  } else if (action == SetEffect) {
    if (!EffectParser::effectObject(p, edit))
      return failure(r, "400 Bad Request", "Invalid effect or transition_ms");
    skipSpace(p);
    if (*p) return failure(r, "400 Bad Request", "Trailing content");
    edit.parameters = Fx::defaults(edit.mode);
  } else {
    bool favSeen = false;
    char favID[33]{};
    if (!EffectParser::parametersObject(p, edit, favSeen, favID))
      return failure(r, "400 Bad Request", "Invalid parameters");
    skipSpace(p);
    if (*p) return failure(r, "400 Bad Request", "Trailing content");
    if (favSeen) {
      int favIdx = -1;
      for (unsigned j = 0; j < Fx::effectCount; ++j)
        if (!strcmp(Fx::metadata[j].id, favID)) { favIdx = int(j); break; }
      if (favIdx >= 0) {
        edit.favorites ^= 1UL << favIdx;
        markEffect();
      }
    }
  }

  if (!ledMutex || xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    return failure(r, "503 Service Unavailable", "LED engine busy");
  if (!ready) { xSemaphoreGive(ledMutex); return failure(r, "503 Service Unavailable", "LED unavailable"); }

  Fx::clampParams(edit.mode, edit.parameters);
  const bool changed = edit.mode != effectSettings.mode ||
    memcmp(&edit.parameters, &effectSettings.parameters, sizeof(edit.parameters)) ||
    edit.fps != effectSettings.fps || edit.transitionMs != effectSettings.transitionMs;

  if (changed || action == StopEffect) {
    if (action == StopEffect) {
      studio.color.gradientMode = false;
      selectStudioGradient();
    }
    if (Fx::isAnimated(edit.mode) && studio.color.gradientMode) {
      studio.color.gradientMode = false;
      markStudio();
    }
    if (changed) {
      captureTransition(esp_timer_get_time(), nullptr);
      effectSettings = edit;
      effectEngine.reset();
      runningTransition.duration = edit.transitionMs;
      runningTransition.style = edit.transitionMs ? 0 : 0;
      runningTransition.curve = 0;
      transitionAt = edit.transitionMs ? esp_timer_get_time() : 0;
      dirtyFrame = true;
      markEffect();
    }
  }

  xSemaphoreGive(ledMutex);
  return getEffectState(r);
}
