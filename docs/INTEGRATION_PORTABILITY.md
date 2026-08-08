# Integration portability

Standalone Editor builds follow the Phase 1–5 EngineCore and Renderer integration branches while the PR stack is open so Git can fetch those unmerged revisions. This prevents child builds from silently fetching stale component revisions and keeps Linux, macOS, and Windows on one dependency graph. Before release, the branch fallbacks are replaced by the merged immutable commit SHAs.
