// AWD field node enclosure v0.1 - Agri Sensors and Controls
//
// Three printed parts:
//   front_shell  - covers the component side of the PCB head (button + LED through thin walls)
//   back_shell   - holds the PCB on a ledge and carries a 1xAA holder for the LiFePO4 14500 cell
//   pipe_collar  - sits on the AWD tube rim, holds the node so the strip hangs down the tube centre,
//                  with a clip for a 20 mm conduit antenna pole
//
// Coordinates follow the PCB: X = across the head (0..40), Y = 0 at the head's top edge and
// negative downwards (head -60..0, strip -60..-360), Z = 0 at the PCB's back face (PCB is 0..1.6).
// Split plane between the shells is Z = 0. Render one part with  -D 'part="front_shell"'  etc.

part = "assembly";        // front_shell | back_shell | pipe_collar | assembly | print_plate
pipe_od = 110;            // AWD tube outside diameter: 110 (4" SWR/PVC) or 160 (6")
pole_d  = 20;             // antenna pole: 20 mm PVC electrical conduit (or a bamboo of that size)
show_pcb = true;          // assembly view only
show_collar = true;
show_front = true;
cut_x = 999;              // assembly: cut away everything with X > cut_x to look inside

$fn = 48;
wall = 2.5;               // ASA/PETG walls
thin = 0.7;               // flex membrane over the button / light window over the LED
clr  = 0.5;               // clearance around the PCB

// cavity (inside of the closed box)
cx0 = -4.8;  cx1 = 40 + clr;   // left side makes room for the SMA hex body
cy0 = -60 - clr;  cy1 = clr;
front_top = 8.6;               // inside ceiling, ~3 mm above the tallest front part (SMA hex, 5.9)
back_bot  = -23;               // room for J3 (6 mm) + AA holder (15 mm)

// flange + screws (M3 x 16 + nut)
fl = 7;                        // flange width
screw_d = 3.4;  nut_af = 5.6;  nut_h = 2.6;

// interface features
sma_y = -25;  sma_z = 1.98;  sma_hole = 6.7;           // 1/4-36 barrel
sw_xy = [5, -14];   led_xy = [30.5, -18.5];
neck_x0 = 15 - 0.4;  neck_x1 = 25 + 0.4;               // strip is 10 mm wide at X 15..25
boss = [16, 8, 17];                                    // neck gland: width, thickness, length below box wall
boss_z0 = -3;                                          // gland spans Z -3..5 (split at Z = 0)

ox0 = cx0 - wall; ox1 = cx1 + wall; oy0 = cy0 - wall; oy1 = cy1 + wall;
fx0 = ox0 - fl;  fx1 = ox1 + fl;  fy0 = oy0 - fl;  fy1 = oy1 + fl;

screws = [ [fx0 + fl/2, fy1 - fl/2], [fx1 - fl/2, fy1 - fl/2],
           [fx0 + fl/2, fy0 + fl/2], [fx1 - fl/2, fy0 + fl/2],
           [fx0 + fl/2, (fy0 + fy1)/2 + 9], [fx1 - fl/2, (fy0 + fy1)/2] ];  // left one dodges the SMA

module rbox(x0, y0, x1, y1, z0, z1, r = 2) {
    translate([0, 0, z0]) linear_extrude(z1 - z0)
        offset(r) offset(-r) translate([x0, y0]) square([x1 - x0, y1 - y0]);
}

module sma_cut()  { translate([ox0 - 1, sma_y, sma_z]) rotate([0, 90, 0]) cylinder(d = sma_hole, h = wall + 2); }

// ---------------------------------------------------------------------------------------------
module front_shell() {
    difference() {
        union() {
            rbox(ox0, oy0, ox1, oy1, 0, front_top + wall, 3);
            rbox(fx0, fy0, fx1, fy1, 0, 3, 4);                                         // flange
            translate([(neck_x0 + neck_x1)/2 - boss[0]/2, oy0 - boss[2], 0])
                cube([boss[0], boss[2] + wall, boss[1] + boss_z0]);                    // gland, front half
        }
        rbox(cx0, cy0, cx1, cy1, -1, front_top, 1);                                    // cavity
        // strip slot through the bottom wall and gland
        translate([neck_x0, oy0 - boss[2] - 1, -1]) cube([neck_x1 - neck_x0, boss[2] + wall + 2, 1.6 + 0.4 + 1]);
        sma_cut();
        for (s = screws) translate([s[0], s[1], -1]) cylinder(d = screw_d, h = 10);
        // flex membrane over the PAIR/BOOT button, light window over the LED
        translate([sw_xy[0], sw_xy[1], front_top + thin]) cylinder(d = 11, h = wall);
        translate([led_xy[0], led_xy[1], front_top + thin]) cylinder(d = 5, h = wall);
        // sealant groove on the flange face (formed-in-place silicone gasket)
        difference() {
            rbox(ox0 - 2.2, oy0 - 2.2, ox1 + 2.2, oy1 + 2.2, -1, 1.2, 3);
            rbox(ox0 - 0.2, oy0 - 0.2, ox1 + 0.2, oy1 + 0.2, -2, 2, 3);
        }
    }
    // plunger under the membrane: 0.3 mm above the TL3342 cap (top at Z 3.14)
    translate([sw_xy[0], sw_xy[1], 3.45]) cylinder(d = 3, h = front_top + thin - 3.45);
    // four posts clamp the PCB edge (Z 1.6) clear of components
    for (p = [[1, -1.5], [38.5, -1.5], [1, -58], [38.5, -58]])
        translate([p[0] - 1, p[1] - 1, 1.65]) cube([2, 2, front_top - 1.65]);
}

// ---------------------------------------------------------------------------------------------
module back_shell() {
    difference() {
        union() {
            rbox(ox0, oy0, ox1, oy1, back_bot - wall, 0, 3);
            rbox(fx0, fy0, fx1, fy1, -3, 0, 4);
            translate([(neck_x0 + neck_x1)/2 - boss[0]/2, oy0 - boss[2], boss_z0])
                cube([boss[0], boss[2] + wall, -boss_z0]);                             // gland, back half
        }
        difference() {
            rbox(cx0, cy0, cx1, cy1, back_bot, 1, 1);
            // ledge under the PCB edge (top at Z 0), 1.5 mm wide, 3 mm deep
            difference() {
                rbox(-0.5, -60.5, 40.5, 0.5, -3, 0.01, 0.5);
                rbox(1.0, -59.0, 39.0, -1.0, -4, 1, 0.5);
                translate([-6, sma_y - 6, -4]) cube([11, 12, 5]);                      // SMA hex body
            }
        }
        sma_cut();
        for (s = screws) {
            translate([s[0], s[1], -10]) cylinder(d = screw_d, h = 11);
            translate([s[0], s[1], -3 - 0.01]) cylinder(d = nut_af / cos(30), h = nut_h, $fn = 6);  // nut trap underneath
        }
        // cradle for a standard 1xAA holder (fits a LiFePO4 14500), 58 x 16.5 mm, fixed with VHB tape
    }
    translate([20 - 16.5/2 - 1.2, -30 - 29, back_bot]) difference() {
        cube([16.5 + 2.4, 58, 3]);
        translate([1.2, -1, 1]) cube([16.5, 60, 3]);
    }
}

// ---------------------------------------------------------------------------------------------
// Collar: the shells' bottom flange edge (Y = fy0) rests on the collar top; the gland drops 10 mm into
// the socket, so the strip hangs on the tube axis. Strip top (Y = -60) is 10 mm above the flange edge,
// the collar top is 3 mm above the tube rim, and SOIL on the strip is 96 mm below the strip top:
// SOIL = rim - 83 mm. So install the AWD tube with its rim about 8 cm above the field surface.
collar_t = 3;  skirt_h = 20;

module pipe_collar() {
    r_in = pipe_od/2 + 0.6;
    difference() {
        union() {
            cylinder(r = r_in + wall, h = collar_t);
            translate([0, 0, -skirt_h]) difference() {
                cylinder(r = r_in + wall, h = skirt_h);
                translate([0, 0, -1]) cylinder(r = r_in, h = skirt_h + 1);
            }
            // socket sleeve for the gland
            translate([-(boss[0] + 4)/2, -(boss[1] + 4)/2, -10]) cube([boss[0] + 4, boss[1] + 4, 10]);
            // antenna-pole clip on the outside of the skirt
            translate([r_in + wall + pole_d/2 + 1, 0, -skirt_h]) difference() {
                cylinder(d = pole_d + 2*wall, h = skirt_h + collar_t);
                translate([0, 0, -1]) cylinder(d = pole_d + 0.4, h = skirt_h + collar_t + 2);
                translate([pole_d/4, -pole_d/2 * 0.75, -1]) cube([pole_d, pole_d * 0.75, skirt_h + collar_t + 2]);
            }
        }
        translate([-(boss[0] + 0.6)/2, -(boss[1] + 0.6)/2, -11]) cube([boss[0] + 0.6, boss[1] + 0.6, 20]);
        // air vents so the tube level follows the field (and rain can drain)
        for (a = [45, 135, 225, 315]) rotate(a) translate([r_in * 0.6, 0, -1]) cylinder(d = 5, h = 10);
        // zip-tie slots to lash the box to the collar
        for (s = [-1, 1]) translate([s * (boss[0]/2 + 6) - 2, -10, -1]) cube([4, 2, 10]);
    }
}

// ---------------------------------------------------------------------------------------------
module pcba() { color("green", 0.9) translate([-97.5, 40, 0]) import("ref/pcba.stl"); }

if (part == "front_shell")  front_shell();
if (part == "back_shell")   back_shell();
if (part == "pipe_collar")  pipe_collar();
if (part == "print_plate") {   // all three parts laid flat the way they print
    translate([0, 0, front_top + wall]) rotate([180, 0, 0]) front_shell();
    translate([75, 0, -back_bot + wall]) back_shell();
    translate([0, -150, collar_t]) rotate([180, 0, 0]) pipe_collar();
}
if (part == "assembly") intersection() {
  translate([-500, -500, -500]) cube([500 + cut_x, 1000, 1000]);
  union() {
    if (show_front) color("white") front_shell();
    color("lightgray") back_shell();
    if (show_pcb) pcba();
    // collar on the strip axis, gland in its socket: rotate so the collar's Z axis runs along -Y
    if (show_collar) color("silver", 0.8) translate([20, fy0 - collar_t, (boss[1] + 2*boss_z0)/2]) rotate([-90, 0, 0]) pipe_collar();
  }
}
