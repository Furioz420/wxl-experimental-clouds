# Experimental Clouds

The repository is named `wxl-experimental-clouds`; its WXL module ID and build target are `wxl-retail-clouds`. This review branch carries the committed module snapshot from `b14c80943b500cd006bd86b3383f67deff238dce`. It changes the generated base cloud field while retaining the client's existing dome, textures, and incremental cloud-update path. An in-game tuning panel exposes the feature settings. This is experimental rendering work.

## Integration and release checks

The module depends on matching WXL sky offsets, hooks, environment support, and shared ImGui integration. Build its Win32 DLL with the pinned WXL core; `shared.cmake` and `target.cmake` are core-integration inputs, not a standalone build. Test clouds in clear and overcast areas, zone changes, option toggles, device reset, and client restart. Check performance and visual continuity before calling it release-ready. The package boundary is the reviewed DLL plus source/license documentation; no client textures or models are included.

Keep this PR in draft until its exact core pair builds and the client smoke test passes. Roll back by closing the client and restoring the prior compatible DLL/core set.

## Credits and license

Preserve WarcraftXL copyright headers and the GPL-3.0 license. Furioz is credited for the local v1.1 integration in Git history. The game's cloud assets remain external and are not redistributed here.
