// Self-Balancing Robot - reverse-T platform v1 (Scenario A)
// Units: mm. PLA or PETG, 0.12-0.16 layers (0.25 nozzle), no supports.
//
// part = "frame"    -> one-piece base + spine (prints standing, base on the bed)
// part = "d153c" | "pico" | "pico_side" | "pi" -> one carrier (prints back-down; "pico_side" = sideways Pico for the stack layout)
// part = "carriers" -> all three carriers on one plate
// part = "assembly" -> everything in place, with ghost boards (preview only)
//
// Assembly coordinates: X side to side (+X = right face),
// Y front to back (+Y = front), Z up from the chassis plate top.
// The brass columns are 40 tall, so their tops sit at Z = 40.

part = "assembly";
layout = "stack";   // "stack" = this milestone: D153C, sideways Pico W and IMU all on the left; "short" = D153C left, Pico right; "tall" = adds the Pi Zero W
short = (layout == "short");
stack = (layout == "stack");
$fn = 48;
eps = 0.05;

// ---------- validated fit values (fit tests, PLA, 0.25 nozzle) ----------
board_clear    = 0.2;
peg_clear      = 0.2;
dovetail_clear = 0.25;
sock_d         = 5.4;    // round socket over the hex column (5.1 was too tight and cracked the bosses)

// ---------- base ----------
col_x = 44; col_y = 26.5;            // column centers, 88 x 53
col_top    = 40;
sock_depth = 10;                     // base sleeves 10 down over each column top
cap_t      = 2;
screw_hole = 2.9;                    // M2.5 clearance
boss_d     = 11.5;                   // thicker wall around the socket (was 10.5)
base_w = 100; base_d = 70; base_t = 4; base_r = 6;
base_z0  = col_top - sock_depth;     // 30: base underside
base_z1  = base_z0 + base_t;         // 34: base top
boss_top = col_top + cap_t;          // 42
slot_x = [-24, -8]; slot_y = [-27, 27];   // battery + motor cable slot, under the D153C

// ---------- spine ----------
spine_t = 5; spine_d = 66; spine_h = stack ? 102 : short ? 80 : 140;  // height above the base top
flange_w = 16; flange_t = 2;               // I-beam flanges on the front/rear edges
fillet = 4;                                // root fillet on both faces
win_y = [18, 27]; win_h = short ? 11 : 14; win_z = stack ? [] : short ? [64] : [74, 110];  // wire pass-through windows (stack: everything is on one face)

// ---------- rail (same profile as the dovetail test) ----------
dt_base = 8; dt_top = 10; dt_h = 3;
rail_z0   = 0;                       // rail runs down to the base top (no overhang at its foot)
pin_z0    = 16.6; pin_pitch = 10; pin_n = stack ? 8 : short ? 7 : 13;   // lock-pin holes every 10
pin_extra = stack ? [97.6] : [];                 // extra hole for the sideways Pico's tab
pin_list  = concat([for (k = [0:pin_n - 1]) pin_z0 + k*pin_pitch], pin_extra);
pin_hole_rail = 2.2; pin_hole_depth = 4.5;      // fits a 1.75 filament pin
pin_hole_carrier = 1.9;                          // snug on the filament pin

// ---------- carriers ----------
pcb_t = 1.6; wall_t = 1.6;
plate_t = 2.5; rib_hw = 9; rib_t = 5;
tab_len = 6; lip_leg = 5;
screw_boss_d = 7;        // boss for M2.5 screws
screw_pilot  = 2.2;      // 2.2 = M2.5 machine screw threads straight into the plastic; ~3.6 for M2.5 heat-set inserts
screw_floor  = 1.0;      // solid material left at the back so the screw tip never reaches the spine
m2_boss_d    = 5.2;      // boss for M2 screws (same footprint as the old peg pads)
m2_pilot     = 1.7;      // M2 machine screw threads straight into the plastic

// board: [name, L (along rail), W (across), hole d, [holes u,v], seat, lips, tab side, usb notch, windows, pin height, face, mount]
//   mount = "pegs" (press-fit pegs), "screws" (bosses with pilot holes for M2.5 screws) or "screws_m2" (M2 screws)
//   seat = board underside height above the carrier back
//   pin height = which rail hole the carrier locks into (above base top); face -1 = left, +1 = right
D153C = ["D153C", 50, 50, 3.2,  [[-22,-22],[22,-22],[-22,22],[22,22]], 8.0, "corners",  1, false, true,  66.6, -1, "screws"];
PICO  = ["PICO",  51, 21, 2.1,  [[-23.5,-5.7],[23.5,-5.7],[-23.5,5.7],[23.5,5.7]], 6.0, "ends", -1, true, false, short ? 16.6 : 76.6, short ? 1 : -1, "pegs"];
// sideways Pico W for the stack layout: long side across the spine, USB to the rear, pads on the top and bottom edges.
// seat 8 lifts the USB plug clear of the rear flange. Held with 4x M2 screws (M2 x 6).
PICO_H = ["PICO", 21, 51, 2.1, [[-5.7,-23.5],[5.7,-23.5],[-5.7,23.5],[5.7,23.5]], 8.0, "sides", 1, true, false, 97.6, -1, "screws_m2"];
PI    = ["PI0W",  65, 30, 2.75, [[-29,-11.5],[29,-11.5],[-29,11.5],[29,11.5]], 6.5, "corners", -1, false, false, 66.6, 1, "pegs"];

// ---------- IMU (GY-521) mount on the base top ----------
imu_cx = stack ? -36 : 18;   // pocket center X; long edge runs front to back (stack: left side, beside the cable slot)
imu_rot = stack ? 0 : 180;    // either way the pad row faces the spine; 0 = pad row toward +X, 180 = toward -X

// =====================================================================
module base_outline() {
    hull() for (sx = [-1, 1], sy = [-1, 1])
        translate([sx*(base_w/2 - base_r), sy*(base_d/2 - base_r)]) circle(r = base_r);
}

module base() {
    difference() {
        union() {
            translate([0, 0, base_z0]) linear_extrude(base_t) base_outline();
            for (sx = [-1, 1], sy = [-1, 1]) translate([sx*col_x, sy*col_y, base_z0])
                cylinder(d = boss_d, h = boss_top - base_z0);
        }
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx*col_x, sy*col_y, 0]) {
            translate([0, 0, base_z0 - eps]) cylinder(d = sock_d, h = sock_depth + eps);
            translate([0, 0, base_z0 - eps]) cylinder(d1 = sock_d + 1, d2 = sock_d, h = 0.5 + eps);
            translate([0, 0, base_z0 - 1]) cylinder(d = screw_hole, h = boss_top - base_z0 + 2);
        }
        translate([slot_x[0], slot_y[0], base_z0 - 1])
            cube([slot_x[1] - slot_x[0], slot_y[1] - slot_y[0], base_t + 2]);
        // front arrow, engraved
        translate([30, 27, base_z1 - 0.6]) linear_extrude(1) polygon([[-3.5, -2.5], [3.5, -2.5], [0, 4]]);
    }
}

module imu_mount() {
    L = 21.11; W = 15.75; hd = 3.12; so = 1.5;
    iL = L + 2*board_clear; iW = W + 2*board_clear;
    rim_h = so + pcb_t - 0.6;
    translate([imu_cx, 0, base_z1 - eps]) rotate([0, 0, imu_rot]) {
        translate([-iW/2 - wall_t, -iL/2 - wall_t, 0]) cube([wall_t, iL + 2*wall_t, rim_h + eps]);
        for (sy = [-1, 1]) translate([-iW/2 - wall_t, sy > 0 ? iL/2 : -iL/2 - wall_t, 0])
            cube([iW + wall_t, wall_t, rim_h + eps]);
        for (sy = [-1, 1]) translate([-W/2 + 2.6, sy*7.78, 0]) {
            cylinder(d = hd + 3, h = so + eps);
            translate([0, 0, so]) {
                cylinder(d = hd - peg_clear, h = pcb_t + 0.3 + eps);
                translate([0, 0, pcb_t + 0.3]) cylinder(d1 = hd - peg_clear, d2 = hd - peg_clear - 0.6, h = 0.5);
            }
        }
    }
}

module rail(s) {
    translate([0, 0, rail_z0]) linear_extrude(spine_h - rail_z0)
        polygon([[s*(spine_t/2 - eps), -dt_base/2], [s*(spine_t/2 - eps), dt_base/2],
                 [s*(spine_t/2 + dt_h),  dt_top/2],  [s*(spine_t/2 + dt_h), -dt_top/2]]);
}

module root_fillet(s) {
    Lf = spine_d - 2*flange_t;
    translate([0, Lf/2, 0]) rotate([90, 0, 0]) linear_extrude(Lf)
        polygon([[s*(spine_t/2 - eps), -eps], [s*(spine_t/2 + fillet), -eps], [s*(spine_t/2 - eps), fillet]]);
}

module spine() {
    translate([0, 0, base_z1]) difference() {
        union() {
            translate([-spine_t/2, -spine_d/2, -eps]) cube([spine_t, spine_d, spine_h + eps]);
            for (sy = [-1, 1]) translate([-flange_w/2, sy > 0 ? spine_d/2 - flange_t : -spine_d/2, -eps])
                cube([flange_w, flange_t, spine_h + eps]);
            for (s = [-1, 1]) { rail(s); root_fillet(s); }
        }
        for (s = [-1, 1], z = pin_list)
            translate([s*(spine_t/2 + dt_h + eps), 0, z])
                rotate([0, -s*90, 0]) cylinder(d = pin_hole_rail, h = pin_hole_depth + eps);
        for (sy = [-1, 1], z = win_z) translate([-spine_t, sy > 0 ? win_y[0] : -win_y[1], z])
            cube([2*spine_t, win_y[1] - win_y[0], win_h]);
    }
}

// ---------- name plate ----------
name_text = "hectorsvill";
module name_engrave() {   // engraved 0.6 deep on the rear flange, reading bottom to top
    multmatrix([[0, -1, 0, 0], [0, 0, -1, -spine_d/2 + 0.6], [1, 0, 0, base_z1 + (stack ? 50 : short ? 40 : 70)], [0, 0, 0, 1]])
        linear_extrude(2) text(name_text, size = 7, halign = "center", valign = "center", font = "DejaVu Sans:style=Bold");
}

module frame() {
    difference() {
        union() { base(); imu_mount(); spine(); }
        name_engrave();
    }
}

// =====================================================================
// Carrier, local coordinates: u along the rail, v across, w = up off the spine face.
// Prints with its back (w = 0) on the bed; the dovetail groove opens at the bed.
function oL(b) = b[1] + 2*board_clear + 2*wall_t;
function oW(b) = b[2] + 2*board_clear + 2*wall_t;
function pin_u(b) = b[7] * (oL(b)/2 + tab_len/2);

module groove2d(c) {
    polygon([[-dt_base/2 - c, -1], [dt_base/2 + c, -1],
             [dt_base/2 + c, 0], [dt_top/2 + c, dt_h + c], [-dt_top/2 - c, dt_h + c], [-dt_base/2 - c, 0]]);
}

module corner_lip(su, sv, pL, pW, top) {
    h = top - plate_t + eps;
    translate([su > 0 ? pL/2 - lip_leg : -pL/2 - wall_t, sv > 0 ? pW/2 : -pW/2 - wall_t, plate_t - eps])
        cube([lip_leg + wall_t, wall_t, h]);
    translate([su > 0 ? pL/2 : -pL/2 - wall_t, sv > 0 ? pW/2 - lip_leg : -pW/2 - wall_t, plate_t - eps])
        cube([wall_t, lip_leg + wall_t, h]);
}

module side_lip(sv, pL, pW, top) {   // wall along a long edge of the pocket (v = +/-)
    translate([-pL/2 - wall_t, sv > 0 ? pW/2 : -pW/2 - wall_t, plate_t - eps])
        cube([pL + 2*wall_t, wall_t, top - plate_t + eps]);
}

module end_lip(su, pL, pW, top) {
    translate([su > 0 ? pL/2 : -pL/2 - wall_t, -pW/2 - wall_t, plate_t - eps])
        cube([wall_t, pW + 2*wall_t, top - plate_t + eps]);
}

module carrier(b) {
    L = b[1]; W = b[2]; hd = b[3]; holes = b[4]; seat = b[5]; lips = b[6];
    tab = b[7]; usb = b[8]; wins = b[9]; mount = b[12];
    screwed = (mount == "screws" || mount == "screws_m2");
    bd  = mount == "screws_m2" ? m2_boss_d : screw_boss_d;
    pil = mount == "screws_m2" ? m2_pilot  : screw_pilot;
    pL = L + 2*board_clear; pW = W + 2*board_clear;
    o_L = oL(b); o_W = oW(b);
    lip_top = seat + pcb_t - 0.6;
    rib_u0 = tab > 0 ? -o_L/2 : -o_L/2 - tab_len;
    difference() {
        union() {
            translate([-o_L/2, -o_W/2, 0]) cube([o_L, o_W, plate_t]);
            translate([rib_u0, -rib_hw, 0]) cube([o_L + tab_len, 2*rib_hw, rib_t]);
            if (screwed) for (h = holes) translate([h[0], h[1], plate_t - eps])
                cylinder(d = bd, h = seat - plate_t + eps);
            if (mount == "pegs") for (h = holes) translate([h[0], h[1], plate_t - eps]) {
                cylinder(d = hd + 3, h = seat - plate_t + eps);
                translate([0, 0, seat - plate_t]) {
                    cylinder(d = hd - peg_clear, h = pcb_t + 0.3 + eps);
                    translate([0, 0, pcb_t + 0.3]) cylinder(d1 = hd - peg_clear, d2 = hd - peg_clear - 0.6, h = 0.5);
                }
            }
            if (lips == "corners") for (su = [-1, 1], sv = [-1, 1]) corner_lip(su, sv, pL, pW, lip_top);
            if (lips == "ends") for (su = [-1, 1]) end_lip(su, pL, pW, lip_top);
            if (lips == "sides") for (sv = [-1, 1]) side_lip(sv, pL, pW, lip_top);
        }
        translate([rib_u0 - 1, 0, 0]) rotate([90, 0, 90])
            linear_extrude(o_L + tab_len + 2) groove2d(dovetail_clear);
        translate([pin_u(b), 0, -1]) cylinder(d = pin_hole_carrier, h = rib_t + 2);
        if (screwed) for (h = holes) translate([h[0], h[1], screw_floor]) {
            cylinder(d = pil, h = seat);
            translate([0, 0, seat - screw_floor - 0.6]) cylinder(d1 = pil, d2 = pil + (mount == "screws_m2" ? 1.0 : 1.2), h = 0.6 + eps);  // lead-in
        }
        if (usb && lips != "sides") translate([pL/2 - 1, -6, plate_t]) cube([wall_t + 2, 12, 20]);
        if (usb && lips == "sides") translate([-6, -pW/2 - wall_t - 1, plate_t]) cube([12, wall_t + 1 + eps, 20]);   // USB end at v- = rear (Y-) on the left face; cuts the lip only
        if (wins) for (sv = [-1, 1]) translate([-pL/2 + 12, sv > 0 ? 10 : -20, -1]) cube([pL - 24, 10, plate_t + 2]);
    }
}

// local carrier frame -> assembly position on its face
module place_on_face(b) {
    s = b[11];
    center_h = b[10] - pin_u(b);              // pocket center height above base top
    multmatrix([[0, 0, s, s*spine_t/2],
                [0, -s, 0, 0],
                [1, 0, 0, base_z1 + center_h],
                [0, 0, 0, 1]]) children();
}

module ghost_board(b) {
    L = b[1]; W = b[2]; seat = b[5];
    translate([-L/2, -W/2, seat]) cube([L, W, pcb_t]);
}

// helpers for other files that `use` this one
boards_in_use = stack ? [D153C, PICO_H] : short ? [D153C, PICO] : [D153C, PICO, PI];
module assembled_carriers() { for (b = boards_in_use) place_on_face(b) carrier(b); }
module assembled_boards()   { for (b = boards_in_use) place_on_face(b) ghost_board(b); }
module carriers_plate() {
    translate([0, 0, 0]) carrier(D153C);
    translate([0, oW(D153C)/2 + 8 + oW(PI)/2, 0]) carrier(PI);
    translate([0, -(oW(D153C)/2 + 8 + oW(PICO)/2), 0]) carrier(PICO);
}

// ---------- context for the preview ----------
module context() {
    color("silver") for (sx = [-1, 1], sy = [-1, 1]) translate([sx*col_x, sy*col_y, 0]) cylinder(d = 4.9, h = col_top, $fn = 6);
    color("dimgray") translate([-49, -30, -3]) cube([98, 60, 3]);
    color("black") for (sx = [-1, 1]) translate([sx*64.3, 0, -21]) rotate([0, 90, 0]) cylinder(d = 48, h = 19, center = true);
    color("royalblue", 0.6) translate([-37.5, -17.5, 1.5]) cube([75, 35, 20]);   // example 2S pack
}

if (part == "frame") translate([0, 0, -base_z0]) frame();
else if (part == "d153c") carrier(D153C);
else if (part == "pico") carrier(PICO);
else if (part == "pico_side") carrier(PICO_H);
else if (part == "pi") carrier(PI);
else if (part == "carriers") carriers_plate();
else if (part == "assembly") {
    color("mediumseagreen") frame();
    for (b = boards_in_use) place_on_face(b) {
        color("orange") carrier(b);
        color("darkgreen", 0.8) ghost_board(b);
    }
    context();
}
