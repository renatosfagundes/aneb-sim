// Theme.js — central palette + metric constants for the aneb-sim QML
// panes. Mirrors the QSS palette in app.py (PCB-green dark) so the
// floating Console / Plot / Flash windows feel like they belong with
// the main board view. Imported with `import "Theme.js" as T`.

.pragma library

// ---- backgrounds (darkest → lightest) -----------------------------
var bgDeep    = "#061410"   // terminal/log canvas
var bg        = "#0a1a14"   // window/panel background
var bgRaised  = "#102a1c"   // surface (cards, group boxes)
var bgHover   = "#1a3a26"   // hover state, subtle dividers

// ---- borders -------------------------------------------------------
var borderSubtle = "#1a3a26"
var border       = "#3e6b4d"
var borderStrong = "#4a8a5d"

// ---- text ----------------------------------------------------------
var text       = "#cdfac0"
var textBright = "#e8ffd8"
var textMuted  = "#7aaa8a"
var textDim    = "#4d7a5a"

// ---- accents -------------------------------------------------------
var accent       = "#4a8a5d"
var accentHover  = "#5da473"
var accentPress  = "#3a6e48"

// ---- semantic ------------------------------------------------------
var success = "#22cc44"
var info    = "#3aaaff"
var warning = "#ffcc44"
var error   = "#ff5566"

// ---- typography ----------------------------------------------------
// Pixel sizes; fonts on the panes are FIXED (no _s scaling) so they
// stay readable when the window is smaller than the main board view.
var monoFamily = "Consolas"
var sansFamily = "Segoe UI"
var fontSmall  = 12   // labels, button text, legend rows
var fontBody   = 14   // terminal text, log lines, input fields
var fontHeader = 15   // pane title in the header strip

// ---- shape ---------------------------------------------------------
var radius      = 4
var radiusSmall = 3

// ---- spacing -------------------------------------------------------
var pad         = 8
var padSmall    = 4
var padLarge    = 12
