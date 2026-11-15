EVERYDAY LIFE — PIXEL ART DIALOGUE UI
Version 1.0 | DadddyDarko

START HERE
Three matching colorways: Cream, Sunset and Midnight.
117 unique component/state images per colorway: 351 native images in total.
Each is supplied at native, 2x (720p) and 3x (1080p): 1,053 transparent PNGs.
351 editable SVG sources are also included. Export SVGs without antialiasing
or use the provided PNGs to retain the exact pixel grid.

WHAT YOU GET
Wide and compact dialogue boxes, narration box, nameplate, menu panel,
confirmation panel, tooltip, notification and quick-menu strip.
Menu buttons, choices, small buttons, tabs and save slots, each in idle,
hover, pressed, selected, selected-hover and disabled states.
Checkboxes (on/off, idle/hover/disabled); 20 original navigation/action
icons (idle/hover/disabled); slider and scrollbar parts; mouse cursor;
three-frame continue indicator. No words are baked into these images.

RESOLUTION
The native design grid is 640x360. A 608x84 dialogue box is intentionally
smaller than the canvas. 720p and 1080p folders contain exact 2x and 3x
copies of each component, not full-screen images.
Use native with a 640x360 virtual canvas, 720p with 1280x720, or 1080p
with 1920x1080. The example supports these three virtual resolutions.
The demo starts at 1920x1080 with a resizable 1280x720 physical window.
Pixel widths can become uneven at non-integer physical window sizes.

TRY THE REN'PY DEMO
The Ren'Py engine is not included. Use your own Ren'Py 8.5.3 SDK.
Keep the extracted folder structure intact. Open RenPy_Demo as a project
from the launcher, or run your SDK's renpy.exe followed by the full path
to RenPy_Demo. Click Start for dialogue/choices; Escape opens Pause.
Preferences changes the theme, text size, indicator motion and text speed.
Save/Load use five pages of six slots; History shows prior dialogue.
The demo uses solid backgrounds so it does not duplicate the background pack.
The screenshots show backgrounds from Everyday Life, available separately.

ADD TO AN EXISTING REN'PY GAME
1. Back up your project.
2. Create game/everyday_ui/. Copy the PNG folder there, plus
   Integration/everyday_ui_components.rpy. Other folders are not required
   by the running game. You may remove unused themes and scales after
   updating the helpers to reference only the files you keep.
3. Use a supported 16:9 virtual canvas (see RESOLUTION). For example:
       define alex = Character("Alex", screen="el_ui_say")
   For narration:
       define el_narrator = Character(None, screen="el_ui_say")
4. Opt in for choices per menu:
       menu (screen="el_ui_choices"):
           "Walk along the shore":
               alex "Let's go."
5. In a label, set a theme and optionally show the quick menu:
       $ el_ui_theme = "midnight"
       show screen el_ui_quick_menu
   Hide it using: hide screen el_ui_quick_menu
6. Open supplied menus with ShowMenu("el_ui_save"),
   ShowMenu("el_ui_load"), ShowMenu("el_ui_preferences") or
   ShowMenu("el_ui_history"). The component file does not overwrite
   your existing say, choice, confirm, main_menu or game_menu screens.
   The standalone demo shows optional wrapper screens. Merge wrappers
   carefully rather than copying duplicate screen definitions.
7. Existing projects need their own working confirm screen for Ren'Py's
   save overwrite, delete and quit prompts. The demo uses el_ui_confirm.

TEXT, LOCALIZATION AND ACCESSIBILITY
All labels are real Ren'Py text, separate from artwork. Use your own fonts
with suitable language coverage. No font files are redistributed; the demo
uses Ren'Py's bundled DejaVu Sans. Translated text may require wider buttons.
The larger-text toggle expands buttons vertically, but dialogue uses a fixed
84px-native height: keep lines short or increase its height for long passages.
Selection has a diamond marker and focus has an inset line, not color alone.
Panel interiors are opaque to preserve text contrast over detailed scenes.
Indicator animation is off by default. No flashing effects or audio included.
Screen-reader and mobile/touch behavior have not been comprehensively tested.
Keep Ren'Py's accessibility menu and test your finished game's language,
keyboard focus, touch target size and long dialogue before release.

CUSTOMIZATION / OTHER ENGINES
ASSET_CATALOG.csv and manifest.json list native dimensions, cursor hotspots,
text padding and nine-slice borders. Multiply all metadata insets by 2 or 3
for the larger exports. Ren'Py uses Frame(..., tile=True); repeat the center
instead of stretching border details. Do not shrink below twice the insets.
Pressed artwork is supplied for engines that expose a pressed state;
the Ren'Py example uses idle, hover, selected and insensitive backgrounds.
Checkboxes, icons, compact frames and scrollbar pieces are reusable art;
the example demonstrates a subset of those components, not every asset.
Editable_SVG contains exact integer geometry and no font dependencies.
Changes in vector-editor SVG rasterization may shift polygon edges; the
PNG exports are the authoritative pixel artwork.

CONTENTS / PRODUCTION
Original procedural pixel UI built on the Everyday Life palette. No UI
images are copied from comparable products. Research informed usability,
not visual reproduction. AI-assisted code and documentation; existing
AI-generated Everyday Life backgrounds appear only in marketing previews.
Review LICENSE.txt for commercial/non-commercial use terms. This is a
standalone companion pack; the background download has not been changed.
