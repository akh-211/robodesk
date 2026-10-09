# P3: Optional external context

External services run on the ESP32-C3 gateway. The ESP32-S3 receives a bounded summary over the existing UART link, but sends provider values to Gemini only through `get_external_context` when the user asks about weather, the calendar, or an allowlisted Home Assistant state. Integration data is not used to wake the microphone, start an activity, or control Home Assistant devices.

## Configure

Open **Integrasi luar** in the C3 dashboard sidebar, or visit `/integrations`. The page uses the dashboard's existing Basic Auth and same-origin mutation guard.

- **Home Assistant:** provide an HTTPS base URL, a long-lived access token, and up to three explicit entity IDs (`domain.object`). The C3 reads only those entities using the read-only REST state endpoint, one request per minute. Entity state text is truncated and filtered before it crosses UART.
- **Open-Meteo:** enable current conditions and enter latitude/longitude. The service is polled every 15 minutes. It caches temperature and a short condition label; location is sent to Open-Meteo.
- **Calendar:** provide a direct HTTPS iCalendar feed URL. The URL is secret because providers commonly embed a private token in it. It stays in the C3-only `robodesk_ext` NVS namespace and is never returned by the config/status API. Up to three upcoming events within 30 days are retained, with short titles.

Tokens and private feed URLs are written only to C3 NVS. The settings are not fields in `RuntimeSettings`, the behavior export/migration format, or the UART cache message. The dashboard never echoes secrets. **Clear all integration settings** removes the saved token, feed URL, and cached values. Factory reset also clears the integration namespace.

## Runtime behavior and bounds

The C3 performs one HTTPS request at a time with certificate-bundle validation, a 7-second request timeout, no automatic redirects, and an 8 KiB response limit. It skips work during OTA, a Gemini TLS session, or when free heap is below 96 KiB or the largest internal block is below 32 KiB. A failed refresh leaves the last successful value visible with its age and error. Provider polling is background work; **Refresh now** queues requests without blocking the dashboard.

The S3 prompt labels this cache as untrusted external data and drops the entire snapshot if its UART update is more than five minutes old. Each provider category is transferred and stored separately. Gemini must request one category per tool call; before returning it, S3 checks that the active user request explicitly refers to that category and looks like a lookup. Matching uses complete words/phrases, and Home Assistant requires explicit home/HA wording or a named device together with a requested state. Generic `schedule` and `temperature` wording alone does not authorize calendar or Home Assistant data; language questions about a word or phrase are also denied. S3 captures only three category-intent bits—not the user transcript—when queuing the tool call, so the gate still works when Gemini reports `turnComplete` before its tool call. The queued intent is valid for at most 15 seconds and is cleared after use or cancellation. Missing, stale, truncated, interrupted, unrelated, or sensitive requests fail closed. Only the requested category's cache is returned. The cache carries provider age, and stale readings are described as unavailable. The data does not enter persistent memory or conversation history storage.

Current parser limits: the calendar reader handles `VEVENT` summaries and UTC (`Z`) or date-only `DTSTART` values; it ignores events using `TZID`, does not expand `RRULE`, limits response size to 8 KiB, and reports event times in UTC. Use a provider feed that emits expanded UTC occurrences for predictable results. HA endpoints and calendar feeds must be reachable directly over HTTPS and present a certificate trusted by the ESP-IDF certificate bundle; redirects and self-signed local certificates are not followed.

## Device qualification still required

Host regression tests and dual-target firmware builds pass. Software wiring is implemented, but no provider has been configured or tested on the actual paired boards in this turn. Before relying on it, verify C3 certificate validation and heap reserve with each provider, bad-token and malformed/oversize responses, UART reconnect/stale behavior, factory reset secret removal, OTA while a request is active, and a full dashboard/Gemini load. The exact C3 heap usage during HTTPS requests remains a hardware measurement.
