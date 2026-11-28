# Native bottom-bar unit statistics

Hover a build-menu item to inspect the unit before building it. Select owned
units of one type to inspect their per-unit stats. Every owned selection also
shows its total count, including mixed groups and selections made while a build
item is hovered. Mixed groups otherwise retain TA's native information display.

The existing 32-pixel bottom bar, faction font and native background are used on
the battlefield and megamap. No additional build-menu panel is introduced.
Text is measured with the actual font and lower-priority fields are omitted
when they cannot fit. The selection count has reserved space.

The title includes health and metal/energy costs. Main statistics are nominal
DPS, weapon range, movement speed and line of sight. Relevant additional fields
include radar, sonar, jammers, build power/range, economy, storage, area of
effect and reload times. Values come from the mod's currently loaded definitions.
A single selected unit shows its current health and measured economy; matching
groups show per-unit values, rather than misleading sums.

## Interpretation

`DPS~` is theoretical base damage per second at the loaded weapon's nominal
reload rate: default damage × burst count × 30 / reload ticks. A zero burst
means one shot; a zero reload is unknown, rather than infinite DPS. Anti-air,
water-weapon and paralysis rates are shown separately. Manual weapons, stockpiled shots and
interceptors are not added to ordinary DPS. Target-specific damage, armour,
health/veterancy reload changes, aim/script delays, projectile misses, splash
against several targets and repeated-contact special weapons are not a claim
about actual combat output.

Movement speed is world distance per second, converted from 16.16 units per
30 Hz tick. Range and sensor radii use the same world-distance convention.
Build power is nominal worker time; build work is not a duration in seconds.
Wind, tidal and extraction output can depend on the map and live conditions.

## Build and runtime selection

`UNIT_STATS_PANEL_ENABLE` is explicitly selected in each mod configuration:
Escalation and ProTA enable it; other profiles default to zero. Values other
than zero or one fail the build. `UnitStatsPanel=FALSE` under `[Preferences]`
disables an included feature at runtime. Restart the game after changing it.

The normal-map native information-panel call is checked before patching. A
different executable or another patch owning that call leaves native behavior
in place. Rendering changes no orders, unit definitions or multiplayer packets.

## Validation

Portable calculation and production-adapter tests cover loaded values,
unsigned damage/32-bit health and ranges, special weapons, homogeneous/mixed
selection behavior, clipping and disabled fallbacks. Windows builds cover
Escalation, ProTA and compiled-out configurations, preserving queue prepending,
formations, extractor/grid defaults and allied cursors. In-game visual checks
are still needed at supported resolutions, on both maps and both factions,
including selection changes, hovering build buttons, menus and match reloads.
