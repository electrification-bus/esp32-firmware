# Controller links

**Status: partially implemented.** `homie_role: controller` builds run a top-level `links:` list, documented in [Controller.md](../Controller.md#controller-links) and [Link.md](../Link.md#on-a-controller). Outstanding:

- **Name addressing.** `@<device-name>/<node>/<property>`, resolved against the names in discovered `$description`s and re-resolved when the name appears under a new device id, so a replaced board needs no `device.yml` edit. A name that matches two devices is refused.
- **`link_format.h`.** Substitution and rounding are in the Arduino-free `lib/logic/link_core.{h,cpp}` with host tests (`test/native/test_link_core`); the shared formatting header was also to carry the unit conversion in [LinkUnitConversion.md](LinkUnitConversion.md).
- **`stale_text`.** Optional text a controller link shows when a source device goes `lost`. Today it keeps showing the last good value.
- **The platform-free link logic in cpp-sdk.** Move `link_core` into the portable core, so other ports run the same links.
