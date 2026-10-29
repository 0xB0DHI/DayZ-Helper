(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const typeNames = ['Survivors', 'Infected', 'Animals', 'Vehicles', 'Loot'];
  const typeColors = ['#6ddfd7', '#d7ab76', '#b3cd82', '#d47f73', '#d6c98e'];
  const lootNames = ['Weapons', 'Ammo', 'Medical', 'Food', 'Tools', 'Clothing', 'Other', 'Drinks'];
  const lootColors = ['#f2c46e', '#e8a77c', '#70d9bb', '#b8dc81', '#8fbde4', '#c0a8e2', '#bac9cd', '#6dd5df'];
  // Category priority is deliberately adjustable: supplies can matter more than gear.
  const nearbyPriority = {
    balanced: [8, 6, 8, 6, 4, 5, 2, 8],
    survival: [5, 4, 8, 9, 5, 5, 2, 10],
    combat: [10, 9, 7, 3, 4, 8, 2, 4]
  };
  const nearbyPreviewCount = 4;
  // The eight outlines mirror DrawItemGlyph in the local ImGui overlay.
  const lootShapes = [
    '<path d="M-6 2 6-3 M-5 2-2 5 M1-1 2 4 M4-3h3"/>',
    '<rect x="-5.1" y="-4" width="2.2" height="9" rx=".7"/><rect x="-1.1" y="-4" width="2.2" height="9" rx=".7"/><rect x="2.9" y="-4" width="2.2" height="9" rx=".7"/><path d="M-5 2h2 M-1 2h2 M3 2h2"/>',
    '<path d="M-1.6-6h3.2v4.4H6v3.2H1.6V6h-3.2V1.6H-6v-3.2h4.4Z" fill="currentColor" stroke="none"/>',
    '<rect x="-4.5" y="-5.5" width="9" height="11" rx="2"/><path d="M-4 0h8 M-3-3.5h6"/>',
    '<path d="M-5 5 4-4"/><circle cx="5" cy="-5" r="2.4"/><circle cx="-5" cy="5" r="1.5"/>',
    '<path d="M-3-5-6-2-4 1-3 0v6h6V0l1 1 2-3-3-3Z"/>',
    '<circle r="4.5"/><circle r="1.2" fill="currentColor" stroke="none"/>',
    '<rect x="-3.8" y="-5" width="7.6" height="10.5" rx="1.4"/><path d="M-3.5-2.5h7 M-2.5-6h5"/>'
  ];
  function glyphHtml(i) {
    return `<span class="loot-glyph" style="color:${lootColors[i]}"><svg viewBox="-9 -9 18 18" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">${lootShapes[i]}</g></svg></span>`;
  }
  const lootIcons = lootShapes.map((_, i) => L.divIcon({
    className: 'loot-map-icon', iconSize: [16, 16], iconAnchor: [8, 8], popupAnchor: [0, -9],
    html: glyphHtml(i)
  }));
  const visibleTypes = [true, true, true, true, true];
  const visibleLoot = [true, true, true, true, true, true, true, true];
  const imageSize = 16384;
  const mapUnits = 512;
  const bounds = [[-mapUnits, 0], [0, mapUnits]];
  const canvas = L.canvas({ padding: 0.3 });
  const map = L.map('map', {
    crs: L.CRS.Simple, minZoom: -2, maxZoom: 7, zoomSnap: 0.25,
    zoomControl: false, attributionControl: false, preferCanvas: true,
    maxBounds: [[-560, -48], [48, 560]], maxBoundsViscosity: 0.8
  });
  map.setView([-256, 256], window.innerWidth < 760 ? -1 : 0);
  if (window.innerWidth >= 760) map.panBy([-110, 0], { animate: false });
  const groups = typeNames.map(() => L.layerGroup().addTo(map));
  const coneLayer = L.layerGroup().addTo(map);
  const markers = new Map();
  const points = new Map();
  const playerRows = new Map();
  const nearbyRows = new Map();
  let localMarker = null, localCone = null, tiles = null;
  let localLabelKey = '';
  const localNameLayout = { placement: 0, x: 0, y: 0 };
  let localDisplay = null;
  let motionFrame = 0, lastMotionDraw = 0, playerOrder = '';
  let motionDuration = 200, lastSampleStamp = 0;
  let nearbyOrder = '', nearbyExpanded = false;
  let localMotion = null;
  let mapDefs = [], selectedMap = 'chernarusplus', manualMap = false;
  let local = { valid: false, x: 0, y: 0, z: 0, hx: 0, hz: 0 };
  let counts = [0, 0, 0, 0, 0], socket = null, retry = 500;
  let connected = false, lastFrame = 0, calibrating = false, snapshotTerrain = '';
  let followMode = 'free', followId = null, showSight = true;

  function activeDef() { return mapDefs.find(m => m.slug === selectedMap); }
  function project(x, z) {
    const m = activeDef()?.affine;
    if (!m) return null;
    const px = m[0] * x + m[1] * z + m[2];
    const py = m[3] * x + m[4] * z + m[5];
    if (!Number.isFinite(px) || !Number.isFinite(py)) return null;
    return L.latLng(-py / 32, px / 32);
  }
  function unprojectPixel(latlng) { return { x: latlng.lng * 32, y: -latlng.lat * 32 }; }
  function inverse(px, py) {
    const m = activeDef()?.affine;
    if (!m) return null;
    const det = m[0] * m[4] - m[1] * m[3];
    if (!Number.isFinite(det) || Math.abs(det) < 1e-12) return null;
    return {
      x: ((px - m[2]) * m[4] - m[1] * (py - m[5])) / det,
      z: (m[0] * (py - m[5]) - (px - m[2]) * m[3]) / det
    };
  }
  function setTerrain(slug, manual = true) {
    if (!mapDefs.some(m => m.slug === slug)) return;
    manualMap = manual;
    selectedMap = slug;
    $('terrain').value = slug;
    if (tiles) map.removeLayer(tiles);
    tiles = L.tileLayer(`/tiles/${slug}/{z}/{x}/{y}.jpg`, {
      tileSize: 512, minNativeZoom: 0, maxNativeZoom: 5,
      minZoom: -2, maxZoom: 7, noWrap: true, bounds,
      attribution: 'Offline terrain imagery'
    }).addTo(map);
    renderAll();
    updateCalibrationUi();
  }
  function markerStyle(point) {
    const color = typeColors[point.k];
    return {
      renderer: canvas, radius: point.k === 0 ? 6 : 4.5,
      color: '#0b1516', weight: 1.5, opacity: 0.9,
      fillColor: color, fillOpacity: point.l === 2 ? 0.45 : 0.92
    };
  }
  function followTarget(finalPosition = false) {
    if (followMode === 'local') return local.valid ?
      (finalPosition ? local : localDisplay || local) : null;
    if (followMode === 'survivor') {
      const target = points.get(followId);
      return target?.k === 0 ?
        (finalPosition ? target : markers.get(followId)?.display || target) : null;
    }
    return null;
  }
  function referencePlayer() {
    if (followMode === 'survivor') {
      const player = points.get(followId);
      return player?.k === 0 ? { point: player, name: player.n || 'Survivor', id: followId } : null;
    }
    return local.valid ? { point: local, name: 'Bodhi', id: null } : null;
  }
  function distanceFrom(anchor, point) {
    if (!anchor) return null;
    const dx = point.x - anchor.point.x;
    const dy = point.y - anchor.point.y;
    const dz = point.z - anchor.point.z;
    return Number.isFinite(dx) && Number.isFinite(dy) && Number.isFinite(dz) ?
      Math.hypot(dx, dy, dz) : null;
  }
  function playerLabel(name, distance, held = '') {
    const label = document.createElement('span'); label.className = 'player-map-label';
    const top = document.createElement('span'); top.className = 'player-map-label-top';
    const title = document.createElement('span'); title.className = 'player-map-label-name';
    title.textContent = name;
    top.append(title);
    if (distance !== null) {
      const range = document.createElement('small'); range.className = 'player-map-label-distance';
      range.textContent = `${Math.round(distance)} m`;
      top.append(range);
    }
    label.append(top);
    if (held) {
      const gear = document.createElement('small'); gear.className = 'player-map-label-held';
      gear.textContent = held; gear.title = held;
      label.append(gear);
    }
    return label;
  }
  function updatePlayerDistanceLabels() {
    const anchor = referencePlayer();
    for (const [id, entry] of markers) {
      if (entry.kind !== 0) continue;
      const player = points.get(id);
      if (!player) continue;
      const name = player.n || 'Survivor';
      const distance = anchor && anchor.id !== id ? distanceFrom(anchor, player) : null;
      const held = player.held || '';
      const key = `${name}\0${distance === null ? '' : Math.round(distance)}\0${held}`;
      if (entry.playerLabelKey === key) continue;
      entry.marker.setTooltipContent(playerLabel(name, distance, held));
      entry.playerLabelKey = key;
    }
    if (localMarker) {
      const distance = anchor?.id ? distanceFrom(anchor, local) : null;
      const held = local.held || '';
      const key = `Bodhi\0${distance === null ? '' : Math.round(distance)}\0${held}`;
      if (localLabelKey !== key) {
        localMarker.setTooltipContent(playerLabel('Bodhi', distance, held));
        localLabelKey = key;
      }
    }
  }
  function centerFollow(animate = false) {
    const target = followTarget(animate);
    if (!target) return;
    const at = project(target.x, target.z);
    if (!at) return;
    const screen = map.latLngToContainerPoint(at);
    const center = map.getSize().divideBy(2);
    if (screen.distanceTo(center) > 0.75)
      map.panTo(at, animate ? { animate: true, duration: motionDuration / 1000,
        easeLinearity: 1 } : { animate: false });
  }
  function updateFollow(animate = false) {
    const target = followTarget();
    const status = $('followStatus');
    if (followMode === 'free') status.textContent = 'Free map';
    else if (followMode === 'local') status.textContent = target ? 'Following Bodhi' : 'Waiting for Bodhi';
    else status.textContent = target ? `Following ${target.n || 'survivor'}` : 'Waiting for survivor';
    $('followLocal').classList.toggle('active', followMode === 'local');
    $('followFree').classList.toggle('active', followMode === 'free');
    centerFollow(animate);
  }
  function setFollow(mode, id = null) {
    followMode = mode; followId = mode === 'survivor' ? id : null;
    renderAll(); updatePlayers();
  }
  function allowed(point) {
    return visibleTypes[point.k] && (point.k !== 4 || visibleLoot[point.c]);
  }
  function coneCoords(x, z, hx, hz, length = 90) {
    const angle = Math.atan2(hx, hz), width = Math.PI / 10;
    const a = project(x, z);
    const b = project(x + Math.sin(angle - width) * length, z + Math.cos(angle - width) * length);
    const c = project(x + Math.sin(angle + width) * length, z + Math.cos(angle + width) * length);
    return a && b && c ? [a, b, c] : null;
  }
  function motionSample(point) {
    return { x: point.x, z: point.z, hx: point.hx || 0, hz: point.hz || 0, n: point.n || '' };
  }
  function startMotion(display, point, previous = null) {
    const target = motionSample(point);
    if (previous && previous.target.x === target.x && previous.target.z === target.z &&
        previous.target.hx === target.hx && previous.target.hz === target.hz)
      return previous;
    // Respawns, teleports, and terrain switches should not sweep a marker across the map.
    if (Math.hypot(display.x - target.x, display.z - target.z) > 100) {
      Object.assign(display, target);
      return null;
    }
    if (display.x === target.x && display.z === target.z &&
        display.hx === target.hx && display.hz === target.hz) return null;
    return { from: { ...display }, target, start: performance.now(), duration: motionDuration };
  }
  function advanceMotion(display, motion, now) {
    const t = Math.min(1, (now - motion.start) / motion.duration);
    display.x = motion.from.x + (motion.target.x - motion.from.x) * t;
    display.z = motion.from.z + (motion.target.z - motion.from.z) * t;
    const fromAngle = Math.atan2(motion.from.hx, motion.from.hz);
    const toAngle = Math.atan2(motion.target.hx, motion.target.hz);
    const arc = Math.atan2(Math.sin(toAngle - fromAngle), Math.cos(toAngle - fromAngle));
    const angle = fromAngle + arc * t;
    const length = Math.hypot(motion.from.hx, motion.from.hz) * (1 - t) +
      Math.hypot(motion.target.hx, motion.target.hz) * t;
    display.hx = Math.sin(angle) * length;
    display.hz = Math.cos(angle) * length;
    display.n = motion.target.n;
    return t < 1;
  }
  function updateCone(entry, x, z, hx, hz, length) {
    const coords = showSight && Math.hypot(hx, hz) > 0.5 ? coneCoords(x, z, hx, hz, length) : null;
    if (!coords) {
      if (entry.cone) { coneLayer.removeLayer(entry.cone); entry.cone = null; }
      return;
    }
    if (entry.cone) entry.cone.setLatLngs(coords);
    else entry.cone = L.polygon(coords, {
      color: typeColors[0], weight: 1, opacity: .4, fillOpacity: .12
    }).addTo(coneLayer);
  }
  function animateMotion(now) {
    motionFrame = 0;
    if (now - lastMotionDraw < 10) { motionFrame = requestAnimationFrame(animateMotion); return; }
    lastMotionDraw = now;
    let active = false;
    for (const entry of markers.values()) {
      if (!entry.motion) continue;
      const moving = advanceMotion(entry.display, entry.motion, now);
      const at = project(entry.display.x, entry.display.z);
      if (at) entry.marker.setLatLng(at);
      if (entry.kind === 0)
        updateCone(entry, entry.display.x, entry.display.z,
          Math.hypot(entry.motion.target.hx, entry.motion.target.hz) > 0.5 ? entry.display.hx : 0,
          Math.hypot(entry.motion.target.hx, entry.motion.target.hz) > 0.5 ? entry.display.hz : 0, 90);
      entry.motion = moving ? entry.motion : null;
      active ||= moving;
    }
    if (localMarker && localMotion && localDisplay) {
      const moving = advanceMotion(localDisplay, localMotion, now);
      const at = project(localDisplay.x, localDisplay.z);
      if (at) localMarker.setLatLng(at);
      const holder = { cone: localCone };
      updateCone(holder, localDisplay.x, localDisplay.z,
        Math.hypot(localMotion.target.hx, localMotion.target.hz) > 0.5 ? localDisplay.hx : 0,
        Math.hypot(localMotion.target.hx, localMotion.target.hz) > 0.5 ? localDisplay.hz : 0, 90);
      localCone = holder.cone;
      localMotion = moving ? localMotion : null;
      active ||= moving;
    }
    if (active) motionFrame = requestAnimationFrame(animateMotion);
    else refreshPlayerNames();
  }
  function scheduleMotion() {
    if (!motionFrame) motionFrame = requestAnimationFrame(animateMotion);
  }
  function removeMarker(id) {
    const entry = markers.get(id);
    if (!entry) return;
    groups[entry.kind].removeLayer(entry.marker);
    if (entry.cone) coneLayer.removeLayer(entry.cone);
    markers.delete(id);
  }
  function tooltipText(value) {
    const span = document.createElement('span');
    span.textContent = value;
    return span;
  }
  function nameLabelOffsets(rect) {
    return [
      [0, 0],
      [0, rect.height + 19],
      [rect.width / 2 + 13, rect.height / 2 + 12],
      [-rect.width / 2 - 13, rect.height / 2 + 12],
      [0, -rect.height - 8]
    ];
  }
  function shiftedRect(rect, x, y) {
    return { left: rect.left + x, right: rect.right + x,
      top: rect.top + y, bottom: rect.bottom + y,
      width: rect.width, height: rect.height };
  }
  function nameFits(rect, placed, view, gap) {
    if (!Number.isFinite(rect.left) || !Number.isFinite(rect.right) ||
        !Number.isFinite(rect.top) || !Number.isFinite(rect.bottom)) return false;
    if (rect.left < view.left + 5 || rect.right > view.right - 5 ||
        rect.top < view.top + 5 || rect.bottom > view.bottom - 5) return false;
    return !placed.some(other => rect.left < other.right + gap &&
      rect.right + gap > other.left && rect.top < other.bottom + gap &&
      rect.bottom + gap > other.top);
  }
  function placePlayerName(element, layout, placed, view) {
    const measured = element.getBoundingClientRect();
    const base = shiftedRect(measured, -layout.x, -layout.y);
    const offsets = nameLabelOffsets(base);
    // Hold a clear placement; return above the blip only with room to spare.
    const preferred = layout.placement;
    const choices = preferred ? [preferred, 0, 1, 2, 3, 4] : [0, 1, 2, 3, 4];
    let chosen = -1;
    if (preferred && nameFits(base, placed, view, 12)) chosen = 0;
    else for (const index of choices) {
      const [x, y] = offsets[index];
      if (nameFits(shiftedRect(base, x, y), placed, view, 4)) {
        chosen = index; break;
      }
    }
    const [x, y] = chosen < 0 ? [0, 0] : offsets[chosen];
    if (layout.x !== x || layout.y !== y)
      element.style.translate = x || y ? `${x}px ${y}px` : 'none';
    layout.x = x; layout.y = y;
    if (chosen >= 0) layout.placement = chosen;
    element.classList.toggle('is-decluttered', chosen < 0);
    if (chosen >= 0) placed.push(shiftedRect(base, x, y));
  }
  function refreshPlayerNames() {
    const entries = [...markers.entries()].filter(([, entry]) => entry.kind === 0);
    entries.sort(([left], [right]) => {
      if (left === followId) return -1;
      if (right === followId) return 1;
      return String(left).localeCompare(String(right));
    });
    const placed = [];
    const view = map.getContainer().getBoundingClientRect();
    const localName = localMarker?.getTooltip()?.getElement();
    const followed = followMode === 'survivor' ? entries.find(([id]) => id === followId) : null;
    if (followed) {
      const element = followed[1].marker.getTooltip()?.getElement();
      if (element) placePlayerName(element, followed[1].nameLayout, placed, view);
    }
    if (localName) placePlayerName(localName, localNameLayout, placed, view);
    for (const [id, entry] of entries) {
      if (followed && id === followId) continue;
      const element = entry.marker.getTooltip()?.getElement();
      if (element) placePlayerName(element, entry.nameLayout, placed, view);
    }
  }
  function drawPoint(point) {
    if (!allowed(point)) { removeMarker(point.id); return; }
    const location = project(point.x, point.z);
    if (!location) { removeMarker(point.id); return; }
    if (point.k === 4) {
      const old = markers.get(point.id);
      if (old?.kind === 4 && old.category === point.c && old.x === point.x &&
          old.y === point.y && old.z === point.z && old.name === point.n &&
          old.quality === point.q) return;
      removeMarker(point.id);
      const marker = L.marker(location, { icon: lootIcons[point.c] || lootIcons[6],
        keyboard: true, bubblingMouseEvents: false, riseOnHover: true });
      const current = () => points.get(point.id) || point;
      marker.bindTooltip(tooltipText(`${point.n || lootNames[point.c] || 'Item'} · ${Math.round(point.d)} m`),
        { direction: 'top' });
      marker.on('tooltipopen', () => {
        const item = current();
        marker.setTooltipContent(tooltipText(`${item.n || lootNames[item.c] || 'Item'} · ${Math.round(item.d)} m`));
      });
      marker.bindPopup(lootDetails(point), { maxWidth: 220 });
      marker.on('popupopen', () => marker.setPopupContent(lootDetails(current())));
      marker.addTo(groups[4]);
      markers.set(point.id, { marker, kind: 4, category: point.c,
        x: point.x, y: point.y, z: point.z, name: point.n, quality: point.q });
      return;
    }
    const old = markers.get(point.id);
    if (old?.kind === point.k) {
      const label = point.n || typeNames[point.k]?.slice(0, -1) || 'Entity';
      const tooltip = point.k === 0 ? label : `${label} · ${Math.round(point.d)} m`;
      if (old.label !== tooltip) {
        if (point.k !== 0) old.marker.setTooltipContent(tooltipText(tooltip));
        old.label = tooltip;
      }
      if (old.life !== point.l) { old.marker.setStyle(markerStyle(point)); old.life = point.l; }
      const changedPosition = old.x !== point.x || old.z !== point.z;
      old.motion = startMotion(old.display, point, old.motion);
      old.display.n = point.n || '';
      old.x = point.x; old.z = point.z;
      if (point.k === 0) {
        if (Math.hypot(point.hx, point.hz) <= 0.5)
          updateCone(old, old.display.x, old.display.z, 0, 0, 90);
        else if (!old.motion)
          updateCone(old, old.display.x, old.display.z, old.display.hx, old.display.hz, 90);
      }
      if (old.motion) scheduleMotion();
      else if (changedPosition) {
        const at = project(old.display.x, old.display.z);
        if (at) old.marker.setLatLng(at);
      }
      return;
    }
    removeMarker(point.id);
    const marker = L.circleMarker(location, { ...markerStyle(point), bubblingMouseEvents: false });
    const label = point.n || typeNames[point.k]?.slice(0, -1) || 'Entity';
    const tooltip = point.k === 0 ? label : `${label} · ${Math.round(point.d)} m`;
    marker.bindTooltip(point.k === 0 ? playerLabel(label, null, point.held) : tooltipText(tooltip), point.k === 0 ? {
      permanent: true, direction: 'top', offset: [0, -7], className: 'player-map-name', opacity: 1
    } : { direction: 'top' });
    if (point.k === 0) {
      marker.on('click', () => setFollow('survivor', point.id));
      marker.on('mouseover', () => marker.getTooltip()?.getElement()?.classList.add('is-hovered'));
      marker.on('mouseout', () => marker.getTooltip()?.getElement()?.classList.remove('is-hovered'));
    }
    marker.addTo(groups[point.k]);
    const entry = { marker, cone: null, kind: point.k, x: point.x, z: point.z,
      life: point.l, label: tooltip, display: motionSample(point),
      motion: null, nameLayout: { placement: 0, x: 0, y: 0 }, playerLabelKey: '' };
    if (point.k === 0) updateCone(entry, point.x, point.z, point.hx, point.hz, 90);
    markers.set(point.id, entry);
  }
  function lootDetails(item) {
    const root = document.createElement('div'); root.className = 'loot-popup';
    const icon = document.createElement('span'); icon.className = 'loot-popup-glyph';
    icon.innerHTML = glyphHtml(item.c < lootShapes.length ? item.c : 6);
    const heading = document.createElement('strong');
    heading.textContent = item.n || lootNames[item.c] || 'Item';
    const meta = document.createElement('small');
    meta.textContent = `${lootNames[item.c] || 'Other'} · ${Math.round(item.d)} m${item.q === 255 ? '' : ` · quality ${item.q}`}`;
    root.append(icon, heading, document.createElement('br'), meta);
    return root;
  }
  function drawLocal() {
    if (!local.valid) {
      if (localMarker) { map.removeLayer(localMarker); localMarker = null; }
      if (localCone) { coneLayer.removeLayer(localCone); localCone = null; }
      localDisplay = null; localMotion = null; localLabelKey = '';
      localNameLayout.placement = 0; localNameLayout.x = 0; localNameLayout.y = 0;
      return;
    }
    const at = project(local.x, local.z);
    if (!at) return;
    if (!localMarker) {
      localDisplay = motionSample(local);
      localMarker = L.circleMarker(at, {
        ...markerStyle({ k: 0, l: 0 }), bubblingMouseEvents: false
      }).bindTooltip(playerLabel('Bodhi', null, local.held), {
        permanent: true, direction: 'top', offset: [0, -7],
        className: 'player-map-name', opacity: 1
      }).addTo(map);
      localLabelKey = '';
      localNameLayout.placement = 0; localNameLayout.x = 0; localNameLayout.y = 0;
      localMarker.on('click', () => setFollow('local'));
      localMarker.on('mouseover', () => localMarker.getTooltip()?.getElement()?.classList.add('is-hovered'));
      localMarker.on('mouseout', () => localMarker.getTooltip()?.getElement()?.classList.remove('is-hovered'));
      const holder = { cone: localCone };
      updateCone(holder, local.x, local.z, local.hx, local.hz, 90);
      localCone = holder.cone;
    } else {
      const beforeX = localDisplay.x, beforeZ = localDisplay.z;
      localMotion = startMotion(localDisplay, local, localMotion);
      if (!localMotion && (beforeX !== localDisplay.x || beforeZ !== localDisplay.z)) {
        const snapped = project(localDisplay.x, localDisplay.z);
        if (snapped) localMarker.setLatLng(snapped);
      }
      if (Math.hypot(local.hx, local.hz) <= 0.5 && localCone) {
        coneLayer.removeLayer(localCone); localCone = null;
      }
      if (localMotion) scheduleMotion();
    }
  }
  function renderAll() {
    [...markers.keys()].forEach(removeMarker);
    if (localMarker) { map.removeLayer(localMarker); localMarker = null; }
    if (localCone) { coneLayer.removeLayer(localCone); localCone = null; }
    localDisplay = null; localMotion = null; localLabelKey = '';
    localNameLayout.placement = 0; localNameLayout.x = 0; localNameLayout.y = 0;
    for (const point of points.values()) drawPoint(point);
    drawLocal(); updateStats(); updateFollow(); updateNearby();
    updatePlayerDistanceLabels(); refreshPlayerNames();
    $('uncalibrated').classList.toggle('hidden', !!activeDef()?.affine);
  }
  function updateStats() {
    $('position').textContent = local.valid ? `${Math.round(local.x)} / ${Math.round(local.z)}` : '— / —';
    $('grid').textContent = local.valid ? `${Math.floor(local.x / 100)} · ${Math.floor(local.z / 100)}` : '—';
    $('counts').textContent = `${counts[0]} S · ${counts[1]} I · ${counts[2]} A · ${counts[3]} V · ${counts[4]} L`;
    $('zoom').textContent = `${map.getZoom().toFixed(1)}×`;
    $('playerCount').textContent = `${counts[0]} NEARBY`;
    if (local.valid) {
      const angle = Math.atan2(local.hx, local.hz) * 180 / Math.PI;
      const cardinal = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
      $('headingText').textContent = cardinal[((Math.round(angle / 45) % 8) + 8) % 8];
      $('compassArrow').style.transform = `rotate(${angle}deg)`;
    } else { $('headingText').textContent = '—'; $('compassArrow').style.transform = ''; }
    $('calibrationLive').textContent = local.valid ? `Live ${Math.round(local.x)} / ${Math.round(local.z)}` : 'Local position unavailable';
    const center = map.getSize().divideBy(2);
    const a = unprojectPixel(map.containerPointToLatLng(center));
    const b = unprojectPixel(map.containerPointToLatLng(L.point(center.x + 100, center.y)));
    const wa = inverse(a.x, a.y), wb = inverse(b.x, b.y);
    $('scale').textContent = wa && wb ? `${Math.round(Math.hypot(wb.x - wa.x, wb.z - wa.z))} m / 100 px` : '—';
  }
  function updateNearby() {
    const list = $('nearbyList');
    const expand = $('nearbyExpand');
    const anchor = referencePlayer();
    const focus = $('nearbyFocus').value;
    const weights = nearbyPriority[focus] || nearbyPriority.balanced;
    const range = Number($('nearbyRange').value);
    const items = [];
    if (anchor) {
      for (const point of points.values()) {
        if (point.k !== 4 || !visibleLoot[point.c]) continue;
        const distance = Math.hypot(point.x - anchor.point.x,
          point.y - anchor.point.y, point.z - anchor.point.z);
        if (!Number.isFinite(distance) || distance > range) continue;
        const armor = point.c === 5 &&
          /\b(plate carrier|ballistic|armor|armour|helmet|vest)\b/i.test(point.n || '');
        const priority = Math.max(weights[point.c] || 0,
          armor ? (focus === 'survival' ? 7 : 9) : 0);
        items.push({ point, distance, priority });
      }
    }
    items.sort((a, b) => b.priority - a.priority ||
      Math.floor(a.distance / 10) - Math.floor(b.distance / 10) ||
      String(a.point.id).localeCompare(String(b.point.id)));
    $('nearbyAnchor').textContent = anchor ? `${anchor.name} · ${items.length}` : 'WAITING';
    $('nearbyAnchor').title = anchor ? `Items near ${anchor.name}` : 'Waiting for a position';
    if (!items.length) {
      nearbyRows.clear(); nearbyOrder = ''; nearbyExpanded = false;
      const message = !anchor ? 'Waiting for a position.' :
        `No published items within ${range} m.`;
      if (list.firstElementChild?.className !== 'empty' ||
          list.firstElementChild.textContent !== message) {
        const empty = document.createElement('p');
        empty.className = 'empty'; empty.textContent = message;
        list.replaceChildren(empty);
      }
      expand.classList.add('hidden');
      return;
    }
    if (items.length <= nearbyPreviewCount) nearbyExpanded = false;
    const visible = nearbyExpanded ? items : items.slice(0, nearbyPreviewCount);
    const current = new Set(visible.map(item => item.point.id));
    for (const id of nearbyRows.keys()) if (!current.has(id)) nearbyRows.delete(id);
    for (const item of visible) {
      const { point, distance } = item;
      let row = nearbyRows.get(point.id);
      if (!row) {
        const button = document.createElement('button'); button.type = 'button';
        button.className = 'nearby-row';
        const icon = document.createElement('span'); icon.innerHTML = glyphHtml(point.c);
        const copy = document.createElement('span'); copy.className = 'nearby-row-copy';
        const name = document.createElement('span'); name.className = 'nearby-row-name';
        const category = document.createElement('span'); category.className = 'nearby-row-category';
        const distanceText = document.createElement('span'); distanceText.className = 'nearby-row-distance';
        copy.append(name, category); button.append(icon, copy, distanceText);
        button.onclick = () => {
          const currentPoint = points.get(point.id);
          if (!currentPoint) return;
          setFollow('free');
          const at = project(currentPoint.x, currentPoint.z);
          if (at) map.panTo(at, { animate: false });
          markers.get(point.id)?.marker.openPopup();
        };
        row = { button, icon, name, category, distanceText, categoryIndex: point.c };
        nearbyRows.set(point.id, row);
      }
      if (row.categoryIndex !== point.c) {
        row.icon.innerHTML = glyphHtml(point.c);
        row.categoryIndex = point.c;
      }
      row.name.textContent = point.n || lootNames[point.c] || 'Item';
      row.category.textContent = lootNames[point.c] || 'Other';
      row.distanceText.textContent = `${Math.round(distance)} m`;
      row.button.setAttribute('aria-label',
        `Show ${row.name.textContent}, ${row.distanceText.textContent}, on map`);
    }
    const order = visible.map(item => item.point.id).join('|');
    if (order !== nearbyOrder || list.children.length !== visible.length) {
      const scrollTop = list.scrollTop;
      list.replaceChildren(...visible.map(item => nearbyRows.get(item.point.id).button));
      list.scrollTop = scrollTop;
      nearbyOrder = order;
    }
    expand.classList.toggle('hidden', items.length <= nearbyPreviewCount);
    expand.textContent = nearbyExpanded ? 'Show fewer' : `Show all ${items.length} items`;
    expand.setAttribute('aria-expanded', String(nearbyExpanded));
  }
  function updatePlayers() {
    const list = $('playerList');
    const anchor = referencePlayer();
    const survivors = [...points.values()].filter(p => p.k === 0).map(player => ({
      player, distance: distanceFrom(anchor, player)
    })).sort((a, b) => (a.distance ?? Infinity) - (b.distance ?? Infinity) ||
      String(a.player.id).localeCompare(String(b.player.id)));
    if (!survivors.length) {
      playerRows.clear(); playerOrder = '';
      const message = connected ? 'No survivors in the published range.' : 'Waiting for a published frame.';
      if (list.firstElementChild?.className !== 'empty' || list.firstElementChild.textContent !== message) {
        const empty = document.createElement('p'); empty.className = 'empty'; empty.textContent = message;
        list.replaceChildren(empty);
      }
      return;
    }
    const current = new Set(survivors.map(({ player }) => player.id));
    for (const id of playerRows.keys()) if (!current.has(id)) playerRows.delete(id);
    for (const { player, distance } of survivors) {
      let entry = playerRows.get(player.id);
      if (!entry) {
        const row = document.createElement('div');
        const name = document.createElement('strong');
        const detail = document.createElement('span');
        const held = document.createElement('span'); held.className = 'player-held';
        const button = document.createElement('button'); button.type = 'button';
        button.className = 'follow-player';
        button.onclick = () => setFollow('survivor', player.id);
        row.append(name, detail, held, button);
        entry = { row, name, detail, held, button };
        playerRows.set(player.id, entry);
      }
      const { row, name, detail, held, button } = entry;
      row.className = `player${player.l === 2 ? ' dead' : ''}${followMode === 'survivor' && followId === player.id ? ' selected' : ''}`;
      name.textContent = player.n || `Survivor ${String(player.id).slice(-4).toUpperCase()}`;
      name.title = name.textContent;
      detail.textContent = `${distance === null ? '—' : `${Math.round(distance)} m`}${player.l === 2 ? ' · dead' : ''}`;
      held.textContent = player.held || '';
      held.title = held.textContent;
      held.classList.toggle('hidden', !held.textContent);
      button.textContent = followMode === 'survivor' && followId === player.id ? 'Following' : 'Follow';
      button.setAttribute('aria-label', `Follow ${name.textContent}`);
    }
    const order = survivors.map(({ player }) => player.id).join('|');
    if (order !== playerOrder || list.children.length !== survivors.length) {
      list.replaceChildren(...survivors.map(({ player }) => playerRows.get(player.id).row));
      playerOrder = order;
    }
  }
  function connectionState(state, message) {
    $('connection').className = `status ${state}`;
    $('connectionText').textContent = message;
  }
  function receive(data) {
    if (data.type === 'status') {
      connected = false;
      connectionState('offline', 'Controller disconnected');
      updatePlayers(); updateFollow(); updateNearby(); return;
    }
    if (data.type !== 'full' && data.type !== 'delta') return;
    if (Number.isFinite(data.t) && data.t > 0) {
      if (lastSampleStamp && data.t > lastSampleStamp)
        motionDuration = Math.max(120, Math.min(300, data.t - lastSampleStamp));
      lastSampleStamp = data.t;
    }
    connected = !!data.connected;
    lastFrame = Date.now(); retry = 500;
    local = data.local || local;
    counts = data.counts || counts;
    snapshotTerrain = data.terrain || '';
    if (!manualMap && snapshotTerrain && mapDefs.some(m => m.slug === snapshotTerrain) && selectedMap !== snapshotTerrain)
      setTerrain(snapshotTerrain, false);
    if (data.type === 'full') {
      points.clear();
      for (const p of data.points || []) points.set(p.id, p);
      renderAll();
    } else {
      for (const id of data.remove || []) {
        points.delete(id); removeMarker(id);
      }
      for (const p of data.upsert || []) {
        points.set(p.id, p); drawPoint(p);
      }
      drawLocal(); updateStats();
    }
    updatePlayers(); updateFollow(data.type === 'delta'); updateNearby();
    updatePlayerDistanceLabels(); refreshPlayerNames();
    connectionState(connected ? 'live' : 'offline', connected ? 'Live · controller linked' : 'Controller disconnected');
  }
  function connect() {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    socket = new WebSocket(`${protocol}//${location.host}/stream`);
    socket.onmessage = event => {
      try { receive(JSON.parse(event.data)); } catch (error) { console.error('Telemetry frame rejected', error); }
    };
    socket.onopen = () => { retry = 500; };
    socket.onclose = () => {
      connected = false; connectionState('offline', 'Reconnecting…');
      setTimeout(connect, retry); retry = Math.min(retry * 1.7, 5000);
    };
    socket.onerror = () => socket.close();
  }
  function addControls() {
    typeNames.forEach((name, i) => {
      const row = document.createElement('label'); row.className = 'switch-row';
      const check = document.createElement('input'); check.type = 'checkbox'; check.checked = true;
      check.onchange = () => { visibleTypes[i] = check.checked; renderAll(); };
      const swatch = document.createElement('span'); swatch.className = 'swatch'; swatch.style.background = typeColors[i];
      const text = document.createElement('b'); text.textContent = name;
      row.append(check, swatch, text); $('layerControls').append(row);
    });
    lootNames.forEach((name, i) => {
      const row = document.createElement('label'); row.className = 'switch-row';
      const check = document.createElement('input'); check.type = 'checkbox'; check.checked = true;
      check.onchange = () => { visibleLoot[i] = check.checked; renderAll(); };
      const swatch = document.createElement('span'); swatch.className = 'swatch'; swatch.style.background = lootColors[i];
      const text = document.createElement('b'); text.textContent = name;
      row.append(check, swatch, text); $('lootControls').append(row);
    });
  }
  function updateCalibrationUi() {
    const def = activeDef();
    if (!def) return;
    const text = def.sourceAligned ?
      `Source-aligned map · compare the live pin with iZurvive. ${def.samples ? 'One manual point recorded; add another to override.' : 'Add two known points to override.'}` :
      def.samples < 2 ? `${def.samples} of 2 initial points · click a known location while standing there.` :
      def.provisional ? 'Two-point north-up fit ready · provisional. Add a third point to check alignment and fit rotation.' :
      `${def.samples} points · affine map ready · third-point check ${def.thirdResidual.toFixed(1)} px${def.samples >= 4 ? ` · fourth-point error ${def.residual.toFixed(1)} px` : ''}`;
    $('calibrationStatus').textContent = text;
    $('uncalibrated').classList.toggle('hidden', !!def.affine);
  }
  async function loadMaps() {
    const response = await fetch('/api/maps', { cache: 'no-store' });
    if (!response.ok) throw new Error('Map metadata unavailable');
    mapDefs = (await response.json()).maps;
    const picker = $('terrain'); picker.replaceChildren();
    for (const def of mapDefs) {
      const option = document.createElement('option'); option.value = def.slug; option.textContent = def.title;
      picker.append(option);
    }
    setTerrain(selectedMap, manualMap);
  }
  async function calibrationPost(values) {
    const response = await fetch('/api/calibrate', {
      method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams(values)
    });
    if (!response.ok) {
      const body = await response.json().catch(() => ({}));
      throw new Error(body.error || 'Calibration failed');
    }
    await loadMaps();
  }
  function wireUi() {
    $('terrain').onchange = event => setTerrain(event.target.value, true);
    $('followLocal').onclick = () => setFollow('local');
    $('followFree').onclick = () => setFollow('free');
    $('nearbyRange').onchange = updateNearby;
    $('nearbyFocus').onchange = updateNearby;
    $('nearbyExpand').onclick = () => { nearbyExpanded = !nearbyExpanded; updateNearby(); };
    $('showSight').onchange = event => { showSight = event.target.checked; renderAll(); };
    $('zoomIn').onclick = () => map.zoomIn(.5);
    $('zoomOut').onclick = () => map.zoomOut(.5);
    $('calibrateButton').onclick = () => { calibrating = !calibrating; $('calibration').classList.toggle('hidden', !calibrating); };
    $('closeCalibration').onclick = () => { calibrating = false; $('calibration').classList.add('hidden'); };
    $('resetCalibration').onclick = async () => {
      try { await calibrationPost({ map: selectedMap, clear: '1' }); }
      catch (error) { $('calibrationStatus').textContent = error.message; }
    };
    map.on('click', async event => {
      if (!calibrating || !local.valid) return;
      const pixel = unprojectPixel(event.latlng);
      if (pixel.x < 0 || pixel.y < 0 || pixel.x > imageSize || pixel.y > imageSize) return;
      try {
        await calibrationPost({ map: selectedMap, wx: local.x.toString(), wz: local.z.toString(),
          px: pixel.x.toString(), py: pixel.y.toString() });
      } catch (error) { $('calibrationStatus').textContent = error.message; }
    });
    map.on('dragstart', () => { if (followMode !== 'free') setFollow('free'); });
    map.on('zoomend', () => { updateStats(); updateFollow(); refreshPlayerNames(); });
  }
  addControls(); wireUi();
  loadMaps().then(connect).catch(error => connectionState('offline', error.message));
  setInterval(() => {
    if (connected && Date.now() - lastFrame > 1200)
      connectionState('paused', 'Controller paused');
  }, 400);
})();
