// TH2 field node - tentative enclosure concept v0.2 - Agri Sensors and Controls
//
// Re-uses the Sasya SMTHSN50 PVC-pipe body: a vertical 103.8 x 42 mm sled PCB inside the
// pipe, a coupler socket at the bottom that slides onto the pole.
// v0.2: the louvered radiation shield sits ON TOP of the pipe (classic stacked-plate screen,
// as in the reference photo). Its solid top plate is also the rain cap; the SHT40 tab hangs
// in the centre of the stack on a short cable through a gland in the pipe's top end cap.
//
// Sled outline and 18650 holder position: from the SMTHSN50 Gerbers (pro mini weather v1.2).
// PVC sizes: ESTIMATES (63 mm pipe assumed from the board fit) - replace with measurements.
//
// Axes: Z up (0 = bottom of the coupler socket), pipe axis = Z axis.
// The sled lies in the plane X = board_x (component side +X), width along Y, length along Z.
//
// Views:  -D 'view="assembly"' | "cutaway" | "exploded" | "sled" | "section" (cross-section)

view = "cutaway";

$fn = 96;

// ---- PVC parts (estimates) -------------------------------------------------------------
pipe_od   = 63;     // body pipe outside diameter (63 mm uPVC / SWR)
pipe_wall = 2.5;    // -> ID 58
pipe_z0   = 30;     // pipe bottom (inside the coupler)
pipe_z1   = 190;    // pipe top

cpl_od = 73;  cpl_len = 60;              // bottom coupler socket (onto the pole)
cap_od = 73;  cap_h = 30;                // top end cap over the pipe (sealed, carries the gland)
pole_od = 60;                            // pole / stake the coupler sits on (shown cut)

// ---- sled PCB (from the SMTHSN50 Gerbers) ------------------------------------------------
pcb_w = 42;  pcb_l = 103.8;  pcb_t = 1.6;
pcb_z0 = 70;                             // bottom edge of the sled
board_x = 7;                             // back face of the PCB, off-centre so the 18650 fits
hold_pin1 = 11.6;  hold_pitch = 72.0;    // holder pins along the board, from the bottom edge
hold_y = -pcb_w / 2 + 10.9;              // holder centre line, 10.9 mm from one long edge
hold_l = 77.5;  hold_w = 21.5;  hold_h = 20;   // typical clone 18650 PCB holder body

// ---- radiation shield (on top of the pipe) --------------------------------------------------
plate_od = 130;  plate_t = 2.5;  plate_drop = 8;    // louvered (conical) ring plates
plate_n = 6;  plate_pitch = 18;
plate_id = 70;                           // opening of the lower rings
shield_z0 = pipe_z1 + cap_h - 4;         // bottom ring sits just above the end cap
post_r = 50;                             // 3 x M5 stainless rods hold the stack

// ---- sensor tab --------------------------------------------------------------------------
tab_w = 12;  tab_l = 25;

pipe_id = pipe_od - 2 * pipe_wall;
shield_top = shield_z0 + (plate_n - 1) * plate_pitch;
tab_z = shield_z0 + 2.2 * plate_pitch;   // SHT40 in the middle of the stack

// colours
c_pvc = [0.93, 0.93, 0.90];  c_pvc_grey = [0.80, 0.81, 0.82];
c_pcb = [0.05, 0.45, 0.20];  c_mod = [0.75, 0.75, 0.78];  c_ant = [0.15, 0.15, 0.15];
c_bat = [0.20, 0.45, 0.85];  c_hold = [0.12, 0.12, 0.12];  c_ic = [0.10, 0.10, 0.10];
c_usb = [0.80, 0.80, 0.82];  c_jst = [0.95, 0.95, 0.92];  c_plate = [1, 1, 1];
c_sht = [0.85, 0.70, 0.20];  c_cable = [0.85, 0.15, 0.15];  c_pole = [0.55, 0.42, 0.25];
c_rod = [0.6, 0.6, 0.62];

module tube(od, id, h) { difference() { cylinder(d = od, h = h); translate([0, 0, -0.1]) cylinder(d = id, h = h + 0.2); } }
module cutter(cut) { if (cut) translate([0, -300, -100]) cube([600, 600, 800]); }   // removes X > 0

// ---------------------------------------------------------------------------------------------
module pvc_body(cut = false) {
    difference() {
        union() {
            color(c_pvc_grey) translate([0, 0, pipe_z0]) tube(pipe_od, pipe_id, pipe_z1 - pipe_z0);
            color(c_pvc) tube(cpl_od, pipe_od + 0.4, cpl_len);                               // coupler
            color(c_pvc) translate([0, 0, pipe_z1 - cap_h + 4]) difference() {               // top cap
                cylinder(d = cap_od, h = cap_h);
                translate([0, 0, -0.1]) cylinder(d = pipe_od + 0.4, h = cap_h - 4);
                translate([0, 0, cap_h - 5]) cylinder(d = 12.5, h = 10);                     // gland hole
            }
        }
        cutter(cut);
    }
}

module gland() {   // PG7 gland in the top cap
    color(c_ic) translate([0, 0, pipe_z1 + 4]) { cylinder(d = 15, h = 3, $fn = 6); translate([0, 0, 3]) cylinder(d = 10, h = 6); }
}

// ---------------------------------------------------------------------------------------------
module sled() {
    translate([board_x, 0, pcb_z0]) {
        color(c_pcb) translate([0, -pcb_w / 2, 0]) cube([pcb_t, pcb_w, pcb_l]);
        fx = pcb_t;   // front face
        // ESP32-C3-WROOM-02 at the top, antenna on the upper edge (18 x 20 mm)
        color(c_mod) translate([fx, -9, pcb_l - 20]) cube([3.2, 18, 13]);
        color(c_ant) translate([fx, -9, pcb_l - 7]) cube([0.8, 18, 7]);
        // LoRa option: ISC-SX1262-B (Ra-01 footprint, 17 x 16 mm) + spring antenna at the top corner
        color(c_mod) translate([fx, -19.5, pcb_l - 42]) cube([2.5, 16, 17]);
        color([0.8, 0.6, 0.2]) translate([fx + 1, -17, pcb_l - 1]) cylinder(d = 3, h = 18, $fn = 12);
        // sensor connector J3 (top)
        color(c_jst) translate([fx, 11, pcb_l - 30]) cube([5.5, 8, 6]);
        // charger / LDO / USB ESD
        color(c_ic) translate([fx, 4, 45]) cube([1.7, 4, 5]);
        color(c_ic) translate([fx, 12, 47]) cube([1.2, 3, 3]);
        color(c_ic) translate([fx, -6, 20]) cube([1.2, 3, 3]);
        color([0.3, 0.3, 0.3]) translate([fx, 12, 60]) cube([3.5, 6, 6]);                   // PAIR button
        // USB-C on the bottom edge + J2 magnetic-connector header
        color(c_usb) translate([fx, -4.5, -1.5]) cube([3.2, 9, 7.3]);
        color(c_jst) translate([fx, 9, 3]) cube([5.5, 10, 6]);
        // 18650 holder + LiFePO4 cell on the back, along one long edge (Gerber position)
        hz0 = hold_pin1 + hold_pitch / 2 - hold_l / 2;
        translate([0, hold_y, hz0]) {
            color(c_hold) difference() {
                translate([-hold_h, -hold_w / 2, 0]) cube([hold_h, hold_w, hold_l]);
                translate([-hold_h / 2 - 0.5, 0, 6]) cylinder(d = 19, h = hold_l - 12);
                translate([-hold_h - 5, -8, 8]) cube([10, 16, hold_l - 16]);
            }
            color(c_bat) translate([-hold_h / 2 - 0.5, 0, (hold_l - 65) / 2]) cylinder(d = 18.3, h = 65);
        }
    }
}

// bottom disc: magnetic charge connector + ePTFE vent (sits in the coupler)
module bottom_disc() {
    color(c_pcb) translate([0, 0, 58]) cylinder(d = pipe_id - 1, h = 1.6);
    color(c_ic) translate([0, 0, 53]) cylinder(d = 10, h = 5);
}

// ---------------------------------------------------------------------------------------------
module ring_plate(id) {
    color(c_plate) rotate_extrude() polygon([
        [id / 2, 0], [plate_od / 2, -plate_drop], [plate_od / 2, -plate_drop - plate_t], [id / 2, -plate_t]]);
}
module top_plate() {   // solid conical roof
    color(c_plate) rotate_extrude() polygon([
        [0, plate_drop * 0.6], [plate_od / 2, -plate_drop], [plate_od / 2, -plate_drop - plate_t], [0, plate_drop * 0.6 - plate_t]]);
}
module shield(cut = false, explode = 0) {
    difference() {
        union() {
            for (i = [0 : plate_n - 2]) translate([0, 0, shield_z0 + i * (plate_pitch + explode)]) ring_plate(plate_id);
            translate([0, 0, shield_top + (plate_n - 1) * explode]) top_plate();
            for (a = [90, 210, 330]) color(c_rod) rotate([0, 0, a]) translate([post_r, 0, shield_z0 - 12])
                cylinder(d = 5, h = shield_top - shield_z0 + 12 + (plate_n - 1) * explode, $fn = 16);
            // base ring that clamps onto the top cap and carries the rods
            color(c_plate) translate([0, 0, shield_z0 - 14]) difference() {
                hull() for (a = [90, 210, 330]) rotate([0, 0, a]) translate([post_r, 0, 0]) cylinder(d = 14, h = 6);
                translate([0, 0, -1]) cylinder(d = cap_od + 0.6, h = 8);
            }
        }
        cutter(cut);
    }
}

module sensor_tab(explode = 0) {
    translate([0, 0, tab_z + explode]) {
        color(c_pcb) translate([-0.8, -tab_w / 2, 0]) cube([1.6, tab_w, tab_l]);          // vertical tab
        color(c_sht) translate([0.8, -0.75, tab_l - 5]) cube([0.6, 1.5, 1.5]);             // SHT40 at the top
        color(c_jst) translate([0.8, -4, 1]) cube([5, 8, 6]);
    }
    // cable from the gland up to the tab
    color(c_cable) translate([0, 0, pipe_z1 + 10]) cylinder(d = 3, h = tab_z - pipe_z1 - 9 + explode, $fn = 16);
}

module internal_cable() {   // J3 (top of the sled) up to the gland
    color(c_cable) hull() {
        translate([board_x + 5, 15, pcb_z0 + pcb_l - 27]) sphere(d = 3, $fn = 12);
        translate([0, 0, pipe_z1 + 4]) sphere(d = 3, $fn = 12);
    }
}

module pole() { color(c_pole) translate([0, 0, -45]) cylinder(d = pole_od, h = 75); }   // shown cut

// ---------------------------------------------------------------------------------------------
module assembly(cut = false, explode = 0) {
    pvc_body(cut);
    gland();
    translate([0, 0, -explode * 2]) sled();
    bottom_disc();
    translate([0, 0, explode]) shield(cut, explode);
    sensor_tab(explode * 3);
    if (explode == 0) internal_cable();
    pole();
}

if (view == "assembly")  assembly();
if (view == "cutaway")   assembly(cut = true);
if (view == "exploded")  assembly(cut = true, explode = 20);
if (view == "sled")      sled();
if (view == "section")   // horizontal slice through the battery: checks the sled fits the pipe
    projection(cut = true) translate([0, 0, -(pcb_z0 + 45)]) { pvc_body(); sled(); }
