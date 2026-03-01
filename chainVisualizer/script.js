
const TEST_INDEX = 0;

let peerLedgers = {};
let cy = null;
let commonHashes = new Set();

async function loadLedgers() {
  const res = await fetch(RUN_FILE);
  if (!res.ok) throw new Error(`Failed to load ${RUN_FILE}: ${res.status}`);
  const run = await res.json();

  // Your file appears to have tests[0].ledgers[0] as the peer-ledger array.
  const ledgersArr = run?.tests?.[TEST_INDEX]?.ledgers?.[0];
  if (!Array.isArray(ledgersArr)) {
    throw new Error("Unexpected ledgers format. Inspect run.tests[0].ledgers in console.");
  }

  ledgersArr.forEach((ledger, i) => {
    peerLedgers[`Peer_${i}`] = Array.isArray(ledger) ? ledger : [];
  });
}

function computeCommonHashes() {
  const ledgers = Object.values(peerLedgers);
  if (!ledgers.length) {
    commonHashes = new Set();
    return;
  }

  let intersection = new Set(ledgers[0].map(b => b.hash));
  for (let i = 1; i < ledgers.length; i++) {
    const set = new Set(ledgers[i].map(b => b.hash));
    intersection = new Set([...intersection].filter(h => set.has(h)));
  }
  commonHashes = intersection;
}

// True longest-chain in a DAG based on parents[] restricted to blocks present in this peer ledger.
// Computes longest ancestral path length for each node and backtracks from the best tip.
function computeLongestChainHashes(blocks) {
  const byHash = new Map(blocks.map(b => [b.hash, b]));
  const memoLen = new Map();       // hash -> best length ending at hash
  const bestPrev = new Map();      // hash -> parent hash chosen for best path

  function bestLen(hash) {
    if (memoLen.has(hash)) return memoLen.get(hash);
    const b = byHash.get(hash);
    if (!b) return 0;

    let best = 1;
    let prev = null;

    for (const p of (b.parents || [])) {
      if (!byHash.has(p)) continue;           // only consider parents that exist in this ledger
      const candidate = 1 + bestLen(p);
      if (candidate > best) {
        best = candidate;
        prev = p;
      }
    }

    memoLen.set(hash, best);
    bestPrev.set(hash, prev);
    return best;
  }

  // choose tip by max bestLen; tie-break by height then hash for stability
  let tip = null;
  let tipLen = -1;

  for (const b of blocks) {
    const len = bestLen(b.hash);
    if (len > tipLen) {
      tipLen = len;
      tip = b.hash;
    } else if (len === tipLen && tip != null) {
      const cur = b.hash;
      const curH = b.height ?? -1;
      const tipB = byHash.get(tip);
      const tipH = tipB?.height ?? -1;
      if (curH > tipH || (curH === tipH && String(cur) > String(tip))) {
        tip = cur;
      }
    }
  }

  const longest = new Set();
  let cur = tip;
  while (cur) {
    longest.add(cur);
    cur = bestPrev.get(cur) || null;
  }
  return longest;
}

function parasiteSvgDataUri() {
  const svg = encodeURIComponent(`
    <svg xmlns="http://www.w3.org/2000/svg" width="120" height="80" viewBox="0 0 120 80">
      <line x1="20" y1="15" x2="100" y2="65" stroke="red" stroke-width="10" stroke-linecap="round"/>
      <line x1="100" y1="15" x2="20" y2="65" stroke="red" stroke-width="10" stroke-linecap="round"/>
    </svg>
  `.trim());
  return `data:image/svg+xml,${svg}`;
}

function buildElementsForPeer(peerName, options) {
  const blocks = peerLedgers[peerName] || [];
  const byHash = new Map(blocks.map(b => [b.hash, b]));
  const longestHashes = computeLongestChainHashes(blocks);

  const includeHash = (h) => !options.onlyLongest || longestHashes.has(h);

  const nodeIds = new Set();
  const elements = [];
  const xSvg = parasiteSvgDataUri();

  // Add nodes
  for (const b of blocks) {
    if (!includeHash(b.hash)) continue;

    const isLongest = longestHashes.has(b.hash);
    const isCommon = commonHashes.has(b.hash);
    const isParasite = !!b.parasite;

    const id = `${peerName}|${b.hash}`;
    nodeIds.add(id);

    elements.push({
      data: {
        id,
        hash: b.hash,
        label: `h=${b.height}\n${b.hash}`,
        height: b.height,
        layer: peerName,
        // IMPORTANT: use 1/0 so selectors can test equality.
        longest: isLongest ? 1 : 0,
        common: isCommon ? 1 : 0,
        parasite: isParasite ? 1 : 0,
        ghost: 0,
        svgX: isParasite ? xSvg : "",
        peersLabel: peerName
      }
    });
  }

  // Optionally add ghost parent nodes (only if missing OR filtered out by onlyLongest)
  function ensureGhost(parentHash, inferredHeight) {
    const ghostId = `${peerName}|${parentHash}`;
    if (nodeIds.has(ghostId)) return ghostId;

    nodeIds.add(ghostId);
    elements.push({
      data: {
        id: ghostId,
        hash: parentHash,
        label: `missing\n${parentHash}`,
        height: inferredHeight,
        layer: peerName,
        longest: 0,
        common: commonHashes.has(parentHash) ? 1 : 0,
        parasite: 0,
        ghost: 1,
        svgX: "",
        peersLabel: peerName
      }
    });
    return ghostId;
  }

  // Add edges parent -> child so h=0 is on the left with rankDir=LR
  for (const b of blocks) {
    if (!includeHash(b.hash)) continue;

    const childId = `${peerName}|${b.hash}`;
    const parents = b.parents || [];

    for (const p of parents) {
      if (!p) continue;

      // parent exists in ledger AND passes filtering => add edge
      if (byHash.has(p) && includeHash(p)) {
        const parentId = `${peerName}|${p}`;
        // Both endpoints should exist (because we added nodes above)
        if (nodeIds.has(parentId) && nodeIds.has(childId)) {
          elements.push({
            data: {
              id: `${parentId}__${childId}`,
              source: parentId,
              target: childId,
              peersLabel: peerName
            }
          });
        }
        continue;
      }

    const parentId = ensureGhost(p, (b.height != null ? b.height - 1 : null));
    elements.push({
        data: {
        id: `${parentId}__${childId}`,
        source: parentId,
        target: childId,
        peersLabel: peerName
        }
    });
    }
  }

  return elements;
}

function buildElements(selectedPeers, options) {
  const all = [];
  for (const peer of selectedPeers) {
    all.push(...buildElementsForPeer(peer, options));
  }
  return all;
}

function buildElementsGlobal(selectedPeers, options) {
  // Union of blocks across selected peers
  const unionByHash = new Map(); // hash -> {hash,height,parents:Set, parasitePeers:Set, presentPeers:Set}
  const peers = Object.keys(peerLedgers); // ALWAYS all peers


  for (const peer of peers) {
    const blocks = peerLedgers[peer] || [];
    for (const b of blocks) {
      const h = b.hash;
      if (!unionByHash.has(h)) {
        unionByHash.set(h, {
          hash: h,
          height: b.height ?? null,
          parents: new Set(),
          parasitePeers: new Set(),
          presentPeers: new Set(),
        });
      }
      const rec = unionByHash.get(h);
      rec.presentPeers.add(peer);
      if (b.height != null && (rec.height == null || b.height > rec.height)) rec.height = b.height;
      (b.parents || []).forEach(p => { if (p) rec.parents.add(p); });
      if (b.parasite) rec.parasitePeers.add(peer);
    }
  }

  // Build a block-like array for longest chain calc (restricted to union)
  const unionBlocks = [...unionByHash.values()].map(r => ({
    hash: r.hash,
    height: r.height,
    parents: [...r.parents],
  }));

  const longestHashes = computeLongestChainHashes(unionBlocks);

  const includeHash = (h) => !options.onlyLongest || longestHashes.has(h);

  const elements = [];
  const nodeIds = new Set();
  const xSvg = parasiteSvgDataUri();

  // Nodes
  for (const r of unionByHash.values()) {
    if (!includeHash(r.hash)) continue;

    const isLongest = longestHashes.has(r.hash);
    const isCommonAllPeers = commonHashes.has(r.hash); // global common still based on ALL peers overall
    const isParasite = r.parasitePeers.size > 0;

    nodeIds.add(r.hash);
    elements.push({
      data: {
        id: r.hash,
        hash: r.hash,
        label: `h=${r.height}\n${r.hash}`,
        height: r.height,
        layer: "GLOBAL",
        longest: isLongest ? 1 : 0,
        common: isCommonAllPeers ? 1 : 0,
        parasite: isParasite ? 1 : 0,
        ghost: 0,
        svgX: isParasite ? xSvg : "",
        peersLabel: `Peers: ${[...r.presentPeers].sort().join(", ")}`,
      }
    });
  }

  // Ghost helper
  function ensureGhost(parentHash, inferredHeight) {
    if (nodeIds.has(parentHash)) return parentHash;
    nodeIds.add(parentHash);
    elements.push({
      data: {
        id: parentHash,
        hash: parentHash,
        label: `missing\n${parentHash}`,
        height: inferredHeight,
        layer: "GLOBAL",
        longest: 0,
        common: commonHashes.has(parentHash) ? 1 : 0,
        parasite: 0,
        ghost: 1,
        svgX: ""
      }
    });
    return parentHash;
  }

  // Edges (parent -> child) across union
  for (const r of unionByHash.values()) {
    if (!includeHash(r.hash)) continue;
    const childId = r.hash;

    for (const p of r.parents) {
      if (!p) continue;

      // parent is in union and included
      if (unionByHash.has(p) && includeHash(p)) {
        if (nodeIds.has(p) && nodeIds.has(childId)) {
          elements.push({
            data: {
              id: `${p}__${childId}`,
              source: p,
              target: childId
            }
          });
        }
        continue;
      }

      // otherwise only if ghosts enabled
        const parentId = ensureGhost(p, (r.height != null ? r.height - 1 : null));
        elements.push({
          data: {
            id: `${parentId}__${childId}`,
            source: parentId,
            target: childId
          }
        });
      
    }
  }

  return elements;
}


function getStyle() {
  return [
    {
      selector: "node",
      style: {
        "shape": "round-rectangle",
        "width": 100,
        "height": 60,
        "background-color": "#ffffff",
        "border-width": 2,
        "border-color": "#666",
        "label": "data(label)",
        "text-wrap": "wrap",
        "text-max-width": 90,
        "font-size": 9,
        "text-valign": "center",
        "text-halign": "center"
      }
    },

    // Longest chain => green border
    {
      selector: "node[longest = 1]",
      style: {
        "border-color": "#2ecc71",
        "border-width": 6
      }
    },

    // Blue fill ONLY if (in all peers) AND (on longest chain)
    {
      selector: "node[common = 1][longest = 1]",
      style: {
        "background-color": "#3498db"
      }
    },

    // Parasite => draw an X overlay (keeps label readable)
    {
      selector: "node[parasite = 1]",
      style: {
        "background-image": "data(svgX)",
        "background-fit": "contain",
        "background-image-opacity": 0.65
      }
    },

    // Ghost nodes
    {
        selector: "node[ghost = 1]",
        style: {
            "border-style": "dotted",
            "border-color": "#999",
            "background-color": "#f2f2f2",
            "color": "#444"
        }
    },


    {
      selector: "edge",
      style: {
        "curve-style": "bezier",
        "source-arrow-shape": "triangle",
        "target-arrow-shape": "none",
        "width": 2,
        "line-color": "#999",
        "source-arrow-color": "#999"
      }
    }
  ];
}

function attachTooltipHandlers(cyInstance) {
  cyInstance.off("mouseover", "node");
    cyInstance.off("mouseout", "node");
    cyInstance.off("mousemove");
    cyInstance.off("tap");

  const tip = document.getElementById("tooltip");
  if (!tip) return;

  let active = false;

  function moveTip(evt) {
    // Use the browser mouse coordinates
    const e = evt.originalEvent;
    if (!e) return;
    tip.style.left = (e.clientX + 12) + "px";
    tip.style.top  = (e.clientY + 12) + "px";
  }

  cyInstance.on("mouseover", "node", (evt) => {
    const n = evt.target;
    const peersLabel = n.data("peersLabel") || "Unknown peer";
    tip.textContent = peersLabel;
    tip.style.display = "block";
    active = true;
  });

  cyInstance.on("mouseout", "node", () => {
    tip.style.display = "none";
    active = false;
  });

  cyInstance.on("mousemove", (evt) => {
    if (!active) return;
    moveTip(evt);
  });

  // Also hide tooltip if user taps/clicks empty canvas
  cyInstance.on("tap", (evt) => {
    if (evt.target === cyInstance) {
      tip.style.display = "none";
      active = false;
    }
  });
}

function applyLayerOffsets(cy) {
  // These are the knobs:
  const GLOBAL_OFFSET = -900;     // move global above peers
  const LANE_PADDING = 120;       // extra spacing between lanes

  // Determine peer layers present
  const peerNames = new Set();
  cy.nodes().forEach(n => {
    const layer = n.data("layer");
    if (layer && layer !== "GLOBAL") peerNames.add(layer);
  });

  // Numeric sort: Peer_6 < Peer_10
  const peers = [...peerNames].sort((a, b) => {
    const ai = parseInt(a.split("_")[1] ?? "0", 10);
    const bi = parseInt(b.split("_")[1] ?? "0", 10);
    return ai - bi;
  });

  // Auto-size lane gap:
  // estimate node height (60) + min intra-height gap (MIN_Y_GAP=75) + padding
  // If you change node height in style, update NODE_H below.
  const NODE_H = 60;
  const MIN_Y_GAP = 75; // keep consistent with draw()
  const PEER_LANE_GAP = NODE_H + MIN_Y_GAP + LANE_PADDING; // e.g. 60+75+120=255

  // Map peer -> yOffset
  const peerOffset = new Map();
  peers.forEach((p, i) => peerOffset.set(p, i * PEER_LANE_GAP));

  // Apply offsets: global up, peers down by lane
  cy.nodes().forEach(n => {
    const layer = n.data("layer");
    if (layer === "GLOBAL") {
      n.position("y", n.position("y") + GLOBAL_OFFSET);
    } else if (peerOffset.has(layer)) {
      n.position("y", n.position("y") + peerOffset.get(layer));
    }
  });
}

function spreadYByHeight(cy, minGap) {
  const buckets = new Map(); // height -> nodes[]
  cy.nodes().forEach(n => {
    const h = n.data("height");
    const layer = n.data("layer") || "UNKNOWN";
    const key = (h != null && Number.isFinite(h)) ? `${layer}:${h}` : `${layer}:noheight`;

    if (!buckets.has(key)) buckets.set(key, []);
    buckets.get(key).push(n);
  });

  for (const [h, nodes] of buckets.entries()) {
    // sort by current y so we preserve dagre’s ordering
    nodes.sort((a, b) => a.position("y") - b.position("y"));

    // enforce minimum vertical spacing
    let lastY = null;
    for (const n of nodes) {
      const y = n.position("y");
      if (lastY == null) {
        lastY = y;
        continue;
      }
      if (y - lastY < minGap) {
        const newY = lastY + minGap;
        n.position("y", newY);
        lastY = newY;
      } else {
        lastY = y;
      }
    }
  }
}


function draw(selectedPeers, options) {
  const elements = [
  ...(options.showGlobal ? buildElementsGlobal(selectedPeers, options) : []),
  ...(options.showPeers ? buildElements(selectedPeers, options) : [])
];



  if (cy) cy.destroy();

  cy = cytoscape({
  container: document.getElementById("cy"),
  elements,
  style: getStyle(),
  wheelSensitivity: 0.35,
  minZoom: 0.03,
  maxZoom: 8
});

const X_SPACING = 140;     // tweak
const MIN_Y_GAP = 75;      // tweak

attachTooltipHandlers(cy);

const layout = cy.layout({
  name: "dagre",
  rankDir: "LR",
  nodeSep: 60,
  rankSep: 120,
  edgeSep: 20,
  // helps overlap a bit in some cases:
  nodeDimensionsIncludeLabels: true
});

layout.run();

// 1) Snap X to height
cy.nodes().forEach(n => {
  const h = n.data("height");
  if (h != null && Number.isFinite(h)) {
    n.position("x", h * X_SPACING);
  }
});

// 2) Spread nodes in Y within each height bucket to reduce overlap
spreadYByHeight(cy, MIN_Y_GAP);
applyLayerOffsets(cy);

// optional: re-fit after adjustments
cy.fit();

}

function initPanelsUI() {
  const controls = document.getElementById("controls");
  const toggleControlsBtn = document.getElementById("toggleControlsBtn");
  const showControlsBtn = document.getElementById("showControlsBtn");

  const statsPanel = document.getElementById("statsPanel");
  const toggleStatsBtn = document.getElementById("toggleStatsBtn");
  const closeStatsBtn = document.getElementById("closeStatsBtn");

  toggleControlsBtn.onclick = () => {
    controls.classList.add("hidden");
    showControlsBtn.classList.remove("hidden");
  };

  showControlsBtn.onclick = () => {
    controls.classList.remove("hidden");
    showControlsBtn.classList.add("hidden");
  };

  toggleStatsBtn.onclick = () => {
    statsPanel.classList.toggle("hidden");
    toggleStatsBtn.textContent = statsPanel.classList.contains("hidden") ? "Show stats" : "Hide stats";
  };

  closeStatsBtn.onclick = () => {
    statsPanel.classList.add("hidden");
    toggleStatsBtn.textContent = "Show stats";
  };
}

function initUI() {
  const select = document.getElementById("peerSelect");
  const globalTree = document.getElementById("globalTree");

  // populate peers
  select.innerHTML = "";
  Object.keys(peerLedgers).forEach(p => {
    const opt = document.createElement("option");
    opt.value = p;
    opt.textContent = p;
    select.appendChild(opt);
  });

  // default: select first peer
  if (select.options.length) select.options[0].selected = true;

  function refresh() {
    const selected = [...select.selectedOptions].map(o => o.value);

    draw(selected, {
  onlyLongest: false,
  showPeers: true,                 // always show selected peers
  showGlobal: globalTree.checked    // global tree only if checked
});


    }


  select.addEventListener("change", refresh);
  globalTree.addEventListener("change", refresh);

  document.getElementById("fitBtn").onclick = () => cy && cy.fit();
  document.getElementById("zoomInBtn").onclick = () => cy && cy.zoom(cy.zoom() * 1.3);
  document.getElementById("zoomOutBtn").onclick = () => cy && cy.zoom(cy.zoom() * 0.7);

  refresh();
  initPanelsUI();
}

function computeGlobalStats(selectedPeers) {
  const peers =
    (selectedPeers && selectedPeers.length > 0)
      ? selectedPeers
      : Object.keys(peerLedgers);

  // ---- Build union blocks (global tree) ----
  const unionByHash = new Map(); // hash -> {hash,height,parents:Set, presentPeers:Set}
  const missingParents = new Set(); // parent hashes referenced but absent from union

  for (const peer of peers) {
    for (const b of (peerLedgers[peer] || [])) {
      const h = b.hash;
      if (!unionByHash.has(h)) {
        unionByHash.set(h, {
          hash: h,
          height: b.height ?? null,
          parents: new Set(),
          presentPeers: new Set(),
        });
      }
      const rec = unionByHash.get(h);
      rec.presentPeers.add(peer);
      if (b.height != null && (rec.height == null || b.height > rec.height)) rec.height = b.height;
      (b.parents || []).forEach(p => { if (p) rec.parents.add(p); });
    }
  }

  // After union is known, track missing parents
  for (const rec of unionByHash.values()) {
    for (const p of rec.parents) {
      if (!unionByHash.has(p)) missingParents.add(p);
    }
  }

  const unionBlocks = [...unionByHash.values()].map(r => ({
    hash: r.hash,
    height: r.height,
    parents: [...r.parents],
  }));

  // ---- Global longest chain (DAG longest path) ----
  const globalLongest = computeLongestChainHashes(unionBlocks);
  const orphanedBlocks = unionBlocks.filter(b => !globalLongest.has(b.hash)).length;

  // ---- Fork sites and fork branch length histogram ----
  // Build children adjacency: parent -> [children]
  const children = new Map();
  for (const b of unionBlocks) {
    for (const p of (b.parents || [])) {
      if (!p) continue;
      if (!children.has(p)) children.set(p, []);
      children.get(p).push(b.hash);
    }
  }

  const forkSites = [];
  for (const [p, kids] of children.entries()) {
    if ((kids?.length || 0) > 1) forkSites.push({ parent: p, kids });
  }

  // Branch length definition (practical):
  // For each fork site parent P and each child C:
  // walk forward along descendants choosing the path that continues the branch
  // until you hit:
  //  - a node on the global longest chain (re-merges conceptually),
  //  - another fork site (branch splits again),
  //  - or a leaf (no children).
  //
  // We count the maximum walk length for that child (greedy by choosing the child that yields longest).
  const forkParentSet = new Set(forkSites.map(x => x.parent));

  function branchLenFrom(startChild) {
    let len = 1;
    let cur = startChild;

    while (true) {
      if (globalLongest.has(cur)) break;
      const kids = children.get(cur) || [];
      if (kids.length === 0) break;
      if (kids.length > 1) break;           // hits another fork site (at cur)
      // single-child continuation
      cur = kids[0];
      len += 1;
      // guard against cycles (shouldn't exist), but prevent infinite loop
      if (len > 100000) break;
    }
    return len;
  }

  const forkBranchLenHist = new Map(); // len -> count
  for (const fs of forkSites) {
    for (const child of fs.kids) {
      if (globalLongest.has(child)) continue; // <-- key fix
      const bl = branchLenFrom(child);
      forkBranchLenHist.set(bl, (forkBranchLenHist.get(bl) || 0) + 1);
    }
  }

  // ---- Longest common chain across peers (intersection of each peer’s *own* longest chain) ----
  // Use peers set above (selected or all).
  const perPeerLongestSets = peers.map(peer => {
    const blocks = peerLedgers[peer] || [];
    return computeLongestChainHashes(blocks);
  });

  let commonLongest = new Set(perPeerLongestSets[0] || []);
  for (let i = 1; i < perPeerLongestSets.length; i++) {
    const s = perPeerLongestSets[i];
    commonLongest = new Set([...commonLongest].filter(h => s.has(h)));
  }

  // The intersection of chains is usually a single chain prefix; measure its longest-path length robustly:
  // Build parent links from unionBlocks and compute longest path restricted to commonLongest set.
  const byHash = new Map(unionBlocks.map(b => [b.hash, b]));
  const memo = new Map();
  function commonLen(hash) {
    if (!commonLongest.has(hash)) return 0;
    if (memo.has(hash)) return memo.get(hash);
    const b = byHash.get(hash);
    if (!b) return 1;
    let best = 1;
    for (const p of (b.parents || [])) {
      if (!commonLongest.has(p)) continue;
      best = Math.max(best, 1 + commonLen(p));
    }
    memo.set(hash, best);
    return best;
  }

  let longestCommonChainLen = 0;
  for (const h of commonLongest) {
    longestCommonChainLen = Math.max(longestCommonChainLen, commonLen(h));
  }

  // ---- Summaries ----
  const totalBlocks = unionBlocks.length;
  const totalGhosts = missingParents.size;
  const forkSitesCount = forkSites.length;

  // Sort histogram for display
  const histPairs = [...forkBranchLenHist.entries()].sort((a,b) => a[0]-b[0]);

  return {
    peersUsed: peers,
    totalBlocks,
    totalGhosts,
    globalLongestLen: globalLongest.size,
    orphanedBlocks,
    forkSitesCount,
    forkBranchLenHist: histPairs,
    longestCommonChainLen,
    commonLongestSetSize: commonLongest.size,
  };
}

function renderStats(stats) {
  const el = document.getElementById("statsBody");
  if (!el) return;

  const lines = [];
  lines.push(`Peers used for stats: ${stats.peersUsed.join(", ")}`);
  lines.push(`Total blocks (union): ${stats.totalBlocks}`);
  lines.push(`Ghost blocks (missing parents): ${stats.totalGhosts}`);
  lines.push(`Global longest chain length: ${stats.globalLongestLen}`);
  lines.push(`Orphaned blocks (not in global longest): ${stats.orphanedBlocks}`);
  lines.push(`Fork sites (#nodes with >1 child): ${stats.forkSitesCount}`);
  lines.push(``);
  lines.push(`Fork branch length histogram (length -> count):`);
  if (stats.forkBranchLenHist.length === 0) {
    lines.push(`  (none)`);
  } else {
    for (const [len, count] of stats.forkBranchLenHist) {
      lines.push(`  ${len} -> ${count}`);
    }
  }
  lines.push(``);
  lines.push(`Longest common chain length (intersection of per-peer longest chains): ${stats.longestCommonChainLen}`);
  lines.push(`Common-longest set size (raw intersection size): ${stats.commonLongestSetSize}`);

  el.textContent = lines.join("\n");
}


async function loadLedgersFromObject(run) {
  const ledgersArr = run?.tests?.[TEST_INDEX]?.ledgers?.[0];

  if (!Array.isArray(ledgersArr)) {
    throw new Error("Unexpected ledgers format. Inspect run.tests[0].ledgers.");
  }

  peerLedgers = {};
  ledgersArr.forEach((ledger, i) => {
    peerLedgers[`Peer_${i}`] = Array.isArray(ledger) ? ledger : [];
  });

  computeCommonHashes();

    const stats = computeGlobalStats(Object.keys(peerLedgers));
    renderStats(stats);

    initUI();

}

function main() {
  const fileInput = document.getElementById("fileInput");

  fileInput.addEventListener("change", (event) => {
    const file = event.target.files[0];
    if (!file) return;

    const reader = new FileReader();

    reader.onload = function(e) {
      try {
        const run = JSON.parse(e.target.result);
        loadLedgersFromObject(run);
      } catch (err) {
        console.error(err);
        alert("Invalid JSON file.");
      }
    };

    reader.readAsText(file);
  });
}

main();

