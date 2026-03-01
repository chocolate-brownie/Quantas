# Block / Ledger Visualizer

A lightweight browser-based visualizer for block DAG / blockchain-style ledgers across multiple peers.

It allows you to:
- Select one or more **peer ledgers** to display
- Display a merged **Global tree** (union of all peers)
- Visually distinguish important block properties
- View structural statistics about the global tree


# Features

## Visual Indicators

- **Blue Fill**
  - Block is on the longest chain **and**
  - Block is present in **all peers**

- **Green Border**
  - Block is on the longest chain (per-peer or global view)

- **Red X Overlay**
  - Block is marked as a parasite

- **Dotted Border**
  - Ghost block (a referenced parent that does not exist in the current dataset view)

- **Hover Tooltip**
  - Peer nodes show their peer name
  - Global nodes show which peers contain that block



# Project Structure

Place all files in the same directory:

```
index.html
style.css
script.js
<your_run_file>.json
```

Example:

```
bitcoinRun_EXP1.txt
```



# Running the Visualizer

You must serve the files through a local web server. Do not open the HTML file directly.

From the project directory, run:

```bash
python -m http.server 8000
```

Then open your browser and navigate to:

```
http://localhost:8000
```



# Loading Data

1. Click the file input control.
2. Select your run file (JSON format).
3. The visualization and stats will render automatically.



# Expected Data Format

The loader expects the JSON structure:

```
tests[TEST_INDEX].ledgers[0]
```

This should contain an array of peer ledgers.

Each peer ledger should be an array of blocks:

```json
{
  "hash": "block_id",
  "height": 12,
  "parents": ["parent_hash_1", "parent_hash_2"],
  "parasite": false
}
```

Notes:

- `parents` may be empty or missing for genesis blocks.
- If a parent is referenced but not found, a **ghost block** is created automatically.



# Using the Interface

## Peer Selection

- Use the multi-select list to choose which peer ledgers to display.
- Multiple peers can be shown simultaneously.

## Global Tree Toggle

- Enables a merged tree built from **all peers**.
- The global tree is displayed in its own lane to avoid overlapping peer lanes.

## Zoom & Navigation

- **Fit**: Auto-fit graph to screen
- **+ / −**: Zoom controls
- Mouse wheel: Zoom
- Drag: Pan



# Stats Panel

The stats panel includes:

- Total blocks (union)
- Ghost block count
- Global longest chain length
- Orphaned block count
- Fork site count
- Fork branch length histogram
- Longest common chain length
  - Intersection of each peer’s individual longest chain

Stats are computed once per file load.



# Layout Controls (Advanced Tuning)

You can adjust layout behavior in `script.js`:

- `X_SPACING` — Horizontal spacing per height
- `MIN_Y_GAP` — Minimum vertical spacing within a height bucket
- `LANE_PADDING` — Vertical separation between peer lanes
- `GLOBAL_OFFSET` — Vertical offset for global tree

Adjust these if:
- Blocks overlap
- Peer lanes appear too close
- Layout appears too compressed



# Troubleshooting

## Nothing Appears

- Confirm you are running:
  ```bash
  python -m http.server 8000
  ```
- Confirm you are visiting:
  ```
  http://localhost:8000
  ```
- Confirm your JSON file is valid.

## Tooltips Not Working

Ensure `index.html` contains:

```html
<div id="tooltip"></div>
```

And that tooltip handlers are attached after Cytoscape is created.

## Overlapping Lanes

Increase:
- `LANE_PADDING`
- `MIN_Y_GAP`



# Summary

This tool provides a structural visualization of peer block DAGs and their merged global tree, including longest chain analysis, fork detection, and shared-chain statistics.

Serve locally with:

```bash
python -m http.server 8000
```

Open:

```
http://localhost:8000
```

Load your run file and explore.
