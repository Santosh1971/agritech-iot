# ASC-StudentKit

These are the student kits (**Mini** and **Mega**) for the Student Product Studio (`docs/student-product-studio.md`). Each kit is a carrier board built on an ESP32-S3, plus starter modules and a ready-made enclosure. Agri Sensors and Controls supplies the kits to colleges, priced per batch.

The kits are also **Project #0** of the studio: they are designed by following the same stages a student follows.

- `docs/StudentKit_Specification_v0.1.md`: system specification with hardware, software and mechanical requirements. It is a draft, and the open decisions are listed in §8.
- `docs/StudentKit_Architecture_v0.1.md`: architecture (stage 3), covering block diagrams, pin map, connectors, power budget and battery life.
- `hardware/pinmap.json`: the pin map for both boards, checked by `python3 hardware/tools/check_pinmap.py`.
- `firmware/`, `mobile-app/`: not started. The mobile app is the shared ASC Studio app (spec §5A).
