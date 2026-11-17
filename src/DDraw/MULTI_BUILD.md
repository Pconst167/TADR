# Combined construction menus (test build)

Select two or more completed mobile constructors to display their combined build
menu. BUILD/ORDERS tabs switch between the combined menu and the normal sidebar.
The unit portrait and lower native command buttons remain available. Use page
arrows or the mouse wheel over the build pane to reach additional options.

Every distinct unit type available in any selected constructor's actual human
build menu appears once. This supports mixed constructor classes, tiers and
factions. Visually similar ARM/CORE structures remain distinct real unit types.
Selecting combat units or factories restores the native selection interface.

Click a building icon, then place it normally on the battlefield or megamap.
One eligible constructor starts each structure and the other constructors assist.
Shift queues work; the existing grid tool places a cooperative group task at
each site. Temporary helpers finish after their corresponding build task and
advance to their queued successors. Stopping or changing a lead task releases
its helpers rather than leaving permanent Guard commands behind.

The test build includes the previous formations, extractor upgrades, grid
building and teammate cursor features. No factory repeat-cycle feature is
included; that remains a design proposal. The build switch MULTI_BUILD_ENABLE
defaults to 1 for Escalation and 0 for other profiles, with no runtime option.

## Implementation

The native multi-selection GUI is left intact underneath a bounded, paged
sidebar overlay, using real build pictures loaded through TA's archive lookup.
It enumerates human GUI pages, including page zero when allowed, rather than
using the AI-only CANBUILD plan. No external GUI or picture files are installed.

The existing mobile-build entry hook intercepts cooperative placements before
the native unfiltered selected-unit BUILD loop. A real capable constructor gets
one ordinary native build order. The remaining constructors get native temporary
Follow/HelpBuild orders associated with that exact task. Destructor notifications
invalidate tasks before native order-allocation reuse. Pending helpers use the
native scheduler delay, and finished helpers exit through native single-order
completion. There is no raw order-list surgery or permanent Guard dependency.

The native unit update path calls order handlers only for locally controlled
human/AI units (0x48AEB5 through 0x48AF9C). Other peers receive the engine's normal
authoritative unit updates, so the feature does not introduce a custom network
protocol. Native byte guards disable cooperative UI/orders on mismatched binaries.
Live Wine and TAF gameplay testing remains necessary.

## Validation

Portable tests compile the actual model, assets, menu and order controllers with
native bridges mocked. They cover union/deduplication, mixed eligibility, interface
priority, paging, Shift cancellation, temporary guard sequencing, native scheduler
returns, ownership/slot reuse, destruction and return-to-menu behavior. Native
Windows builds cover all features combined, independent combined construction
with the other optional controls excluded, and the unchanged ProTA defaults.
