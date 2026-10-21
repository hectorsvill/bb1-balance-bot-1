// Self-Balancing Robot - chassis bottom plate (replacement for the broken acrylic plate)
// Units: mm. Hole layout measured from the manufacturer's 98 x 60 chassis drawing,
// seen from the top (the side the brass columns stand on). +Y and -Y are mirror images.
// Print flat in PETG, 100% infill (or 5+ walls), no supports.
//
// part = "plate"         -> the plate
// part = "template"      -> 0.6 mm thin copy to check hole alignment against the motor bracket first
// part = "hcsr04_holder" -> HC-SR04 holder that screws onto the front of the plate (print standing on its base)
// part = "preview"       -> plate + holder + ghost sensor

part = "plate";
$fn = 48;

plate_w  = 98;     // along the axle (wheel to wheel)
plate_d  = 60;     // front to back
plate_t  = 3;      // original acrylic was ~3; keeps the original screws usable
corner_r = 2.5;

// holes that matter
col_hole     = 2.9;               // M2.5 screws up into the brass columns
col_pos      = [44, 26.5];        // 88 x 53, same as the frame's column sockets
bracket_hole = 3.4;               // screws into the motor bracket (M3 or M2.5)
bracket_pos  = [32.17, 8.57];     // 64.3 x 17.1

// expansion holes from the drawing (top half; mirrored to the bottom half)
misc_hole = 3.2;
misc_pos  = [[-39.2, 24.65], [-37.17, 18.63], [0, 18.63], [37.16, 18.63], [19.06, 24.7]];
ovals     = [[-22.87, 22.19], [22.85, 22.19]];      // 4.75 x 3.3, long side along X
oval_size = [4.75, 3.3];
// slots: [x, y, length, width]
edge_slot   = [0, 26.72, 20.1, 3.3];                // front and rear wire slots
center_slot = [0, 0, 20.1, 8.1];                    // center wire pass-through

module stadium(l, w) hull() for (sx = [-1, 1]) translate([sx*(l - w)/2, 0]) circle(d = w);

module outline() offset(r = corner_r) square([plate_w - 2*corner_r, plate_d - 2*corner_r], center = true);

module holes2d() {
    for (sx = [-1, 1], sy = [-1, 1]) {
        translate([sx*col_pos[0], sy*col_pos[1]]) circle(d = col_hole);
        translate([sx*bracket_pos[0], sy*bracket_pos[1]]) circle(d = bracket_hole);
    }
    for (sy = [-1, 1]) {
        for (p = misc_pos) translate([p[0], sy*p[1]]) circle(d = misc_hole);
        for (p = ovals) translate([p[0], sy*p[1]]) stadium(oval_size[0], oval_size[1]);
        translate([edge_slot[0], sy*edge_slot[1]]) stadium(edge_slot[2], edge_slot[3]);
    }
    translate([center_slot[0], center_slot[1]]) stadium(center_slot[2], center_slot[3]);
}

module plate(t) difference() {
    linear_extrude(t) difference() { outline(); holes2d(); }
    // "TOP" mark on the top face (the asymmetric expansion holes only line up this way up)
    if (t > 1.5) translate([0, 12.5, t - 0.4]) linear_extrude(1)
        text("TOP", size = 4, halign = "center", valign = "center", font = "DejaVu Sans:style=Bold");
}

// ---------- HC-SR04 holder ----------
// Sits on the front edge of the plate in front of the battery, low to the floor, aimed up a little so
// the beam doesn't see the floor. Two M2.5 screws come up from under the plate through the ovals at
// (+/-22.87, 22.19) into the holder. The sensor drops in from the top (pins down); its pins reach into a
// channel that runs back under the holder so the wires exit behind it, over the battery.
// The side guards stick out past the cans and work as a front bumper if the robot tips forward.
hc_tilt   = 10;              // aim the sensor up by this many degrees
hc_pivot  = [0, 26, 3];      // bottom edge of the board's back face (plate coords; z = top of holder base)
hc_base   = [[-27, 18.5], [27, 42]];
hc_screws = [[-22.87, 22.19], [22.87, 22.19]];
module hc_local() translate(hc_pivot) rotate([hc_tilt, 0, 0]) children();
module hcsr04_holder() {
    intersection() {
        difference() {
            union() {
                translate([hc_base[0][0], hc_base[0][1], 0]) cube([hc_base[1][0] - hc_base[0][0], hc_base[1][1] - hc_base[0][1], 3]);
                for (p = hc_screws) translate([p[0], p[1], 0]) cylinder(d = 7, h = 9);
                hc_local() {
                    translate([-25.5, -4.2, -6]) cube([51, 2, 27]);                         // back wall
                    for (sx = [-1, 1]) translate([sx > 0 ? 21.5 : -25.5, -4.2, -6]) cube([4, 20.2, 29]);   // side guards
                }
            }
            hc_local() {
                translate([-22.75, -0.2, 0]) cube([45.5, 2.0, 40]);     // board slot (45 x 20 x 1.6)
                translate([-21.5, 1.8, 0]) cube([43, 40, 40]);           // cans + crystal, open front and top
                translate([-21.5, -2.2, 0]) cube([43, 2.0, 40]);         // clearance for parts on the back of the board
                translate([-6.5, -2.6, -12]) cube([13, 5.2, 15]);        // header pins + their plastic strip
                translate([-30, -0.4, 20.4]) cube([60, 3.2, 1.6]);       // zip-tie slot over the top edge of the board
            }
            translate([-6.5, hc_base[0][1] - 1, -1]) cube([13, 27 - hc_base[0][1] + 1, 3.4]);   // wire tunnel out the back
            for (p = hc_screws) translate([p[0], p[1], -1]) cylinder(d = 2.2, h = 10);           // M2.5 pilots, screw from below
        }
        translate([-50, 0, 0]) cube([100, 60, 40]);   // trim anything below the plate top
    }
}
module hcsr04_ghost() hc_local() {
    color("#1d5fa5") translate([-22.5, 0, 0]) cube([45, 1.6, 20]);
    color("silver") for (sx = [-1, 1]) translate([sx*13, 1.6, 10]) rotate([-90, 0, 0]) cylinder(d = 16, h = 12);
}

if (part == "plate") plate(plate_t);
else if (part == "template") plate(0.6);
else if (part == "hcsr04_holder") hcsr04_holder();
else if (part == "preview") { color("dimgray") translate([0, 0, -plate_t]) plate(plate_t); color("mediumseagreen") hcsr04_holder(); hcsr04_ghost(); }
