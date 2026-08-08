# Integration portability

Standalone Editor builds pin the exact EngineCore and Renderer revisions validated by the Phase 1-5 integration branch. This prevents child builds from silently fetching stale component revisions and keeps Linux, macOS, and Windows on one dependency graph.
