# Treatment markers and OpenAPS forecasts

## Treatments

| Marker and compact label | Meaning |
| --- | --- |
| Orange triangle, C10 | 10 g carbohydrates |
| One solid cyan dot, 0.5 | 0.5 U explicitly identified as an SMB |
| Gray dot, B1 | 1 U of another reported bolus |

Markers sit inside the graph near its bottom edge; compact labels are
below the graph in one carb row and two insulin rows. Cyan SMB labels omit the S prefix;
units are indicated by the SMB U legend. Labels use measured text widths and can
shift slightly with a connector to their timestamp marker. Trailing decimal zeros
are removed and up to two decimals retained.

When both insulin rows are too crowded, adjacent SMB labels combine their unit
amounts: `=0.8` means a combined 0.8 U. A cyan bracket spans the included doses;
each individual timestamp dot remains. Grouping affects display labels only,
not treatment records or IOB. Carb and other bolus amounts are never included
in an SMB sum. Carb/manual bolus labels may be omitted when they cannot fit.

SMB identification requires boolean `isSMB: true` or `type: "SMB"`. Correction
Bolus alone is not sufficient. Combined carb/insulin entries appear in both
lanes. Extended/combo totals are not shown as instantaneous insulin doses.

The v3 request reads the configured history window, selects necessary fields and
limits the response to 200 records. The newest 128 qualifying events are retained,
even for unordered input. A limit notice appears at response/storage bounds.
Read failure retains prior markers with a Treat error; valid empty results clear
them. Old markers leave the moving history window.

Timestamp priority is `date`, `mills`, `timestamp`, then `created_at`. Invalid,
deleted, future and out-of-window records are ignored. V3 public `identifier`
is preferred; legacy `_id` is supported. IDs are deduplicated. Repeated SMBs also
use a pump identity scoped by pump type, serial and pumpId when available.
Without that identity, only identical millisecond timestamp, insulin and carb
amounts are deduplicated. This parsing deduplication is separate from the visual label grouping above.

Treatments may come from any uploader in the same Nightscout account.
`AAPS_DEVICE` filters device status/forecasts, not treatments. Treatment read
permission is required. No treatment writes are performed.

## Forecasts

Button C toggles forecasts. With fresh glucose and a fresh calculation, a
one-hour future pane appears right of a vertical now divider, ending at +1h.
Configured history remains on the left. Missing/stale predictions remove the
future pane and return history to full width.

| Dashed glucose curve | Color |
| --- | --- |
| IOB | Magenta |
| COB | Orange |
| UAM | Yellow |
| ZT, zero-temp | Cyan |

These are uploaded `openaps.suggested/enacted.predBGs` glucose arrays, not future
insulin amounts or locally computed predictions. Values are mg/dL internally
and convert with display units. The first sample is at the calculation timestamp;
subsequent samples are five minutes apart. Up to 49 samples per curve are retained,
with only the next hour drawn. The newest usable suggested/enacted run is selected
as a whole, without mixing curves from different calculations.

Missing arrays are omitted. Invalid samples leave gaps without shifting time
indices. Unsupported aCOB/other variants are not rendered. Forecasts are hidden
when their calculation or current SGV exceeds the configured stale limit.
Server modification/upload time does not refresh an old calculation.

The IOB metric uses the current IOB object/first array item or newer suggested
IOB, not future IOB activity samples. Forecasts read the separate predBGs arrays.
Neither forecasts nor a suggestion establish pump mode or delivery.

Five-minute spacing is described in the [OpenAPS glossary](https://github.com/openaps/docs/blob/master/docs/docs/Resources/glossary.md).
See [V3.md](V3.md) for requests and [VERIFICATION.md](VERIFICATION.md) for evidence.

Forecasts use the automatic SGV history scale. Off-scale forecast portions are
clipped with a FCAST CLIPPED legend warning; predictions do not stretch that scale.
