const API_BASE = "http://localhost:8000/api";

let campusMap = null;
let top3Rooms = [];
let selectedRoom = null;

// "OVERVIEW" -> Clean 3-building plane (Central Library, Department A, Department B)
// "NAVIGATE" -> Step-by-step tracker from building to building, then floor to floor
let mode = "OVERVIEW";
let fullPath = [];
let stepIndex = 0;
let autoWalkTimer = null;
let pulsePhase = 0;

let buildingHitTargets = [];

const el = (id) => document.getElementById(id);

function currentDayShift() {
  return {
    day: el("daySelect").value,
    shift: el("shiftSelect").value,
  };
}

async function apiGet(path) {
  const res = await fetch(`${API_BASE}${path}`);
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error || `HTTP ${res.status}`);
  }
  return res.json();
}

async function apiPost(path, payload) {
  const res = await fetch(`${API_BASE}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error || `HTTP ${res.status}`);
  }
  return res.json();
}

function resizeCanvas() {
  const canvas = el("floorCanvas");
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = Math.max(1, Math.floor(rect.width * dpr));
  canvas.height = Math.max(1, Math.floor(rect.height * dpr));
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

async function refreshHealth() {
  const badge = el("healthBadge");
  const { day, shift } = currentDayShift();
  try {
    const data = await apiGet(`/health?day=${encodeURIComponent(day)}&shift=${encodeURIComponent(shift)}`);
    badge.className = "status-pill";
    badge.textContent = `${data.free_rooms}/${data.total_rooms} Free • ${data.day} ${data.shift}`;
    el("slotSummary").textContent = `${data.day} · ${data.shift}`;
  } catch (e) {
    badge.className = "status-pill err";
    badge.textContent = "Backend Offline";
  }
}

async function loadMap() {
  if (!campusMap) {
    campusMap = await apiGet("/map");
  }
}

function resolveLocationNode(rawVal) {
  const cleaned = (rawVal || "").trim();
  const lower = cleaned.toLowerCase();
  if (!lower) return "LIB_F1_HALL_01";
  if (lower === "library" || lower === "lib") return "LIB_F1_HALL_01";
  if (lower === "dept a" || lower === "department a" || lower === "dept_a") return "DEPT_A_F1_HALL_01";
  if (lower === "dept b" || lower === "department b" || lower === "dept_b") return "DEPT_B_F1_HALL_01";

  // Match by exact node ID or room name if found in campusMap
  if (campusMap) {
    for (const b of campusMap.buildings || []) {
      for (const f of b.floors || []) {
        for (const n of f.nodes || []) {
          if (
            n.id.toLowerCase() === lower ||
            (n.name && n.name.toLowerCase() === lower)
          ) {
            return n.id;
          }
        }
      }
    }
  }
  return cleaned;
}

async function fetchTop3() {
  const resolvedStart = resolveLocationNode(el("startNodeInput").value);
  el("startNodeInput").value = resolvedStart;

  const { day, shift } = currentDayShift();
  const preferBuilding = el("preferBuildingSelect").value;
  const preferStairs = el("preferStairsCheck").checked;
  const preferLowerFloor = el("preferLowerFloorCheck").checked;

  const params = new URLSearchParams({
    start: resolvedStart,
    day,
    shift,
    prefer_stairs: String(preferStairs),
    prefer_lower_floor: String(preferLowerFloor),
  });
  if (preferBuilding) params.set("prefer_building", preferBuilding);

  try {
    const res = await apiGet(`/top-rooms?${params.toString()}`);
    top3Rooms = res.rooms || [];
    renderTop3();
  } catch (e) {
    top3Rooms = [];
    el("top3List").innerHTML = `<div class="muted-xs">${e.message}</div>`;
  }
}

function renderTop3() {
  const list = el("top3List");
  if (!top3Rooms.length) {
    list.innerHTML = `<div class="muted-xs">No free rooms available.</div>`;
    return;
  }

  const tierNames = { 1: "Classroom", 2: "Lab", 3: "Seminar", 4: "Quiet Zone" };
  list.innerHTML = top3Rooms
    .map((r, idx) => {
      const active = selectedRoom && selectedRoom.room_id === r.room_id;
      return `
        <button class="room-pick ${active ? "active" : ""}" type="button" data-idx="${idx}">
          <div>
            <div class="room-pick-name">${idx + 1}. ${r.room_name}</div>
            <div class="room-pick-meta">${r.building_id} · Floor ${r.floor} · ${tierNames[r.priority_tier] || r.room_type}</div>
          </div>
          <span class="cost-pill">Cost ${r.path_cost}</span>
        </button>
      `;
    })
    .join("");

  list.querySelectorAll(".room-pick").forEach((btn) => {
    btn.addEventListener("click", () => {
      startNavigationToRoom(top3Rooms[Number(btn.dataset.idx)]);
    });
  });
}

function stopAutoWalk() {
  if (autoWalkTimer) {
    clearInterval(autoWalkTimer);
    autoWalkTimer = null;
    el("autoWalkBtn").textContent = "Auto Walk";
  }
}

async function startNavigationToRoom(room) {
  stopAutoWalk();
  selectedRoom = room;
  fullPath = room.path && room.path.length ? room.path : [el("startNodeInput").value.trim(), room.room_id];
  stepIndex = 0;
  mode = "NAVIGATE";

  const { day, shift } = currentDayShift();
  if (el("autoBookCheck").checked) {
    try {
      await apiPost("/rooms/book", { room_id: room.room_id, day, shift });
      await refreshHealth();
    } catch (_) {}
  }

  renderTop3();
  el("navHud").classList.remove("hidden");
  updateNavHud();
}

function exitNavigationToOverview() {
  stopAutoWalk();
  mode = "OVERVIEW";
  selectedRoom = null;
  fullPath = [];
  stepIndex = 0;
  el("navHud").classList.add("hidden");
  renderTop3();
}

function parseNodeInfo(nodeId) {
  let bldg = "LIB";
  if (nodeId.startsWith("DEPT_A")) bldg = "DEPT_A";
  else if (nodeId.startsWith("DEPT_B")) bldg = "DEPT_B";

  const fMatch = nodeId.match(/_F(\d+)_/i);
  const floor = fMatch ? Number(fMatch[1]) : 1;

  if (campusMap) {
    for (const b of campusMap.buildings || []) {
      for (const f of b.floors || []) {
        const found = (f.nodes || []).find((n) => n.id === nodeId);
        if (found) {
          return {
            id: nodeId,
            name: found.name || nodeId,
            building_id: b.building_id,
            floor: f.floor_number,
          };
        }
      }
    }
  }
  return { id: nodeId, name: nodeId, building_id: bldg, floor };
}

function updateNavHud() {
  if (!selectedRoom || !fullPath.length) return;
  const curId = fullPath[Math.min(stepIndex, fullPath.length - 1)];
  const curInfo = parseNodeInfo(curId);
  const nextId = fullPath[Math.min(stepIndex + 1, fullPath.length - 1)];
  const nextInfo = parseNodeInfo(nextId);

  el("navTargetTitle").textContent = `→ ${selectedRoom.room_name} (${selectedRoom.building_id} F${selectedRoom.floor})`;

  if (stepIndex >= fullPath.length - 1) {
    el("navStepCaption").textContent = `Arrived at ${curInfo.name} (Floor ${curInfo.floor})`;
  } else if (curInfo.building_id !== nextInfo.building_id) {
    el("navStepCaption").textContent =
      `Step ${stepIndex + 1}/${fullPath.length}: Walk ${curInfo.building_id} → ${nextInfo.building_id}`;
  } else if (curInfo.floor !== nextInfo.floor) {
    el("navStepCaption").textContent =
      `Step ${stepIndex + 1}/${fullPath.length}: Floor ${curInfo.floor} → Floor ${nextInfo.floor} (${nextInfo.name})`;
  } else {
    el("navStepCaption").textContent =
      `Step ${stepIndex + 1}/${fullPath.length}: Next → ${nextInfo.name}`;
  }
}

function drawLabelWithCollisionAvoidance(ctx, text, x, y, preferAbove, placedBoxes, highlightColor) {
  const fontSize = 11;
  ctx.font = highlightColor ? `700 ${fontSize}px system-ui, sans-serif` : `500 ${fontSize}px system-ui, sans-serif`;
  const textW = ctx.measureText(text).width;
  const padX = 6;
  const boxH = 18;
  const boxW = textW + padX * 2;

  const offsets = preferAbove ? [-24, 10, -42, 26] : [10, -24, 26, -42];
  for (const dy of offsets) {
    const bx = Math.max(12, x - boxW / 2);
    const by = y + dy;
    const candidate = { x: bx, y: by, w: boxW, h: boxH };

    const collides = placedBoxes.some(
      (b) =>
        candidate.x < b.x + b.w + 4 &&
        candidate.x + candidate.w + 4 > b.x &&
        candidate.y < b.y + b.h + 2 &&
        candidate.y + candidate.h + 2 > b.y
    );

    if (!collides) {
      placedBoxes.push(candidate);
      ctx.fillStyle = "rgba(12, 18, 29, 0.86)";
      ctx.fillRect(candidate.x, candidate.y, candidate.w, candidate.h);
      ctx.strokeStyle = highlightColor ? "rgba(251, 191, 36, 0.45)" : "rgba(255, 255, 255, 0.1)";
      ctx.lineWidth = 1;
      ctx.strokeRect(candidate.x, candidate.y, candidate.w, candidate.h);

      ctx.fillStyle = highlightColor || "#e2e8f0";
      ctx.textAlign = "center";
      ctx.fillText(text, candidate.x + candidate.w / 2, candidate.y + 13);
      ctx.textAlign = "left";
      return;
    }
  }
}

function drawCanvas() {
  const canvas = el("floorCanvas");
  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.width / dpr;
  const h = canvas.height / dpr;

  pulsePhase += 0.05;
  ctx.clearRect(0, 0, w, h);

  ctx.fillStyle = "#0c121d";
  ctx.fillRect(0, 0, w, h);

  ctx.strokeStyle = "rgba(148, 163, 184, 0.045)";
  ctx.lineWidth = 1;
  for (let x = 0; x < w; x += 44) {
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
  }
  for (let y = 0; y < h; y += 44) {
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
  }

  const hubs = {
    LIB: {
      id: "LIB",
      title: "Central Library",
      sub: "2 Floors • Hub",
      x: w * 0.34,
      y: h * 0.54,
      color: "#38bdf8",
    },
    DEPT_A: {
      id: "DEPT_A",
      title: "Department A",
      sub: "3 Floors • North",
      x: w * 0.68,
      y: h * 0.31,
      color: "#34d399",
    },
    DEPT_B: {
      id: "DEPT_B",
      title: "Department B",
      sub: "3 Floors • South",
      x: w * 0.68,
      y: h * 0.75,
      color: "#a78bfa",
    },
  };

  buildingHitTargets = Object.values(hubs);

  const curNodeId = fullPath[Math.min(stepIndex, Math.max(0, fullPath.length - 1))];
  const curInfo = curNodeId ? parseNodeInfo(curNodeId) : null;
  const destBldgId = selectedRoom?.building_id || "DEPT_A";
  const inTargetBldg = mode === "NAVIGATE" && curInfo && curInfo.building_id === destBldgId && destBldgId !== "LIB";

  if (!inTargetBldg) {
    // Campus plane: Library <-> Departments
    ctx.strokeStyle = "rgba(148, 163, 184, 0.24)";
    ctx.lineWidth = 3.5;
    ctx.setLineDash([7, 7]);
    ctx.beginPath();
    ctx.moveTo(hubs.LIB.x, hubs.LIB.y);
    ctx.lineTo(hubs.DEPT_A.x, hubs.DEPT_A.y);
    ctx.moveTo(hubs.LIB.x, hubs.LIB.y);
    ctx.lineTo(hubs.DEPT_B.x, hubs.DEPT_B.y);
    ctx.stroke();
    ctx.setLineDash([]);

    if (mode === "NAVIGATE" && fullPath.length) {
      const startBldgId = parseNodeInfo(fullPath[0]).building_id;
      const a = hubs[startBldgId] || hubs.LIB;
      const b = hubs[destBldgId] || hubs.DEPT_A;

      ctx.strokeStyle = "#fbbf24";
      ctx.lineWidth = 4.5;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();

      const progress = fullPath.length > 1 ? stepIndex / (fullPath.length - 1) : 0;
      const ax = a.x + (b.x - a.x) * Math.min(1, progress * 1.4);
      const ay = a.y + (b.y - a.y) * Math.min(1, progress * 1.4);
      drawRadarAvatar(ctx, ax, ay);
    }

    for (const hub of buildingHitTargets) {
      const isGoal = mode === "NAVIGATE" && hub.id === destBldgId;
      ctx.beginPath();
      ctx.arc(hub.x, hub.y, 20, 0, Math.PI * 2);
      ctx.fillStyle = isGoal ? "rgba(244, 114, 182, 0.22)" : "#162238";
      ctx.fill();
      ctx.lineWidth = 3;
      ctx.strokeStyle = isGoal ? "#f472b6" : hub.color;
      ctx.stroke();

      ctx.font = "700 13px system-ui, sans-serif";
      ctx.fillStyle = "#f8fafc";
      ctx.textAlign = "center";
      ctx.fillText(hub.title, hub.x, hub.y + 38);

      ctx.font = "500 11px system-ui, sans-serif";
      ctx.fillStyle = "#94a3b8";
      ctx.fillText(hub.sub, hub.x, hub.y + 53);
    }
    ctx.textAlign = "left";
    return;
  }

  // Shifted into target building: clean floor-by-floor view
  const bldgHub = hubs[destBldgId] || hubs.DEPT_A;
  const bldgObj = (campusMap?.buildings || []).find((b) => b.building_id === destBldgId);
  const floors = bldgObj?.floors || [{ floor_number: 1 }, { floor_number: 2 }, { floor_number: 3 }];

  const laneY = (fl) => {
    const idx = Math.max(0, floors.findIndex((f) => Number(f.floor_number) === Number(fl)));
    const total = Math.max(1, floors.length);
    return h * (0.25 + (idx / Math.max(1, total - 1)) * 0.52);
  };

  const leftX = Math.max(380, w * 0.35);
  const rightX = Math.max(leftX + 260, w * 0.9);

  for (const f of floors) {
    const ly = laneY(f.floor_number);
    ctx.strokeStyle = "rgba(148, 163, 184, 0.16)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(leftX, ly);
    ctx.lineTo(rightX, ly);
    ctx.stroke();

    ctx.font = "700 12px system-ui, sans-serif";
    ctx.fillStyle = "#94a3b8";
    ctx.fillText(`${bldgHub.title} • Floor ${f.floor_number}`, leftX, ly - 12);
  }

  const bldgPath = fullPath.filter((nid) => parseNodeInfo(nid).building_id === destBldgId);
  const pts = bldgPath.map((nid, i) => {
    const info = parseNodeInfo(nid);
    const frac = bldgPath.length > 1 ? i / (bldgPath.length - 1) : 0.5;
    return {
      id: nid,
      info,
      x: leftX + 24 + frac * Math.max(120, rightX - leftX - 80),
      y: laneY(info.floor),
    };
  });

  ctx.strokeStyle = "#fbbf24";
  ctx.lineWidth = 4;
  for (let i = 0; i < pts.length - 1; i++) {
    ctx.beginPath();
    ctx.moveTo(pts[i].x, pts[i].y);
    ctx.lineTo(pts[i + 1].x, pts[i + 1].y);
    ctx.stroke();
  }

  const placedBoxes = [];
  pts.forEach((p, idx) => {
    const isTarget = idx === pts.length - 1;
    const isCurrent = p.id === curNodeId;

    ctx.beginPath();
    ctx.arc(p.x, p.y, isTarget ? 10 : 6, 0, Math.PI * 2);
    ctx.fillStyle = isTarget ? "#f472b6" : "#fbbf24";
    ctx.fill();

    // Label key points cleanly so nearby nodes never overlap
    const prevFloor = idx > 0 ? pts[idx - 1].info.floor : p.info.floor;
    const isFloorChange = p.info.floor !== prevFloor;
    if (isTarget || isCurrent || idx === 0 || isFloorChange) {
      const label = isTarget ? selectedRoom.room_name : p.info.name;
      drawLabelWithCollisionAvoidance(
        ctx,
        label,
        p.x,
        p.y,
        idx % 2 === 0,
        placedBoxes,
        isTarget ? "#fbcfe8" : isCurrent ? "#fde68a" : null
      );
    }
  });

  const activePos = Math.max(0, pts.findIndex((p) => p.id === curNodeId));
  const avatarPt = pts[activePos] || pts[0];
  if (avatarPt) {
    drawRadarAvatar(ctx, avatarPt.x, avatarPt.y);
  }
}

function drawRadarAvatar(ctx, x, y) {
  const radius = 14 + Math.sin(pulsePhase) * 4;
  ctx.beginPath();
  ctx.arc(x, y, radius, 0, Math.PI * 2);
  ctx.strokeStyle = "rgba(52, 211, 153, 0.55)";
  ctx.lineWidth = 2;
  ctx.stroke();

  ctx.beginPath();
  ctx.arc(x, y, 7, 0, Math.PI * 2);
  ctx.fillStyle = "#34d399";
  ctx.fill();
  ctx.lineWidth = 2;
  ctx.strokeStyle = "#0c121d";
  ctx.stroke();
}

function animationLoop() {
  drawCanvas();
  requestAnimationFrame(animationLoop);
}

function bindEvents() {
  el("floorCanvas").addEventListener("click", (e) => {
    if (mode !== "OVERVIEW") return;
    const rect = e.target.getBoundingClientRect();
    const cx = e.clientX - rect.left;
    const cy = e.clientY - rect.top;

    for (const b of buildingHitTargets) {
      if (Math.hypot(cx - b.x, cy - b.y) <= 28) {
        el("preferBuildingSelect").value = b.id;
        fetchTop3();
        break;
      }
    }
  });

  el("changeCriteriaBtn").addEventListener("click", () => {
    el("criteriaPanel").classList.toggle("hidden");
  });

  el("applyCriteriaBtn").addEventListener("click", async () => {
    await refreshHealth();
    await fetchTop3();
  });

  el("startNodeInput").addEventListener("change", fetchTop3);

  el("stepNextBtn").addEventListener("click", () => {
    if (!fullPath.length) return;
    stepIndex = Math.min(fullPath.length - 1, stepIndex + 1);
    updateNavHud();
  });

  el("stepPrevBtn").addEventListener("click", () => {
    if (!fullPath.length) return;
    stepIndex = Math.max(0, stepIndex - 1);
    updateNavHud();
  });

  el("autoWalkBtn").addEventListener("click", () => {
    if (autoWalkTimer) {
      stopAutoWalk();
      return;
    }
    el("autoWalkBtn").textContent = "Pause";
    autoWalkTimer = setInterval(() => {
      if (stepIndex < fullPath.length - 1) {
        stepIndex++;
        updateNavHud();
      } else {
        stopAutoWalk();
      }
    }, 900);
    el("applyCriteriaBtn").addEventListener("click", async () => {
    await refreshHealth();
    await fetchTop3();
    el("criteriaPanel").classList.add("hidden");
  });

  el("startNodeInput").addEventListener("keydown", async (e) => {
    if (e.key === "Enter") {
      await fetchTop3();
    }
  });
  });

  el("exitNavBtn").addEventListener("click", exitNavigationToOverview);
  el("resetViewBtn").addEventListener("click", exitNavigationToOverview);

  el("toggleLibFullBtn").addEventListener("click", async () => {
    const { day, shift } = currentDayShift();
    await apiPost("/library/overflow", { day, shift, library_full: true });
    await refreshHealth();
    await fetchTop3();
  });

  el("releaseLibBtn").addEventListener("click", async () => {
    const { day, shift } = currentDayShift();
    await apiPost("/library/overflow", { day, shift, library_full: false });
    await refreshHealth();
    await fetchTop3();
  });

  for (const id of ["daySelect", "shiftSelect", "preferBuildingSelect", "preferStairsCheck", "preferLowerFloorCheck"]) {
    el(id).addEventListener("change", async () => {
      await refreshHealth();
      await fetchTop3();
    });
  }

  window.addEventListener("resize", resizeCanvas);
}

bindEvents();
resizeCanvas();
loadMap().then(async () => {
  await refreshHealth();
  await fetchTop3();
  animationLoop();
}).catch((err) => {
  el("healthBadge").className = "status-pill err";
  el("healthBadge").textContent = `Error: ${err.message}`;
  animationLoop();
});