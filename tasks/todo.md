## Aspect Ratio Fix Plan (2026-05-03)
- [x] Locate the GPS/map transform chain (scale/rotation/translation) and identify where aspect correction is applied.
- [x] Re-derive the correct matrix order: apply rotation on the un-stretched map, then apply aspect correction in the GPS space.
- [x] Update the transform code to enforce the new order and remove any redundant correction paths.
- [ ] Verify with a controlled test scene (fixed heading, then rotating) to ensure no accordion distortion.
- [ ] Check for regressions in other UI overlays that share the same transform utility.
