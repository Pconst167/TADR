# Move order destination feedback

An accepted move command briefly shows a green line and a green arrow at each
unit's destination. Each line follows its unit as it moves while its destination
arrow stays fixed. The feedback lasts **0.5 seconds** from the command, including
while the game is paused, and appears on both the battlefield and megamap
without holding Shift.

This applies to ordinary moves, queued moves, Ctrl+Shift priority moves and
Alt-drag formations. Formation arrows show the individual destinations assigned
to the selected units. Queued commands show their feedback when issued rather
than when the units eventually reach that part of the queue. Cancelling a
queued destination removes its matching temporary feedback. Refused commands
do not create destination feedback.

The green lines/arrows and dark arrow outlines blend at approximately **80%
opacity** with the terrain beneath them. A cached palette lookup keeps this
fast on TA's indexed-color surface. The live palette is checked on each draw,
and each pixel of a marker is blended only once. The arrow size follows
the current view: larger on the battlefield and smaller on the megamap. Lines
and arrows are clipped to the map view and kept out of native popup panels.

## Build and runtime behavior

Feedback uses the existing `DRAWN_MOVE_FORMATIONS_ENABLE` compile switch. It is
included by default in Escalation and ProTA, and excluded by default in the
other mod profiles. There is no additional compile switch or INI preference.

`DrawnMoveFormations=FALSE` disables the Alt-drag gesture; `DrawnMovePreview=FALSE`
disables its drawing preview. Neither preference disables the brief feedback
for accepted move orders. To exclude feedback and formations together, build
with the existing MSBuild property:

```bat
msbuild src\DDraw\ddraw.vcxproj /p:Configuration=ReleasePublic /p:Platform=Win32 /p:TDRAW_CONFIG=TDRAW_CONFIG_ESCALATION /p:DRAWN_MOVE_FORMATIONS_ENABLE=0
```

Queue prepending remains independent. Feedback works when prepending is compiled
out or disabled with `QueuePrepend=FALSE`. Extractor upgrades, building grids and
allied cursors retain their existing feature selections.

## Capture and rendering

The capture wrapper observes the native user move-order call and records a
presentation snapshot only after the engine accepts the command. It handles
both immediate and appended destinations, including native Shift cancellation.
When queue prepending is included, the two features share the same guarded call
wrapper so commands are dispatched once. Accepted native moves determine
eligibility; constructor progress and legacy movement-capability hints do not
suppress feedback for commanders or other units whose move was accepted.

Snapshots contain the destination, unit index and actor identity values. Each
frame resolves the unit from the current game bank and checks its identity and
liveness before reading its position; cached pointer values are compared, never
dereferenced. Observed death or replacement ends that unit's feedback. A bounded
buffer keeps input capture allocation-free. Feedback clears when the game or
local player context changes, and is not shown while watching a replay or
spectating. The renderer does not issue commands or change simulation state.

## Validation

Portable geometry, controller and native capture tests cover clipping, endpoint
arrows, palette blending and changes, moving origins, unit replacement, expiry,
context changes, accepted/cancelled/refused commands and the prepending
combinations. Existing formation, queue, grid, extractor and allied
cursor regressions run alongside them.

The dedicated workflow builds Escalation and ProTA with feedback, Escalation
with prepending excluded, Escalation with formations/feedback excluded, and OTA
with the optional controls excluded. Each build checks its feature markers and
Windows 7 imports. These checks validate builds and code paths; in-game visual
behavior still needs playtesting.
