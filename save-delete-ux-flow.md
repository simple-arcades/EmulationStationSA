# Save Delete UX Flow — Native ES Implementation

## Overview

Delete save states from within EmulationStation's savestates system. Two-phase approach: automatic cleanup of watcher-created files, then optional save-RAM cleanup with user confirmation.

---

## Entry Point

**Where:** `GuiGamelistOptions` menu (the Select-button menu when browsing a gamelist)  
**Condition:** Only show "DELETE THIS SAVE" when `mSystem->getName() == "savestates"`  
**Position:** Add as a new row in the options menu, likely near the bottom (after metadata edit, before close)

```
┌─────────────────────────────────┐
│  GAME OPTIONS                   │
│                                 │
│  Edit This Game's Metadata      │
│  ▶ DELETE THIS SAVE             │  ← only in savestates system
│  Close                          │
└─────────────────────────────────┘
```

**Visual style:** Use `SA_TEXT_COLOR` / `saFont()` / `FONT_SIZE_MEDIUM` consistent with other menu items. Consider making the text red or using a warning color if SAStyle.h has one, to signal destructiveness.

---

## Phase 1: Confirm & Delete Watcher Files

User selects "DELETE THIS SAVE" → immediate confirmation dialog:

```
┌─────────────────────────────────────────┐
│                                         │
│   Delete save state?                    │
│                                         │
│   "Super Mario World - Slot 3"          │  ← game name from gamelist
│                                         │
│   This will permanently delete this     │
│   save state and its screenshot.        │
│   This cannot be undone.                │
│                                         │
│          [ YES ]    [ NO ]              │
│                                         │
└─────────────────────────────────────────┘
```

**On YES — Phase 1 executes automatically (no further user input):**

1. Read the `.metadata` file → extract `ROM=` field → derive ROM base filename
2. Delete the `.state{N}` file (raw save state)
3. Delete the `.state{N}.entry` file (ES-visible "ROM")
4. Delete the `.state{N}.metadata` file
5. Delete the screenshot: `media/images/{romname}.state{N}.png`
6. **Video check:** Parse `gamelist.xml` — does any *other* `<game>` entry reference the same `<video>` path?
   - **YES** (other saves share this video) → leave video alone
   - **NO** (this is the last reference) → delete the video file
7. Remove the `<game>` entry from `gamelist.xml` (using pugixml internally)
8. Trigger gamelist reload (parseGamelist/updateGamelist/reloadGameListView)

**On NO** → return to gamelist, nothing happens.

---

## Phase 2: Save-RAM Cleanup (Conditional)

**Trigger condition:** Phase 1 just deleted the *last* save state for a given ROM.

How to detect "last save for this ROM":
- After removing our entry from gamelist.xml, scan the remaining entries
- Check if any other entry's `.metadata` file references the same `ROM=` value
- Alternatively: glob the savestates directory for other `.entry` files matching the same ROM base name

**If this was NOT the last save** → Phase 1 is done. Show brief success feedback, return to gamelist.

**If this WAS the last save** → scan for save-RAM files and present Phase 2:

### Save-RAM File Discovery

1. From the `.metadata` `ROM=` field, extract the ROM filename (e.g., `Super Mario World (USA).sfc` or `dkong.zip`)
2. Strip the extension to get the base name: `Super Mario World (USA)` or `dkong`
3. Glob `savefiles/` for anything matching that base name:
   - `savefiles/{basename}.*` (catches `.srm`, `.mcd`, `.nvmem2`, etc.)
   - `savefiles/{basename}/` (catches subdirectory-based saves)
   - `savefiles/{basename}*` (catches `.mcd0`, `.mcd1` style numbered variants)

### Phase 2 Dialog — Only if files found

```
┌───────────────────────────────────────────────┐
│                                               │
│   Save-RAM files found                        │
│                                               │
│   This was your last save state for           │
│   "Super Mario World". The following          │
│   in-game save files were found:              │
│                                               │
│   • Super Mario World (USA).srm               │
│                                               │
│   Delete these files too?                      │
│                                               │
│   These are separate from save states —        │
│   they contain in-game progress (memory        │
│   card data, battery saves, etc.)              │
│                                               │
│          [ YES ]    [ NO ]                     │
│                                               │
└───────────────────────────────────────────────┘
```

For multiple files:

```
┌───────────────────────────────────────────────┐
│                                               │
│   Save-RAM files found                        │
│                                               │
│   This was your last save state for           │
│   "Ridge Racer Type 4". The following         │
│   in-game save files were found:              │
│                                               │
│   • Ridge Racer Type 4 (USA).mcd              │
│   • Ridge Racer Type 4 (USA).mcd1             │
│                                               │
│   Delete these files too?                      │
│                                               │
│   These are separate from save states —        │
│   they contain in-game progress (memory        │
│   card data, battery saves, etc.)              │
│                                               │
│          [ YES ]    [ NO ]                     │
│                                               │
└───────────────────────────────────────────────┘
```

**On YES** → delete listed save-RAM files, show brief success, return to gamelist.  
**On NO** → keep save-RAM files, show brief success (Phase 1 still completed), return to gamelist.

**If no save-RAM files found** → skip Phase 2 entirely. Phase 1 success, return to gamelist.

---

## Post-Delete Behavior

After all deletions complete:

1. **"DELETED!" confirmation** — show a brief `GuiMsgBox` with "DELETED!" and a single OK button. When dismissed, proceed to step 2.
2. **Gamelist reload** — call the ES internal reload path:
   - Remove the `<game>` node from the in-memory gamelist XML (already done in Phase 1 step 7)
   - Call `reloadGameListView()` or equivalent to refresh the view
   - Cursor should land on the next entry (or previous if we were at the end)
3. **No restart** — no black screen, no ES restart, instant visual feedback
4. **If savestates system is now empty** — ES should handle this gracefully (show empty system or go back to system view, depending on existing ES behavior for empty gamelists)

---

## Edge Cases & Error Handling

| Scenario | Handling |
|---|---|
| `.metadata` file missing/unreadable | Warn user: "Metadata file missing — cannot determine associated files. Delete the save entry only?" YES/NO |
| `.state` file already gone (manually deleted) | Skip silently, continue with other cleanup |
| `gamelist.xml` write fails (permissions) | Show error: "Could not update gamelist — save files were deleted but the list entry remains. You may need to restart." |
| Save-RAM glob finds unexpected files | Present whatever is found — the glob-based approach means we don't need to know all extensions |
| MAME short names (e.g., `dkong`) | Works fine — the base name from `ROM=` field in metadata is what we glob against |
| Video shared by many saves, deleting middle one | Video stays (other references exist) — only last reference triggers deletion |
| User deletes saves rapidly in succession | Each delete does its own gamelist reload — should be fine since it's in-place XML manipulation |

---

## Implementation Mapping

| UX Step | Code Location | Approach |
|---|---|---|
| Show "DELETE THIS SAVE" conditionally | `GuiGamelistOptions.cpp` | Check `mSystem->getName() == "savestates"` before adding menu row |
| Phase 1 confirmation dialog | New `GuiMsgBox` instance | Standard ES two-button dialog pattern |
| Read `.metadata` | New utility function | Simple text file parse (line-by-line, split on `=`) |
| Delete files | Standard `boost::filesystem::remove()` | Already used elsewhere in ES codebase |
| Video reference check | Parse `gamelist.xml` with pugixml | Walk `<game>` nodes, check `<video>` values |
| Remove gamelist entry | pugixml `node.parent().remove_child(node)` | Same pattern as existing metadata editor delete |
| Save-RAM glob | `boost::filesystem::directory_iterator` | Filter by basename prefix match |
| Phase 2 dialog | New `GuiMsgBox` with custom text | Build file list string dynamically |
| Gamelist reload | `ViewController::reloadGameListView()` | Existing ES internal API |

---

## Design Decisions (Confirmed)

1. **Success toast:** YES — show a "DELETED!" message after deletion completes, before returning to gamelist.

2. **Menu item styling:** Keep consistent with current SA fork styling. No special red/danger color — use the same `SA_TEXT_COLOR`, `saFont()`, `FONT_SIZE_MEDIUM` as all other menu items.

3. **Delete key binding:** NO — Select menu only. All buttons are already bound. Access through the options menu is intentional and prevents accidental deletes.

4. **Undo:** NO undo. The confirmation dialog(s) should include a warning that deletion is permanent (e.g., "This cannot be undone.") to give the user fair warning without overcomplicating the flow.

5. **Batch delete:** NOT in scope. Users delete saves individually to prevent mass accidental deletes. Could be a future enhancement.
