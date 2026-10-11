// AWD vs TH packaging in the COMMON box - Agri Sensors and Controls
//
// Left: AWD head (real routed board, ref/pcba.stl from KiCad) + 1xAA holder on the back-shell cradle.
// Right: TH2 (40 x 60 board model from th2_enclosure.scad): parts + AA holder on the PCB top side.
// Both use the same AWD front/back shells (called, not copied).
//
// Views:  -D 'view="exploded"'  (front shell lifted, both side by side)
//         -D 'view="underside"' (back shells as ghosts, seen from below: where each battery sits)
//         -D 'view="section"'   (both cut at X = 20 through the PCB centre, seen from -X; TH above AWD)

use <../../../../AWD1-paddy/hardware/AWD-FieldNode/enclosure/awd_enclosure.scad>
use <th2_enclosure.scad>

view = "exploded";
$fn = 64;

back_bot = -23;  front_top = 8.6;
gap = 95;                 // spacing between the two boxes (X)
lift = 32;                // front shell lift in the exploded view
strip_show = 45;          // show only the top of the AWD strip

c_f = [0.97, 0.97, 0.97];  c_b = [0.85, 0.86, 0.87];
c_hold = [0.12, 0.12, 0.12];  c_bat = [0.20, 0.45, 0.85];  c_lead = [0.85, 0.15, 0.15];

// AWD board: KiCad export, strip clipped to strip_show mm below the box
module awd_pcba() {
    intersection() {
        color("green") translate([-97.5, 40, 0])
            import("../../../../AWD1-paddy/hardware/AWD-FieldNode/enclosure/ref/pcba.stl");
        translate([-20, -60 - 20 - strip_show, -10]) cube([90, 60 + 20 + strip_show + 5, 30]);
    }
}

// AWD battery: 1xAA holder with leads, VHB-taped into the back-shell floor cradle, JST to J3
module awd_battery() {
    translate([20 - 16.5 / 2, -59, back_bot + 1]) {
        color(c_hold) difference() {
            cube([16.5, 58, 14]);
            translate([8.25, 4, 8]) rotate([-90, 0, 0]) cylinder(d = 14.8, h = 50);
            translate([-1, 6, 8]) cube([18.5, 46, 10]);
        }
        color(c_bat) translate([8.25, 4, 8]) rotate([-90, 0, 0]) cylinder(d = 14.2, h = 50);
    }
    // leads up to J3 (on the PCB back, under the SX1262)
    color(c_lead) hull() { translate([20, -2, back_bot + 15]) sphere(d = 1.6); translate([22, -8, -3]) sphere(d = 1.6); }
}

module awd_box(explode, cut) {
    difference() {
        union() {
            color(c_f) translate([0, 0, explode]) front_shell();
            color(c_b) back_shell();
            awd_pcba();
            awd_battery();
        }
        if (cut) translate([-50, -200, -50]) cube([70, 400, 100]);
    }
}

module th_box(explode, cut) {
    difference() {
        union() {
            color(c_f) translate([0, 0, explode]) th_front_shell();
            color(c_b) back_shell();
            th_pcb();
        }
        if (cut) translate([-50, -200, -50]) cube([70, 400, 100]);
    }
}

module label(t) { color([0.15, 0.15, 0.15]) linear_extrude(1) text(t, size = 7, halign = "center", font = "Liberation Sans:style=Bold"); }

if (view == "exploded") {
    awd_box(lift, false);
    translate([gap, 0, 0]) th_box(lift, false);
    translate([20, 18, 0]) label("AWD");
    translate([gap + 20, 18, 0]) label("TH2");
}
if (view == "section") {   // stacked along Y so a camera on -X sees both cut faces
    awd_box(0, true);
    translate([0, 105, 0]) th_box(0, true);
    translate([19, -40, 22]) rotate([90, 0, 90]) mirror([1, 0, 0]) label("AWD");
    translate([19, 65, 22]) rotate([90, 0, 90]) mirror([1, 0, 0]) label("TH2");
}

if (view == "underside") {
    % back_shell();  awd_pcba();  awd_battery();
    translate([gap, 0, 0]) { % back_shell();  th_pcb(); }
    translate([20, 18, back_bot - 3]) mirror([0, 0, 1]) mirror([1, 0, 0]) label("AWD");
    translate([gap + 20, 18, back_bot - 3]) mirror([0, 0, 1]) mirror([1, 0, 0]) label("TH2");
}
