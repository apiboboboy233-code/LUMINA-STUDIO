/* Color Library module: palette CRUD, favorites, history, curated collections */
const StudioLibrary = (() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const STORAGE_VERSION = 1;
  const LIMITS = { palettes: 8, paletteColors: 12, favorites: 20, history: 20 };
  
  // Curated palettes (12 collections)
  const CURATED = [
    { id: 'curated-ocean', name: 'Ocean Depths', description: 'Cool blues and teals', category: 'Nature', colors: ['#0A192F', '#1E3A5F', '#2E5984', '#59E1FF', '#7DD3FC', '#A5F3FC', '#CFFAFE', '#E0F2FE', '#F0F9FF', '#DBEAFE', '#BFDBFE', '#93C5FD'] },
    { id: 'curated-sunset', name: 'Sunset Blaze', description: 'Warm oranges and pinks', category: 'Nature', colors: ['#4A1A1A', '#7C2D12', '#9A3412', '#EA580C', '#FB923C', '#FDBA74', '#FED7AA', '#FFEDD5', '#FEF3C7', '#FDE68A', '#FCD34D', '#FBBF24'] },
    { id: 'curated-forest', name: 'Forest Canopy', description: 'Earth greens and browns', category: 'Nature', colors: ['#14532D', '#166534', '#15803D', '#16A34A', '#22C55E', '#4ADE80', '#86EFAC', '#BBF7D0', '#DCFCE7', '#D1FAE5', '#A7F3D0', '#6EE7B7'] },
    { id: 'curated-cyber', name: 'Cyberpunk Night', description: 'Neon on dark', category: 'Synthetic', colors: ['#0F0F23', '#1A1A2E', '#16213E', '#0F3460', '#533483', '#E94560', '#FF2E63', '#FF1654', '#FF006E', '#FB5607', '#FFBE0B', '#8338EC'] },
    { id: 'curated-pastel', name: 'Soft Pastels', description: 'Gentle hues', category: 'Gentle', colors: ['#FFF5F7', '#FFE4E6', '#FECDD3', '#FDA4AF', '#F0ABFC', '#E9D5FF', '#DDD6FE', '#C7D2FE', '#DBEAFE', '#BAE6FD', '#A5F3FC', '#CCFBF1'] },
    { id: 'curated-mono', name: 'Monochrome Scale', description: 'Grayscale progression', category: 'Neutral', colors: ['#000000', '#171717', '#262626', '#404040', '#525252', '#737373', '#A3A3A3', '#D4D4D4', '#E5E5E5', '#F5F5F5', '#FAFAFA', '#FFFFFF'] },
    { id: 'curated-fire', name: 'Flame', description: 'Heat gradient', category: 'Nature', colors: ['#450A0A', '#7F1D1D', '#991B1B', '#B91C1C', '#DC2626', '#EF4444', '#F87171', '#FCA5A5', '#FECACA', '#FEE2E2', '#FEF2F2', '#FFF5F5'] },
    { id: 'curated-lavender', name: 'Lavender Fields', description: 'Purple spectrum', category: 'Gentle', colors: ['#3B0764', '#581C87', '#6B21A8', '#7E22CE', '#9333EA', '#A855F7', '#C084FC', '#D8B4FE', '#E9D5FF', '#F3E8FF', '#FAF5FF', '#FDF4FF'] },
    { id: 'curated-tech', name: 'Tech Interface', description: 'UI-friendly palette', category: 'Synthetic', colors: ['#0C1222', '#1A2332', '#2D3748', '#4A5568', '#59E1FF', '#63B3ED', '#4299E1', '#3182CE', '#2B6CB0', '#2C5282', '#2A4365', '#1A365D'] },
    { id: 'curated-desert', name: 'Desert Sands', description: 'Warm earth tones', category: 'Nature', colors: ['#78350F', '#92400E', '#B45309', '#D97706', '#F59E0B', '#FBBF24', '#FCD34D', '#FDE68A', '#FEF3C7', '#FFFBEB', '#FEF8E6', '#FEF5DC'] },
    { id: 'curated-ice', name: 'Arctic Ice', description: 'Cool whites and blues', category: 'Nature', colors: ['#1E3A8A', '#1E40AF', '#1D4ED8', '#2563EB', '#3B82F6', '#60A5FA', '#93C5FD', '#BFDBFE', '#DBEAFE', '#EFF6FF', '#F0F9FF', '#FFFFFF'] },
    { id: 'curated-neon', name: 'Neon Signs', description: 'Vibrant electric colors', category: 'Synthetic', colors: ['#FF00FF', '#FF00AA', '#FF0080', '#FF0055', '#FF3366', '#FF6699', '#00FFFF', '#00FFAA', '#00FF55', '#AAFF00', '#FFFF00', '#FFAA00'] }
  ];
  
  let storage = loadStorage();
  let selectedPaletteId = storage.selected || null;
  
  // Storage interface
  function loadStorage() {
    try {
      const raw = localStorage.getItem('lumina-library');
      if (!raw) return initStorage();
      const data = JSON.parse(raw);
      return data?.version === STORAGE_VERSION ? normalize(data) : initStorage();
    } catch {
      return initStorage();
    }
  }
  
  function initStorage() {
    return { version: STORAGE_VERSION, palettes: [], selected: null, favorites: [], history: [] };
  }
  
  function saveStorage() {
    try {
      localStorage.setItem('lumina-library', JSON.stringify(storage));
      return true;
    } catch (err) {
      throw new Error('Browser storage is unavailable; changes may not survive a reload');
    }
  }
  
  const isDemo = () => typeof demo !== 'undefined' ? demo : ['file:', 'content:'].includes(location.protocol);

  async function deviceRequest(path, method = 'GET', body) {
    if (isDemo()) return simulateDevice(path, method, body);
    return Studio.request(path, method, body === undefined ? undefined : JSON.stringify(body));
  }

  async function reload(path) {
    const data = await deviceRequest(path);
    const key = path.split('/').pop();
    if (!Array.isArray(data?.[key])) throw new Error('Invalid library response');
    storage = normalize({ ...storage, [key]: data[key], ...(key === 'palettes' ? { selected: data.selected } : {}) });
    selectedPaletteId = storage.selected;
    renderAll();
  }

  function normalize(data) {
    const list = v => Array.isArray(v) ? v : [];
    const text = (v, n) => typeof v === 'string' ? v.slice(0, n) : '';
    const hex = v => ColorEngine.rgbToHex(ColorEngine.hexToRgb(v));
    const id = v => typeof v === 'string' && /^[\w-]{1,64}$/.test(v) ? v : null;
    return {
      version: STORAGE_VERSION, selected: id(data.selected),
      palettes: list(data.palettes).filter(p => id(p?.id)).slice(0, LIMITS.palettes).map(p => ({
        id: p.id, name: text(p.name, 32), description: text(p.description, 96), category: text(p.category, 24),
        favorite: !!p.favorite, created: Number.isFinite(p.created) ? p.created : 0,
        colors: list(p.colors).slice(0, LIMITS.paletteColors).map(hex).filter(Boolean)
      })).filter(p => p.colors.length),
      favorites: list(data.favorites).filter(f => hex(f?.hex)).slice(0, LIMITS.favorites).map(f => ({ hex: hex(f.hex), name: text(f.name, 32) })),
      history: list(data.history).filter(h => hex(h?.hex)).slice(0, LIMITS.history).map(h => ({ hex: hex(h.hex), time: Number.isFinite(h.time) ? h.time : 0 }))
    };
  }
  
  function simulateDevice(path, method, body) {
    if (path === 'palettes') {
      if (method === 'GET') return { palettes: storage.palettes, selected: selectedPaletteId };
      if (method === 'POST' && body?.selected !== undefined) {
        selectedPaletteId = body.selected;
        storage.selected = selectedPaletteId;
        saveStorage();
        return { selected: selectedPaletteId };
      }
      if (method === 'POST' && body?.id) {
        const idx = storage.palettes.findIndex(p => p.id === body.id);
        const palette = { ...body, created: body.created || Date.now() };
        if (idx >= 0) storage.palettes[idx] = palette;
        else if (storage.palettes.length < LIMITS.palettes) storage.palettes.push(palette);
        else throw new Error(`Maximum ${LIMITS.palettes} user palettes`);
        saveStorage();
        return palette;
      }
      if (method === 'DELETE' && body?.id) {
        storage.palettes = storage.palettes.filter(p => p.id !== body.id);
        if (storage.selected === body.id) storage.selected = selectedPaletteId = null;
        saveStorage();
        return { id: body.id };
      }
    }
    if (path === 'color/history') {
      if (method === 'GET') return { history: storage.history };
      if (method === 'POST' && body?.action === 'clear') {
        storage.history = [];
        saveStorage();
        return { history: [] };
      }
      if (method === 'POST' && body?.action === 'remove' && body?.hex) {
        storage.history = storage.history.filter(h => h.hex.toUpperCase() !== body.hex.toUpperCase());
        saveStorage();
        return { history: storage.history };
      }
    }
    if (path === 'color/favorites') {
      if (method === 'GET') return { favorites: storage.favorites };
      if (method === 'POST' && body?.hex) {
        const hex = body.hex.toUpperCase();
        if (body.remove) {
          storage.favorites = storage.favorites.filter(f => f.hex !== hex);
        } else {
          storage.favorites = storage.favorites.filter(f => f.hex !== hex);
          if (storage.favorites.length >= LIMITS.favorites) storage.favorites.shift();
          storage.favorites.push({ hex, name: body.name || '' });
        }
        saveStorage();
        return { favorites: storage.favorites };
      }
    }
    throw new Error('Unknown library endpoint');
  }
  
  // UI helpers
  function status(msg, error = false) {
    const el = $('library-status');
    clearTimeout(status.timer);
    el.textContent = msg;
    el.style.color = error ? '#ffb1a6' : '';
    if (msg) status.timer = setTimeout(() => el.textContent = '', error ? 5000 : 3000);
  }
  
  function parseColors(input) {
    const tokens = input.trim().split(/[\s,]+/).filter(Boolean);
    const colors = [];
    for (const token of tokens) {
      const hex = ColorEngine.hexToRgb(token);
      if (!hex) throw new Error(`Invalid hex: ${token}`);
      colors.push(ColorEngine.rgbToHex(hex));
    }
    if (colors.length < 1 || colors.length > LIMITS.paletteColors) {
      throw new Error(`Palettes need 1–${LIMITS.paletteColors} colors`);
    }
    return colors;
  }
  
  function generateId(prefix) {
    return `${prefix}-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`;
  }
  
  // Rendering
  function renderPalettes() {
    const container = $('library-palettes');
    if (!storage.palettes.length) {
      container.innerHTML = '<p>No user palettes yet. Create one to get started.</p>';
      return;
    }
    container.replaceChildren(...storage.palettes.map(p => {
      const card = document.createElement('div');
      card.className = 'library-card';
      const selected = p.id === selectedPaletteId ? ' [Selected]' : '';
      card.innerHTML = `
        <h4>${escapeHtml(p.name)}${selected}</h4>
        ${p.description ? `<p>${escapeHtml(p.description)}</p>` : ''}
        ${p.category ? `<p><small>Category: ${escapeHtml(p.category)}</small></p>` : ''}
        <div class="library-swatches">${p.colors.map(hex => `<button type="button" class="library-swatch" style="background:${hex}" data-hex="${hex}" title="${hex}" aria-label="Apply ${hex}"></button>`).join('')}</div>
        <div class="library-actions">
          <button type="button" data-action="select" data-id="${p.id}">Select</button>
          <button type="button" data-action="gradient" data-id="${p.id}">Load gradient</button>
          <button type="button" data-action="edit" data-id="${p.id}">Edit</button>
          <button type="button" data-action="delete" data-id="${p.id}">Delete</button>
          ${p.favorite ? '⭐' : ''}
        </div>
      `;
      return card;
    }));
  }
  
  function renderCurated() {
    const container = $('library-curated');
    container.replaceChildren(...CURATED.map(p => {
      const card = document.createElement('div');
      card.className = 'library-card';
      card.innerHTML = `
        <h4>${escapeHtml(p.name)}</h4>
        <p>${escapeHtml(p.description)}</p>
        <div class="library-swatches">${p.colors.map(hex => `<button type="button" class="library-swatch" style="background:${hex}" data-hex="${hex}" title="${hex}" aria-label="Apply ${hex}"></button>`).join('')}</div>
        <div class="library-actions">
          <button type="button" data-action="gradient" data-id="${p.id}">Load gradient</button>
          <button type="button" data-action="copy-curated" data-id="${p.id}">Copy to My Palettes</button>
        </div>
      `;
      return card;
    }));
  }
  
  function renderFavorites() {
    const container = $('library-favorites');
    if (!storage.favorites.length) {
      container.innerHTML = '<li>No favorite colors yet.</li>';
      return;
    }
    container.replaceChildren(...storage.favorites.map(fav => {
      const li = document.createElement('li');
      li.innerHTML = `
        <button type="button" class="library-swatch" style="background:${fav.hex}" data-hex="${fav.hex}" title="${fav.hex}" aria-label="Apply ${fav.hex}"></button>
        <span>${fav.hex}${fav.name ? ` — ${escapeHtml(fav.name)}` : ''}</span>
        <button type="button" data-action="unfavorite" data-hex="${fav.hex}">Remove</button>
      `;
      return li;
    }));
  }
  
  function renderHistory() {
    const container = $('library-history');
    if (!storage.history.length) {
      container.innerHTML = '<li>No recent colors.</li>';
      return;
    }
    container.replaceChildren(...storage.history.slice().sort((a, b) => b.time - a.time).map(entry => {
      const li = document.createElement('li');
      const seconds = Math.floor(entry.time / 1000);
      const time = entry.time > 1e12 ? new Date(entry.time).toLocaleString() : `${seconds}s device uptime`;
      li.innerHTML = `
        <button type="button" class="library-swatch" style="background:${entry.hex}" data-hex="${entry.hex}" title="${entry.hex}" aria-label="Apply ${entry.hex}"></button>
        <span>${entry.hex} <small>${time}</small></span>
        <button type="button" data-action="remove-history" data-hex="${entry.hex}">Remove</button>
      `;
      return li;
    }));
  }
  
  function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
  }
  
  function renderAll() {
    renderPalettes();
    renderCurated();
    renderFavorites();
    renderHistory();
  }
  
  // Editor
  function showEditor(palette = null) {
    const form = $('library-editor');
    const title = $('library-editor-title');
    form.hidden = false;
    if (palette) {
      title.textContent = 'Edit palette';
      form.elements.namedItem('name').value = palette.name;
      form.description.value = palette.description || '';
      form.category.value = palette.category || '';
      form.favorite.checked = !!palette.favorite;
      form.colors.value = palette.colors.join(', ');
      form.dataset.editId = palette.id;
    } else {
      title.textContent = 'New palette';
      form.reset();
      delete form.dataset.editId;
    }
    form.elements.namedItem('name').focus();
  }
  
  function hideEditor() {
    $('library-editor').hidden = true;
  }
  
  // Actions
  async function savePalette(formData) {
    try {
      const colors = parseColors(formData.get('colors'));
      const editId = $('library-editor').dataset.editId;
      const palette = {
        id: editId || generateId('user'),
        name: formData.get('name').trim(),
        description: formData.get('description').trim(),
        category: formData.get('category').trim(),
        favorite: formData.get('favorite') === 'on',
        colors,
        created: editId ? storage.palettes.find(p => p.id === editId)?.created || Date.now() : Date.now()
      };
      if (!palette.name) throw new Error('Enter a palette name');
      if (!storage.palettes.some(p => p.id === palette.id) && storage.palettes.length >= LIMITS.palettes) throw new Error('Maximum 8 user palettes');
      await deviceRequest('palettes', 'POST', palette);
      await reload('palettes');
      hideEditor();
      renderAll();
      status(editId ? 'Palette updated' : 'Palette created');
    } catch (err) {
      status(err.message, true);
    }
  }
  
  async function selectPalette(id) {
    try {
      await deviceRequest('palettes', 'POST', { selected: id });
      selectedPaletteId = id;
      storage.selected = id;
      renderPalettes();
      status('Palette selected');
    } catch (err) {
      status(err.message, true);
    }
  }
  
  async function deletePalette(id) {
    if (!confirm('Delete this palette?')) return;
    try {
      await deviceRequest('palettes', 'DELETE', { id });
      await reload('palettes');
      renderAll();
      status('Palette deleted');
    } catch (err) {
      status(err.message, true);
    }
  }
  
  async function copyCurated(id) {
    const curated = CURATED.find(p => p.id === id);
    if (!curated) return;
    try {
      const palette = {
        id: generateId('user'),
        name: curated.name + ' (copy)',
        description: curated.description,
        category: curated.category,
        favorite: false,
        colors: curated.colors.slice(0, LIMITS.paletteColors),
        created: Date.now()
      };
      if (!palette.name) throw new Error('Enter a palette name');
      if (!storage.palettes.some(p => p.id === palette.id) && storage.palettes.length >= LIMITS.palettes) throw new Error('Maximum 8 user palettes');
      await deviceRequest('palettes', 'POST', palette);
      await reload('palettes');
      renderAll();
      status('Curated palette copied');
    } catch (err) {
      status(err.message, true);
    }
  }
  
  async function addFavorite(name) {
    try {
      const rgb = Studio.getColor();
      const hex = ColorEngine.rgbToHex(rgb);
      if (!hex) throw new Error('Invalid current color');
      await deviceRequest('color/favorites', 'POST', { hex, name: name.trim() });
      await reload('color/favorites');
      renderFavorites();
      status('Color favorited');
    } catch (err) {
      status(err.message, true);
    }
  }
  
  async function removeFavorite(hex) {
    try {
      await deviceRequest('color/favorites', 'POST', { hex, remove: true });
      await reload('color/favorites');
      renderFavorites();
      status('Favorite removed');
    } catch (err) {
      status(err.message, true);
    }
  }
  
  async function clearHistory() {
    if (!confirm('Clear all recent colors?')) return;
    try {
      await deviceRequest('color/history', 'POST', { action: 'clear' });
      await reload('color/history');
      renderHistory();
      status('History cleared');
    } catch (err) {
      status(err.message, true);
    }
  }
  
  async function removeHistory(hex) {
    try {
      await deviceRequest('color/history', 'POST', { action: 'remove', hex });
      await reload('color/history');
      renderHistory();
      status('Entry removed');
    } catch (err) {
      status(err.message, true);
    }
  }
  
  function applyColor(hex) {
    const rgb = ColorEngine.hexToRgb(hex);
    if (rgb) {
      Studio.setColor(rgb, true);
      status(`Applied ${hex}`);
    }
  }
  
  // Public API
  async function refresh() {
    try {
      if (!isDemo()) await Promise.all(['palettes', 'color/history', 'color/favorites'].map(reload));
      renderAll();
      status('Library refreshed');
    } catch (err) {
      status('Refresh failed: ' + err.message, true);
    }
  }
  
  function recordDemo(rgb) {
    if (!isDemo()) return;
    const hex = ColorEngine.rgbToHex(rgb);
    if (!hex) return;
    const existing = storage.history.findIndex(h => h.hex === hex);
    if (existing >= 0) storage.history.splice(existing, 1);
    storage.history.push({ hex, time: Date.now() });
    if (storage.history.length > LIMITS.history) storage.history.shift();
    try { saveStorage(); }
    catch (err) { status(err.message, true); }
    renderHistory();
  }

  // Event delegation
  document.addEventListener('click', e => {
    const target = e.target;
    if (!target.closest('#studio-library')) return;
    
    const action = target.dataset?.action;
    if (action === 'select') selectPalette(target.dataset.id);
    else if (action === 'gradient') {
      const palette = [...storage.palettes, ...CURATED].find(p => p.id === target.dataset.id);
      if (!palette) return;
      const colors = palette.colors.length === 1 ? [...palette.colors, ...palette.colors] : palette.colors;
      Studio.applyGradient({ interpolation: 'rgb', reverse: false, position: 0, scale: 100,
        stops: colors.map((hex, i) => ({ color: ColorEngine.hexToRgb(hex), position: Math.round(100 * i / (colors.length - 1)), enabled: true })) });
      status('Gradient loaded in the studio');
    }
    else if (action === 'edit') {
      const palette = storage.palettes.find(p => p.id === target.dataset.id);
      if (palette) showEditor(palette);
    }
    else if (action === 'delete') deletePalette(target.dataset.id);
    else if (action === 'copy-curated') copyCurated(target.dataset.id);
    else if (action === 'unfavorite') removeFavorite(target.dataset.hex);
    else if (action === 'remove-history') removeHistory(target.dataset.hex);
    
    if (target.classList.contains('library-swatch') && target.dataset.hex) {
      applyColor(target.dataset.hex);
    }
  });
  
  $('library-new').addEventListener('click', () => showEditor());
  $('library-from-current').addEventListener('click', () => {
    const rgb = Studio.getColor();
    const hex = ColorEngine.rgbToHex(rgb);
    showEditor();
    if (hex) $('library-editor').colors.value = hex;
  });
  $('library-add-current').addEventListener('click', () => {
    const rgb = Studio.getColor();
    const hex = ColorEngine.rgbToHex(rgb);
    if (!hex) return;
    const field = $('library-editor').colors;
    const existing = field.value.trim().replace(/,+$/, '');
    if (existing.split(/[\s,]+/).filter(Boolean).length >= LIMITS.paletteColors) {
      status('This palette already has 12 colors.', true);
      return;
    }
    field.value = existing ? `${existing}, ${hex}` : hex;
  });
  $('library-cancel').addEventListener('click', hideEditor);
  $('library-editor').addEventListener('submit', e => {
    e.preventDefault();
    savePalette(new FormData(e.target));
  });
  $('library-refresh').addEventListener('click', refresh);
  $('library-favorite-form').addEventListener('submit', e => {
    e.preventDefault();
    const formData = new FormData(e.target);
    addFavorite(formData.get('name'));
    e.target.reset();
  });
  $('library-clear').addEventListener('click', clearHistory);
  
  renderAll();
  if (isDemo()) Studio.onColor?.(recordDemo);
  else void refresh();

  return { refresh, recordDemo };
})();
window.StudioLibrary = StudioLibrary;
