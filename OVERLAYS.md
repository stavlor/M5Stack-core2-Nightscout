Latest marker/color update: each SMB now uses one solid cyan dot. Repeated SMB uploads are deduplicated by pump type/serial/pumpId when available; without pump identity, only identical millisecond timestamps and doses/carbs are deduplicated. Separate SMBs at different times remain separate, even when crowded label text is suppressed. Glucose history now colors each sample and interpolated segment green in range, orange above range, and red below range; stale history stays gray. This supersedes older outlined-diamond and single-color descriptions. Four new deduplication checks bring the pre-flash suite to 33; their execution and the firmware build remain pending on the user PC.

Latest layout: treatment markers are inside the graph above the lower threshold line. Labels below that line use compact C10 (10 g carbs), S0.5 (0.5 U SMB) and B1 (1 U other bolus) notation. Carb and insulin labels occupy separate rows, with collision suppression. Trailing decimal zeros are removed; precision remains up to two decimals. This supersedes older marker-lane/label descriptions below.

# Treatment and prediction overlays

This update supersedes README statements that C is unused or treatments and
prediction arrays are excluded.

Orange C triangles show carbohydrate entries in grams. Cyan S diamonds identify
SMBs only with explicit isSMB=true or type=SMB; gray B dots show other reported
boluses. Correction Bolus alone does not establish SMB. Combined carb/insulin
records appear in both lanes. Insulin labels use two decimal places. Labels that
would overlap are suppressed, while their timestamp markers remain visible.
Extended/combo totals are not plotted as instantaneous insulin delivery.

The third read-only API resource is /api/v1/treatments.json, filtered to the graph
window with a 200-record limit. The token needs api:treatments:read. Numeric date,
mills, timestamp and created_at are supported, in that priority order. Invalid,
deleted, future and out-of-window events are excluded. IDs are deduplicated.
The newest 128 qualifying events are retained, with a limited-history notice at
the bounds. Treatment failures retain historical markers and show a Treat error;
successful empty responses clear them. Treatments may come from any uploader in
the same Nightscout account.

Button C toggles forecasts, initially on. Fresh glucose and predictions enable
a one-hour future pane to the right of a vertical now separator. Dashed IOB,
COB, UAM and ZT glucose predictions use magenta, orange, yellow and cyan.
Samples are five minutes apart, anchored at the result timestamp. Up to 49
samples per curve are retained; only the next hour is drawn. Values convert
with glucose units. The newest usable suggested/enacted run is chosen as a
whole; curves from different calculations are never merged. Missing curves are
omitted and malformed samples leave gaps without shifting time. Stale
predictions disappear, and history returns to full width. Forecasts visualize
uploaded AAPS calculations and do not establish pump delivery or closed-loop mode.
A refreshes and B toggles units as before. Settings reset at reboot.

Verification: the user confirmed the prior firmware works, and its local test
log shows 16 passed, 0 failed. Added 13 checks for treatment classification,
validation, timestamps, ID deduplication, filtering and bounds; and prediction
validation, timestamp selection, staleness, missing arrays and bounds.
Flash-Core2.cmd automatically runs all 29 checks before building/uploading.
This session could not launch the compiler, so the new checks and hardware
build remain pending on the user's PC. No new-revision test pass is claimed.

AAPS serializer fields were checked in the downloaded source. OpenAPS prediction
spacing: https://github.com/openaps/docs/blob/master/docs/docs/Resources/glossary.md

After flashing, verify marker times/amounts against Nightscout, dense marker
labels, C toggle, unit conversion, absent curves and stale suppression.
