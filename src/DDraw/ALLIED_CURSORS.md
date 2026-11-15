# Teammate cursors (test build)

Escalation builds include always-visible teammate cursor lights and names on
the battlefield and megamap. The megamap uses a smaller light and font. There is
no runtime switch or key to hold. Both players need a compatible cursor build;
an older DLL can continue playing but cannot share its pointer.

Only active, mutually allied human peers participate. The local mouse is shared
while it is over the map and the game has focus. It is hidden over interface
panels, outside the window and when minimized. Disconnected, expired, spectator
and former-ally cursors are not drawn. Replays do not transmit or show live
cursor data. Returning to the menu or changing matches starts a fresh session.

The display draws a palette-blended light with an outlined name, without
changing selection, orders, line formations, extractor upgrades or grid building.
Normal and megamap projection use the same terrain coordinates as the native
pointer. It clips to the playable viewport and respects popup panels.

## Build and wire format

`ALLIED_CURSORS_ENABLE` defaults to 1 for Escalation and 0 for other profiles.
A compiler definition can override it; the feature has no INI option. ProTA is
left unchanged in this test update.

Transport uses the existing CHAT05 extension router and a dedicated ID, 0x61.
The 65-byte packet has a NUL chat string and a zero final byte. Hello/HelloAck
negotiate a version and per-match session before any coordinates are sent.
Position updates are sent at most ten times per second per peer, only when
changed, plus one-second heartbeats. Discovery, replies and data share a global
90-packet/second budget. Duplicate or out-of-order updates are ignored; cursor
positions expire after two seconds without fresh data.

The fixed `->` guard and canonical uppercase hex fields are deliberate: the
legacy recorder copies some directed chat messages to enemy-chat subscribers.
This encoding cannot match that forwarding condition or recorder commands.
It requires no recorder DLL update and does not change synchronized game state.

## Validation

The dedicated test workflow compiles native Windows DLLs for Escalation,
Escalation with the three optional unit controls excluded, and ProTA with
cursors excluded. Portable tests exercise the actual capture/render/controller
code with platform calls mocked, indexed-color blending and clipping, session
negotiation, pacing, lifecycle resets, and the legacy recorder routing rules.

Live multiplayer and Wine testing are still required before proposing the
cursor feature upstream. This test branch is separate from the combined unit
controls pull request.
