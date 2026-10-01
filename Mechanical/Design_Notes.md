# HerComfort refined external concept

This model follows the supplied correction brief and real product photos. It contains only exterior geometry. The screen in the reference product is deliberately omitted.

## Design dimensions (mm)

| Feature | Nominal design |
|---|---|
| Therapy module | 185 long × 78 high nominal; approximately 80.1 mm maximum loft width and 21.4 mm overall curved depth envelope |
| Abdominal curvature | Approximately 3.5 mm rear-pad sag across 160 mm |
| Rear comfort pad | 2.4 mm modeled section; approximately 1.6–2.3 mm exposed; gray textile appearance |
| Top controls | Two 15 × 8 mm pill buttons; approximately 1.7 mm exposed height |
| Button spacing | 8 mm clear gap |
| Charging opening | One 10 × 3.8 mm rounded USB-C access recess |
| Elastic strap | One continuous solid band, 48 mm wide × 3 mm thick |
| Wearable size shown | Approximately 830 mm circumference including the module |
| Connection | Fixed left anchor; right receiver, strap connector and raised release button |
| Adjustment | One simple two-slot slider |
| Dry EMG contacts | Three 18 mm circular silver pads on the inner front-right fabric |
| EMG sensing pair | 30 mm center-to-center along the strap centerline |
| EMG reference spacing | 30 mm from the nearer sensing pad; approximately 45 mm from the sensing pair midpoint |
| EMG contact profile | 1 mm total pad thickness, embedded into the 3 mm band; 0.35 mm maximum center projection; 0.25 mm edge fillets |

The main housing, front panel and contact pad use lofted sections with a gradual symmetric bow. The controls, connector details and slider use rounded sketches, extrusions and small fillets. The band is a single extruded curved profile behind the module.

Profile connection points are aligned consistently across each loft to preserve a clean capsule outline. The housing width adjusts slightly from the nominal sketch dimensions as the loft interpolates between sections.

## Verification

The final model rebuilt successfully. All 11 solid bodies returned zero errors from SolidWorks' body check. No feature errors were reported. The native and STEP exports both saved successfully with zero errors; the native save also reported zero warnings. The model includes centered HerComfort branding in muted plum on the lighter front panel. The editable wordmark and recreated heart use flush surface regions that follow the curved panel; they add no enclosure thickness. The branded model also rebuilt without feature errors and all 11 bodies passed the native body check. Native and STEP files saved with zero errors.

The six original views were inspected for the strap continuity, capsule outline, top controls, rear contact surface and detachable connector exterior.

## Files

- `HerComfort_Refined.SLDPRT`: editable SolidWorks 2024 multibody part with named modeling features.
- `HerComfort_Refined.step`: neutral solid geometry export.
- `Views/`: nine model views, including a front branding view, covering the front, rear pad, top controls, continuous band, detachable connection, abdominal curvature and inner EMG placement.
- `Appearances/`: rose finishes and gray fabric assets used by the native model. Keep these files together if moving the model. STEP primarily preserves geometry; use the native file for the intended appearance.

This is an exterior industrial design concept. The push-release connector and adjustment slider represent their visible form and connection arrangement. Their internal mechanisms, operation, structural performance, manufacturing clearances and heating performance are outside this model's scope.


## Inner dry EMG electrode update

The three contacts are separate editable solid bodies with a satin silver appearance. All three sit on the skin-facing side of the soft elastic band immediately beyond the right-hand pink connector. Their contact faces point inward toward the abdomen. All three pad centers lie on the middle of the 48 mm band width, forming one row along the curved inner fabric. Adjacent pads are evenly spaced at 30 mm center-to-center, leaving approximately 12 mm of fabric between their 18 mm diameters. The reference is approximately 45 mm from the sensing pair midpoint. This row replaces the previous triangular arrangement. Each pad remains tangent to its local inner fabric surface.

Each 18 mm pad is tangent to the local inner curve and extends into the fabric. Its central contact surface projects inward by 0.35 mm; projection reduces toward its rounded perimeter. The 1 mm pad remains within the strap thickness and does not emerge on the outside. The appearance represents a metallic contact surface; no wiring, sensors behind the contact, or electronics were modeled.

All 14 final solid bodies passed SolidWorks body checks with zero errors. No feature errors were reported. The native model and STEP geometry saved with zero errors, and the native save reported zero warnings. The recorded bounding boxes of all 11 pre-existing bodies match those before this update. Existing branding, colors, strap geometry, controls and proportions are preserved.

`Views/08_Inner_Right_EMG_Electrodes.png` shows all three contacts clearly beside the connector and control pod. `Views/09_Inner_Belt_EMG_Overview.png` verifies the contacts are on the inside of the wearable loop. These are exterior CAD contact representations; actual skin contact and EMG measurement performance were not tested.


